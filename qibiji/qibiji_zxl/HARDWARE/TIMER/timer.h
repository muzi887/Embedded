#ifndef __TIMER_H
#define __TIMER_H

#include "sys.h"

/* ---------------------------------------------------------------------------
 * 10ms 系统节拍
 *
 * TIM3 中断里只置一个标志，主循环取走后自行调度各模块。
 * 不使用 SysTick：SYSTEM/delay 的 delay_us/ms 会反复重配 SysTick 计数值。
 * 时钟前提：SYSCLK = 72MHz（system_stm32f10x.c 中 SYSCLK_FREQ_72MHz）。
 * --------------------------------------------------------------------------- */

/* global millisecond counter, incremented by TIM3 ISR every 10 ms */
extern volatile uint32_t g_sys_ms;

void    TIM3_10ms_Init(void);
uint8_t Tick_10ms_Take(void);   

#endif