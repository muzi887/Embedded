#ifndef __USART_H
#define __USART_H
#include "stm32f4xx.h"
#include "sys.h"
#include "stdio.h"
#include "string.h"

#define USART_REC_LEN  200  // 接收缓冲区大小

extern __align(4) u8 USART_RX_BUF[USART_REC_LEN]; // DMA使用的接收缓冲区
extern u16 USART_RX_CNT;      // 接收到的数据长度
extern u8 USART_RX_Complete;  // 接收完成标志

void uart_init(u32 bound);
void USART_DMA_ReInit(void); // DMA重新初始化函数
void PCProcess(void);
#endif
