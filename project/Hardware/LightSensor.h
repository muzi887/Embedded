#ifndef __LIGHTSENSOR_H
#define __LIGHTSENSOR_H

/**
	* @brief  光敏传感器驱动模块
	* @note   DO → PB13；数字输出，上拉输入读取模块比较器结果
	 */

void LightSensor_Init(void);	/* 初始化 PB13 为上拉输入 */
uint8_t LightSensor_Get(void);	/* 读取 DO 引脚电平，返回 0 或 1 */

#endif
