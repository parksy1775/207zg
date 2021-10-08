#include "operate_gpio.h"
#include "stm32f2xx_hal.h"
enum OPERATION_STATE onoff = OFF;
enum STM_STATE state = READY;

void oper(){
	if(onoff == ON){
		switch(state){
			case READY:
				oper_M1();
				state = M1;
				break;
			case M1:
				state = M2;
				break;
			case M2:
				state = M3;
				break;
			case M3:
				state = M4;
				break;
			case M4:
				state = READY;
				break;
		}
	}

}

void oper_M1(){
	if(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15)){
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, SET);
		while(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10)){
			continue;
		}
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, RESET);
	}
}

void oper_M2(){
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, SET);

	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, RESET);
}
void oper_M3(){

}
void oper_M4(){

}
