#include "app_comm.h"
#include "app_config.h"
#include "uart4.h"
#include "usart3.h"
#include "data_handler.h"
#include "cmd.h"
#include "sha1.h"
#include "RTC.h"
#include "string.h"
#include "stdio.h"
#include "main.h"
#include "qr_comm.h"
#include "floor_ctrl.h"
#include "rly.h"
#include "timer.h"
#include "Live_data.h"
#include "RTC.h"
#include "app_run.h"

extern char *valid_data;

static char s_received_uart5[MAX_RECEIVE_LEN];
static char s_received_uart4[MAX_RECEIVE_LEN];
static int s_received_len_uart5;
static int s_received_len_uart4;
static char order_type_uart5[8];
static char order_type_uart4[8];
static char card_Number_uart5[32];
static char card_Number_uart4[32];

#define UART5_RX_ACCUM_MAX 512
static u8 uart5_rx_accum[UART5_RX_ACCUM_MAX];
static u16 uart5_rx_accum_len;

#define UART5_DATA_SLICE_MAX 256
static u8 uart5_data_slice[UART5_DATA_SLICE_MAX];
static u16 uart5_data_slice_len;
static u8 uart5_type_slice[UART5_DATA_SLICE_MAX];
static u16 uart5_type_slice_len;

#define UART4_RX_ACCUM_MAX 512
static u8 uart4_rx_accum[UART4_RX_ACCUM_MAX];
static u16 uart4_rx_accum_len;

#define UART4_DATA_SLICE_MAX 256
static u8 uart4_data_slice[UART4_DATA_SLICE_MAX];
static u16 uart4_data_slice_len;
static u8 uart4_type_slice[UART4_DATA_SLICE_MAX];
static u16 uart4_type_slice_len;

static int uart5_find_sub(const u8 *buf, u16 len, const char *needle)
{
	u16 n = (u16)strlen(needle);
	u16 a, b;

	if (n == 0 || len < n)
		return -1;
	for (a = 0; a + n <= len; a++)
	{
		for (b = 0; b < n; b++)
			if (buf[a + b] != (u8)needle[b])
				break;
		if (b == n)
			return (int)a;
	}
	return -1;
}

static int uart4_find_sub(const u8 *buf, u16 len, const char *needle)
{
	u16 n = (u16)strlen(needle);
	u16 a, b;

	if (n == 0 || len < n)
		return -1;
	for (a = 0; a + n <= len; a++)
	{
		for (b = 0; b < n; b++)
			if (buf[a + b] != (u8)needle[b])
				break;
		if (b == n)
			return (int)a;
	}
	return -1;
}

/* currentTime 按小时平移（dh 可为负），日月年自动进位；分秒与星期不变 */
static int ds3231_days_in_month(int full_year, int month)
{
	static const int md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int d;

	if (month < 1 || month > 12)
		return 31;
	d = md[month - 1];
	if (month == 2)
	{
		int leap = ((full_year % 4 == 0) && (full_year % 100 != 0)) || (full_year % 400 == 0);
		if (leap)
			d = 29;
	}
	return d;
}


/* 按分钟平移（dm 为正则加、负则减），时分日期自动进位；秒与星期沿用 in（与 ds3231_shift_hours 一致） */
void ds3231_shift_minutes(RTC_Time *out, const RTC_Time *in, int dm)
{
	int y = 2000 + (int)in->year;
	int mo = (int)in->month;
	int da = (int)in->date;
	int ho = (int)in->hour;
	int mi = (int)in->minute + dm;

	while (mi < 0)
	{
		mi += 60;
		ho--;
	}
	while (mi >= 60)
	{
		mi -= 60;
		ho++;
	}

	while (ho < 0)
	{
		ho += 24;
		da--;
		if (da < 1)
		{
			mo--;
			if (mo < 1)
			{
				mo = 12;
				y--;
			}
			da = ds3231_days_in_month(y, mo);
		}
	}
	while (ho >= 24)
	{
		ho -= 24;
		da++;
		{
			int dim = ds3231_days_in_month(y, mo);
			if (da > dim)
			{
				da = 1;
				mo++;
				if (mo > 12)
				{
					mo = 1;
					y++;
				}
			}
		}
	}
	if (y < 2000)
		y = 2000;
	if (y > 2099)
		y = 2099;

	out->year = (uint8_t)(y - 2000);
	out->month = (uint8_t)mo;
	out->date = (uint8_t)da;
	out->hour = (uint8_t)ho;
	out->minute = (uint8_t)mi;
	out->second = in->second;
	out->dayOfWeek = in->dayOfWeek;
}

/* 由 DS3231_Time 生成 "YYYYMMDDhh" 字符串（内部静态缓冲，勿嵌套多次调用依赖其值） */
static char *creatYMDHM(const RTC_Time *currenttim)
{
	static char ymdh_buf[13];
	int full_year;

	if (!currenttim)
	{
		ymdh_buf[0] = '\0';
		return ymdh_buf;
	}
	full_year = 2000 + (int)currenttim->year;
	snprintf(ymdh_buf, sizeof(ymdh_buf), "%04d%02d%02d%02d%02d",
			 full_year,
			 (int)currenttim->month,
			 (int)currenttim->date,
			 (int)currenttim->hour,
			 (int)currenttim->minute);
	return ymdh_buf;
}

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

	strncpy(now_ymdh, creatYMDHM(t), (size_t)(now_ymdh_sz - 1u));
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
static void qr_handle_password_input(const char *s_received_data, int uart_port)
{
	int pwd4_value = -1;
	int pwd_len;
	char now_ymdh[13];
	char now_ymdh2[13];
	char now_ymdh3[13];
	char auth_src[100];
	char auth_dic_hash[100];
	char auth_hash_left10[11];
	int auth_hash_ascii_sum = 0;
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
		ds3231_shift_minutes(&ct_ahead, &ct, -drift_min);
		printf("current time ahead by drift: %04d%02d%02d%02d%02d%02d\r\n",
			   2000 + (int)ct_ahead.year, (int)ct_ahead.month, (int)ct_ahead.date,
			   (int)ct_ahead.hour, (int)ct_ahead.minute, (int)ct_ahead.second);
		ds3231_shift_minutes(&ct_behind, &ct, drift_min);
		printf("current time behind by drift: %04d%02d%02d%02d%02d%02d\r\n",
			   2000 + (int)ct_behind.year, (int)ct_behind.month, (int)ct_behind.date,
			   (int)ct_behind.hour, (int)ct_behind.minute, (int)ct_behind.second);

		auth_hash_ascii_sum = qr_build_password_auth_digest(&ct,
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
	printf("auth_hash_ascii_sum: %d\r\n", auth_hash_ascii_sum % 10);
	printf("auth_hash_ascii_sum2: %d\r\n", auth_hash_ascii_sum2 % 10);
	printf("auth_hash_ascii_sum3: %d\r\n", auth_hash_ascii_sum3 % 10);
	printf("gate num: %d\r\n", pwd4_value / 1000);
	printf("auth_hash_ascii_sum: %d\r\n", auth_hash_ascii_sum % 25);
	printf("auth_hash_ascii_sum2: %d\r\n", auth_hash_ascii_sum2 % 25);
	printf("auth_hash_ascii_sum3: %d\r\n", auth_hash_ascii_sum3 % 25);
	printf("unit num: %d\r\n", (pwd4_value % 1000) / 40);
	printf("elevator num :%d\r\n", pwd4_value % 40);
	printf("=======================================\r\n");
	/* 园区门 */
	if (strcmp(g_device_type, "1") == 0)
	{
		if (auth_hash_ascii_sum % 10 == pwd4_value / 1000 || auth_hash_ascii_sum2 % 10 == pwd4_value / 1000 || auth_hash_ascii_sum3 % 10 == pwd4_value / 1000)
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
			printf("password correct for gate control---gate is authorize\r\n");
			g_result.msg = "password correct for gate control---gate is authorize";
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
		if (auth_hash_ascii_sum % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum2 % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum3 % 25 == (pwd4_value % 1000) / 40)
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
			printf("password correct for unit control---unit is authorize\r\n");
			g_result.msg = "password correct for unit control---unit is authorize";
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
		if (auth_hash_ascii_sum % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum2 % 25 == (pwd4_value % 1000) / 40 || auth_hash_ascii_sum3 % 25 == (pwd4_value % 1000) / 40)
		{
			printf("password correct for unit control ---elevator is authorize \r\n");
			printf("elevator num :%d\r\n", pwd4_value % 40);
			g_result.msg = "password correct for elevator control---elevator is authorize";
			g_result.code = 200;
			FloorCtrl_GetLimit(elevator_limit5);
			//TODO 如果改为人工按键，会错误，因为常开的开启与关闭会影响安建，这个问题后面解决
			Floor_AuthCheck_limit_off(elevator_limit5);
			//Floor_DirectGo(pwd4_value % 40);
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

/**
 * QRProcessUart5 — 读头 2（UART5）通行业务轮询
 *
 * 由 app_poll() 调用。驱动 DMA/IDLE 置 UART5_RX_Complete 后：
 *  1) 将 UART5_RX_BUF 追加到 uart5_rx_accum
 *  2) 对齐到 '{'，凑齐完整 "{...}" 帧后再解析
 *  3) 切片 type / data / uid（搜键名须带引号，避免 data 内容误匹配）
 *  4) type 为 '0'/'1' → CommContrl(..., 5)；'2' → 密码处理
 * 说明见 docs/pass/qr-process-uart45.md
 */
void QRProcessUart5(void)
{
	int i;

	if (UART5_RX_Complete)
	{
		/* 追加本帧 DMA 缓冲到累积区（可跨多次 IDLE 拼包） */
		for (i = 0; i < UART5_RX_CNT && uart5_rx_accum_len < UART5_RX_ACCUM_MAX; i++)
			uart5_rx_accum[uart5_rx_accum_len++] = UART5_RX_BUF[i];
		/* 丢掉 '{' 之前的噪声 */
		for (i = 0; i < (int)uart5_rx_accum_len; i++)
		{
			if (uart5_rx_accum[i] == '{')
				break;
		}
		if (i > 0 && i < (int)uart5_rx_accum_len)
		{
			u16 keep = uart5_rx_accum_len - (u16)i;
			memmove(uart5_rx_accum, uart5_rx_accum + i, keep);
			uart5_rx_accum_len = keep;
		}
		/* 首 '{' 且末 '}' 才视为完整 JSON 对象帧 */
		if (uart5_rx_accum_len >= 2u &&
			uart5_rx_accum[0] == '{' &&
			uart5_rx_accum[uart5_rx_accum_len - 1u] == '}')
		{
			int itype = uart5_find_sub(uart5_rx_accum, uart5_rx_accum_len, "\"type\"");
			int idata = uart5_find_sub(uart5_rx_accum, uart5_rx_accum_len, "\"data\"");
			/* 须搜 "\"uid\""，勿仅用 uid，以免 data 内十六进制子串误匹配 */
			int iuid = uart5_find_sub(uart5_rx_accum, uart5_rx_accum_len, "\"uid\"");

			printf("Received UART5 data: ");
			for (i = 0; i < uart5_rx_accum_len; i++)
			{
				printf("%c", uart5_rx_accum[i]);
			}
			printf("\r\n");

			/* --- 切 type --- */
			uart5_type_slice_len = 0;
			if (itype >= 0 && idata >= 0 && idata >= 2)
			{
				/* itype 为 "\"type\"" 的起始引号：跳过 \"type\"\": 共 7 字到类型值 */
				u16 ts = (u16)itype + 7u;
				/* idata 为 "\"data\"" 的起始引号：类型值在逗号前结束，即 idata 前两字为 ," */
				u16 te = (u16)idata - 1u;

				if (ts < te && te <= uart5_rx_accum_len)
				{
					u16 n = te - ts;
					if (n > UART5_DATA_SLICE_MAX)
						n = UART5_DATA_SLICE_MAX;
					memcpy(uart5_type_slice, uart5_rx_accum + ts, n);
					uart5_type_slice_len = n;
					if (n < UART5_DATA_SLICE_MAX)
						uart5_type_slice[n] = '\0';
				}
			}
			memset(order_type_uart5, 0, sizeof(order_type_uart5));
			for (i = 0; i < uart5_type_slice_len && i < (int)sizeof(order_type_uart5) - 1; i++)
			{
				printf("type-->%c\r\n", uart5_type_slice[i]);
				order_type_uart5[i] = uart5_type_slice[i];
			}
			/* --- 切 data（值位于 "data": 与 ,"uid" 之间） --- */
			uart5_data_slice_len = 0;
			if (idata >= 0 && iuid >= 2)
			{
				/* idata 指向 "\"data\"" 起始引号：+8 跳到值；cut_end=iuid-2 不含结尾引号与逗号 */
				u16 cut_start = (u16)idata + 8u;
				u16 cut_end = (u16)iuid - 2u;

				if (cut_start < cut_end && cut_end <= uart5_rx_accum_len)
				{
					u16 n = cut_end - cut_start;
					if (n > UART5_DATA_SLICE_MAX)
						n = UART5_DATA_SLICE_MAX;
					memcpy(uart5_data_slice, uart5_rx_accum + cut_start, n);
					uart5_data_slice_len = n;
					if (n < UART5_DATA_SLICE_MAX)
						uart5_data_slice[n] = '\0';
				}
			}
			printf("data-->");
			for (i = 0; i < uart5_data_slice_len; i++)
			{
				printf("%c", uart5_data_slice[i]);
			}
			printf("\r\n");
			/* --- 切 uid --- */
			card_Number_uart5[0] = '\0';
			if (iuid >= 0)
			{
				u16 p = (u16)iuid + (u16)strlen("\"uid\"");
				if (p < uart5_rx_accum_len && uart5_rx_accum[p] == ':')
					p++;
				while (p < uart5_rx_accum_len && (uart5_rx_accum[p] == ' ' || uart5_rx_accum[p] == '\t'))
					p++;
				{
					u16 start;
					u16 n = 0;

					if (p < uart5_rx_accum_len && uart5_rx_accum[p] == '"')
					{
						p++;
						start = p;
						while (p < uart5_rx_accum_len && uart5_rx_accum[p] != '"' && n < 31u)
						{
							p++;
							n++;
						}
					}
					else
					{
						start = p;
						while (p < uart5_rx_accum_len && uart5_rx_accum[p] >= '0' && uart5_rx_accum[p] <= '9' && n < 31u)
						{
							p++;
							n++;
						}
					}
					if (n > 0u)
					{
						memcpy(card_Number_uart5, uart5_rx_accum + start, n);
						card_Number_uart5[n] = '\0';
					}
				}
			}

			if (uart5_type_slice_len > 0 && uart5_data_slice_len > 0)
			{
				s_received_len_uart5 = (uart5_data_slice_len < MAX_RECEIVE_LEN - 1) ? uart5_data_slice_len : (MAX_RECEIVE_LEN - 1);
				memcpy(s_received_uart5, uart5_data_slice, s_received_len_uart5);
				s_received_uart5[s_received_len_uart5] = '\0';
				/* type: '1' 二维码 / '0' NFC → 命令分发；'2' 密码 */
				if (uart5_type_slice_len == 1 && (uart5_type_slice[0] == '1' || uart5_type_slice[0] == '0'))
				{
					CommContrl(s_received_uart5, order_type_uart5, card_Number_uart5, 5);
				}
				else if (uart5_type_slice_len == 1 && uart5_type_slice[0] == '2')
				{
					qr_handle_password_input(s_received_uart5, 5);
				}
			}
			/* 完整帧已处理：清空累积与切片，准备下一帧 */
			uart5_rx_accum_len = 0;
			uart5_data_slice_len = 0;
			uart5_type_slice_len = 0;
			memset(uart5_rx_accum, 0, sizeof(uart5_rx_accum));
			memset(uart5_type_slice, 0, sizeof(uart5_type_slice));
			memset(uart5_data_slice, 0, sizeof(uart5_data_slice));
		}

		/* 无论是否凑齐完整帧，都清驱动标志，允许收下一截 */
		UART5_RX_Complete = 0;
		UART5_RX_CNT = 0;

		return;
	}
}

/**
 * QRProcessUart4 — 读头 1（UART4）通行业务轮询
 *
 * 逻辑与 QRProcessUart5 对称，独立缓冲 uart4_*；CommContrl / 密码端口号为 4。
 * 说明见 docs/pass/qr-process-uart45.md
 */
void QRProcessUart4(void)
{
	int i;

	if (UART4_RX_Complete)
	{
		/* 追加本帧 DMA 缓冲到累积区（可跨多次 IDLE 拼包） */
		for (i = 0; i < UART4_RX_CNT && uart4_rx_accum_len < UART4_RX_ACCUM_MAX; i++)
			uart4_rx_accum[uart4_rx_accum_len++] = UART4_RX_BUF[i];
		/* 丢掉 '{' 之前的噪声 */
		for (i = 0; i < (int)uart4_rx_accum_len; i++)
		{
			if (uart4_rx_accum[i] == '{')
				break;
		}
		if (i > 0 && i < (int)uart4_rx_accum_len)
		{
			u16 keep = uart4_rx_accum_len - (u16)i;
			memmove(uart4_rx_accum, uart4_rx_accum + i, keep);
			uart4_rx_accum_len = keep;
		}
		/* 首 '{' 且末 '}' 才视为完整 JSON 对象帧 */
		if (uart4_rx_accum_len >= 2u &&
			uart4_rx_accum[0] == '{' &&
			uart4_rx_accum[uart4_rx_accum_len - 1u] == '}')
		{
			int itype = uart4_find_sub(uart4_rx_accum, uart4_rx_accum_len, "\"type\"");
			int idata = uart4_find_sub(uart4_rx_accum, uart4_rx_accum_len, "\"data\"");
			/* 须搜 "\"uid\""，勿仅用 uid，以免 data 内十六进制子串误匹配 */
			int iuid = uart4_find_sub(uart4_rx_accum, uart4_rx_accum_len, "\"uid\"");

			printf("Received UART4 data: ");
			for (i = 0; i < uart4_rx_accum_len; i++)
			{
				printf("%c", uart4_rx_accum[i]);
			}
			printf("\r\n");

			/* --- 切 type --- */
			uart4_type_slice_len = 0;
			if (itype >= 0 && idata >= 0 && idata >= 2)
			{
				/* itype 为 "\"type\"" 的起始引号：跳过 \"type\"\": 共 7 字到类型值 */
				u16 ts = (u16)itype + 7u;
				/* idata 为 "\"data\"" 的起始引号：类型值在逗号前结束，即 idata 前两字为 ," */
				u16 te = (u16)idata - 1u;

				if (ts < te && te <= uart4_rx_accum_len)
				{
					u16 n = te - ts;
					if (n > UART4_DATA_SLICE_MAX)
						n = UART4_DATA_SLICE_MAX;
					memcpy(uart4_type_slice, uart4_rx_accum + ts, n);
					uart4_type_slice_len = n;
					if (n < UART4_DATA_SLICE_MAX)
						uart4_type_slice[n] = '\0';
				}
			}
			memset(order_type_uart4, 0, sizeof(order_type_uart4));
			for (i = 0; i < uart4_type_slice_len && i < (int)sizeof(order_type_uart4) - 1; i++)
			{
				printf("type-->%c\r\n", uart4_type_slice[i]);
				order_type_uart4[i] = uart4_type_slice[i];
			}
			/* --- 切 data（值位于 "data": 与 ,"uid" 之间） --- */
			uart4_data_slice_len = 0;
			if (idata >= 0 && iuid >= 2)
			{
				/* idata 指向 "\"data\"" 起始引号：+8 跳到值；cut_end=iuid-2 不含结尾引号与逗号 */
				u16 cut_start = (u16)idata + 8u;
				u16 cut_end = (u16)iuid - 2u;

				if (cut_start < cut_end && cut_end <= uart4_rx_accum_len)
				{
					u16 n = cut_end - cut_start;
					if (n > UART4_DATA_SLICE_MAX)
						n = UART4_DATA_SLICE_MAX;
					memcpy(uart4_data_slice, uart4_rx_accum + cut_start, n);
					uart4_data_slice_len = n;
					if (n < UART4_DATA_SLICE_MAX)
						uart4_data_slice[n] = '\0';
				}
			}
			printf("data-->");
			for (i = 0; i < uart4_data_slice_len; i++)
			{
				printf("%c", uart4_data_slice[i]);
			}
			printf("\r\n");
			/* --- 切 uid --- */
			card_Number_uart4[0] = '\0';
			if (iuid >= 0)
			{
				u16 p = (u16)iuid + (u16)strlen("\"uid\"");
				if (p < uart4_rx_accum_len && uart4_rx_accum[p] == ':')
					p++;
				while (p < uart4_rx_accum_len && (uart4_rx_accum[p] == ' ' || uart4_rx_accum[p] == '\t'))
					p++;
				{
					u16 start;
					u16 n = 0;

					if (p < uart4_rx_accum_len && uart4_rx_accum[p] == '"')
					{
						p++;
						start = p;
						while (p < uart4_rx_accum_len && uart4_rx_accum[p] != '"' && n < 31u)
						{
							p++;
							n++;
						}
					}
					else
					{
						start = p;
						while (p < uart4_rx_accum_len && uart4_rx_accum[p] >= '0' && uart4_rx_accum[p] <= '9' && n < 31u)
						{
							p++;
							n++;
						}
					}
					if (n > 0u)
					{
						memcpy(card_Number_uart4, uart4_rx_accum + start, n);
						card_Number_uart4[n] = '\0';
					}
				}
			}

			if (uart4_type_slice_len > 0 && uart4_data_slice_len > 0)
			{
				s_received_len_uart4 = (uart4_data_slice_len < MAX_RECEIVE_LEN - 1) ? uart4_data_slice_len : (MAX_RECEIVE_LEN - 1);
				memcpy(s_received_uart4, uart4_data_slice, s_received_len_uart4);
				s_received_uart4[s_received_len_uart4] = '\0';
				/* type: '1' 二维码 / '0' NFC → 命令分发；'2' 密码 */
				if (uart4_type_slice_len == 1 && (uart4_type_slice[0] == '1' || uart4_type_slice[0] == '0'))
				{
					CommContrl(s_received_uart4, order_type_uart4, card_Number_uart4, 4);
				}
				else if (uart4_type_slice_len == 1 && uart4_type_slice[0] == '2')
				{
					qr_handle_password_input(s_received_uart4, 4);
				}
			}
			/* 完整帧已处理：清空累积与切片，准备下一帧 */
			uart4_rx_accum_len = 0;
			uart4_data_slice_len = 0;
			uart4_type_slice_len = 0;
			memset(uart4_rx_accum, 0, sizeof(uart4_rx_accum));
			memset(uart4_type_slice, 0, sizeof(uart4_type_slice));
			memset(uart4_data_slice, 0, sizeof(uart4_data_slice));
		}

		/* 无论是否凑齐完整帧，都清驱动标志，允许收下一截 */
		UART4_RX_Complete = 0;
		UART4_RX_CNT = 0;

		return;
	}
}
