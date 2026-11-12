#include "usart.h"
#include "uart3_at_sequence.h"
#include <string.h>

char DMA_Rece_Buf2[DMA_Rec2_Len];
u16 Usart2_Rec_Cnt;
u8 UART2_RX_Complete = 0;

/*
 * RX 乒乓暂存：IDLE 中断里把刚收到的整帧搬进暂存槽，主循环再从容消费。
 *
 * 为什么需要：DMA 是 Normal 模式（非循环），IDLE 中断会立刻 MYDMA_Enable2 复用同一块
 * DMA_Rece_Buf2。若主循环正被长阻塞占住（例如 BLE 一次写 128 字节 EEPROM ≈ 1.28s），
 * 第二帧到达时会把第一帧的数据直接覆盖掉 —— 这是静默丢下行。加了暂存后，
 * 每帧在中断里就已被搬走，主循环停顿不再造成覆盖；两个槽都满时记一次覆盖计数。
 */
static char rxStage[UART2_RX_STAGE_SLOTS][DMA_Rec2_Len];
static volatile u16 rxStageLen[UART2_RX_STAGE_SLOTS];
static volatile u8 rxStageFull[UART2_RX_STAGE_SLOTS];
static volatile u8 rxStageWrite = 0;
volatile u16 g_uart2_rx_overwrite = 0; /* 两槽都满导致覆盖最旧帧的次数 */
volatile u8 g_uart2_banner_seen = 0;   /* 收到过模块启动 banner（WH-LTE-7S1） */
// GPRS数据传输 ---4g
void uart2_init(uint32_t bound)
{
	// GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE); // 使能USART2和GPIOA时钟
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);	   // 使能DMA时钟

	USART_DeInit(USART2); // 复位串口2
	// USART2_TX   PA.2
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; // PA.2
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure);			// 初始化PA2

	// USART2_RX	  PA.3
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);				  // 初始化PA3

	// Usart2 NVIC 配置
	// NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;		  // 子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			  // IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);							  // 根据指定的参数初始化VIC寄存器

	// USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;										// 一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;						// 字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;							// 一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;								// 无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;					// 收发模式

	USART_Init(USART2, &USART_InitStructure);	   // 初始化串口
	USART_ITConfig(USART2, USART_IT_IDLE, ENABLE); // 开启空闲中断
	USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE); // 使能串口1 DMA接收
	USART_Cmd(USART2, ENABLE);					   // 使能串口

	// 相应的DMA配置
	DMA_DeInit(DMA1_Channel6);												// 将DMA的通道5寄存器重设为缺省值  串口1对应的是DMA通道5
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DR;			// DMA外设ADC基地址
	DMA_InitStructure.DMA_MemoryBaseAddr = (u32)DMA_Rece_Buf2;				// DMA内存基地址
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;						// 数据传输方向，从外设读取发送到内存
	DMA_InitStructure.DMA_BufferSize = DMA_Rec2_Len;						// DMA通道的DMA缓存的大小
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;		// 外设地址寄存器不变
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;					// 内存地址寄存器递增
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 数据宽度为8位
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;			// 数据宽度为8位
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;							// 工作在正常缓存模式
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;					// DMA通道 x拥有中优先级
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;							// DMA通道x没有设置为内存到内存传输
	DMA_Init(DMA1_Channel6, &DMA_InitStructure);							// 根据DMA_InitStruct中指定的参数初始化DMA的通道USART1_Tx_DMA_Channel所标识的寄存器

	DMA_Cmd(DMA1_Channel6, ENABLE); // 正式驱动DMA传输
}

//----------------------------------串口发送函数------------------------------------
// 串口2数据发送函数
void Usart2_Send(char *buf, u16 len)
{
	u16 t;
	for (t = 0; t < len; t++) // 循环发送数据
	{
		while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
			;
		USART_SendData(USART2, buf[t]); 
	}
	while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
		;
}

//----------------------------------中断服务程序-------------------------------------
/* 长度受限的子串查找：buf 不一定以 '\0' 结尾，不能直接 strstr */
u8 Uart2_BufContains(const char *buf, u16 len, const char *needle)
{
	u16 nlen = (u16)strlen(needle);
	u16 i;

	if (buf == NULL || needle == NULL || len < nlen || nlen == 0u)
		return 0u;

	for (i = 0; i <= (u16)(len - nlen); i++)
	{
		if (memcmp(buf + i, needle, nlen) == 0)
			return 1u;
	}
	return 0u;
}

/* 取出一帧暂存（供主循环消费）；返回 1 表示取到，*buf 指向暂存槽内部数据 */
u8 Uart2_RxStageTake(char **buf, u16 *len)
{
	u8 i;

	if (buf == NULL || len == NULL)
		return 0u;

	for (i = 0u; i < UART2_RX_STAGE_SLOTS; i++)
	{
		if (rxStageFull[i])
		{
			*buf = rxStage[i];
			*len = rxStageLen[i];
			rxStageFull[i] = 0;
			return 1u;
		}
	}
	return 0u;
}

// 串口2中断服务程序
void USART2_IRQHandler(void)
{
	if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET) // 空闲中断
	{
		USART_ReceiveData(USART2);											   // 读取数据 注意：这里必须要读取数据才能够清除中断标志位，我也不知道为啥
		Usart2_Rec_Cnt = DMA_Rec2_Len - DMA_GetCurrDataCounter(DMA1_Channel6); // 计算接收帧数据长度

		/*
		 * 先把整帧搬进暂存槽，再去复用 DMA 缓冲（否则下一帧会覆盖本帧）。
		 * AT 配置序列运行期间跳过暂存：那一阶段由 uart3_at_sequence.c 直接读 DMA 缓冲，
		 * 暂存槽不会被消费，只会白白累加覆盖计数。
		 */
		if (Usart2_Rec_Cnt > 0u && !uart3_at_sequence_should_run())
		{
			u16 n = Usart2_Rec_Cnt;

			if (n > DMA_Rec2_Len)
				n = DMA_Rec2_Len;

			if (rxStageFull[rxStageWrite])
			{
				/* 当前槽还没被主循环取走 → 两槽都满，丢弃最旧的这一槽 */
				rxStageFull[rxStageWrite] = 0;
				g_uart2_rx_overwrite++;
			}

			memcpy(rxStage[rxStageWrite], DMA_Rece_Buf2, n);
			rxStageLen[rxStageWrite] = n;
			/* 用长度受限的查找（暂存槽没有 NUL 结尾，不能直接 strstr） */
			if (Uart2_BufContains(rxStage[rxStageWrite], n, "WH-LTE-7S1") != 0u)
				g_uart2_banner_seen = 1;
			rxStageFull[rxStageWrite] = 1;
			rxStageWrite = (u8)((rxStageWrite + 1u) % UART2_RX_STAGE_SLOTS);
		}

		// printf("u2len = %d\r\n",Usart2_Rec_Cnt);

		//Usart3_Send((char *)DMA_Rece_Buf2, Usart2_Rec_Cnt);
		UART2_RX_Complete = 1;

		USART_ClearITPendingBit(USART2, USART_IT_IDLE); // 清除中断标志
		MYDMA_Enable2(DMA1_Channel6);
	}
}
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx)
{
	DMA_Cmd(DMA_CHx, DISABLE);					   // 关闭USART1 TX DMA1 所指示的通道
	DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec2_Len); // DMA通道的DMA缓存的大小
	DMA_Cmd(DMA_CHx, ENABLE);					   // 使能USART1 TX DMA1 所指示的通道
}
