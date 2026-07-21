#include "usart.h"

char DMA_Rece_Buf4[DMA_Rec4_Len];
u16 Usart4_Rec_Cnt;

// 串口4
// bound:波特率
void uart4_init(uint32_t bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	DMA_InitTypeDef DMA_InitStructure;
  
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOC, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);	  //使能USART4，GPIO4S时钟
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);			//使能DMA时钟
	
	//U4CE
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;	 
  GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP; 		     //推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		       //IO口速度为50MHz
  GPIO_Init(GPIOD, &GPIO_InitStructure);
	GPIO_ResetBits(GPIOD, GPIO_Pin_6);
	 
	USART_DeInit(UART4);  //复位串口4
	//UART4_TX    PC.10
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10; 							 
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;					//复用推挽输出
	GPIO_Init(GPIOC, &GPIO_InitStructure); 									//初始化PA2
 
	//UART4_RX	  PC.11
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;		//浮空输入
	GPIO_Init(GPIOC, &GPIO_InitStructure);  								//初始化PA3

 //UART4 NVIC 配置
	//NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);  
	
	NVIC_InitStructure.NVIC_IRQChannel = UART4_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=1 ;//抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;			//子优先级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;					//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	

	//USART 初始化设置
 	USART_InitStructure.USART_BaudRate = bound;									//一般设置为9600;
 	USART_InitStructure.USART_WordLength = USART_WordLength_8b; //字长为8位数据格式
 	USART_InitStructure.USART_StopBits = USART_StopBits_1;			//一个停止位
 	USART_InitStructure.USART_Parity = USART_Parity_No;					//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

	USART_Init(UART4, &USART_InitStructure); 				//初始化串口
	USART_ITConfig(UART4, USART_IT_IDLE, ENABLE);		//开启空闲中断
 	USART_DMACmd(UART4,USART_DMAReq_Rx,ENABLE);  		//使能串口1 DMA接收
	USART_Cmd(UART4, ENABLE);                    		//使能串口 

	//相应的DMA配置
	DMA_DeInit(DMA2_Channel3);    
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&UART4->DR;   		//DMA外设ADC基地址
	DMA_InitStructure.DMA_MemoryBaseAddr = (u32)DMA_Rece_Buf4;    		//DMA内存基地址
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                //数据传输方向，从外设读取发送到内存
	DMA_InitStructure.DMA_BufferSize = DMA_Rec4_Len;                  //DMA通道的DMA缓存的大小
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;  //外设地址寄存器不变
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;  					//内存地址寄存器递增
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  //数据宽度为8位
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;   //数据宽度为8位
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                     //工作在正常缓存模式
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;             //DMA通道 x拥有中优先级 
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                      //DMA通道x没有设置为内存到内存传输
	DMA_Init(DMA2_Channel3, &DMA_InitStructure);  //根据DMA_InitStruct中指定的参数初始化DMA的通道USART1_Tx_DMA_Channel所标识的寄存器
			
	DMA_Cmd(DMA2_Channel3, ENABLE);  //正式驱动DMA传输
}

//---------------------串口数据发送函数----------------------------//
// 串口1数据发送函数
// 发送len个字节.
// buf:发送区首地址
// len:发送的字节数
void Usart4_Send(char *buf, u16 len)
{
    u16 t;
		RS485_CE4_H;
    for (t = 0; t < len; t++) // 循环发送数据
    {
        while (USART_GetFlagStatus(UART4, USART_FLAG_TC) == RESET){}
        USART_SendData(UART4, buf[t]);
    }
    while (USART_GetFlagStatus(UART4, USART_FLAG_TC) == RESET){}
		RS485_CE4_L;
}
//----------------------------串口中断函数----------------------------
// 串口1中断服务程序
void UART4_IRQHandler(void)
{
    if (USART_GetITStatus(UART4, USART_IT_IDLE) != RESET)
    {
        USART_ReceiveData(UART4);
        Usart4_Rec_Cnt = DMA_Rec4_Len - DMA_GetCurrDataCounter(DMA2_Channel3);
				Usart1_Send((char *)DMA_Rece_Buf4,Usart4_Rec_Cnt);   //GPRS
        USART_ClearITPendingBit(UART4, USART_IT_IDLE);
        MYDMA_Enable4(DMA2_Channel3);
    }
}
void MYDMA_Enable4(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);                     // 关闭USART1 TX DMA1 所指示的通道
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec4_Len); // DMA通道的DMA缓存的大小
    DMA_Cmd(DMA_CHx, ENABLE);                      // 使能USART1 TX DMA1 所指示的通道
}