#include "stm32f10x.h"                  // Device header
#include "LED.h"

/**
	* @brief  LED 初始化
	* @param  无
	* @retval 无
	* @note   配置 PA1、PA2 为推挽输出，默认高电平（LED 熄灭）
	 */
void LED_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	// 开启 GPIOA 时钟（使用 GPIO 前必须开时钟）
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;			// 推挽输出，可强驱动高/低电平
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;	// PA1 → LED1，PA2 → LED2
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;			// 输出速度，LED 开关选 50MHz 即可
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	GPIO_SetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);				// 置高，LED 初始为灭
}

/**
	* @brief  LED1 点亮
	* @param  无
	* @retval 无
	 */
void LED1_ON(void)
{
	GPIO_ResetBits(GPIOA, GPIO_Pin_1);							// 低电平点亮（共阳：VCC → 限流电阻 → LED → PA）
}

/**
	* @brief  LED1 熄灭
	* @param  无
	* @retval 无
	 */
void LED1_OFF(void)
{
	GPIO_SetBits(GPIOA, GPIO_Pin_1);								// 高电平熄灭
}

/**
	* @brief  LED1 状态翻转
	* @param  无
	* @retval 无
	* @note   读输出数据寄存器 ODR 判断当前电平，再取反
	 */
void LED1_Turn(void)
{
	if(GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0)			// 当前为低（亮）→ 置高灭
	{
		GPIO_SetBits(GPIOA, GPIO_Pin_1);
	}
	else																				// 当前为高（灭）→ 置低亮
	{
		GPIO_ResetBits(GPIOA, GPIO_Pin_1);
	}
}

/**
	* @brief  LED2 点亮
	* @param  无
	* @retval 无
	 */
void LED2_ON(void)
{
	GPIO_ResetBits(GPIOA, GPIO_Pin_2);							// 低电平点亮
}

/**
	* @brief  LED2 熄灭
	* @param  无
	* @retval 无
	 */
void LED2_OFF(void)
{
	GPIO_SetBits(GPIOA, GPIO_Pin_2);								// 高电平熄灭
}

/**
	* @brief  LED2 状态翻转
	* @param  无
	* @retval 无
	* @note   读输出数据寄存器 ODR 判断当前电平，再取反
	 */
void LED2_Turn(void)
{
	if(GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_2) == 0)
	{
		GPIO_SetBits(GPIOA, GPIO_Pin_2);
	}
	else
	{
		GPIO_ResetBits(GPIOA, GPIO_Pin_2);
	}
}
