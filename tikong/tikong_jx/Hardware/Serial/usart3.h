#ifndef __USART3_H
#define __USART3_H
#include "stm32f4xx.h"
#include "sys.h"

#define USART3_REC_LEN  1024  // 接收缓冲区大小

extern __align(4) u8 USART3_RX_BUF[USART3_REC_LEN]; // DMA使用的接收缓冲区
extern u16 USART3_RX_CNT;      // 接收到的数据长度
extern u8 USART3_RX_Complete;  // 接收完成标志

void uart3_init(u32 bound);
void USART3_DMA_ReInit(void); // DMA重新初始化函数
void USART3_SendData(uint8_t *data, uint16_t length);	// 通过USART3发送数据
#endif
