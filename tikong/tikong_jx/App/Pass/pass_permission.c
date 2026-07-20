#include "pass_permission.h"
#include "data_handler.h"
#include "pass_setting.h"
#include "pass_crypto.h"
#include "app_run.h"
#include "eeprom.h"
#include "rtc.h"
#include "sha1.h"
#include "blackList.h"
#include "floor_ctrl.h"
#include "rly.h"
#include "timer.h"
#include "Live_data.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define QR_RECV_CSV_MAX 16
/* 含 NUL：单列最多 QR_RECV_CSV_FIELD-1 字符。梯控权限列可达 64+ 字符；MQTT 等最长 64 */
#define QR_RECV_CSV_FIELD 128
/* data 段为 CSV：固定 8 个逗号，即 9 列 */
#define QR_RECV_CSV_COMMA_N 8u
#define QR_RECV_CSV_COL_N (QR_RECV_CSV_COMMA_N + 1u)

static char s_recv_csv_field[QR_RECV_CSV_MAX][QR_RECV_CSV_FIELD];
static int s_recv_csv_count;

/* static char *recv_cmd; */
static char *recv_arg_id;		  // 卡号
/* static char *recv_arg_count; */	  // 有效次数
static char *recv_arg_begin;	  // 有效开始时间
static char *recv_arg_end;		  // 有效结束时间
static char *recv_arg_gate;	  // 大门权限
static char *recv_arg_unit;	  // 单元门权限
static char *recv_arg_elevator;  // 梯控权限
static char *recv_arg_signature; // 签名

static char hash_str[80];							// 存放sha1结果

// 院大门权限
static int Cmd_Permission_ProcessGate(char *received_data, int uart_port)
{

	char *code_pos;
	// 验签
	Cmd_Permission_VerifySignature(received_data);
	if (g_result.code == 400)
		return -1;
	// 黑名单校验
	Cmd_Permission_CheckBlacklist();
	if (g_result.code == 400)
		return -1;
	// 时间窗校验
	Cmd_Permission_CheckTimeWindow();
	if (g_result.code == 400)
		return -1;

	if (recv_arg_gate[0] == '-' && recv_arg_gate[1] == '1' && recv_arg_gate[2] == '\0')
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
		TIM2_ClearPendingAndEnable();
		printf("all gate is all authorized\r\n");
		g_result.msg = "all gate is all authorized";
		g_result.code = 200;
		return 1;
	}
	if (recv_arg_gate[0] == '0' && recv_arg_gate[1] == '\0')
	{
		printf("all gate is unauthorized\r\n");
		g_result.error = "all gate is unauthorized";
		g_result.code = 400;
		return -1;
	}
	code_pos = strstr(recv_arg_gate, g_device_code);
	if (!code_pos)
	{
		printf("device code '%s' not found in gate arg\r\n", g_device_code);
		g_result.error = "device code not found in gate arg";
		g_result.code = 400;
		return -1;
	}
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
	TIM2_ClearPendingAndEnable();
	printf("device code '%s' found in gate arg\r\n", g_device_code);
	g_result.msg = "device code found in gate arg";
	g_result.code = 200;
	return 1;
}
// 单元门权限：
static int Cmd_Permission_ProcessUnit(char *received_data, int uart_port)
{
	char *code_pos;
	// 验签
	Cmd_Permission_VerifySignature(received_data);
	if (g_result.code == 400)
		return -1;

	// 黑名单校验
	Cmd_Permission_CheckBlacklist();
	if (g_result.code == 400)
		return -1;
	// 时间窗校验
	Cmd_Permission_CheckTimeWindow();
	if (g_result.code == 400)
		return -1;

	if (recv_arg_unit[0] == '-' && recv_arg_unit[1] == '1' && recv_arg_unit[2] == '\0')
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
		TIM2_ClearPendingAndEnable();
		printf("all unit is all authorized\r\n");
		g_result.msg = "all unit is all authorized";
		g_result.code = 200;
		return 1;
	}
	if (recv_arg_unit[0] == '0' && recv_arg_unit[1] == '\0')
	{
		printf("all unit is disabled\r\n");
		g_result.error = "all unit is disabled";
		g_result.code = 400;
		return -1;
	}
	code_pos = strstr(recv_arg_unit, g_device_code);
	if (!code_pos)
	{
		printf("device code '%s' not found in unit arg\r\n", g_device_code);
		g_result.error = "device code not found in unit arg";
		g_result.code = 400;
		return -1;
	}
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

	printf("device code '%s' found in unit arg\r\n", g_device_code);
	g_result.msg = "device code found in unit arg";
	g_result.code = 200;
	return 1;
}

/* 梯控权限：解析 recv_arg_elevator 中设备码段、hex 转 5 字节、多层/单层授权 */
static int Cmd_Permission_ProcessElevator(char *received_data)
{
	char *code_pos;
	const char *underscore;
	const char *seg_start;
	const char *seg_end;
	int seg_len;
	int hex_len;
	int byte_len;
	int out_idx;
	int in_idx;
	int nibble;
	int first_nibble;
	int i;
	char elevator_match[QR_RECV_CSV_FIELD];
	uint64_t elevator_tail5_int;
	uint64_t limit_int;
	u8 qr_tail5[5];
	u8 mixed5[5];
	u8 limit5[5];

	Cmd_Permission_VerifySignature(received_data);
	if (g_result.code == 400)
	{
		return -1;
	}
	// 黑名单校验
	Cmd_Permission_CheckBlacklist();
	if (g_result.code == 400)
	{
		return -1;
	}
	// 时间窗校验
	Cmd_Permission_CheckTimeWindow();
	if (g_result.code == 400)
	{
		return -1;
	}

	// 判断是否为所有电梯全楼层权限，字符串为0,即全部禁用
	if (recv_arg_elevator[0] == '0' && recv_arg_elevator[1] == '\0')
	{
		g_result.error = "all elevator is unauthorized";
		g_result.code = 400;
		printf("all elevator is unauthorized\r\n");
		return -1;
	}

	// //判断是否为所有电梯全楼层权限，字符串为-1
	// if (recv_arg_elevator[0] == '-' && recv_arg_elevator[1] == '1' && recv_arg_elevator[2] == '\0')
	// {
	// 	Floor_AllOn();
	// 	printf("all floor is all authorized\r\n");
	// 	return 1;
	// }

	// 判断设备码在梯控权限字段中是否存在，并提取对应段
	code_pos = strstr(recv_arg_elevator, g_device_code);
	if (!code_pos)
	{
		g_result.error = "device code not found in elevator arg";
		g_result.code = 400;
		printf("device code '%s' not found in elevator arg\r\n", g_device_code);
		return -1;
	}

	// 按照“，”或“|”分段，提取设备码对应段
	seg_start = code_pos;
	while (seg_start > recv_arg_elevator &&
		   *(seg_start - 1) != ',' &&
		   *(seg_start - 1) != '|')
	{
		seg_start--;
	}

	seg_end = code_pos + strlen(g_device_code);
	while (*seg_end && *seg_end != ',' && *seg_end != '|')
		seg_end++;

	seg_len = (int)(seg_end - seg_start);
	if (seg_len >= (int)sizeof(elevator_match))
		seg_len = (int)sizeof(elevator_match) - 1;
	memcpy(elevator_match, seg_start, seg_len);
	elevator_match[seg_len] = '\0';
	printf("elevator matched segment: %s\r\n", elevator_match);

	// 判断是否为该设备对应全楼层权限
	if (strstr(elevator_match, "-1") != NULL)
	{
		Floor_AllOn();
		printf("all floor on\r\n");
		g_result.msg = "all floor is all authorized";
		g_result.code = 200;
		return 1;
	}

	// 从“_”后面开始提取hex字符
	underscore = strchr(elevator_match, '_');
	if (!underscore || *(underscore + 1) == '\0')
	{
		printf("elevator segment has no data after '_'\r\n");
		g_result.error = "elevator segment has no data after '_'";
		g_result.code = 400;
		return -1;
	}

	hex_len = (int)strlen(underscore + 1);
	byte_len = (hex_len + 1) / 2;
	if (byte_len > 5)
	{
		printf("elevator hex byte len %d > 5\r\n", byte_len);
		g_result.error = "elevator hex byte len > 5";
		g_result.code = 400;
		return -1;
	}

	memset(qr_tail5, 0x00, sizeof(qr_tail5));
	out_idx = 5 - byte_len;
	in_idx = 0;

	/* 处理奇数位 hex：首个半字节作为低 4 位，高 4 位补 0 */
	if ((hex_len & 1) != 0)
	{
		nibble = qr_hex_char_to_nibble(underscore[1]);
		if (nibble < 0)
		{
			printf("invalid hex char '%c'\r\n", underscore[1]);
			g_result.error = "invalid hex char in elevator tail";
			g_result.code = 400;
			return -1;
		}
		qr_tail5[out_idx++] = (u8)nibble;
		in_idx = 1;
	}

	// 将 hex 转为 byte，存入 elevator_tail5 的后面部分
	while (in_idx < hex_len)
	{
		first_nibble = qr_hex_char_to_nibble(underscore[1 + in_idx]);
		nibble = qr_hex_char_to_nibble(underscore[1 + in_idx + 1]);
		if (first_nibble < 0 || nibble < 0)
		{
			printf("invalid hex char in elevator tail\r\n");
			g_result.error = "invalid hex char in elevator tail";
			g_result.code = 400;
			return -1;
		}
		qr_tail5[out_idx++] = (u8)((first_nibble << 4) | nibble);
		in_idx += 2;
	}
	printf("elevator tail(5B) QR=%02X %02X %02X %02X %02X\r\n",
		   qr_tail5[0], qr_tail5[1], qr_tail5[2], qr_tail5[3],
		   qr_tail5[4]);

	elevator_tail5_int = 0;
	for (i = 0; i < 5; i++)
		elevator_tail5_int = (elevator_tail5_int << 8) | qr_tail5[i];

	//混合权限
	FloorCtrl_GetLimit(limit5);
	limit_int = 0;
	for (i = 0; i < 5; i++)
		limit_int = (limit_int << 8) | limit5[i];
	elevator_tail5_int |= limit_int;

	for (i = 0; i < 5; i++)
		mixed5[i] = (u8)((elevator_tail5_int >> (int)(8 * (4 - i))) & 0xFFU);

	FloorCtrl_SetTail5Mixed(mixed5);

	printf("elevator tail(5B) after OR g_elevator_tail5_limit=%02X %02X %02X %02X %02X\r\n",
		   mixed5[0], mixed5[1], mixed5[2], mixed5[3],
		   mixed5[4]);
	printf("elevator_tail5_int=%llu\r\n", (unsigned long long)elevator_tail5_int);

	/* 根据合并后 40 位判断单层或多层授权 */
	if (elevator_tail5_int != 0 && (elevator_tail5_int & (elevator_tail5_int - 1)))
	{
		int bi, bj, bit_no;

		// printf("elevator_tail5_int is not a power of 2\r\n");
		bit_no = 0;
		for (bi = 4; bi >= 0; bi--)
		{
			for (bj = 0; bj < 8; bj++)
			{
				if (mixed5[bi] & (1u << (unsigned)bj))
				{
					printf("\r\nmulti control: at floor[%d]\r\n", bit_no + 1);
					Floor_AuthCheck((uint8_t)bit_no + 1);
				}
				bit_no++;
			}
		}
		Bsp_SetBeep(1);
		g_result.msg = "multi floor authorized";
		g_result.code = 200;
		return 1;
	}
	else
	{
		int bit_rl;
		int bb;
		uint64_t v;

		v = elevator_tail5_int;
		bit_rl = -1;
		for (bb = 0; bb < 40; bb++)
		{
			if (v & ((uint64_t)1u << bb))
			{
				bit_rl = bb;
				break;
			}
		}

		printf("single control: at floor[%d]\r\n", bit_rl + 1);
		Floor_DirectGo((uint8_t)bit_rl + 1);
		Bsp_SetBeep(1);
		g_result.msg = "single floor authorized";
		g_result.code = 200;
		return 1;
	}
}

/* 验签：将最后一个逗号后的字段替换为固定值后计算 SHA1；成功 g_result.code=200，失败 400 */
static void Cmd_Permission_VerifySignature(char received_data[])
{
	int i;
	char *last_comma;
	char calc10[10];

	last_comma = strrchr(received_data, ',');
	if (!last_comma)
	{
		g_result.error = "signature: no comma";
		g_result.code = 400;
		return;
	}
	strcpy(last_comma + 1, g_device_public_Key);

	sha1_hash(received_data, hash_str);
	for (i = 0; i < 10; i++)
	{
		char c = hash_str[i];
		calc10[i] = toLowerHex(c);
	}
	printf("calculated signature: %s\r\n", calc10);
	if (strncmp(calc10, recv_arg_signature, 10) != 0)
	{
		g_result.error = "signature verification failed";
		g_result.code = 400;
		printf("signature verification failed\r\n");
		return;
	}
	printf("signature verification passed\r\n");
	g_result.msg = "signature verification passed";
	g_result.code = 200;
}

/* 黑名单检查；通过 g_result.code=200，命中黑名单 400（依赖已解析的 recv_arg_id） */
static void Cmd_Permission_CheckBlacklist(void)
{
	int i;

	//(void)received_data;

	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		if ((strncmp((char *)dataArray[i].data, recv_arg_id, (DATA_SIZE - 1)) == 0) && (dataArray[i].isEmpty == 0))
		{
			printf("user is in blacklist!\r\n");
			g_result.error = "user is in blacklist";
			g_result.code = 400;
			return;
		}
	}
	g_result.msg = "blacklist ok";
	g_result.code = 200;
}

/* 时间检验：按 EEPROM 配置的漂移分钟放宽有效区间；0 在有效期内，-4 不在（依赖已解析的 recv_arg_begin/end） */
static void Cmd_Permission_CheckTimeWindow(void)
{
	int drift_min;
	char now14[15];

	RTC_Time ct;
	//(void)received_data;

	Refresh_CurrentTime();
	ct = Get_CurrentTime();
	MakeTimestamp14(&ct, now14);
	drift_min = atoi(g_device_drift_time);
	Get_newTime(recv_arg_begin, drift_min, 0);
	Get_newTime(recv_arg_end, drift_min, 1);

	printf("begin time: %s--end time: %s\r\n", recv_arg_begin, recv_arg_end);
	printf("current time: %s\r\n", now14);
	if (!(strcmp(recv_arg_begin, now14) <= 0) || !(strcmp(now14, recv_arg_end) <= 0))
	{
		g_result.error = "time is out of range";
		g_result.code = 400;
		return;
	}
	g_result.msg = "time window ok";
	g_result.code = 200;
}

int Cmd_Permission(char received_data[], const char order_type_meta[8], const char card_number_meta[32], int uart_port)
{
	int k;
	int ret;
	char ts[20];
	qr_split_s_received_by_comma(received_data);
	if (s_recv_csv_count != 9)
	{
		printf("cmd string field count need 9, got %d\r\n", s_recv_csv_count);
		printf("received_data: %s\r\n", received_data);
		return -401;
	}

	/* recv_cmd = s_recv_csv_field[0]; */
	recv_arg_id = s_recv_csv_field[1];
	/* recv_arg_count = s_recv_csv_field[2]; */
	recv_arg_begin = s_recv_csv_field[3];
	recv_arg_end = s_recv_csv_field[4];
	recv_arg_gate = s_recv_csv_field[5];
	recv_arg_unit = s_recv_csv_field[6];
	recv_arg_elevator = s_recv_csv_field[7];
	recv_arg_signature = s_recv_csv_field[8];

	for (k = 0; k < (int)QR_RECV_CSV_COL_N; k++)
		printf("csv[%d]=%s\r\n", k, s_recv_csv_field[k]);

	//-----------------------test---------------------------------------------
	// strncpy(g_device_code, "0102", sizeof(g_device_code) - 1);
	// g_device_code[sizeof(g_device_code) - 1] = '\0';
	// recv_arg_elevator="DQ0A010101_5|0102_8000000001|DQ0A010201_-1"; //测试数据，代表1层权限
	// return Cmd_Permission_ProcessElevator();
	//---------------------------------------------------------------------

	// 分辨设备类型
	printf("g_device_type : %s\r\n", g_device_type);

	// recv_device_type
	if (strchr(g_device_type, '1') != 0) // 院门禁
	{
		ret = Cmd_Permission_ProcessGate(received_data, uart_port);
		// return ret;
	}
	else if (strchr(g_device_type, '2') != 0) // 楼门禁
	{
		ret = Cmd_Permission_ProcessUnit(received_data, uart_port);
		// return ret;
	}
	else // 梯控
	{
		ret = Cmd_Permission_ProcessElevator(received_data);
		// return ret;
	}
	RtcChip_TimestampMillisToStr(ts, sizeof(ts));
	// re_data="dddd";
	if (order_type_meta[0] == '1')
		qr_Reply(ts, ts, g_result, received_data);
	if (order_type_meta[0] == '0')
		card_Reply(ts, ts, g_result, received_data, card_number_meta);
	return ret;
}
