#ifndef __TIMER_H
#define __TIMER_H
#include "sys.h"

/*
 * 1ms system tick driven by the TIM4 update interrupt.
 * Used as the single time base for the SBF-20 transport layer timeouts and
 * for the 4G AT sequence state machine (the SysTick in delay.c is a busy-wait
 * delay generator and cannot be used as a free running counter).
 */
extern volatile u32 g_ms_tick;

void TIM4_Int_Init(u16 arr, u16 psc);

#endif
