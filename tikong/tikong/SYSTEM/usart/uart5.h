#ifndef __UART5_H
#define __UART5_H

#include "sys.h"

#define UART5_REC_LEN 512

extern __align(4) u8 UART5_RX_BUF[UART5_REC_LEN];
extern u16 UART5_RX_CNT;
extern u8 UART5_RX_Complete;

void uart5_init(u32 bound);
void UART5_DMA_ReInit(void);
void USART5_SendData(const char *buf, uint16_t len);

#endif
