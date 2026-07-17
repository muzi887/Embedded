#include <string.h>
#include "misc.h"
#include "board_init.h"
#include "app_run.h"
#include "uart3_at_sequence.h"
#include "cmd.h"
#include "main.h"
#include "timer.h"
#include "floor_ctrl.h"
#include "data_handler.h"
#include "4g.h"
#include "wdg.h"

int setting = 0;
int online_count = 0;
int main(void)
{
	char now14[15];
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	board_init();
	app_init();

	// beepAlarm(8);
	printf("\n sys init ok \n ");

	IWDG_Init(4, 1990); // 3180MS 看门狗初始化
	IWDG_Feed();		// 喂狗

	while (1)
	{
		IWDG_Feed();
		if (g_uart3_at_sequence_request)
		{ // 4G模块AT指令配置流程
			switch (uart3_at_sequence_poll())
			{
			case UART3_AT_SEQ_BUSY:
				break;
			case UART3_AT_SEQ_DONE_ALL_OK:
				Cmd_Setting_OnAtSequenceDone();
				GM4G_Restart(); // 必须的，否则网络可能会异常
				NVIC_SystemReset();
				break;
			case UART3_AT_SEQ_ABORTED:
				Cmd_Setting_OnAtSequenceAbort();
				g_uart3_at_sequence_request = 0;
				break;
			default:
				break;
			}
		}
		else
		{ // 正常逻辑流程
			app_poll();
			// 定时获取时间
			if (gettimeflag)
			{
				GetNetTime();
				gettimeflag = 0;
			}
			// 定时检查限层时间
			if (check_limit_time_flag)
			{
				RTC_Time now;
				RTC_Time limit_tm; /* parse limit string only; must not overwrite now */
				check_limit_time_flag = 0;
				//在1分钟内超3次链接4g，直接复位
				if (online_count > 3)
				{
					GM4G_Restart();
					NVIC_SystemReset();
				}
				online_count = 0;
				Refresh_CurrentTime();
				now = Get_CurrentTime();
				if (parseYMDHMS(g_floors_limit_time, &limit_tm) == 0)
				{
					MakeTimestamp14FromDS3231(&now, now14);

					printf("current time111: %s\r\n", now14);
					printf("floors limit time: %s\r\n", g_floors_limit_time);
					if (strcmp(g_floors_limit_time, now14) <= 0)
					{
						u8 clr[PUBLIC_FLOORS_LIMIT_TIME_LEN];
						u8 limit5[PUBLIC_FLOORS_LIMIT_LEN];
						FloorCtrl_GetLimit(limit5);
						(void)Floor_AuthCheck_limit_off(limit5);
						memset(g_floors_limit_time, 0, sizeof(g_floors_limit_time));
						memset(clr, 0, sizeof(clr));
						memset(limit5, 0, sizeof(limit5));
						WriteRawBytes(clr, PUBLIC_FLOORS_LIMIT_TIME_ADDR, PUBLIC_FLOORS_LIMIT_TIME_LEN);
						WriteRawBytes(limit5, PUBLIC_FLOORS_LIMIT_ADDR, PUBLIC_FLOORS_LIMIT_LEN);
						FloorCtrl_SetLimit(limit5);
						printf("floors limit Terminate\r\n");
					}
				}
			}
		}
	}
}
