#include "usart.h"

/* USART2 接收侧数据 */
char DMA_Rece_Buf2[DMA_Rec2_Len];   /* DMA 硬件自动写入的接收缓冲 */
u16  Usart2_Rec_Cnt;                /* 最近一帧实收字节数，IDLE 中断里更新 */

/* ---- USART2 接收环形缓冲 ----
   中断只写（生产者），业务层只读/消费（消费者）， head/tail 单向前进不回退，
   用「写指针 - 读指针」的无符号回绕差值表达在途字节数。
   临界保护用 PRIMASK 现场保存/恢复：只关中断的瞬间取快照，不在临界区里做拷贝。 */
static char         s_usart2_rx_ring[USART2_RX_RING_SIZE];   /* 环形存储区 */
static volatile u16 s_usart2_rx_head = 0;                    /* 写指针：中断里前进 */
static volatile u16 s_usart2_rx_tail = 0;                    /* 读指针：业务层消费时前进 */
static volatile u32 s_usart2_rx_overflow_count = 0;          /* 溢出累计：环满丢帧时递增 */

/* 下标回绕掩码：length 为 2 的幂时，index & mask 等价于 index % length */
#define USART2_RX_RING_MASK ((u16)(USART2_RX_RING_SIZE - 1))

/* 编译期强制「长度为 2 的幂」：非 2 的幂会让掩码回绕失效，直接编译失败 */
#if (USART2_RX_RING_SIZE & (USART2_RX_RING_SIZE - 1)) != 0
#error "USART2_RX_RING_SIZE must be a power of two"
#endif

/* 任意下标折算成环内下标 */
static u16 Usart2_RxUseMask(u16 index)
{
    return (u16)(index & USART2_RX_RING_MASK);
}

/* 当前可读字节数：临界区内取 head/tail 快照，差值用无符号回绕计算 */
u16 Usart2_RxAvailable(void)
{
    u16 head;
    u16 tail;
    u32 primask = __get_PRIMASK();

    __disable_irq();
    head = s_usart2_rx_head;
    tail = s_usart2_rx_tail;
    __set_PRIMASK(primask);

    return (u16)((head - tail) & USART2_RX_RING_MASK);
}

/* 拷出最多 max_len 字节：临界区内取快照并限长，拷贝动作在临界区外，
   DMA 缓冲→环形缓冲只发生在中断里，环形缓冲本身在读指针之前不会被覆盖 */
u16 Usart2_RxCopy(char *dst, u16 max_len)
{
    u16 head;
    u16 tail;
    u16 available;
    u16 i;
    u32 primask;

    if (dst == 0 || max_len == 0)
        return 0;   /* 参数非法：拷 0 字节 */

    primask = __get_PRIMASK();
    __disable_irq();
    head = s_usart2_rx_head;
    tail = s_usart2_rx_tail;
    available = (u16)((head - tail) & USART2_RX_RING_MASK);   /* 实际可读量 */
    if (available > max_len)
        available = max_len;                                  /* 不超过调用方缓冲 */
    for (i = 0; i < available; i++)
    {
        dst[i] = s_usart2_rx_ring[Usart2_RxUseMask((u16)(tail + i))];
    }
    __set_PRIMASK(primask);

    return available;
}

/* 消费 len 字节：只移动读指针，超出可读量的部分按可读量截断 */
void Usart2_RxConsume(u16 len)
{
    u16 available;
    u32 primask = __get_PRIMASK();

    __disable_irq();
    available = (u16)((s_usart2_rx_head - s_usart2_rx_tail) & USART2_RX_RING_MASK);
    if (len > available)
        len = available;   /* 防御：消费量不得超过在途数据 */
    s_usart2_rx_tail = Usart2_RxUseMask((u16)(s_usart2_rx_tail + len));
    __set_PRIMASK(primask);
}

/* 清空缓冲：读指针追平写指针，效果等于丢弃全部未读数据 */
void Usart2_RxClear(void)
{
    u32 primask = __get_PRIMASK();

    __disable_irq();
    s_usart2_rx_tail = s_usart2_rx_head;
    __set_PRIMASK(primask);
}

/* 读溢出累计值 */
u32 Usart2_RxOverflowCount(void)
{
    return s_usart2_rx_overflow_count;
}

/* 4G 模块通信口初始化：PA2 TX / PA3 RX，接收走 DMA1 通道 6 + IDLE 空闲中断 */
void uart2_init(uint32_t bound)   
{
    GPIO_InitTypeDef GPIO_InitStructure;     
    USART_InitTypeDef USART_InitStructure;   
    NVIC_InitTypeDef NVIC_InitStructure;  
    DMA_InitTypeDef DMA_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE); /* USART2 挂 APB1（与 USART1 的 APB2 不同） */   
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);     /* DMA1 挂 AHB，接收搬运用 */
    USART_DeInit(USART2);                                  /* 复位 USART2 全部寄存器，从缺省状态开始配置，避免残留态 */

    /* TX：PA2 复用推挽，引脚控制权交给 USART 外设 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：PA3 浮空输入，电平完全由对方驱动 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 中断配置：抢占 1 / 响应 1（比 USART1 的响应优先级更高，4G 数据不能等） */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口参数：8N1 无流控，收发双向 */
    USART_InitStructure.USART_BaudRate            = bound;                            /* 波特率，调用方传入 */
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;              /* 8 位数据位 */
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;                 /* 1 位停止位 */
    USART_InitStructure.USART_Parity              = USART_Parity_No;                  /* 无校验位 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;   /* 无硬件流控 */
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;   
    USART_Init(USART2, &USART_InitStructure);                                         /* 参数写入 USART2 寄存器 */

    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);      /* 开 IDLE 空闲中断：一帧结束的信号源 */
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);      /* RX 数据流交给 DMA 搬运，不占 CPU */
    USART_Cmd(USART2, ENABLE);                          /* 使能外设 */

    /* DMA1 通道 6 = USART2 RX：外设→内存，字节宽度，普通模式（发满即停，靠中断重启） */
    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DR;       /* 源：USART2 数据寄存器 */
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)DMA_Rece_Buf2;     /* 目的：接收缓冲 */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;  /* 方向：外设 → 内存 */
    DMA_InitStructure.DMA_BufferSize         = DMA_Rec2_Len;           /* 缓冲总长 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;  /* 外设地址固定（同一寄存器） */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;   /* 内存地址递增（逐字节排入缓冲） */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  /* 8 位宽度 */
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;        /* 普通模式：计数到 0 停止，等中断重启 */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;        /* 非内存到内存 */
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE);   /* 启动接收搬运 */
}

/* 阻塞式数组发送：逐字节等 TC 再发下一个，全部发完后再等一次 TC 确认收尾 */
void Usart2_Send(char *buf, u16 len)
{
    u16 t;   /* 字节循环计数 */
    for (t = 0; t < len; t++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, buf[t]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

/* USART2 中断服务：IDLE 到来即「4G 发来一帧」。
   DMA 缓冲仍由硬件按帧填充，软件立即把它追加进环形缓冲；
   环满时丢弃本帧剩余字节并计一次溢出，不覆盖未消费数据 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        u16 len;   /* 本帧实收字节数 */
        u16 i;     /* 逐字节入环计数 */

        USART_ReceiveData(USART2);   /* 读 DR：清 IDLE 置位条件 */
        len = (u16)(DMA_Rec2_Len - DMA_GetCurrDataCounter(DMA1_Channel6));
        if (len > DMA_Rec2_Len)
            len = DMA_Rec2_Len;      /* 防御：计数异常时不越界 */
        Usart2_Rec_Cnt = len;

        for (i = 0; i < len; i++)
        {
            u16 next = Usart2_RxUseMask((u16)(s_usart2_rx_head + 1));   /* 写指针的下一位置 */
            if (next == s_usart2_rx_tail)   /* 环满：追上读指针 */
            {
                s_usart2_rx_overflow_count++;   /* 记一次溢出 */
                break;                          /* 丢弃本帧剩余字节，绝不覆盖未读数据 */
            }
            s_usart2_rx_ring[s_usart2_rx_head] = DMA_Rece_Buf2[i];
            s_usart2_rx_head = next;
        }

        USART_ClearITPendingBit(USART2, USART_IT_IDLE);
        MYDMA_Enable2(DMA1_Channel6);   /* 重启 DMA，准备下一帧 */
    }
}


/* 重启接收 DMA：先关通道才能改计数器，重装满长度后再打开 */
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);                       /* 关通道：计数器仅在关闭时可写 */
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec2_Len);   /* 重装缓冲长度 */
    DMA_Cmd(DMA_CHx, ENABLE);                        /* 重新启动搬运 */
}
