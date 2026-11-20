#include "wdg.h"

volatile uint8_t g_wdg_reset_cause = 0u;

/* 初始化独立看门狗：窗口 3.18s@40kHz LSI，最坏情况 2.12s */
void WDG_Init(void)
{
    /* 1) 先记录复位来源，再清标志；打印交给 main（此时串口可能还没初始化） */
    g_wdg_reset_cause = (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) ? 1u : 0u;
    RCC_ClearFlag();

    /* 2) 写保护 → 预分频 → 重装值 → 重装载 → 使能 */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(WDG_PRESCALER);
    IWDG_SetReload(WDG_RELOAD);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/* 喂独立看门狗 */
void WDG_Feed(void)
{
    IWDG_ReloadCounter();
}