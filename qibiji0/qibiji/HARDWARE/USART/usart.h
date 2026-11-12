#ifndef __USART_H
#define __USART_H
#include "stm32f10x.h"
#include "stdio.h"
/*
USART1��DMAͨ����Ӧ���£�
TXͨ��DMA1ͨ��4
RXͨ��DMA1ͨ��5
USART2��DMAͨ����Ӧ���£�
TXͨ��DMA1ͨ��6
RXͨ��DMA1ͨ��7
USART3��DMAͨ����Ӧ���£�
TXͨ��DMA1ͨ��2
RXͨ��DMA1ͨ��3
USART4��DMAͨ����Ӧ���£�
TXͨ��DMA2ͨ��5
RXͨ��DMA2ͨ��3
*/
#define DMA_Rec1_Len 256
#define DMA_Rec2_Len 1024
#define DMA_Rec3_Len 256
#define DMA_Rec4_Len 256


extern char DMA_Rece_Buf1[DMA_Rec1_Len];
extern char DMA_Rece_Buf2[DMA_Rec2_Len];
extern char DMA_Rece_Buf3[DMA_Rec3_Len];
extern char DMA_Rece_Buf4[DMA_Rec4_Len];

extern u16 Usart1_Rec_Cnt;
extern u16 Usart2_Rec_Cnt;
extern u16 Usart3_Rec_Cnt;
extern u16 Usart4_Rec_Cnt;
extern u16 Usart5_Rec_Cnt; // 实际接收数据长度


extern u8 UART2_RX_Complete;
extern u8 UART3_RX_Complete;

extern u8 Dma_Tx1_okflag;
extern u8 Dma_Tx2_okflag;
extern u8 Dma_Tx3_okflag;
extern u8 Dma_Tx4_okflag;


#define RS485_CE4_H  GPIO_SetBits(GPIOD,GPIO_Pin_6)
#define RS485_CE4_L  GPIO_ResetBits(GPIOD,GPIO_Pin_6)

//------------------------------------------------------
void uart1_init(uint32_t bound);
void uart2_init(uint32_t bound);
void uart3_init(uint32_t bound);
void uart4_init(uint32_t bound);
void uart5_init(uint32_t bound);

void Usart1_Send(char *buf, u16 len);
void Usart2_Send(char *buf, u16 len);
void Usart3_Send(char *buf, u16 len);
void Usart4_Send(char *buf, u16 len);
void Usart5_Send(char *buf, u16 len);

void MYDMA_Enable(DMA_Channel_TypeDef *DMA_CHx, u16 DMA_LEN);
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx);
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx);
void MYDMA_Enable3(DMA_Channel_TypeDef *DMA_CHx);
void MYDMA_Enable4(DMA_Channel_TypeDef *DMA_CHx);

#endif
