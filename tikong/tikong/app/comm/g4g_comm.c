#include "app_comm.h"
#include "usart3.h"
#include "Live_data_r.h"
#include "stdio.h"

void G4GProcess(void)
{
	int i;
	if (USART3_RX_Complete)
	{
		if (USART3_RX_CNT < USART3_REC_LEN)
		{
			USART3_RX_BUF[USART3_RX_CNT] = '\0';
		}
		else
		{
			USART3_RX_BUF[USART3_REC_LEN - 1] = '\0';
		}

		printf("rx->\r\n");
		for (i = 0; i < USART3_RX_CNT; i++)
		{
			printf("%c", USART3_RX_BUF[i]);
		}
		printf("\r\n");

		parseSerialData((const char *)USART3_RX_BUF);

		USART3_RX_Complete = 0;
		USART3_RX_CNT = 0;
	}
}
