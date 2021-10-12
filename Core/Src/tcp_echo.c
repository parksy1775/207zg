#include "tcp_echo.h"
#include "cmsis_os.h"
#include "operate_gpio.h"

static struct tcp_pcb *pcb_server;		//echoserver pcb
/* callback functions */
static err_t app_callback_accepted(void *arg, struct tcp_pcb *pcb_new, err_t err);
static err_t app_callback_received(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void app_callback_error(void *arg, err_t err);
static err_t app_callback_poll(void *arg, struct tcp_pcb *tpcb);
static err_t app_callback_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

/* functions */
static void app_send_data(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es); //send function
static void app_close_connection(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es); //close function

osThreadId defaultTaskHandle;

/* initialize echo server */
err_t app_echoserver_init(void)
{
  err_t err;
  pcb_server = tcp_new();		//allocate pcb memory

  if (pcb_server == NULL)
  {
    //lack of memory
    memp_free(MEMP_TCP_PCB, pcb_server);
    return ERR_MEM;
  }

  err = tcp_bind(pcb_server, IP_ADDR_ANY, ECHO_SERVER_LISTEN_PORT); //bind to port 7
  if (err != ERR_OK)
  {
    //fail to bind
    memp_free(MEMP_TCP_PCB, pcb_server);
    return err;
  }

  pcb_server = tcp_listen(pcb_server);	//listen
  tcp_accept(pcb_server, app_callback_accepted);	//register accept callback


  return ERR_OK;
}

void gpioCmd(void const * argument)
{
  while(1)
  {
	  oper();
	  osDelay(1);
  }
  /* USER CODE END 5 */
}

/* accept callback */
static err_t app_callback_accepted(void *arg, struct tcp_pcb *pcb_new, err_t err)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(arg); //remove warning
  LWIP_UNUSED_ARG(err);

  tcp_setprio(pcb_new, TCP_PRIO_NORMAL); //set priority for new pcb

  es = (struct tcp_echoserver_struct*)
      mem_malloc(sizeof(struct tcp_echoserver_struct)); //allocate tcp_echoserver_struct

  if (es == NULL) //lack of memory
  {
    app_close_connection(pcb_new, es); //close connection
    return ERR_MEM;
  }

  es->state = ES_ACCEPTED; //accepted
  es->pcb = pcb_new; //set pcb pointer
  es->retries = 0; //clear counter
  es->p = NULL; //clear buffer pointer

  tcp_arg(pcb_new, es);		//send es structure as an argument
  tcp_recv(pcb_new, app_callback_received);	//register receive callback
  tcp_err(pcb_new, app_callback_error); //register error callback
  tcp_poll(pcb_new, app_callback_poll, 0); //register poll callback

  return ERR_OK;
}

/* receive callback */
static err_t app_callback_received(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct tcp_echoserver_struct *es;
  err_t ret_err;

  LWIP_ASSERT("arg != NULL", arg != NULL); //check argument
  es = (struct tcp_echoserver_struct*) arg;


  if (p == NULL) //callback is called but there's no data
  {
    es->state = ES_CLOSING;
    if (es->p == NULL)
    {
      app_close_connection(tpcb, es); //close connection
    }
    else //if there's remained data to send
    {
      tcp_sent(tpcb, app_callback_sent); //register send callback
      app_send_data(tpcb, es); //send remained data
    }
    ret_err = ERR_OK;
  }
  else if (err != ERR_OK) //error when receiving
  {
    if (p != NULL) //buffer is not empty
    {
      es->p = NULL;
      pbuf_free(p); //clear buffer
    }
    ret_err = err;
  }
  else if (es->state == ES_ACCEPTED) //first data receiving
  {
    es->state = ES_RECEIVED; //change state
    es->p = p; //set buffer pointer

    tcp_sent(tpcb, app_callback_sent); //register send callback
    app_send_data(tpcb, es); //send data via es->p
    ret_err = ERR_OK;
  }
  else if (es->state == ES_RECEIVED) //additional data receiving
  {

    if (es->p == NULL) //no data to send
    {
      es->p = p; //set buffer pointer
      app_send_data(tpcb, es); //send data
    }
    else //buffer is not empty, there's data to send
    {
      struct pbuf *ptr = es->p; //head buffer
      pbuf_chain(ptr, p);	//append tail buffer to the head, this buffer will be handled by poll callback
    }
    ret_err = ERR_OK;
  }
  else if (es->state == ES_CLOSING) //receiving data when connection is closing
  {
    tcp_recved(tpcb, p->tot_len);	//advertise window size


    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  else //undefined condition
  {
    tcp_recved(tpcb, p->tot_len);	//advertise window size
    es->p = NULL;
    pbuf_free(p);
    ret_err = ERR_OK;
  }
  return ret_err;
}

/* error callback */
static void app_callback_error(void *arg, err_t err)
{
  struct tcp_echoserver_struct *es;
  LWIP_UNUSED_ARG(err);

  es = (struct tcp_echoserver_struct*) arg;
  if (es != NULL)
  {
    mem_free(es);	//free es structure
  }

  //HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_SET); //turn on blue LED when there's error.
}

/* poll callback */
static err_t app_callback_poll(void *arg, struct tcp_pcb *tpcb)
{
  struct tcp_echoserver_struct *es;
  es = (struct tcp_echoserver_struct*) arg;

  if (es == NULL) //if there's no es structure
  {
    tcp_abort(tpcb); //abort connection
    return ERR_ABRT;
  }

  if (es->p != NULL) //if there's data to send
  {
    tcp_sent(tpcb, app_callback_sent); //register send callback
    app_send_data(tpcb, es); //send data
  }
  else //no data to send
  {
    if (es->state == ES_CLOSING)
    {
      app_close_connection(tpcb, es);		//close connection
    }
  }

  return ERR_OK;
}

/* send callback */
static err_t app_callback_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct tcp_echoserver_struct *es;
  LWIP_UNUSED_ARG(len);

  es = (struct tcp_echoserver_struct*) arg;
  es->retries = 0;

  if (es->p != NULL) //if there's data to send
  {
    tcp_sent(tpcb, app_callback_sent); //register send callback
    app_send_data(tpcb, es); //send data
  }
  else //no data to send
  {
    if (es->state == ES_CLOSING)
    {
      app_close_connection(tpcb, es); //close connection
    }
  }
  return ERR_OK;
}

/* send data */
static void app_send_data(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;

  // while no error, data to send, data size is smaller than the size of the send buffer
  while ((wr_err == ERR_OK) && (es->p != NULL)
      && (es->p->len <= tcp_sndbuf(tpcb)))
  {
    ptr = es->p;
    uint8_t data[100];
    uint8_t obuf[100];
    u16_t obuf_len = 0;
    memcpy(data,es->p->payload,es->p->len);
    if(data[0]==1){
    	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, data[4]);

    	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, data[5]);
    	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, data[6]);

    	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, data[7]);
    	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, data[8]);

    	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, data[9]);
    	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, data[10]);
    	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, data[11]);

    	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, data[12]);
    	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, data[13]);
    	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4, data[14]);
    	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, data[15]);

    	HAL_GPIO_WritePin(GPIOF, GPIO_PIN_1, data[13]);
    	memcpy(obuf, data, es->p->len);
    	obuf_len = 14;
    }
    else if(data[0]==2){
    	obuf[0] = 2;
    	obuf[1] = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12);
    	obuf[2] = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10);
    	obuf[3] = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15);
    	obuf[4] = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4);
    	obuf[5] = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5);
    	obuf[6] = HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_4);
    	obuf[7] = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_8);
    	obuf[8] = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_13);
    	obuf[9] = HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_10);
    	obuf[10] = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_12);
    	obuf[11] = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_7);
    	obuf[12] = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_11);
    	obuf[13] = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_14);
    	obuf[14] = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_10);
    	obuf[15] = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_15);
    	obuf[16] = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_12);
    	obuf[17] = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_14);
    	obuf[18] = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9);
    	obuf_len = 19;
    }
    else if(data[0]==3){
    	//todo start
    	oper();
    }
    else if(data[0]==4){
    	//todo stop
    }
    else{
    	obuf[0] = 0;
    	obuf_len = 1;
    }


    wr_err = tcp_write(tpcb, obuf, obuf_len, 1); //send data

    if (wr_err == ERR_OK)
    {
      u16_t plen;
      u8_t freed;

      plen = ptr->len;
      es->p = ptr->next;

      if (es->p != NULL) //there's chained buffer to send
      {
        pbuf_ref(es->p);	//increase reference counter
      }

      do
      {
        freed = pbuf_free(ptr);		//free old buffer
      }
      while (freed == 0);

      tcp_recved(tpcb, plen);			//advertise window size
    }
    else
    {
      es->p = ptr;  //fail to send, recover buffer pointer
      es->retries++;  //increase counter
    }
  }
}

/* close connection */
static void app_close_connection(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  /* clear callback functions */
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);

  if (es != NULL)
  {
    mem_free(es);		//free es structure
  }

  tcp_close(tpcb);		//close connection
}
