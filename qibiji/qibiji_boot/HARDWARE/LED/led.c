#include "led.h"

void LED_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);	    //使能GPIOD端口时钟

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;	 
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP; 		     //推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		       //IO口速度为50MHz
    GPIO_Init(GPIOD, &GPIO_InitStructure);
	
    GPIO_SetBits(GPIOD,GPIO_Pin_14);
    GPIO_SetBits(GPIOD,GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13);
}

void LED_CLOSE(void)
{
	LED_UP=0 ;
  LED_DN=0;
  LED_LINE=0 ;
  LED_ERR=0 ;
}