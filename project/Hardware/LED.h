#ifndef __LED_H
#define __LED_H

/**
	* @brief  LED 驱动模块
	* @note   LED1 → PA1，LED2 → PA2；低电平点亮（共阳接法）
	*/

void LED_Init(void);		/* 初始化 PA1、PA2 为推挽输出 */
void LED1_ON(void);			/* 点亮 LED1 */
void LED1_OFF(void);		/* 熄灭 LED1 */
void LED1_Turn(void);		/* 翻转 LED1 状态 */
void LED2_ON(void);			/* 点亮 LED2 */
void LED2_OFF(void);		/* 熄灭 LED2 */
void LED2_Turn(void);		/* 翻转 LED2 状态 */

#endif
