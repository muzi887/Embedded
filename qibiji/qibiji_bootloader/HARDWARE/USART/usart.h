#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include "stdio.h"

/* ---- DMA 接收通道对照 ----
   USART1 RX = DMA1 通道 5；USART2 RX = DMA1 通道 6。 */

/* 接收缓冲长度：DMA 单帧最大字节数；IDLE 中断按「本帧实收字节数」取走数据后重启 DMA */
#define DMA_Rec1_Len  256    /* USART1 接收缓冲：调试口，PC 命令帧短，256 足够 */
#define DMA_Rec2_Len  1024   /* USART2 接收缓冲：4G 口透传帧可达 1KB */

/* USART2 接收环形缓冲总长：必须为 2 的幂（usart2.c 里有编译期检查强制保证）。
   DMA 单帧最多 1KB，环形缓冲给到 4KB，可容纳多个未消费帧不丢数据 */
#define USART2_RX_RING_SIZE  4096

/* printf 总开关。
   1 = printf 正常输出（fputc 重定向到 USART1）；
   0 = printf 编译期替换为空操作，所有调用点被优化掉、不占代码空间，
       需要产出"静默"固件时改这一个数字即可，调用点不用动。 */
#define ENABLE_PRINTF  1

#if !ENABLE_PRINTF
#define printf(...)  ((void)0)   /* 空操作替换；可变参数宏依赖编译器扩展，仅在开关关闭时才会编译到此行 */
#endif

extern char DMA_Rece_Buf1[DMA_Rec1_Len];   /* USART1 DMA 接收缓冲：硬件逐字节自动写入 */
extern char DMA_Rece_Buf2[DMA_Rec2_Len];   /* USART2 DMA 接收缓冲：同上（4G 口） */

extern u16 Usart1_Rec_Cnt;   /* USART1 最近一帧实收字节数，IDLE 中断里更新 */
extern u16 Usart2_Rec_Cnt;   /* USART2 最近一帧实收字节数，同上 */

/* ---- 阻塞式数组发送：逐字节等 TC（发送完成）再发下一个 ---- */
void Usart1_Send(char *buf, u16 len);
void Usart2_Send(char *buf, u16 len);

/* ---- DMA 接收重启：IDLE 中断取走一帧后调用，让 DMA 从缓冲头重新写入 ---- */
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx);   /* 重启 USART1 的 DMA1 通道 5 */
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx);   /* 重启 USART2 的 DMA1 通道 6 */

/* ---- USART2 接收环形缓冲：中断只写入，业务层只读取/消费 ---- */
u16 Usart2_RxAvailable(void);                /* 当前可读字节数 */
u16 Usart2_RxCopy(char *dst, u16 max_len);   /* 拷出最多 max_len 字节（只读不消费） */
void Usart2_RxConsume(u16 len);              /* 消费 len 字节（移动读指针） */
void Usart2_RxClear(void);                   /* 清空缓冲（读指针追平写指针） */
u32  Usart2_RxOverflowCount(void);           /* 溢出累计：环形缓冲满时每帧记一次并丢弃本帧剩余字节 */

/* ---- 初始化 ---- */
void uart1_init(uint32_t bound);   /* 调试口：PA9 TX / PA10 RX */
void uart2_init(uint32_t bound);   /* 4G 口：PA2 TX / PA3 RX */

#endif
