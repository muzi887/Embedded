#include "rly.h"

// 继电器初始化函数
void RLY_Init(void) // IO初始化
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE); 

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_3; // key_up / key_down / key_stop /key_switch
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                            // 设置成推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

	RLY_1H_OFF;
	RLY_2H_OFF;
}



 