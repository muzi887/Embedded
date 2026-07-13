#include "stm32f10x.h"                  // Device header

void LED_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = ;
	GPIO_InitStructure.GPIO_Pin = ;
	GPIO_InitStructure.GPIO_Speed = ;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}