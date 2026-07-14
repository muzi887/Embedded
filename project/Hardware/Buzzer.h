#ifndef __BUZZER_H
#define __BUZZER_H

/**
	* @brief  蜂鸣器驱动模块
	* @note   Buzzer → PB12；低电平响（有源蜂鸣器模块 I/O 接 PB12）
	*/

void Buzzer_Init(void);		/* 初始化 PB12 为推挽输出 */
void Buzzer_ON(void);			/* 打开蜂鸣器 */
void Buzzer_OFF(void);		/* 关闭蜂鸣器 */
void Buzzer_Turn(void);		/* 翻转蜂鸣器状态 */

#endif
