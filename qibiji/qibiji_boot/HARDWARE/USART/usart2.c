#include "usart.h"

char DMA_Rece_Buf2[DMA_Rec2_Len];
u16 Usart2_Rec_Cnt;

static char s_usart2_rx_ring[USART2_RX_RING_SIZE];
static volatile u16 s_usart2_rx_head = 0;
static volatile u16 s_usart2_rx_tail = 0;
static volatile u32 s_usart2_rx_overflow_count = 0;

#define USART2_RX_RING_MASK ((u16)(USART2_RX_RING_SIZE - 1))

#if (USART2_RX_RING_SIZE & (USART2_RX_RING_SIZE - 1)) != 0
#error "USART2_RX_RING_SIZE must be a power of two"
#endif

static u16 Usart2_RxUseMask(u16 index)
{
	return (u16)(index & USART2_RX_RING_MASK);
}

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

u16 Usart2_RxCopy(char *dst, u16 max_len)
{
	u16 head;
	u16 tail;
	u16 available;
	u16 i;
	u32 primask;

	if (dst == 0 || max_len == 0)
		return 0;

	primask = __get_PRIMASK();
	__disable_irq();
	head = s_usart2_rx_head;
	tail = s_usart2_rx_tail;
	available = (u16)((head - tail) & USART2_RX_RING_MASK);
	if (available > max_len)
		available = max_len;
	for (i = 0; i < available; i++)
		dst[i] = s_usart2_rx_ring[Usart2_RxUseMask((u16)(tail + i))];
	__set_PRIMASK(primask);

	return available;
}

void Usart2_RxConsume(u16 len)
{
	u16 available;
	u32 primask = __get_PRIMASK();

	__disable_irq();
	available = (u16)((s_usart2_rx_head - s_usart2_rx_tail) & USART2_RX_RING_MASK);
	if (len > available)
		len = available;
	s_usart2_rx_tail = Usart2_RxUseMask((u16)(s_usart2_rx_tail + len));
	__set_PRIMASK(primask);
}

void Usart2_RxClear(void)
{
	u32 primask = __get_PRIMASK();

	__disable_irq();
	s_usart2_rx_tail = s_usart2_rx_head;
	__set_PRIMASK(primask);
}

u32 Usart2_RxOverflowCount(void)
{
	return s_usart2_rx_overflow_count;
}

// GPRS数据通信 ---4g
void uart2_init(uint32_t bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;
  
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);	//使能USART2和GPIOA时钟
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);			//使能DMA时钟
	 
	USART_DeInit(USART2);  //复位串口2
 //USART2_TX   PA.2
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; 							//PA.2
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;					//复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure); 									//初始化PA2
 
	//USART2_RX	  PA.3
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;		//浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);  								//初始化PA3

 //Usart2 NVIC 配置
	//NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);   
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=1 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;		//响应优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器

 //USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;//一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

	USART_Init(USART2, &USART_InitStructure); //初始化串口
	USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);//开启空闲中断
	USART_DMACmd(USART2,USART_DMAReq_Rx,ENABLE);   //使能串口1 DMA接收
	USART_Cmd(USART2, ENABLE);                    //使能串口 

	//相应的DMA配置
	DMA_DeInit(DMA1_Channel6);   //将DMA的通道5的寄存器重设为缺省值  串口1对应的是DMA通道5
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DR;  //DMA外设ADC基地址
	DMA_InitStructure.DMA_MemoryBaseAddr = (u32)DMA_Rece_Buf2;  //DMA内存基地址
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;  //数据传输方向，从外设读取发送到内存
	DMA_InitStructure.DMA_BufferSize = DMA_Rec2_Len;  //DMA通道的DMA缓存的大小
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;  //外设地址寄存器不变
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;  //内存地址寄存器递增
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  //数据宽度为8位
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; //数据宽度为8位
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;  //工作在正常缓存模式
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium; //DMA通道 x拥有中优先级 
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;  //DMA通道x没有设置为内存到内存传输
	DMA_Init(DMA1_Channel6, &DMA_InitStructure);  //根据DMA_InitStruct中指定的参数初始化DMA的通道USART1_Tx_DMA_Channel所标识的寄存器
			
	DMA_Cmd(DMA1_Channel6, ENABLE);  //正式驱动DMA传输
}

//----------------------------------串口发送函数------------------------------------
// 串口2数据发送函数
void Usart2_Send(char *buf, u16 len)
{
	u16 t;
  for(t=0;t<len;t++)		//循环发送数据
	{		   
		while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);	  
		USART_SendData(USART2,buf[t]);
	}	 
	while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);	
}

//----------------------------------中断服务程序-------------------------------------
// 串口2中断服务程序
void USART2_IRQHandler(void)
{
	if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET) // 空闲中断
	{
		u16 len;
		u16 i;

		USART_ReceiveData(USART2); // 读取数据以清除 IDLE 标志
		len = (u16)(DMA_Rec2_Len - DMA_GetCurrDataCounter(DMA1_Channel6));
		if (len > DMA_Rec2_Len)
			len = DMA_Rec2_Len;
		Usart2_Rec_Cnt = len;

		/*
		 * DMA 缓冲区仍由硬件按当前空闲帧填充；软件立即把它追加到环形缓冲。
		 * 不覆盖未消费数据：满时丢弃本帧剩余字节并递增溢出计数。
		 */
		for (i = 0; i < len; i++)
		{
			u16 next = Usart2_RxUseMask((u16)(s_usart2_rx_head + 1));
			if (next == s_usart2_rx_tail)
			{
				s_usart2_rx_overflow_count++;
				break;
			}
			s_usart2_rx_ring[s_usart2_rx_head] = DMA_Rece_Buf2[i];
			s_usart2_rx_head = next;
		}

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
