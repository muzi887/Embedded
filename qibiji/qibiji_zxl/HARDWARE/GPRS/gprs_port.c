#include "gprs_port.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "debug_uart.h"

static char s_dma_buf[GPRS_DMA_BUF_LEN];
static char s_stage[GPRS_RX_STAGE_SLOTS][GPRS_DMA_BUF_LEN];
static volatile uint16_t s_stage_len[GPRS_RX_STAGE_SLOTS]; /* s_stage_* ISR 和主循环两头访问 */
static volatile uint8_t  s_stage_full[GPRS_RX_STAGE_SLOTS];
static volatile uint8_t  s_stage_write = 0u;
volatile uint8_t g_gprs_banner_seen = 0u;

/* C90-safe printf wrapper: format into a stack buffer, then send via DEBUG_UART */
void GPRS_Printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;                                 /* 可变参数游标 variable argument pointer */
    va_start(ap, fmt);                          /* 初始化游标，指向第1个可变参数 */
    (void)vsnprintf(buf, sizeof(buf), fmt, ap); /* 取参数+格式化写buf */
    va_end(ap);                                 /* 清理收尾 */
    DEBUG_UART_SendString(buf);
}

uint8_t GPRS_PORT_BufContains(const char *buf, uint16_t len, const char *needle)
{
    uint16_t nlen = (uint16_t)strlen(needle);
    uint16_t i;
    if ((buf == NULL) || (needle == NULL) || (len < nlen) || (nlen == 0u))
        return 0u;
    for (i = 0u; i <= (uint16_t)(len - nlen); i++)
    {
        if (memcmp(buf + i, needle, nlen) == 0)
            return 1u;
    }
    return 0u;
}

void GPRS_PORT_Init(uint32_t baud)
{
	// GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE); 
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);	   

  /* PA2 TX */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; 
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure);

  /* PA3 RX */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);	

	// USART 初始化设置
	USART_DeInit(USART2);                                                           // 复位串口2
	USART_InitStructure.USART_BaudRate = baud;										                  // 一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;						          // 字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;							            // 一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;								              // 无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;					        // 收发模式
	USART_Init(USART2, &USART_InitStructure);	 

	// NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1u; 
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1u;	
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;	
	NVIC_Init(&NVIC_InitStructure);

	USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);              // 开启空闲中断
	USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);              // 使能串口1 DMA接收
	USART_Cmd(USART2, ENABLE);					                        // 使能串口

    /* USART2_RX 固定映射到 DMA1_Channel6 */
    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DR;            /* 外设端：串口数据寄存器，读它就取走一个字节 */
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)s_dma_buf;              /* 内存端：接收缓冲区首地址 */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;       /* 方向：外设 → 内存，即接收 */
    DMA_InitStructure.DMA_BufferSize         = GPRS_DMA_BUF_LEN;            /* 传输个数；数据宽度是字节，所以等于字节数 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;   /* 外设地址不自增：始终从 DR 取 */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;        /* 内存地址自增：逐字节往后填 */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; /* 外设端宽度 8 位 */
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;     /* 内存端宽度 8 位 */
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;             /* 单次模式：传满即停，通道自动关闭，下次接收要重新装载 */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;         /* 中优先级 */
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;             /* 关闭内存到内存模式 */
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE);                                         /* 使能通道，开始搬运；Normal 模式下只生效一轮 */
}

/* 重新装载 DMA 接收通道。
 * Normal 模式传满即停、CNDTR 归零，不重装就再也收不到下一帧。
 * 顺序不能改：运行中 CNDTR 只读，必须先关通道。 */
static void s_dma_restart(void)
{
    DMA_Cmd(DMA1_Channel6, DISABLE);                              /* 关通道，CNDTR 才可写 */
    DMA_SetCurrDataCounter(DMA1_Channel6, GPRS_DMA_BUF_LEN);      /* 计数回满 */
    DMA_Cmd(DMA1_Channel6, ENABLE);                               /* 重新开闸 */
}

/* USART2 IDLE ISR: entered only after the bus has been quiet for one
 * character time, i.e. a complete frame has arrived.  Interrupt Service Routine */
void USART2_IRQHandler(void)   /* Interrupt Request */
{
    uint16_t len;
    uint8_t  slot;

    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        /* Clear IDLE: read SR first, then DR. Reverse order never clears it. */
        (void)USART2->SR;
        (void)USART2->DR;

        DMA_Cmd(DMA1_Channel6, DISABLE);            /* Stop first: CNDTR readable, no new byte lands */
        len = GPRS_DMA_BUF_LEN - DMA_GetCurrDataCounter(DMA1_Channel6);  /* full - remaining = frame length */

        slot = s_stage_write;                       /* next free slot */
        if (!s_stage_full[slot] && (len > 0u))      /* slot busy or empty frame: drop, never block in ISR */
        {
            memcpy(s_stage[slot], s_dma_buf, len);  /* copy whole frame, s_dma_buf reusable at once */
            s_stage_len[slot]  = len;
            s_stage_full[slot] = 1u;
            s_stage_write = (uint8_t)((slot + 1u) % GPRS_RX_STAGE_SLOTS);

            /* Banner check done here to save the main loop a rescan.
             * Length-bounded compare: strstr would read past len. */
            if (GPRS_PORT_BufContains(s_stage[slot], len, "WH-LTE-7S1"))
                g_gprs_banner_seen = 1u;
        }
        s_dma_restart();                            /* Without this, the next frame is never received */
    }
}

/* Take one staged frame (oldest first).
 * Returns 1 and hands back a pointer into the staging buffer, 0 if empty. */
uint8_t GPRS_PORT_RxStageTake(char **buf, uint16_t *len)
{
    uint8_t i;

    for (i = 0u; i < GPRS_RX_STAGE_SLOTS; i++)
    {
        if (s_stage_full[i])                        /* slot the ISR has finished filling */
        {
            *buf = s_stage[i];                      /* hand out data BEFORE releasing the slot */
            *len = s_stage_len[i];
            s_stage_full[i] = 0u;                   /* release: ISR may now reuse this slot */
            return 1u;
        }
    }
    return 0u;                                      /* nothing staged */
}

void GPRS_PORT_Send(const char *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0u; i < len; i++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
            ;
        USART_SendData(USART2, (uint16_t)buf[i]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
        ;
}