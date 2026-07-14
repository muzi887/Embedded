#include "stm32f10x.h"                  // Device header
#include "LightSensor.h"

/**
	* @brief  光敏传感器初始化
	* @param  无
	* @retval 无
	* @note   配置 PB13 为上拉输入，接模块 DO（数字输出）引脚
	 */
void LightSensor_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	// 开启 GPIOB 时钟
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;				// 上拉输入，读模块数字输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;					// PB13 → 光敏模块 DO
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

/**
	* @brief  读取光敏传感器数字输出
	* @param  无
	* @retval 0 或 1（Bit_RESET / Bit_SET），对应引脚当前电平
	* @note   具体 0/1 与亮/暗的对应关系取决于模块比较器与电位器阈值，需实测
	 */
uint8_t LightSensor_Get(void)
{
	return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13);			// 读 IDR，返回 DO 状态
}
