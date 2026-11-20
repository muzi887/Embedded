#ifndef __WDG_H
#define __WDG_H

#include "stm32f10x.h"   /* 提供 uint8_t / uint16_t，并引入 IWDG 寄存器定义 */

/*
 * 窗口配置：IWDG 时钟源为内部 LSI（数据手册 30~60kHz，典型 40kHz）
 *   分频因子 = 4 * 2^WDG_PRESCALER = 4 * 2^4 = 64
 *   Tout = 64 * 1990 / f_LSI
 *     f_LSI = 60kHz → 2123ms（最短，所有单段阻塞必须小于该值）
 *     f_LSI = 40kHz → 3184ms（标称，与原工程 main.c 的 3180ms 一致）
 *     f_LSI = 30kHz → 4245ms（最长）
 */
#define WDG_PRESCALER        4u
#define WDG_RELOAD           1990u
#define WDG_WINDOW_MIN_MS    2123u

/* 上次复位是否由 IWDG 引起（1=是）。main 在串口初始化完成后打印 */
extern volatile uint8_t g_wdg_reset_cause;

/* 使能 IWDG。注意：一旦使能，软件无法关闭，只能靠复位恢复 */
void WDG_Init(void);

/* 重装载 IWDG 计数器 */
void WDG_Feed(void);

#endif