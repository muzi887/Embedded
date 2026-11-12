#include "usart.h"
// #include "queue.h"
char Rece_Buf5[Rec5_Len];
u8 rx_lcd_complete = 0;
u16 Usart5_Rec_Cnt = 0;
//-----------------------------------------------------------
//   LCD -- DMG48270C043_04W
void uart5_init(uint32_t bound)
{
	// GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE); // 使能USART5时钟

	USART_DeInit(UART5); // 复位串口5
	// UART5_TX    PC.12
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
	GPIO_Init(GPIOC, &GPIO_InitStructure);			// 初始化PC12

	// UART5_RX	  PD.02
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // GPIO_Mode_IN_FLOATING;				//浮空输入
	GPIO_Init(GPIOD, &GPIO_InitStructure);		  // 初始化PD2

	// UART5 NVIC 配置
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
	NVIC_InitStructure.NVIC_IRQChannel = UART5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		  // 响应优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			  // IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);

	// USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;										// 一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;						// 字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;							// 一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;								// 无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;					// 收发模式

	USART_Init(UART5, &USART_InitStructure); // 初始化串口

	USART_ITConfig(UART5, USART_IT_IDLE, ENABLE); // 开启空闲中断
	USART_ITConfig(UART5, USART_IT_RXNE, ENABLE); // 开启接收中断

	USART_Cmd(UART5, ENABLE); // 使能串口
}

//---------------------串口数据发送函数----------------------------//
// 串口5数据发送函数
void Usart5_Send(char *buf, u16 len)
{
	u16 t;
	for (t = 0; t < len; t++) // 循环发送数据
	{
		while (USART_GetFlagStatus(UART5, USART_FLAG_TC) == RESET)
			;
		USART_SendData(UART5, buf[t]);
	}
	while (USART_GetFlagStatus(UART5, USART_FLAG_TC) == RESET)
	{
	}
}
//
// void Usart5_SendToLcd(char *buf)
// {
// 	int t;
// 	for (t = 0; t < buf[MAX_LENGTH_LCD - 1]; t++) // 循环发送数据
// 	{
// 		while (USART_GetFlagStatus(UART5, USART_FLAG_TC) == RESET)
// 			;
// 		USART_SendData(UART5, buf[t]);
// 	}
// 	while (USART_GetFlagStatus(UART5, USART_FLAG_TC) == RESET)
// 	{
// 	}
// }

// 串口5中断服务程序
void UART5_IRQHandler(void)
{ // TODO 判断是否接收完成标志，目前只有硬件空闲中断接收完成
	static u16 u5cnt = 0;
	// 中断一次，接收一个字节数据，数据存入缓冲区
	if (USART_GetITStatus(UART5, USART_IT_RXNE) != RESET)
	{
		Rece_Buf5[Usart5_Rec_Cnt++] = USART_ReceiveData(UART5);
		USART_ClearITPendingBit(UART5, USART_IT_RXNE);
		//Usart3_Send((char *)Rece_Buf5, Usart5_Rec_Cnt);
		// printf("0");
	}

	if (USART_GetITStatus(UART5, USART_IT_IDLE) != RESET)
	{
		USART_ReceiveData(UART5);
		// 接收完成标志
		rx_lcd_complete = 1;
		// Usart5_Rec_Cnt = u5cnt;
		// u5cnt = 0;
		USART_ClearITPendingBit(UART5, USART_IT_IDLE);
		//		printf("len = %d\n",Usart5_Rec_Cnt);
	}
}
