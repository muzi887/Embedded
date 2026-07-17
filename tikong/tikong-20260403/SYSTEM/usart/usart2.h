#ifndef __USART2_H
#define __USART2_H
#include "sys.h"

#define RS485_REC_LEN  128  // 接收缓冲区大小

//extern __align(4) u8 RS485_RX_BUF[RS485_REC_LEN]; // DMA使用的接收缓冲区
//extern u16 RS485_RX_CNT;      // 接收到的数据长度
//extern u8 RS485_RX_Complete;  // 接收完成标志

#define  RS485_TX_EN  PAout(1)  // 485模式控制，0为接收，1为发送

void RS485_Init(u32 bound);
void RS485_DMA_ReInit(void); // DMA重新初始化函数
void RS485_SendData(u8 *buf, u8 len);  // 发送数据

void Rs485Process(void);

#endif
