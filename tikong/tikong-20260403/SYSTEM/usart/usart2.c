#include "usart2.h"
#include "delay.h"
#include "DS3231.h"
#include "Live_data.h"

__align(4) u8 RS485_RX_BUF[RS485_REC_LEN]; // RS485接收缓冲区
u16 RS485_RX_CNT = 0;
u8 RS485_RX_Complete = 0;

extern int caseNumber;
extern long long messageId;
extern const char *requestId;
extern long long requestTimestamp;
extern long long requestTimestamp_1;
extern char requestId_1[8];
extern DS3231_Time currentTime;      // 存放时间结构体
char result1[RS485_REC_LEN * 2 + 1]; // 每字节两个HEX字符，加结尾\0

// DMA接收初始化
void RS485_DMA_Init(void)
{
  DMA_InitTypeDef DMA_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE); // 使能DMA时钟

  DMA_DeInit(DMA1_Stream5);                                               // USART2 RX使用DMA1 Stream5
  DMA_InitStructure.DMA_Channel = DMA_Channel_4;                          // 选择DMA通道
  DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;       // 外设地址
  DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)RS485_RX_BUF;         // 内存地址
  DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;                 // 外设到内存
  DMA_InitStructure.DMA_BufferSize = RS485_REC_LEN;                       // 缓冲区大小
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;        // 外设地址不递增
  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                 // 内存地址递增
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设数据大小
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         // 内存数据大小
  DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                           // 普通传输模式
  DMA_InitStructure.DMA_Priority = DMA_Priority_High;                     // 优先级
  DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;                  // 禁止FIFO模式
  DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
  DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
  DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
  DMA_Init(DMA1_Stream5, &DMA_InitStructure); // 初始化DMA结构体

  // 使能USART2 DMA接收
  USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);
  DMA_Cmd(DMA1_Stream5, ENABLE); // 使能DMA通道
}

// 初始化USART2（RS485）和DMA
void RS485_Init(u32 bound)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);  // 使能GPIO时钟
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE); // 使能USART2时钟

  // 配置引脚复用功能
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2); // PA2为USART2
  GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2); // PA3为USART2

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3; // 配置PA2, PA3为复用功能
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(GPIOA, &GPIO_InitStructure); // 初始化PA2, PA3

  // 485发送接收控制引脚
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;     // PA1控制发送/接收
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT; // 配置为输出模式
  GPIO_Init(GPIOA, &GPIO_InitStructure);        // 初始化PA1

  // 配置USART2
  USART_InitStructure.USART_BaudRate = bound;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 收发模式
  USART_Init(USART2, &USART_InitStructure);

  // 使能空闲中断（检测帧结束）
  USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);

  USART_Cmd(USART2, ENABLE); // 使能USART2

  // 配置485收发模式
  RS485_TX_EN = 0; // 默认接收模式

  // 初始化DMA接收
  RS485_DMA_Init();

  // 配置USART2中断
  NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

// DMA重新初始化
// void RS485_DMA_ReInit(void)
//{
//    // 关闭DMA通道
//    DMA_Cmd(DMA1_Stream5, DISABLE);

//    // 清除中断标志
//    DMA1->LIFCR = DMA_FLAG_TCIF5 | DMA_FLAG_HTIF5 | DMA_FLAG_TEIF5 | DMA_FLAG_DMEIF5 | DMA_FLAG_FEIF5;

//    /* 3. 配置DMA参数 */
//    DMA1_Stream5->NDTR = RS485_REC_LEN;                  // 设置数据长度
//    DMA1_Stream5->M0AR = (uint32_t)RS485_RX_BUF;         // 重新设置内存地址
//    DMA1_Stream5->CR |= DMA_SxCR_EN;                     // 重新使能通道
//}

void RS485_DMA_ReInit(void)
{
  FunctionalState st;
  __IO uint32_t tmp;

  /* 1) 关闭 */
  DMA_Cmd(DMA1_Stream5, DISABLE);

  /* 2) 等待 EN 位变为 0 */
  do
  {
    st = DMA_GetCmdStatus(DMA1_Stream5);
  } while (st != DISABLE);

  /* 3) 清除所有中断标志 注意：Stream5 用 HIFCR */
  DMA_ClearFlag(DMA1_Stream5,
                DMA_FLAG_TCIF5 | DMA_FLAG_HTIF5 | DMA_FLAG_TEIF5 |
                    DMA_FLAG_DMEIF5 | DMA_FLAG_FEIF5);
  /* 等效写法
     DMA1->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 |
                   DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;
  */

  /* 4) 重新设置数据长度和地址，Normal 模式下需要重新装载 */
  DMA1_Stream5->NDTR = RS485_REC_LEN;
  DMA1_Stream5->M0AR = (uint32_t)RS485_RX_BUF;

  /* 5) 读 USART IDLE清除 SR 再读 DR */
  tmp = USART2->SR;
  tmp = USART2->DR;
  (void)tmp;

  /* 6) 重新开启 */
  DMA_Cmd(DMA1_Stream5, ENABLE);
}

void USART2_IRQHandler(void)
{
  // 检查是否触发了空闲中断
  if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
  {
    USART_ReceiveData(USART2); // 清除空闲中断标志
    // 计算接收到的数据长度
    RS485_RX_CNT = RS485_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream5);
    RS485_RX_Complete = 1;
    // 重新配置DMA
    RS485_DMA_ReInit();
  }
}

// RS485发送len个字节.
// buf:待发送数据地址
// len:发送的字节数(为了和标准库的接收匹配,这里建议不要超过64个字节)
void RS485_SendData(u8 *buf, u8 len)
{
  u8 t;
  RS485_TX_EN = 1;          // 切换为发送模式
  for (t = 0; t < len; t++) // 循环发送数据
  {
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
    {
    }                               // 等待发送完成
    USART_SendData(USART2, buf[t]); // 发送数据
  }
  while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
    ;              // 等待发送完成
  RS485_TX_EN = 0; // 切换为接收模式
  RS485_RX_CNT = 0;
  delay_ms(5);
}

//------------RS485串口2----485接收---------------------------
void Rs485Process(void)
{
  //	int i;
  //	long long ts1;
  //	long long qr_code_time;
  //	char jsonStr[2048]; // 分配足够大的空间
  //	long long messageId111;
  //
  //
  //	if (RS485_RX_Complete)
  //	{
  //		for (i = 0; i < RS485_RX_CNT; i++)
  //		{
  //			sprintf(&result1[i * 2], "%02X", RS485_RX_BUF[i]);
  //		}
  //		result1[RS485_RX_CNT * 2] = '\0'; // 末尾加结束符

  //		printf("result = %s\r\n", result1);

  //		if (caseNumber == 1)
  //		{
  //			First_Reply(messageId, requestId_1, requestTimestamp_1);
  //		}
  //		else
  //		{
  //			DS3231_GetTime(&currentTime);
  //			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
  //			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
  //			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);

  //			snprintf(jsonStr, sizeof(jsonStr),
  //					 "1,{"
  //					 "\"messageId\": %lld,"
  //					 "\"body\": [{"
  //					 "\"key\": \"qr_code_report\","
  //					 "\"ts\": %lld,"
  //					 "\"info\": [{"
  //					 "\"key\": \"qr_code_data\","
  //					 "\"value\": \"%s\""
  //					 "}, {"
  //					 "\"key\": \"qr_code_time\","
  //					 "\"value\": %lld"
  //					 "}, {"
  //					 "\"key\": \"handle_result\","
  //					 "\"value\": \"%s\""
  //					 "}]"
  //					 "}]"
  //					 "}",
  //					 messageId111, ts1, json1, qr_code_time, result1);
  //			printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
  //			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
  //		}
  //		RS485_RX_Complete = 0;
  //		RS485_RX_CNT = 0;
  //	}
}
