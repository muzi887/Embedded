#ifndef __USART_H
#define __USART_H
#include "stm32f10x.h"
#include "stdio.h"
/*
USART1和DMA通道对应关系如下：
TX通道DMA1通道4
RX通道DMA1通道5
USART2和DMA通道对应关系如下：
TX通道DMA1通道6
RX通道DMA1通道7
USART3和DMA通道对应关系如下：
TX通道DMA1通道2
RX通道DMA1通道3
USART4和DMA通道对应关系如下：
TX通道DMA2通道5
RX通道DMA2通道3
*/
#define DMA_Rec1_Len 256
#define DMA_Rec2_Len 1024
#define DMA_Rec3_Len 256
#define DMA_Rec4_Len 256

//printf函数开关控制宏定义
//设置为1：printf正常工作
//设置为0：printf被定义为空操作，不输出任何内容
#define ENABLE_PRINTF			1		//使能（1）/禁止（0）printf函数

//根据ENABLE_PRINTF宏定义来重定义printf函数
#if ENABLE_PRINTF
	//printf正常工作，使用标准库的printf函数
	//printf函数通过fputc重定向到串口（在usart1.c中实现）
	//注意：如果stdio.h中printf是函数声明，这里不需要额外定义
#else
	//printf被定义为空操作，编译时会被优化掉，不占用代码空间
	//使用可变参数宏来支持printf的所有调用形式
	#define printf(...) ((void)0)
#endif

extern char DMA_Rece_Buf1[DMA_Rec1_Len];
extern char DMA_Rece_Buf2[DMA_Rec2_Len];
extern char DMA_Rece_Buf3[DMA_Rec3_Len];
extern char DMA_Rece_Buf4[DMA_Rec4_Len];

extern u16 Usart1_Rec_Cnt;
extern u16 Usart2_Rec_Cnt;
extern u16 Usart3_Rec_Cnt;
extern u16 Usart4_Rec_Cnt;
extern u16 Usart5_Rec_Cnt; // 实际接收数据长度


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
