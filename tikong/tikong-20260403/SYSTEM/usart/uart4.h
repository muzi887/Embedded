#ifndef __UART4_H
#define __UART4_H
#include "stm32f4xx.h"
#include "sys.h"

#define UART4_REC_LEN  512  // 接收缓冲区大小

extern __align(4) u8 UART4_RX_BUF[UART4_REC_LEN]; // DMA对齐缓冲区
extern u16 UART4_RX_CNT;      // 接收数据长度
extern u8 UART4_RX_Complete;  // 接收完成标志

void uart4_init(u32 bound);
void UART4_DMA_ReInit(void); // DMA重新初始化函数


void QRProcess(void);
#endif
