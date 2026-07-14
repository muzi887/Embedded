#include "stm32f10x.h"                  // Device header
#include "Buzzer.h"

/**
	* @brief  蜂鸣器初始化
	* @param  无
	* @retval 无
	* @note   配置 PB12 为推挽输出，默认高电平（蜂鸣器不响）
	 */
void Buzzer_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	// 开启 GPIOB 时钟
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;			// 推挽输出，驱动有源蜂鸣器 I/O 脚
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;					// PB12 → 蜂鸣器模块 I/O
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	GPIO_SetBits(GPIOB, GPIO_Pin_12);							// 置高，蜂鸣器初始关闭
}

/**
	* @brief  打开蜂鸣器
	* @param  无
	* @retval 无
	* @note   有源蜂鸣器：I/O 低电平触发鸣叫
	 */
void Buzzer_ON(void)
{
	GPIO_ResetBits(GPIOB, GPIO_Pin_12);							// 低电平响
}

/**
	* @brief  关闭蜂鸣器
	* @param  无
	* @retval 无
	 */
void Buzzer_OFF(void)
{
	GPIO_SetBits(GPIOB, GPIO_Pin_12);							// 高电平不响
}

/**
	* @brief  蜂鸣器状态翻转
	* @param  无
	* @retval 无
	* @note   读 ODR 判断当前输出，再切换响/停
	 */
void Buzzer_Turn(void)
{
	if(GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_12) == 0)			// 当前响 → 关闭
	{
		GPIO_SetBits(GPIOB, GPIO_Pin_12);
	}
	else																				// 当前停 → 打开
	{
		GPIO_ResetBits(GPIOB, GPIO_Pin_12);
	}
}
