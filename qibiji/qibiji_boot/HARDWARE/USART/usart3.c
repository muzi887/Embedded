#include "usart.h"

char DMA_Rece_Buf3[DMA_Rec3_Len];
u16 Usart3_Rec_Cnt;

void uart3_init(uint32_t bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD|RCC_APB2Periph_AFIO, ENABLE);		//使能USART3和GPIOB时钟
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);			//使能DMA时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);		//使能USART3时钟
	
	USART_DeInit(USART3);  //复位串口3
	//GPIO_FullRemap_USART3 完全重映射 D8 D9
	GPIO_PinRemapConfig(GPIO_FullRemap_USART3,ENABLE);
 //USART3_TX   
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8; 							//PB.10  -- PD8
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;					//复用推挽输出
	GPIO_Init(GPIOD, &GPIO_InitStructure); 									//初始化
 
	//USART3_RX	  
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;							//PB11  -- PD9
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;		//浮空输入
	GPIO_Init(GPIOD, &GPIO_InitStructure);  								//初始化
	
 //Usart3 NVIC 配置
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);   
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=0 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;			//响应优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;					//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);													//根据指定的参数初始化VIC寄存器

 //USART 初始化设置
  USART_InitStructure.USART_BaudRate = bound;									//一般设置为9600;

	USART_InitStructure.USART_WordLength = USART_WordLength_8b;	//字长为8位数据格式 // 使用奇偶校验，一个字长为9位数据
	USART_InitStructure.USART_StopBits   = USART_StopBits_1;		//一个停止位
	USART_InitStructure.USART_Parity     = USART_Parity_No;			//偶校验位
	
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

	USART_Init(USART3, &USART_InitStructure);      //初始化串口
	USART_ITConfig(USART3, USART_IT_IDLE, ENABLE); //开启空闲中断
	
	USART_DMACmd(USART3,USART_DMAReq_Rx,ENABLE);   //使能串口1 DMA接收
	USART_Cmd(USART3, ENABLE);                     //使能串口 

	//相应的DMA配置
	DMA_DeInit(DMA1_Channel3);   																			//将DMA的通道5的寄存器重设为缺省值  串口1对应的是DMA通道5
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART3->DR;  		//DMA外设ADC基地址
	DMA_InitStructure.DMA_MemoryBaseAddr = (u32)DMA_Rece_Buf3;  			//DMA内存基地址
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;  							//数据传输方向，从外设读取发送到内存
	DMA_InitStructure.DMA_BufferSize = DMA_Rec3_Len;  								//DMA通道的DMA缓存的大小
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;  //外设地址寄存器不变
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;  					//内存地址寄存器递增
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  //数据宽度为8位
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; 	//数据宽度为8位
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;  										//工作在正常缓存模式
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium; 						//DMA通道 x拥有中优先级 
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;  										//DMA通道x没有设置为内存到内存传输
	DMA_Init(DMA1_Channel3, &DMA_InitStructure);  										//根据DMA_InitStruct中指定的参数初始化DMA的通道USART1_Tx_DMA_Channel所标识的寄存器
			
	DMA_Cmd(DMA1_Channel3, ENABLE);  //正式驱动DMA传输
}

//---------------------串口中断函数----------------------------//
// 串口3中断服务程序
void USART3_IRQHandler(void)
{
	if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET) // 空闲中断
	{
		USART_ReceiveData(USART3);											   // 读取数据 注意：这里必须要读取数据才能够清除中断标志位，我也不知道为什么
		Usart3_Rec_Cnt = DMA_Rec3_Len - DMA_GetCurrDataCounter(DMA1_Channel3); // 计算接收帧数据长度
		USART_ClearITPendingBit(USART3, USART_IT_IDLE);						   // 清除中断标志
		MYDMA_Enable3(DMA1_Channel3); // 恢复DMA指针，等待下一次的接收
	}
}


// 串口3数据发送函数
void Usart3_Send(char *buf, u16 len)
{
	u8 t;
	RS485_CE3_H;
	for (t = 0; t < len; t++) // 循环发送数据
	{
		while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET){}
		USART_SendData(USART3, buf[t]);
	}
	while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET){}
	RS485_CE3_L;
}

//------------------------------------------------------------------------
void MYDMA_Enable3(DMA_Channel_TypeDef *DMA_CHx)
{
	DMA_Cmd(DMA_CHx, DISABLE);					   // 关闭USART1 TX DMA1 所指示的通道
	DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec3_Len); // DMA通道的DMA缓存的大小
	DMA_Cmd(DMA_CHx, ENABLE);					   // 使能USART1 TX DMA1 所指示的通道
}