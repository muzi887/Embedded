#include "uart4.h"
#include "delay.h"
#include "main.h"
#include "cmd.h"
__align(4) u8 UART4_RX_BUF[UART4_REC_LEN];
u16 UART4_RX_CNT = 0;
u8 UART4_RX_Complete = 0;

char received_data[MAX_RECEIVE_LEN]; // 用于存放接收到的内容
int received_data_len = 0;			 // 存放接收到的数据长度
int USART4_RX_FLAG = 0;				 // 接收完成标志

extern char *valid_data;
extern u8 BeepNum;

// DMA接收初始化
static void UART4_DMA_RX_Init(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

	DMA_DeInit(DMA1_Stream2); // UART4_RX使用DMA1 Stream2
	DMA_InitStructure.DMA_Channel = DMA_Channel_4;
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&UART4->DR;
	DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)UART4_RX_BUF;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
	DMA_InitStructure.DMA_BufferSize = UART4_REC_LEN;
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; // 普通模式
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
	DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
	DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_Init(DMA1_Stream2, &DMA_InitStructure);

	// NVIC配置
	NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_DMACmd(UART4, USART_DMAReq_Rx, ENABLE);
	DMA_Cmd(DMA1_Stream2, ENABLE);
}

// UART4初始化
void uart4_init(u32 bound)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);

	// 引脚复用
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource10, GPIO_AF_UART4);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource11, GPIO_AF_UART4);

	// UART4端口配置
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	// UART4初始化参数
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx; // 接收模式
	USART_Init(UART4, &USART_InitStructure);

	// 使能空闲中断（检测帧结束）
	USART_ITConfig(UART4, USART_IT_IDLE, ENABLE);

	USART_Cmd(UART4, ENABLE);

	// 初始化DMA接收
	UART4_DMA_RX_Init();

	// UART4中断配置
	NVIC_InitStructure.NVIC_IRQChannel = UART4_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

// DMA重新初始化
void UART4_DMA_ReInit(void)
{
	/* 1. 关闭DMA通道 */
	DMA_Cmd(DMA1_Stream2, DISABLE);

	/* 2. 清除中断标志  */
	DMA1->LIFCR = DMA_FLAG_TCIF2 | DMA_FLAG_HTIF2 | DMA_FLAG_TEIF2 | DMA_FLAG_DMEIF2 | DMA_FLAG_FEIF2;

	/* 3. 配置DMA参数 */
	DMA1_Stream2->NDTR = UART4_REC_LEN;			 // 设置数据长度
	DMA1_Stream2->M0AR = (uint32_t)UART4_RX_BUF; // 重新设置内存地址
	DMA1_Stream2->CR |= DMA_SxCR_EN;			 // 重新使能通道
}

// UART4中断服务函数（处理空闲中断）
void UART4_IRQHandler(void)
{
	if (USART_GetITStatus(UART4, USART_IT_IDLE) != RESET)
	{
		USART_ReceiveData(UART4); // 清除空闲中断标志

		// 计算接收长度
		UART4_RX_CNT = UART4_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream2);
		UART4_RX_Complete = 1;

		// 重新配置DMA
		UART4_DMA_ReInit();
	}
}

//------------二维码串口4------------------------------
void QRProcess(void)
{
  int i;
	int ret = 0;
	long long ts1;
	char jsonStr[2048]; // 分配足够大的空间
	long long messageId111;
	long long qr_code_time;
	int len;

	if (UART4_RX_Complete)
	{
		 printf("RX[%d]: ", UART4_RX_CNT);
				for (i = 0; i < UART4_RX_CNT; i++)
				{
					printf("%c", UART4_RX_BUF[i]);
				}
				printf("\r\n");

		// 将接收到的内容存入 received_data 数组
		if (UART4_RX_CNT < MAX_RECEIVE_LEN)
		{
			// 使用 memcpy 确保按字节完全复制
			memcpy(received_data, UART4_RX_BUF, UART4_RX_CNT);
			received_data_len = UART4_RX_CNT; // 更新接收到的数据长度
		}
		else
		{
			// printf("Received data overflow!\r\n");
			received_data_len = MAX_RECEIVE_LEN; // 设定一个最大值
		}
		// 在数据复制后手动加上 '\0' 字符，确保 received_data 成为一个有效的 C 字符串
		received_data[received_data_len] = '\0';

		// 清零接收状态
		UART4_RX_Complete = 0;
		UART4_RX_CNT = 0;
		// 提取干净信息（二维码模块表头表尾乱码）
		valid_data = ExtractValidData_Bytes((const unsigned char *)received_data, received_data_len);

		//  访客指令（串口2发送） 二维码处理（已去除头尾）
		if (IsCvjFormat(valid_data))
		{
			// printf("new code\n");
			/*
			命令1： 添加黑名单
			命令2： 删除黑名单
			命令3： 上报二维码--需要调出黑名单验证
			命令4： 更新公钥
			命令5： 更新时间
			命令6： 清空黑名单
			*/

			// 公钥更新
			// CmdChar = ;
			switch (IsCmd(valid_data))
			{
			case '1':
				ret = DoCmd1(received_data); // 二维码黑名单新增
				break;
			case '2':
				ret = DoCmd2(received_data); // 删除黑名单
				break;
			case '3':
				ret = DoCmd3(received_data); // 上报二维码--需要调出黑名单验证
				break;
			case '4':
				ret = DoCmd4(received_data); // 更新公钥
				break;
			case '5':
				ret = DoCmd5(received_data); // 更新时间
				break;
			case '6':
				ret = DoCmd6(received_data); // 清空黑名单
				break;
			default:
				break;
			}

			if (ret >= 0)
				BeepNum = 1; // 正确
			else
			{
				BeepNum = 2;

				// 上报事件
				DS3231_GetTime(&currentTime);
				messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
				ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
				qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
				len = snprintf(jsonStr, sizeof(jsonStr),
						 "1,{"
						 "\"messageId\": %lld,"
						 "\"body\": [{"
						 "\"key\": \"qr_code_report\","
						 "\"ts\": %lld,"
						 "\"info\": [{"
						 "\"key\": \"qr_code_data\","
						 "\"value\": \"%s\""
						 "}, {"
						 "\"key\": \"qr_code_time\","
						 "\"value\": %lld"
						 "}, {"
						 "\"key\": \"handle_result\","
						 "\"value\": 0"
						 "}]"
						 "}]"
						 "}",
						 messageId111, ts1, received_data, qr_code_time);
						 if(len > sizeof(jsonStr)) {
								BeepNum = 3;	// 报错
								return;
						 }
				// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
				//offline disable
				USART3_SendData((uint8_t *)jsonStr, len);
			}

			// loadDataFromEEPROM(); // 读取黑名单
		}
		// 非访客梯控指令
		else
		{
			//			//  旧指令通过串口5发送
			//			USART5_Send_Data(received_data, received_data_len);

			// 报错
			// BeepNum = 3;

			// 上报事件
			DS3231_GetTime(&currentTime);
			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
			len = snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 0"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
					 if(len > sizeof(jsonStr)) {
							BeepNum = 3;	// 报错
							return;
					 }
			// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
			//offline disable
			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
		}

		// 清空内容存放区
		memset(received_data, 0, MAX_RECEIVE_LEN);
		USART4_RX_FLAG = 0;
	}
}
