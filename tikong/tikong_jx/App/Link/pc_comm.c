#include "app_comm.h"
#include "usart1.h"
#include "delay.h"
#include "stdio.h"

void PCProcess(void)
{
	int i;
	if (USART_RX_Complete)
	{
		delay_ms(10);
		printf("RX[%d]: ", USART_RX_CNT);
		for (i = 0; i < USART_RX_CNT; i++)
		{
			printf("%c", USART_RX_BUF[i]);
		}
		printf("\r\n");
		USART_RX_Complete = 0;
		USART_RX_CNT = 0;
	}
}
