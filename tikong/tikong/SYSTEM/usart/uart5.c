#include "uart5.h"
#include "delay.h"
#include "main.h"

__align(4) u8 UART5_RX_BUF[UART5_REC_LEN];
u16 UART5_RX_CNT = 0;
u8 UART5_RX_Complete = 0;

/* DMA接收初始化 */
static void UART5_DMA_RX_Init(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

	DMA_DeInit(DMA1_Stream0); /* UART5_RX 使用 DMA1 Stream0 */
	while (DMA_GetCmdStatus(DMA1_Stream0) != DISABLE)
	{
	}

	DMA_InitStructure.DMA_Channel = DMA_Channel_4;
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&UART5->DR;
	DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)UART5_RX_BUF;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
	DMA_InitStructure.DMA_BufferSize = UART5_REC_LEN;
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; /* 参考UART4，使用普通模式 */
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
	DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
	DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_Init(DMA1_Stream0, &DMA_InitStructure);

	/* 这里和UART4一样保留NVIC风格，虽然当前没开DMA中断 */
	NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream0_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_DMACmd(UART5, USART_DMAReq_Rx, ENABLE);
	DMA_Cmd(DMA1_Stream0, ENABLE);
}

/* UART5初始化 */
void uart5_init(u32 bound)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE);

	/* 引脚复用 */
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource12, GPIO_AF_UART5);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource2, GPIO_AF_UART5);

	/* UART5端口配置
	   PC12 -> TX
	   PD2  -> RX
	*/
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	/* UART5初始化参数 */
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(UART5, &USART_InitStructure);

	/* 使能空闲中断 */
	USART_ITConfig(UART5, USART_IT_IDLE, ENABLE);

	USART_Cmd(UART5, ENABLE);

	/* 初始化DMA接收 */
	UART5_DMA_RX_Init();

	/* UART5中断配置 */
	NVIC_InitStructure.NVIC_IRQChannel = UART5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

/* DMA重新初始化 */
void UART5_DMA_ReInit(void)
{
	/* 1. 关闭DMA通道 */
	DMA_Cmd(DMA1_Stream0, DISABLE);
	while (DMA_GetCmdStatus(DMA1_Stream0) != DISABLE)
	{
	}

	/* 2. 清除中断标志 */
	DMA1->LIFCR = DMA_FLAG_TCIF0 | DMA_FLAG_HTIF0 | DMA_FLAG_TEIF0 | DMA_FLAG_DMEIF0 | DMA_FLAG_FEIF0;

	/* 3. 配置DMA参数 */
	DMA1_Stream0->NDTR = UART5_REC_LEN;
	DMA1_Stream0->M0AR = (uint32_t)UART5_RX_BUF;
	DMA1_Stream0->CR |= DMA_SxCR_EN;
}

/* UART5中断服务函数（处理空闲中断） */
void UART5_IRQHandler(void)
{
	if (USART_GetITStatus(UART5, USART_IT_IDLE) != RESET)
	{
		USART_ReceiveData(UART5); /* 清除空闲中断标志 */

		/* 计算接收长度 */
		UART5_RX_CNT = UART5_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream0);
		UART5_RX_Complete = 1;

		/* 重新配置DMA */
		UART5_DMA_ReInit();
	}
}

/* 发送len个字节 */
void USART5_SendData(const char *buf, uint16_t len)
{
	uint16_t t;

	for (t = 0; t < len; t++)
	{
		while (USART_GetFlagStatus(UART5, USART_FLAG_TXE) == RESET)
		{
		}
		USART_SendData(UART5, (uint8_t)buf[t]);
	}

	while (USART_GetFlagStatus(UART5, USART_FLAG_TC) == RESET)
	{
	}
}
