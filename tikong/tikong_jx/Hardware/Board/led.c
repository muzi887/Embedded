#include "led.h"
#include "delay.h"
#include "rly.h"
#include "RTC.h"
#include "usart3.h"
#include "uart4.h"
#include "uart5.h"
#include "usart1.h"
#include "usart2.h"
#include "4G.h"

extern u8 led_flag;

/* ===================== 初始化 ===================== */
void Led_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC, ENABLE);

	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;

	/* LED4 PA7 */
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_7;
	GPIO_Init(GPIOA, &GPIO_InitStruct);

	/* LED3 LED2 */
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_Init(GPIOC, &GPIO_InitStruct);

	/* LED1 */ // 红色灯，告警灯
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_SetBits(GPIOA, GPIO_Pin_7); 
	GPIO_SetBits(GPIOC, GPIO_Pin_4);
	GPIO_SetBits(GPIOC, GPIO_Pin_5);
	GPIO_SetBits(GPIOB, GPIO_Pin_0);
}

void test(void)
{
	static u8 cnt = 0, rlycnt = 0, tcnt = 0;
	RTC_Time CTime;
	uint64_t timestamp_millis;
	char *cmd;
	if (led_flag) // 1s
	{
		led_flag = 0;
		cnt++;
		if (cnt == 4)
		{
			cnt = 0;
			GPIO_SetBits(GPIOA, GPIO_Pin_7); // 灭
			GPIO_SetBits(GPIOC, GPIO_Pin_4); // 低电平亮
			GPIO_SetBits(GPIOC, GPIO_Pin_5);
			GPIO_ResetBits(GPIOB, GPIO_Pin_0);
		}
		else if (cnt == 3)
		{
			GPIO_SetBits(GPIOA, GPIO_Pin_7); // 灭
			GPIO_SetBits(GPIOC, GPIO_Pin_4); // 低电平亮
			GPIO_ResetBits(GPIOC, GPIO_Pin_5);
			GPIO_SetBits(GPIOB, GPIO_Pin_0);
		}
		else if (cnt == 2)
		{
			GPIO_SetBits(GPIOA, GPIO_Pin_7);   // 灭
			GPIO_ResetBits(GPIOC, GPIO_Pin_4); // 低电平亮
			GPIO_SetBits(GPIOC, GPIO_Pin_5);
			GPIO_SetBits(GPIOB, GPIO_Pin_0);
		}
		else
		{
			GPIO_ResetBits(GPIOA, GPIO_Pin_7); // 亮
			GPIO_SetBits(GPIOC, GPIO_Pin_4);
			GPIO_SetBits(GPIOC, GPIO_Pin_5);
			GPIO_SetBits(GPIOB, GPIO_Pin_0);
		}

		if (tcnt == 0)
		{
			RtcChip_GetTime(&CTime);
			printf("%d-%d-%d  %d:%d:%d  w:%X", CTime.year, CTime.month, CTime.date,
				   CTime.hour, CTime.minute, CTime.second, CTime.dayOfWeek);

			timestamp_millis = RtcChip_To_MillisTimestamp_s(&CTime);
			printf(" -- %llu\n", timestamp_millis);

			if (GPRS_LINKA)
			{
				printf("GPRS ON LINE\n");
			}
		}
		tcnt++;

		if (tcnt >= 20)
		{
			tcnt = 0;
		}

		rlycnt++;

		if (rlycnt == 1)
		{
			rly1out = 1; // 1:继电器吸合   0：继电器断开
		}
		else if (rlycnt == 6)
		{
			rly2out = 1;
		}
		else if (rlycnt == 10)
		{
			rly2out = 0;
			rly1out = 0;
			cmd = "usr.cn#AT+CSQ?\r\n";
			USART3_SendData((uint8_t *)cmd, 16);
		}
		else if (rlycnt >= 11)
		{
			rlycnt = 11;
		}

		//		//rs485 发送
		//		RS485_SendData(buf,8);
	}

	//	if(RS485_RX_Complete)
	//	{
	//		RS485_RX_Complete = 0;
	//		printf("RS485:  ");
	//		for(i=0;i<RS485_RX_CNT;i++)
	//		{
	//			printf("%x ",RS485_RX_BUF[i]);
	//		}
	//		printf("\n");
	//	}
	//
	//	if(USART3_RX_Complete)
	//	{
	//		USART3_RX_Complete = 0;
	//		printf("uart3:  ");
	//		for(i=0;i<USART3_RX_CNT;i++)
	//		{
	//			printf("%c",USART3_RX_BUF[i]);
	//		}
	//		printf("\n");
	//	}
	//
	//	if (UART5_RX_Complete)
	//	{
	//		UART5_RX_Complete=0;
	//		USART5_SendData(UART5_RX_BUF,UART5_RX_CNT);
	//	}
	//
	//	if (UART4_RX_Complete)
	//	{
	//		UART4_RX_Complete=0;
	//		USART4_SendData(UART4_RX_BUF,UART4_RX_CNT);
	//	}
}
