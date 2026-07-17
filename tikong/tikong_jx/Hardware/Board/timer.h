#ifndef _TIMER_H
#define _TIMER_H
#include "sys.h"

#define beepout PEout(15)
/** 设置蜂鸣器鸣叫次数；TIM4 ISR 每次 10ms 递减 */
void Bsp_SetBeep(u8 count);
/** 获取当前蜂鸣器剩余次数 */
u8 Bsp_GetBeep(void);
extern u8 updateflag;
extern u8 gettimeflag;
/** 主循环中处理一次常开限位楼层（由 TIM3 等置位） */
extern u8 check_limit_time_flag;
void TIM2_Int_Init(u16 arr, u16 psc);
void TIM2_ClearPendingAndEnable(void);
/** 由 stm32f4xx_it.c 中 TIM2_IRQHandler 调用，勿重复定义 TIM2_IRQHandler */
void TIM2_Update_ISR(void);

/** TIM1：APB2；与 TIM10 共用 TIM1_UP_TIM10_IRQn，勿再初始化 TIM10 Update 中断以免冲突 */
void TIM1_Int_Init(u16 arr, u16 psc);
void TIM1_ClearPendingAndEnable(void);
void TIM1_Update_ISR(void);
void TIM3_Int_Init(u16 arr, u16 psc);
void TIM4_Int_Init(u16 arr, u16 psc);
void TIM5_Init(void);
/* 清 UIF/NVIC 悬挂、CNT 置 0 后再使能，避免首次 ENABLE 立刻进中断 */
void TIM5_ClearPendingAndEnable(void);
void Beep_Init(void);
/* num: 1..16，按 num 的低 4 位（与 num 的二进制一致，16 为 0000），位 1 长音、位 0 短音，自 MSB 至 LSB 依次播放 */
void beepAlarm(int num);
extern u16 ctrl_time;
/** TIM4 节拍约 10ms（board_init 与 timer.c 一致），单调递增供超时等非阻塞逻辑使用 */
extern volatile uint32_t g_tim4_ms_tick;
#endif
