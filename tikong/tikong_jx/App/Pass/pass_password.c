#include "pass_password.h"
#include "data_handler.h"
#include "app_run.h"
#include "Live_data.h"
#include "rtc.h"
#include "rtc_util.h"
#include "sha1.h"
#include "floor_ctrl.h"
#include "rly.h"
#include "timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 按时刻 t 生成 now_ymdh、拼接 auth_src、SHA1 转小写、取左 10 位并累加 ASCII */
static int qr_build_password_auth_digest(const RTC_Time *t,
										 char *now_ymdh, unsigned now_ymdh_sz,
										 char *auth_src, unsigned auth_src_sz,
										 char *auth_dic_hash,
										 char auth_hash_left10[11])
{
	int idx;
	int sum = 0;

	if (!t || !now_ymdh || !auth_src || !auth_dic_hash || !auth_hash_left10)
		return 0;
	if (now_ymdh_sz < 2u || auth_src_sz < 2u)
		return 0;

	strncpy(now_ymdh, create_ymdhm(t), (size_t)(now_ymdh_sz - 1u));
	now_ymdh[now_ymdh_sz - 1u] = '\0';

	printf("******************************\r\n");
	printf("current time ymdh: %s\r\n", now_ymdh);
	snprintf(auth_src, (size_t)auth_src_sz, "%s,%s,%s", g_device_code, g_device_public_Key, now_ymdh);
	printf("auth source str: %s\r\n", auth_src);
	sha1_hash(auth_src, auth_dic_hash);
	for (idx = 0; auth_dic_hash[idx] != '\0'; idx++)
	{
		if (auth_dic_hash[idx] >= 'A' && auth_dic_hash[idx] <= 'Z')
			auth_dic_hash[idx] = (char)(auth_dic_hash[idx] - 'A' + 'a');
	}
	printf("auth dic hash: 	 %s\r\n", auth_dic_hash);
	strncpy(auth_hash_left10, auth_dic_hash, 10);
	auth_hash_left10[10] = '\0';
	for (idx = 0; idx < 10 && auth_hash_left10[idx] != '\0'; idx++)
		sum += (unsigned char)auth_hash_left10[idx];
	printf("auth hash left10:%s, ascii sum: %d\r\n", auth_hash_left10, sum);
	return sum;
}

/* 类型为密码（'2'）时的校验：4 位数字、RTC 三时刻摘要、按设备类型授权；入参须为 NUL 结尾字符串 */
void qr_handle_password_input(const char *s_received_data, int uart_port)
{
	int pwd4_value = -1;
	int pwd_len;
	char now_ymdh[13];
	char now_ymdh2[13];
	char now_ymdh3[13];
	char auth_src[100];
	char auth_dic_hash[100];
	char auth_hash_left10[11];
	int auth_hash_ascii_sum1 = 0;
	int auth_hash_ascii_sum2 = 0;
	int auth_hash_ascii_sum3 = 0;
	char ts[20];
	uint8_t elevator_limit5[PUBLIC_FLOORS_LIMIT_LEN];

	RtcChip_TimestampMillisToStr(ts, sizeof(ts));

	pwd_len = (int)strlen(s_received_data);
	// printf("Received password data: %s\r\n", s_received_data);

	/* 密码要求 4 位数字，转换为整数类型用于后续比较/校验 */
	if (pwd_len == 4 &&
		s_received_data[0] >= '0' && s_received_data[0] <= '9' &&
		s_received_data[1] >= '0' && s_received_data[1] <= '9' &&
		s_received_data[2] >= '0' && s_received_data[2] <= '9' &&
		s_received_data[3] >= '0' && s_received_data[3] <= '9')
	{
		pwd4_value = (s_received_data[0] - '0') * 1000 +
					 (s_received_data[1] - '0') * 100 +
					 (s_received_data[2] - '0') * 10 +
					 (s_received_data[3] - '0');
		printf("password int value: %d\r\n", pwd4_value);
	}
	else
	{
		printf("password format invalid, need 4 digits: %s\r\n", s_received_data);
		g_result.error = "password format invalid, need 4 digits";
		g_result.code = 400;
		Bsp_SetBeep(2);
		return;
	}

	{
		RTC_Time ct, ct_ahead, ct_behind;
		int drift_min;

		Refresh_CurrentTime();
		ct = Get_CurrentTime();
		/* 提前 / 错后*/
		drift_min = atoi(g_device_drift_time);
		// 先将时间的按颗粒度求模运算
		printf("current time before drift_min=%d minute mod: %04d%02d%02d%02d%02d%02d\r\n",
			   drift_min, 2000 + (int)ct.year, (int)ct.month, (int)ct.date,
			   (int)ct.hour, (int)ct.minute, (int)ct.second);
		Rtc_Time_ZeroSecondsMinuteMod(&ct, &ct, drift_min);
		printf("current time after drift_min=%d minute mod:   %04d%02d%02d%02d%02d%02d\r\n",
			   drift_min, 2000 + (int)ct.year, (int)ct.month, (int)ct.date,
			   (int)ct.hour, (int)ct.minute, (int)ct.second);
		// 再生成提前和错后的时间
		ds1302_shift_minutes(&ct_ahead, &ct, -drift_min);
		printf("current time ahead by drift: %04d%02d%02d%02d%02d%02d\r\n",
			   2000 + (int)ct_ahead.year, (int)ct_ahead.month, (int)ct_ahead.date,
			   (int)ct_ahead.hour, (int)ct_ahead.minute, (int)ct_ahead.second);
		ds1302_shift_minutes(&ct_behind, &ct, drift_min);
		printf("current time behind by drift: %04d%02d%02d%02d%02d%02d\r\n",
			   2000 + (int)ct_behind.year, (int)ct_behind.month, (int)ct_behind.date,
			   (int)ct_behind.hour, (int)ct_behind.minute, (int)ct_behind.second);

		auth_hash_ascii_sum1 = qr_build_password_auth_digest(&ct,
															now_ymdh, (unsigned)sizeof(now_ymdh),
															auth_src, (unsigned)sizeof(auth_src),
															auth_dic_hash,
															auth_hash_left10);
		auth_hash_ascii_sum2 = qr_build_password_auth_digest(&ct_ahead,
															 now_ymdh2, (unsigned)sizeof(now_ymdh2),
															 auth_src, (unsigned)sizeof(auth_src),
															 auth_dic_hash,
															 auth_hash_left10);
		auth_hash_ascii_sum3 = qr_build_password_auth_digest(&ct_behind,
															 now_ymdh3, (unsigned)sizeof(now_ymdh3),
														 auth_src, (unsigned)sizeof(auth_src),
														 auth_dic_hash,
														 auth_hash_left10);
	}

	printf("=======================================\r\n");
	printf("auth_hash_ascii_sum1: %d\r\n", auth_hash_ascii_sum1 % 10);
	printf("auth_hash_ascii_sum2: %d\r\n", auth_hash_ascii_sum2 % 10);
	printf("auth_hash_ascii_sum3: %d\r\n", auth_hash_ascii_sum3 % 10);
	printf("gate num: %d\r\n", pwd4_value / 1000);
	printf("auth_hash_ascii_sum1: %d\r\n", auth_hash_ascii_sum1 % 25);
	printf("auth_hash_ascii_sum2: %d\r\n", auth_hash_ascii_sum2 % 25);
	printf("auth_hash_ascii_sum3: %d\r\n", auth_hash_ascii_sum3 % 25);
	printf("unit num: %d\r\n", (pwd4_value % 1000) / 40);
	printf("elevator num :%d\r\n", pwd4_value % 40);
	printf("=======================================\r\n");
	/* 园区门 */
	if (strcmp(g_device_type, "1") == 0)
	{
		if (auth_hash_ascii_sum1 % 10 == pwd4_value / 1000 || auth_hash_ascii_sum2 % 10 == pwd4_value / 1000 || auth_hash_ascii_sum3 % 10 == pwd4_value / 1000)
		{
			if (uart_port == 4)
			{
				Rly_Set1(1);
				TIM1_ClearPendingAndEnable();
			}
			if (uart_port == 5)
			{
				Rly_Set2(1);
				TIM2_ClearPendingAndEnable();
			}
			printf("password correct for gate control---gate is authorized\r\n");
			g_result.msg = "password correct for gate control---gate is authorized";
			g_result.code = 200;
			Bsp_SetBeep(1);
		}
		else
		{
			g_result.error = "password incorrect for gate control";
			g_result.code = 400;
			printf("password incorrect for gate control\r\n");
			Bsp_SetBeep(2);
		}
	}
	/* 楼门禁 */
	else if (strcmp(g_device_type, "2") == 0)
	{
		if (auth_hash_ascii_sum1 % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum2 % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum3 % 25 == (pwd4_value % 1000) / 40)
		{
			if (uart_port == 4)
			{
				Rly_Set1(1);
				TIM1_ClearPendingAndEnable();
			}
			if (uart_port == 5)
			{
				Rly_Set2(1);
				TIM2_ClearPendingAndEnable();
			}
			printf("password correct for unit control---unit is authorized\r\n");
			g_result.msg = "password correct for unit control---unit is authorized";
			g_result.code = 200;
			Bsp_SetBeep(1);
		}
		else
		{
			g_result.error = "password incorrect for unit control";
			g_result.code = 400;
			printf("password incorrect for unit control\r\n");
			Bsp_SetBeep(2);
		}
	}
	/* 电梯,解析出的楼层数与app的有差异，原因是app的楼层起点不为1 */
	else if (strcmp(g_device_type, "3") == 0)
	{
		if (auth_hash_ascii_sum1 % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum2 % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum3 % 25 == (pwd4_value % 1000) / 40)
		{
			printf("password correct for unit control ---elevator is authorized \r\n");
			printf("elevator num :%d\r\n", pwd4_value % 40);
			g_result.msg = "password correct for elevator control---elevator is authorized";
			g_result.code = 200;
			FloorCtrl_GetLimit(elevator_limit5);
			// TODO：如果改为人工按键，会错误，因为常开的开启与关闭会影响按键，这个问题后面解决
			Floor_AuthCheck_limit_off(elevator_limit5);
			// Floor_DirectGo(pwd4_value % 40);
			Floor_CallOne(pwd4_value % 40);
			Floor_AuthCheck_limit(elevator_limit5);
			Bsp_SetBeep(1);
		}
		else
		{
			g_result.error = "password incorrect for elevator control";
			g_result.code = 400;
			printf("password incorrect for elevator control\r\n");
			Bsp_SetBeep(2);
		}
	}

	pwd_Reply(ts, ts, pwd4_value, g_result);
}
