#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Key.h"

/**
	* @brief  按键初始化
	* @param  无
	* @retval 无
	* @note   配置 PB1、PB11 为上拉输入；未按下时引脚被拉高为 1
	 */
void Key_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	// 开启 GPIOB 时钟
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;				// 上拉输入：内部电阻接 VCC，悬空/松开时为高
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_11;	// PB1 → Key1，PB11 → Key2
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

/**
	* @brief  获取按键编号（带软件消抖）
	* @param  无
	* @retval 0：无按键；1：Key1（PB1）；2：Key2（PB11）
	* @note   阻塞式：检测到按下后等待松手才返回；若两键同时按，Key2 优先级更高
	 */
uint8_t Key_GetNum(void)
{
	uint8_t KeyNum = 0;
	
	if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)			// Key1 按下（低电平有效）
	{
		Delay_ms(20);														// 按下消抖，滤除机械抖动
		while(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0);	// 阻塞等待松手，避免连触发
		Delay_ms(20);														// 松手消抖
		KeyNum = 1;
	}
	
	if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0)			// Key2 按下
	{
		Delay_ms(20);														// 按下消抖
		while(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0);	// 等待松手
		Delay_ms(20);														// 松手消抖
		KeyNum = 2;
	}
	
	return KeyNum;
}
