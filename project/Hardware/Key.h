#ifndef __KEY_H
#define __KEY_H

/**
	* @brief  按键驱动模块
	* @note   Key1 → PB1，Key2 → PB11；上拉输入，按下为低电平
	*/

void Key_Init(void);		/* 初始化 PB1、PB11 为上拉输入 */
uint8_t Key_GetNum(void);	/* 轮询读键，返回 0/1/2（含消抖，阻塞至松手） */

#endif
