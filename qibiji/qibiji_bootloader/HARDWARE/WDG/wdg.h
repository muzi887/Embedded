#ifndef __WDG_H
#define __WDG_H

#include "sys.h"        /* u8、stm32f10x.h（IWDG/RCC 寄存器与固件库声明经此引入） */

/*
 * 窗口配置：IWDG 时钟源为内部 LSI（数据手册 30~60kHz，典型 40kHz）
 *   分频因子 = 4 * 2^WDG_PRESCALER = 4 * 2^4 = 64
 *   Tout = 64 * WDG_RELOAD / f_LSI
 *     f_LSI = 60kHz → 2123ms（最短，所有单段阻塞必须小于该值）
 *     f_LSI = 40kHz → 3184ms（标称，与原工程 main.c 的 3180ms 一致）
 *     f_LSI = 30kHz → 4245ms（最长）
 * 落盘时改这两个宏即可调窗口；改大后必须同步复核全工程最长阻塞段
 * （LCD 初始化 1.5s、EEPROM 连续写、4G 复位脉冲 0.5s/1s）是否仍在窗口内
 */
#define WDG_PRESCALER        4u
#define WDG_RELOAD           1990u
#define WDG_WINDOW_MIN_MS    2123u

/* 上次复位是否由 IWDG 引起（1=是，0=上电/其它复位）。
 * WDG_Init 时采样（读标志后立即清除），main 在串口初始化完成后打印 */
extern volatile u8 g_wdg_reset_cause;

/* 使能 IWDG（写保护解锁 → 预分频 → 重装值 → 重装载 → 使能）。
 * 注意：一旦使能，软件无法关闭，只能靠复位恢复 */
void WDG_Init(void);

/* 重装载 IWDG 计数器（喂狗）。喂狗间隔必须小于最坏窗口 WDG_WINDOW_MIN_MS */
void WDG_Feed(void);

#endif
