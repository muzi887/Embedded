#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include "stdio.h"

/* printf 总开关。
   1 = printf 正常输出（fputc 重定向到 USART1）；
   0 = printf 编译期替换为空操作，所有调用点被优化掉、不占代码空间，
       需要产出"静默"固件时改这一个数字即可，调用点不用动。 */
#define ENABLE_PRINTF  1

#if !ENABLE_PRINTF
#define printf(...)  ((void)0)   /* 空操作替换；可变参数宏依赖编译器扩展，仅在开关关闭时才会编译到此行 */
#endif

/* USART1 调试口初始化 */
void uart1_init(uint32_t bound);

#endif
