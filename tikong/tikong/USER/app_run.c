#include "app_run.h"
#include "../app/comm/app_comm.h"
#include "RTC.h"
#include "eeprom.h"
#include "blackList.h"
#include "qr_comm.h"
#include "cmd.h"
#include "floor_ctrl.h"
#include <stdio.h>
#include "data_handler.h"

static RTC_Time currentTime;
//static RTC_Time currentTime_ahead, currentTime_behind;

RTC_Time Get_CurrentTime(void) { return currentTime; }

void Set_CurrentTime(const RTC_Time *t)
{
  if (!t) return;
  currentTime = *t;
  RtcChip_SetTime(&currentTime);
}

void Refresh_CurrentTime(void)
{
  RtcChip_GetTime(&currentTime);
}

void Get_TimeWithDrift(int drift_min, RTC_Time *ahead, RTC_Time *behind)
{
  if (!ahead || !behind) return;
  ds3231_shift_minutes(ahead, &currentTime, -drift_min);
  ds3231_shift_minutes(behind, &currentTime, drift_min);
}

void app_init(void)
{
	char now14[15];

	loadDataFromEEPROM();
	//rtc时间设置
	RtcChip_GetTime(&currentTime);
	if(currentTime.year <26)
	{
		currentTime.year = 26;
		currentTime.month = 4;
		currentTime.date = 20;
		currentTime.hour = 8;
		currentTime.minute = 57;
		currentTime.second = 0;
		currentTime.dayOfWeek = 1;
		RtcChip_SetTime(&currentTime);
	}
	printf("Current RTC time: 20%02d-%02d-%02d %02d:%02d:%02d\r\n",
		   currentTime.year, currentTime.month, currentTime.date,
		   currentTime.hour, currentTime.minute, currentTime.second);


	if(parseYMDHMS(g_floors_limit_time, &currentTime)!=0)
		return;

	RtcChip_GetTime(&currentTime);
	MakeTimestamp14FromDS3231(&currentTime, now14);

	printf("current time: %s\r\n", now14);
	printf("floors limit time: %s\r\n", g_floors_limit_time);
	if (strcmp(g_floors_limit_time, now14) >= 0)
	{
		u8 limit5[5];
		FloorCtrl_GetLimit(limit5);
		(void)Floor_AuthCheck_limit(limit5);
	}

}

void app_poll(void)
{
	PCProcess();//调试输出
	QRProcessUart5();//读头 UART5
	QRProcessUart4();//读头 UART4
	G4GProcess();
}
