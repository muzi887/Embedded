#include "usart.h"

char DMA_Rece_Buf3[DMA_Rec3_Len];
u16 Usart3_Rec_Cnt;
u8 UART3_RX_Complete = 0;

/*
 * BLE (DX-BT24) reply accumulator: the idle ISR appends every received chunk
 * here, so a reply split over several bursts is not lost. Same scheme as the
 * USART2 (4G module) accumulator.
 */
char g_uart3_rx_acc[UART3_ACC_LEN];
volatile u16 g_uart3_rx_acc_len = 0;

/* Reset the accumulator before starting a new AT command. */
void uart3_rx_acc_reset(void)
{
	g_uart3_rx_acc_len = 0;
	g_uart3_rx_acc[0] = '\0';
}

/* Called from the USART3 idle ISR: keep every chunk of the module reply. */
void uart3_rx_acc_append(const char *src, u16 len)
{
	u16 i;

	for (i = 0; i < len; i++)
	{
		if (g_uart3_rx_acc_len >= (UART3_ACC_LEN - 1))
			break;
		g_uart3_rx_acc[g_uart3_rx_acc_len] = src[i];
		g_uart3_rx_acc_len++;
	}
	g_uart3_rx_acc[g_uart3_rx_acc_len] = '\0';
}

u16 uart3_rx_acc_len(void)
{
	return g_uart3_rx_acc_len;
}

const char *uart3_rx_acc_data(void)
{
	g_uart3_rx_acc[g_uart3_rx_acc_len] = '\0';
	return (const char *)g_uart3_rx_acc;
}

/*
 * USART3 with full remap: PD8 = TX, PD9 = RX, 9600 8N1.
 * Wired to the DX-BT24 BLE module (see README "蓝牙").
 */
void uart3_init(uint32_t bound)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO, ENABLE);
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

	USART_DeInit(USART3);
	GPIO_PinRemapConfig(GPIO_FullRemap_USART3, ENABLE); /* PD8 = TX, PD9 = RX */

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	/* Priority grouping is configured once in NVIC_Configuration() (2/2),
	 * so only the channel priorities are set here. */
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

	USART_Init(USART3, &USART_InitStructure);
	USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);
	USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
	USART_Cmd(USART3, ENABLE);

	DMA_DeInit(DMA1_Channel3);
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART3->DR;
	DMA_InitStructure.DMA_MemoryBaseAddr = (u32)DMA_Rece_Buf3;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_BufferSize = DMA_Rec3_Len;
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
	DMA_Init(DMA1_Channel3, &DMA_InitStructure);

	DMA_Cmd(DMA1_Channel3, ENABLE);
}

/* USART3 idle interrupt: capture the chunk, then re-arm the DMA. */
void USART3_IRQHandler(void)
{
	if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
	{
		USART_ReceiveData(USART3);
		Usart3_Rec_Cnt = DMA_Rec3_Len - DMA_GetCurrDataCounter(DMA1_Channel3);

		uart3_rx_acc_append(DMA_Rece_Buf3, Usart3_Rec_Cnt);
		UART3_RX_Complete = 1;

		USART_ClearITPendingBit(USART3, USART_IT_IDLE);
		MYDMA_Enable3(DMA1_Channel3);
	}
}

void Usart3_Send(char *buf, u16 len)
{
	u16 t;

	for (t = 0; t < len; t++)
	{
		while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET) {}
		USART_SendData(USART3, buf[t]);
	}
	while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET) {}
}

void MYDMA_Enable3(DMA_Channel_TypeDef *DMA_CHx)
{
	DMA_Cmd(DMA_CHx, DISABLE);
	DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec3_Len);
	DMA_Cmd(DMA_CHx, ENABLE);
}
