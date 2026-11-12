#ifndef __TIMER_H
#define __TIMER_H
#include "sys.h"

extern u8 flag10ms,flag20ms,flag300ms,flag500ms,flag1s,flag1min;
extern volatile u32 sys_ms; /* 毫秒计数，用于 UartTx 超时/间隔 */
extern volatile uint32_t g_tim4_ms_tick;

void TIM2_Int_Init(u16 arr,u16 psc);
void TIM3_Int_Init(u16 arr,u16 psc);
void TIM5_Int_Init(u16 arr,u16 psc);
void TIM1_PWM_Init(u16 arr,u16 psc);  //tim1 pwm初始化
void TIM6_Int_Init(u16 arr, u16 psc);
#endif
