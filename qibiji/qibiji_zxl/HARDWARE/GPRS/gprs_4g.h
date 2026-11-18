#ifndef __GPRS_4G_H
#define __GPRS_4G_H

#include "gprs_config.h"

/* ---------------------------------------------------------------------------
 * 4G 模块硬件接口（WH-LTE-7S1）—— 复位 / LINK 引脚 / 节流
 *
 * 职责：PA1 复位脉冲、PC5 LINKA 输入、EXTI 边沿中断、硬复位统一节流。
 *
 * 备注：模块主电源 3.8V 不由 MCU 控制；复位与状态机共用
 *       s_last_reset_ms 时间线（gprs_4g.c）。
 * --------------------------------------------------------------------------- */

#define GPRS_LINKA     GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5)
#define GPRS_RST_ON()  GPIO_SetBits(GPIOA, GPIO_Pin_1)
#define GPRS_RST_OFF() GPIO_ResetBits(GPIOA, GPIO_Pin_1)

void GPRS_4G_Init(void);
void GPRS_4G_Reset(void);
uint8_t GPRS_4G_ResetAllowed(uint32_t min_interval_ms);

#endif