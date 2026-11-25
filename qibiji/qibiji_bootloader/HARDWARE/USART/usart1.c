#include "usart.h"

/* ---- printf 重定向（不启用 MicroLIB 的标准做法） ----
   用 __use_no_semihosting 摆脱半主机模式：半主机下 printf 会触发 SWI 陷阱，
   脱离调试器单独运行时程序直接卡死，所以必须补齐标准库要求的三个符号。 */
#if 1
#pragma import(__use_no_semihosting)

/* 标准库要求 FILE 结构体可见且可实例化，这里只做占位 */
struct __FILE
{
    int handle;   /* 句柄占位成员，重定向下不使用具体值 */
};

FILE __stdout;   /* printf 的默认输出流对象，fputc 通过它被间接调用 */

/* 替换标准库的系统退出桩，防止链接器拉入半主机模式的退出实现；退出码不使用 */
void _sys_exit(int x)
{
    x = x;   /* 空实现，仅为满足符号引用、消除告警 */
}

/* printf 的底层单字符出口，重定向到 USART1 数据寄存器；f 为输出流指针，重定向下无实际用途 */
int fputc(int ch, FILE *f)
{
    while((USART1->SR & 0X40) == 0);   /* 轮询等待 TC（发送完成）标志：上一字符未发完就写 DR 会丢字 */
    USART1->DR = (u8) ch;              /* 写入数据寄存器即自动启动本次发送 */
    return ch;
}
#endif

/* USART1 接收侧数据 */
char DMA_Rece_Buf1[DMA_Rec1_Len];   /* DMA 硬件自动写入的接收缓冲 */
u16  Usart1_Rec_Cnt;                /* 最近一帧实收字节数，IDLE 中断里更新 */

/* 调试口初始化：PA9 TX / PA10 RX，接收走 DMA1 通道 5 + IDLE 空闲中断。
   IDLE = 总线连续一个字节时间无数据，即「一帧发完了」，此时 DMA 计数器里
   剩多少没写，缓冲里就是这一帧的实收长度——不需要约定帧尾字符。*/
void uart1_init(uint32_t bound)   
{
    GPIO_InitTypeDef GPIO_InitStructure;     
    USART_InitTypeDef USART_InitStructure;   
    NVIC_InitTypeDef NVIC_InitStructure;  
    DMA_InitTypeDef DMA_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);   
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);    /* DMA1 挂 AHB，接收搬运用 */
    USART_DeInit(USART1);                                 /* 复位 USART1 全部寄存器，从缺省状态开始配置，避免残留态 */

    /* TX：PA9 复用推挽，引脚控制权交给 USART 外设 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：PA10 浮空输入，电平完全由对方驱动 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 中断配置：抢占 1 / 响应 2 */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口参数：8N1 无流控，收发双向 */
    USART_InitStructure.USART_BaudRate            = bound;                            /* 波特率，调用方传入 */
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;              /* 8 位数据位 */
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;                 /* 1 位停止位 */
    USART_InitStructure.USART_Parity              = USART_Parity_No;                  /* 无校验位 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;   /* 无硬件流控 */
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;   
    USART_Init(USART1, &USART_InitStructure);                                         /* 参数写入 USART1 寄存器 */

    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);      /* 开 IDLE 空闲中断：一帧结束的信号源 */
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);      /* RX 数据流交给 DMA 搬运，不占 CPU */
    USART_Cmd(USART1, ENABLE);                          /* 使能外设 */

    /* DMA1 通道 5 = USART1 RX：外设→内存，字节宽度，普通模式（发满即停，靠中断重启） */
    DMA_DeInit(DMA1_Channel5);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART1->DR;       /* 源：USART1 数据寄存器 */
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)DMA_Rece_Buf1;     /* 目的：接收缓冲 */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;  /* 方向：外设 → 内存 */
    DMA_InitStructure.DMA_BufferSize         = DMA_Rec1_Len;           /* 缓冲总长 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;  /* 外设地址固定（同一寄存器） */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;   /* 内存地址递增（逐字节排入缓冲） */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  /* 8 位宽度 */
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;        /* 普通模式：计数到 0 停止，等中断重启 */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;        /* 非内存到内存 */
    DMA_Init(DMA1_Channel5, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel5, ENABLE);   /* 启动接收搬运 */
}

/* 阻塞式数组发送：逐字节等 TC 再发下一个，全部发完后再等一次 TC 确认收尾 */
void Usart1_Send(char *buf, u16 len)
{
    u16 t;   /* 字节循环计数 */
    for (t = 0; t < len; t++)
    {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, buf[t]);
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

/* USART1 中断服务：只处理 IDLE。
   读一次 DR 清掉 IDLE 的置位条件，取走本帧长度，把整帧转发给 4G 口（USART2），
   最后重启 DMA 让下一帧从头写缓冲。PC 发来的数据由此透传给 4G 模块。 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        USART_ReceiveData(USART1);   /* 读 DR：清 IDLE 置位条件（读动作本身即清除） */
        Usart1_Rec_Cnt = DMA_Rec1_Len - DMA_GetCurrDataCounter(DMA1_Channel5);   /* 实收 = 总长 - 剩余计数 */
        Usart2_Send((char *)DMA_Rece_Buf1, Usart1_Rec_Cnt);   /* 整帧转发 4G 口 */
        USART_ClearITPendingBit(USART1, USART_IT_IDLE);       /* 清中断标志 */
        MYDMA_Enable1(DMA1_Channel5);                         /* 重启 DMA，准备下一帧 */
    }
}

/* 重启接收 DMA：先关通道才能改计数器，重装满长度后再打开 */
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);                       /* 关通道：计数器仅在关闭时可写 */
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec1_Len);   /* 重装缓冲长度 */
    DMA_Cmd(DMA_CHx, ENABLE);                        /* 重新启动搬运 */
}
