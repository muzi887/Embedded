#include "dc_motor.h"
// IO初始化
void dcmotor_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_3;  //继电器控制 -- 继电器输出
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                           
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;  //485控制 -- 485复位和方向控制
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                           
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	RLY_1H_OFF;
	RLY_2H_OFF;
	
	V01_CTRL = 1;  //复位
	V02_CTRL = 1;  //复位
}
