#ifndef __DEBUG_UART_H
#define __DEBUG_UART_H

/* ---------------------------------------------------------------------------
 * 调试串口模块（USART1，PA9 = TX，115200 8N1，只发不收）
 *
 * 职责：把字符串发到 PC 调试口。上板时用它确认固件在跑、
 *       并观察按键事件是否被主循环分发出来。
 *
 * 边界：不接收、不开中断、不重定向 printf，因此不依赖 microLIB / 半主机，
 *       也不占用 NVIC 或 DMA。
 *
 * 用法：上电调一次 DEBUG_UART_Init()，之后随时调 DEBUG_UART_SendString()。
 * 约束：发送是轮询等待 TXE，只能在主循环里调用，不要在中断里调用。
 * --------------------------------------------------------------------------- */

void DEBUG_UART_Init(void);
void DEBUG_UART_SendString(const char *str);

#endif