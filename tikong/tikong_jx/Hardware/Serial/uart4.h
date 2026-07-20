#ifndef __UART4_H
#define __UART4_H
#include "stm32f4xx.h"
#include "sys.h"

#define UART4_REC_LEN  512  

extern __align(4) u8 UART4_RX_BUF[UART4_REC_LEN]; // DMA���뻺����
extern u16 UART4_RX_CNT;      // �������ݳ���
extern u8 UART4_RX_Complete;  // ������ɱ�־

void uart4_init(u32 bound);
void UART4_DMA_ReInit(void); // DMA���³�ʼ������
/* ����len���ֽ� */
void UART4_SendData(char *buf, uint16_t len);
#endif
