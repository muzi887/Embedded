#include "cmd.h"
#include "rly.h"
#include "uart3_at_sequence.h"
#include "app_run.h"
#include "sha1.h"
#include "time.h"
#include "usart3.h"
#include "usart2.h"
#include "data_handler.h"
#include "ext_io.h"
#include "floor_ctrl.h"
#include "RTC.h"
#include "timer.h"
#include <string.h>
#include <stdlib.h>
#include "Live_data.h"
#include "eeprom.h"

extern volatile u8 Elevator_AuthCheck_Flag; // 电梯权限检查标志
char *json;									// 存放待加签json
char *valid_data;							// 存放干净的数据
char hash_str[80];							// 存放sha1结果
CmdResult g_result;
// char order_type[8];
// char card_Number[32];
#define QR_RECV_CSV_MAX 16
/* 含 NUL：单列最多 QR_RECV_CSV_FIELD-1 字符。梯控权限列可达 64+ 字符；MQTT 等最长 64 */
#define QR_RECV_CSV_FIELD 128
/* data 段为 CSV：固定 8 个逗号，即 9 列 */
#define QR_RECV_CSV_COMMA_N 8u
#define QR_RECV_CSV_COL_N (QR_RECV_CSV_COMMA_N + 1u)
static char s_recv_csv_field[QR_RECV_CSV_MAX][QR_RECV_CSV_FIELD];
static int s_recv_csv_count;
/** Cmd_Setting 已解析并设 RTC，待 4G AT 成功后再写 EEPROM */
static volatile u8 s_cmd_setting_eeprom_pending;
/** 待写 EEPROM 的快照：recv_* 指向 s_recv_csv_field，从 Cmd_Setting 返回到 OnAtSequenceDone 期间会被权限/校时等命令覆盖 */
static char s_es_id[PUBLIC_DEVICE_ID_LEN + 1];
static char s_es_password[PUBLIC_DEVICE_PASSWORD_LEN + 1];
static char s_es_type[PUBLIC_DEVICE_TYPE_LEN + 1];
static char s_es_code[PUBLIC_DEVICE_CODE_LEN + 1];
static char s_es_public_key[PUBLIC_KEY_LEN + 1];
static char s_es_drift_time[PUBLIC_DRIFT_TIME_LEN + 1];
static char s_es_mode[PUBLIC_DEVICE_MODE_LEN + 1];
static char s_es_mqtt_addr[PUBLIC_MQTT_ADDR_LEN + 1];
static char s_es_mqtt_pk[PUBLIC_MQTT_PRODUCTKEY_LEN + 1];
static char s_es_mqtt_dk[PUBLIC_MQTT_DEVICEKEY_LEN + 1];
static char s_es_mqtt_secret[PUBLIC_MQTT_DEVICESECRET_LEN + 1];

//--------1-----------------------------------
char *recv_device_id;		  // 设备账号
char *recv_device_password;	  // 设备密码
char *recv_device_type;		  // 设备类型
char *recv_device_code;		  // 设备编码
char *recv_device_public_key; // 加密密钥
char *recv_sys_time;		  // 系统时间
char *recv_floors_limit;	  // 限定楼层
char *recv_floors_limit_time; // 限定时间
char *recv_drift_time;		  // 漂移时间
char *recv_device_mode;		  // 设备模式

static void copy_field_snap(char *dst, size_t dst_sz, const char *src)
{
	if (!dst || dst_sz == 0)
		return;
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, dst_sz - 1);
	dst[dst_sz - 1] = '\0';
}

static void Cmd_Setting_SnapshotForEeprom(void)
{
	copy_field_snap(s_es_id, sizeof(s_es_id), recv_device_id);
	copy_field_snap(s_es_password, sizeof(s_es_password), recv_device_password);
	copy_field_snap(s_es_type, sizeof(s_es_type), recv_device_type);
	copy_field_snap(s_es_code, sizeof(s_es_code), recv_device_code);
	copy_field_snap(s_es_public_key, sizeof(s_es_public_key), recv_device_public_key);
	copy_field_snap(s_es_mode, sizeof(s_es_mode), recv_device_mode);
	copy_field_snap(s_es_drift_time, sizeof(s_es_drift_time), recv_drift_time);
	copy_field_snap(s_es_mqtt_addr, sizeof(s_es_mqtt_addr), g_mqtt_addr);
	copy_field_snap(s_es_mqtt_pk, sizeof(s_es_mqtt_pk), g_mqtt_productKey);
	copy_field_snap(s_es_mqtt_dk, sizeof(s_es_mqtt_dk), g_mqtt_deviceKey);
	copy_field_snap(s_es_mqtt_secret, sizeof(s_es_mqtt_secret), g_mqtt_deviceSecret);
}

//--------2----------------------------------
char *recv_cmd;
char *recv_arg_id;		  // 卡号
char *recv_arg_count;	  // 有效次数
char *recv_arg_begin;	  // 有效开始时间
char *recv_arg_end;		  // 有效结束时间
char *recv_arg_gate;	  // 大门权限
char *recv_arg_unit;	  // 单元门权限
char *recv_arg_elevator;  // 梯控权限
char *recv_arg_signature; // 签名

static void Cmd_Permission_VerifySignature(char received_data[]);
static void Cmd_Permission_CheckBlacklist(void);
static void Cmd_Permission_CheckTimeWindow(void);

static int qr_hex_char_to_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void qr_split_s_received_by_comma(const char *src)
{
	const char *p;
	const char *end;
	int fi;

	s_recv_csv_count = 0;
	if (!src)
		return;
	p = src;
	for (fi = 0; fi < QR_RECV_CSV_MAX && *p; fi++)
	{
		int j = 0;
		while (*p && *p != ',' && j < QR_RECV_CSV_FIELD - 1)
			s_recv_csv_field[fi][j++] = *p++;
		s_recv_csv_field[fi][j] = '\0';
		/* 本列未遇逗号但缓冲已满：跳过后续字符直至逗号，避免同一物理列被拆成多列 */
		if (*p && *p != ',')
		{
			printf("qr csv: field %d truncated (max %d chars)\r\n", fi, QR_RECV_CSV_FIELD - 1);
			while (*p && *p != ',')
				p++;
		}
		s_recv_csv_count++;
		if (*p == ',')
			p++;
	}

	/* 尾部为 ',' 时，补一个空字段（例如 "a,b," => 3 列） */
	end = src;
	while (*end)
		end++;
	if (end > src && *(end - 1) == ',' && s_recv_csv_count < QR_RECV_CSV_MAX)
	{
		s_recv_csv_field[s_recv_csv_count][0] = '\0';
		s_recv_csv_count++;
	}
}

/* recv_sys_time: 14 位数字 YYYYMMDDhhmmss -> RTC_Time；0 成功，-1 失败 */
int parseYMDHMS(const char *s, RTC_Time *t)
{
	int y, mo, da, hh, mi, se;
	int yy, mm, dow;
	static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	static const int sakamoto[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	if (!s || !t)
		return -1;
	if (strlen(s) < 14u)
		return -1;
	if (sscanf(s, "%4d%2d%2d%2d%2d%2d", &y, &mo, &da, &hh, &mi, &se) != 6)
		return -1;
	if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || da < 1 || da > 31)
		return -1;
	if (hh < 0 || hh > 23 || mi < 0 || mi > 59 || se < 0 || se > 59)
		return -1;
	{
		int dim = mdays[mo - 1];
		if (mo == 2 && (((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0)))
			dim = 29;
		if (da > dim)
			return -1;
	}

	yy = y;
	mm = mo;
	if (mm < 3)
		yy -= 1;
	dow = (yy + yy / 4 - yy / 100 + yy / 400 + sakamoto[mm - 1] + da) % 7;
	if (dow < 0)
		dow += 7;
	/* 与 cmd 中 tm_wday 约定一致：周日=7，周一=1…周六=6 */
	t->dayOfWeek = (uint8_t)(dow == 0 ? 7 : dow);

	t->year = (uint8_t)(y - 2000);
	t->month = (uint8_t)mo;
	t->date = (uint8_t)da;
	t->hour = (uint8_t)hh;
	t->minute = (uint8_t)mi;
	t->second = (uint8_t)se;
	return 0;
}

/** 将 Cmd_Setting 解析得到的 recv_* 写入 EEPROM（设备参数 + MQTT） */
static void Cmd_Setting_WriteKeysToEeprom(void)
{
	WriteKey(s_es_id, PUBLIC_DEVICE_ID_ADDR, PUBLIC_DEVICE_ID_LEN);
	WriteKey(s_es_password, PUBLIC_DEVICE_PASSWORD_ADDR, PUBLIC_DEVICE_PASSWORD_LEN);
	WriteKey(s_es_type, PUBLIC_DEVICE_TYPE_ADDR, PUBLIC_DEVICE_TYPE_LEN);
	WriteKey(s_es_code, PUBLIC_DEVICE_CODE_ADDR, PUBLIC_DEVICE_CODE_LEN);
	WriteKey(s_es_public_key, PUBLIC_KEY_ADDR, PUBLIC_KEY_LEN);
	WriteKey(s_es_drift_time, PUBLIC_DRIFT_TIME_ADDR, PUBLIC_DRIFT_TIME_LEN);
	WriteKey(s_es_mode, PUBLIC_DEVICE_MODE_ADDR, PUBLIC_DEVICE_MODE_LEN);
	WriteKey(s_es_mqtt_addr, PUBLIC_MQTT_ADDR_ADDR, PUBLIC_MQTT_ADDR_LEN);
	WriteKey(s_es_mqtt_pk, PUBLIC_MQTT_PRODUCTKEY_ADDR, PUBLIC_MQTT_PRODUCTKEY_LEN);
	WriteKey(s_es_mqtt_dk, PUBLIC_MQTT_DEVICEKEY_ADDR, PUBLIC_MQTT_DEVICEKEY_LEN);
	WriteKey(s_es_mqtt_secret, PUBLIC_MQTT_DEVICESECRET_ADDR, PUBLIC_MQTT_DEVICESECRET_LEN);
}

static void Cmd_Setting_WriteKeysToEeprom_sim(void)
{
	WriteKey(s_es_id, PUBLIC_DEVICE_ID_ADDR, PUBLIC_DEVICE_ID_LEN);
	WriteKey(s_es_password, PUBLIC_DEVICE_PASSWORD_ADDR, PUBLIC_DEVICE_PASSWORD_LEN);
	WriteKey(s_es_type, PUBLIC_DEVICE_TYPE_ADDR, PUBLIC_DEVICE_TYPE_LEN);
	WriteKey(s_es_code, PUBLIC_DEVICE_CODE_ADDR, PUBLIC_DEVICE_CODE_LEN);
	WriteKey(s_es_public_key, PUBLIC_KEY_ADDR, PUBLIC_KEY_LEN);
	WriteKey(s_es_drift_time, PUBLIC_DRIFT_TIME_ADDR, PUBLIC_DRIFT_TIME_LEN);
	WriteKey(s_es_mode, PUBLIC_DEVICE_MODE_ADDR, PUBLIC_DEVICE_MODE_LEN);
}

void Cmd_Setting_OnAtSequenceDone(void)
{
	if (!s_cmd_setting_eeprom_pending)
		return;
	s_cmd_setting_eeprom_pending = 0;
	Cmd_Setting_WriteKeysToEeprom();
	printf("[Cmd_Setting] EEPROM written after 4G AT OK\r\n");
}

void Cmd_Setting_OnAtSequenceAbort(void)
{
	s_cmd_setting_eeprom_pending = 0;
}

static int Cmd_Setting(char received_data[])
{
	memset(s_recv_csv_field, 0, sizeof(s_recv_csv_field));
	qr_split_s_received_by_comma(received_data);
	if (s_recv_csv_count != 13 && s_recv_csv_count != 9)
	{
		printf("qr csv: setting field count need 13 or 9, got %d\r\n", s_recv_csv_count);
		return -1;
	}

	recv_cmd = s_recv_csv_field[0];
	recv_device_id = s_recv_csv_field[1];
	recv_device_password = s_recv_csv_field[2];
	recv_device_type = s_recv_csv_field[3];
	recv_device_code = s_recv_csv_field[4];
	recv_device_public_key = s_recv_csv_field[5];
	recv_sys_time = s_recv_csv_field[6];
	recv_drift_time = s_recv_csv_field[7];
	recv_device_mode = s_recv_csv_field[8];

	{
		RTC_Time ct;
		if (parseYMDHMS(recv_sys_time, &ct) != 0)
		{
			printf("qr csv: recv_sys_time parse failed, need 14 digits YYYYMMDDhhmmss: %s\r\n",
				   recv_sys_time ? recv_sys_time : "(null)");
			return -1;
		}
		// 测试人为干扰时间设置
		// ct.hour = 9;
		// ct.minute =30;
		Set_CurrentTime(&ct);
	}

	if (s_recv_csv_count == 9)
	{
		Cmd_Setting_WriteKeysToEeprom_sim();
		printf("[Cmd_Setting] EEPROM written  simplify with 9 fields\r\n");
		return 1;
	}

	/* MQTT 参数拷贝到全局缓冲，供 uart3_at_sequence 读取 */
	strncpy(g_mqtt_addr, s_recv_csv_field[9], sizeof(g_mqtt_addr) - 1);
	strncpy(g_mqtt_productKey, s_recv_csv_field[10], sizeof(g_mqtt_productKey) - 1);
	strncpy(g_mqtt_deviceKey, s_recv_csv_field[11], sizeof(g_mqtt_deviceKey) - 1);
	strncpy(g_mqtt_deviceSecret, s_recv_csv_field[12], sizeof(g_mqtt_deviceSecret) - 1);
	g_mqtt_addr[sizeof(g_mqtt_addr) - 1] = '\0';
	g_mqtt_productKey[sizeof(g_mqtt_productKey) - 1] = '\0';
	g_mqtt_deviceKey[sizeof(g_mqtt_deviceKey) - 1] = '\0';
	g_mqtt_deviceSecret[sizeof(g_mqtt_deviceSecret) - 1] = '\0';

	printf("recv_device_id: %s\r\n", recv_device_id);
	printf("recv_device_password: %s\r\n", recv_device_password);
	printf("recv_device_type: %s\r\n", recv_device_type);
	printf("recv_device_code: %s\r\n", recv_device_code);
	printf("recv_device_public_key: %s\r\n", recv_device_public_key);
	printf("recv_sys_time: %s\r\n", recv_sys_time);
	printf("recv_drift_time: %s\r\n", recv_drift_time);
	printf("recv_device_mode: %s\r\n", recv_device_mode);
	printf("g_mqtt_addr: %s\r\n", g_mqtt_addr);
	printf("g_mqtt_productKey: %s\r\n", g_mqtt_productKey);
	printf("g_mqtt_deviceKey: %s\r\n", g_mqtt_deviceKey);
	printf("g_mqtt_deviceSecret: %s\r\n", g_mqtt_deviceSecret);

	s_cmd_setting_eeprom_pending = 1;
	Cmd_Setting_SnapshotForEeprom();
	g_uart3_at_sequence_request = 1;
	printf("[Cmd_Setting] 4G AT sequence started; EEPROM after all OK\r\n");

	return 1;
}
// 更新时间
static int Cmd_Set_Time(char received_data[])
{

	int i;
	char *last_comma;
	char calc10[10];
	RTC_Time ct;
	memset(s_recv_csv_field, 0, sizeof(s_recv_csv_field));
	qr_split_s_received_by_comma(received_data);

	if (s_recv_csv_count != 3)
	{
		printf("qr csv: setting field count need 3, got %d\r\n", s_recv_csv_count);
		return -1;
	}

	recv_cmd = s_recv_csv_field[0];
	recv_sys_time = s_recv_csv_field[1];
	// recv_drift_time = s_recv_csv_field[2];
	recv_arg_signature = s_recv_csv_field[2];

	printf("recv_cmd: %s\r\n", recv_cmd);
	printf("recv_sys_time: %s\r\n", recv_sys_time);
	// printf("recv_drift_time: %s\r\n", recv_drift_time);

	/* 验签：将最后一个逗号后的字段替换为固定值后计算 SHA1 前10位 */
	last_comma = strrchr(received_data, ',');
	if (!last_comma)
		return -2;
	// g_device_public_Key
	strcpy(last_comma + 1, g_device_public_Key);
	printf("qr csv tail replaced: %s\r\n", received_data);

	sha1_hash(received_data, hash_str);
	printf("hash_str: %s\r\n", hash_str);
	for (i = 0; i < 10; i++)
	{
		char c = hash_str[i];
		calc10[i] = toLowerHex(c);
	}
	if (strncmp(calc10, recv_arg_signature, 10) != 0)
	{
		printf("qr csv: signature verification failed\r\n");
		return -2;
	}

	if (parseYMDHMS(recv_sys_time, &ct) != 0)
	{
		printf("qr csv: recv_sys_time parse failed, need 14 digits YYYYMMDDhhmmss: %s\r\n",
			   recv_sys_time ? recv_sys_time : "(null)");
		return -1;
	}

	Set_CurrentTime(&ct);

	// 漂移时间写 EEPROM
	// WriteKey(recv_drift_time, PUBLIC_DRIFT_TIME_ADDR, PUBLIC_DRIFT_TIME_LEN);

	// printf("[Cmd_Set_Time_And_Drift] time and drift is set OK!\r\n");

	return 1;
}
// 电梯权限限定
static int Cmd_Set_OpenLimit(char received_data[])
{
	int i;
	char *last_comma;
	char calc10[10];
	int byte_len;
	int hex_len;
	int out_idx;
	int in_idx;
	int nibble;
	int first_nibble;
	u8 elevator_tail5_limit[5];
	uint64_t elevator_tail5_int_limit;
	char now14[15];
	RTC_Time now;
	RTC_Time ct;
	memset(s_recv_csv_field, 0, sizeof(s_recv_csv_field));
	qr_split_s_received_by_comma(received_data);

	if (s_recv_csv_count != 4)
	{
		printf("qr csv: open limit field count need 4 (cmd,hex,time14,sig), got %d\r\n", s_recv_csv_count);
		return -1;
	}

	recv_cmd = s_recv_csv_field[0];
	recv_floors_limit = s_recv_csv_field[1];
	recv_floors_limit_time = s_recv_csv_field[2];
	recv_arg_signature = s_recv_csv_field[3];

	printf("recv_cmd: %s\r\n", recv_cmd);
	printf("recv_floors_limit: %s\r\n", recv_floors_limit);
	printf("recv_floors_limit_time: %s\r\n", recv_floors_limit_time);

	/* 验签：将最后一个逗号后的字段替换为固定值后计算 SHA1 前10位 */
	last_comma = strrchr(received_data, ',');
	if (!last_comma)
		return -2;
	// g_device_public_Key
	strcpy(last_comma + 1, g_device_public_Key);
	printf("qr csv tail replaced: %s\r\n", received_data);

	sha1_hash(received_data, hash_str);
	printf("hash_str: %s\r\n", hash_str);
	for (i = 0; i < 10; i++)
	{
		char c = hash_str[i];
		calc10[i] = toLowerHex(c);
	}
	if (strncmp(calc10, recv_arg_signature, 10) != 0)
	{
		printf("qr csv: signature verification failed\r\n");
		return -2;
	}

	if(strcmp(recv_floors_limit_time, "") == 0){
		printf("qr csv: recv_floors_limit_time is null, use default max time\r\n");
		recv_floors_limit_time="20991231235959";
	}

	if (parseYMDHMS(recv_floors_limit_time, &ct) != 0)
	{
		printf("qr csv: recv_floors_limit_time parse failed, need 14 digits YYYYMMDDhhmmss: %s\r\n",
			   recv_floors_limit_time ? recv_floors_limit_time : "(null)");
		return -1;
	}

	Refresh_CurrentTime();
	now = Get_CurrentTime();

	MakeTimestamp14FromDS3231(&now, now14);

	printf("current time: %s\r\n", now14);
	printf("floors limit time: %s\r\n", recv_floors_limit_time);
	if (strcmp(recv_floors_limit_time, now14) >= 0)
	{
		printf("floors limit time is ok, applying limit immediately\r\n");
	}
	else
	{
		printf("floors limit time is failure\r\n");
		return -1;
	}


	// 判断是否为所有电梯全楼层权限，字符串为0,即全部禁用
	if (recv_floors_limit[0] == '0' && recv_floors_limit[1] == '\0')
	{
		printf("all elevator is limited\r\n");
		memset(elevator_tail5_limit, 0x00, sizeof(elevator_tail5_limit));
		 //memcpy(g_elevator_tail5_limit, elevator_tail5_limit, sizeof(g_elevator_tail5_limit));
		// WriteRawBytes(elevator_tail5_limit, PUBLIC_FLOORS_LIMIT_ADDR, PUBLIC_FLOORS_LIMIT_LEN);
		// WriteKey(recv_floors_limit_time, PUBLIC_FLOORS_LIMIT_TIME_ADDR, PUBLIC_FLOORS_LIMIT_TIME_LEN);
		// Floor_AuthCheck_limit(elevator_tail5_limit);
		// return 1;
	}

	// //判断是否为所有电梯全楼层权限，字符串为-1
	else if (recv_floors_limit[0] == '-' && recv_floors_limit[1] == '1' && recv_floors_limit[2] == '\0')
	{
		printf("all floor is all unlimited\r\n");
		memset(elevator_tail5_limit, 0xFF, sizeof(elevator_tail5_limit));
		 //memcpy(g_elevator_tail5_limit, elevator_tail5_limit, sizeof(g_elevator_tail5_limit));
		// WriteRawBytes(elevator_tail5_limit, PUBLIC_FLOORS_LIMIT_ADDR, PUBLIC_FLOORS_LIMIT_LEN);
		// WriteKey(recv_floors_limit_time, PUBLIC_FLOORS_LIMIT_TIME_ADDR, PUBLIC_FLOORS_LIMIT_TIME_LEN);
		/// Floor_AuthCheck_limit(elevator_tail5_limit);
		// return 1;
	}

	else
	{
		hex_len = (int)strlen(recv_floors_limit);
		byte_len = (hex_len + 1) / 2;
		// byte_len = (int)strlen(recv_floors_limit)/2;
		if (byte_len > 5)
		{
			printf("elevator hex byte len %d > 5\r\n", byte_len);
			return -1;
		}

		memset(elevator_tail5_limit, 0x00, sizeof(elevator_tail5_limit));
		out_idx = 5 - byte_len;
		in_idx = 0;

		/* 处理奇数位 hex：首个半字节作为低 4 位，高 4 位补 0 */
		if ((hex_len & 1) != 0)
		{
			nibble = qr_hex_char_to_nibble(recv_floors_limit[0]);
			if (nibble < 0)
			{
				printf("invalid hex char '%c'\r\n", recv_floors_limit[0]);
				return -1;
			}
			elevator_tail5_limit[out_idx++] = (u8)nibble;
			in_idx = 1;
		}

		// 将 hex 转为 byte，存入 elevator_tail5 的后面部分
		while (in_idx < hex_len)
		{
			first_nibble = qr_hex_char_to_nibble(recv_floors_limit[in_idx]);
			nibble = qr_hex_char_to_nibble(recv_floors_limit[in_idx + 1]);
			if (first_nibble < 0 || nibble < 0)
			{
				printf("invalid hex char in elevator_match_limit tail\r\n");
				return -1;
			}
			elevator_tail5_limit[out_idx++] = (u8)((first_nibble << 4) | nibble);
			in_idx += 2;
		}
		printf("elevator tail(5B)=%02X %02X %02X %02X %02X\r\n",
			   elevator_tail5_limit[0], elevator_tail5_limit[1], elevator_tail5_limit[2], elevator_tail5_limit[3],
			   elevator_tail5_limit[4]);

		elevator_tail5_int_limit = 0;
		for (i = 0; i < 5; i++)
			elevator_tail5_int_limit = (elevator_tail5_int_limit << 8) | elevator_tail5_limit[i];
	}

	printf("elevator_tail5_int_limit=%llu\r\n", elevator_tail5_int_limit);
	FloorCtrl_SetLimit(elevator_tail5_limit);
	WriteRawBytes(elevator_tail5_limit, PUBLIC_FLOORS_LIMIT_ADDR, PUBLIC_FLOORS_LIMIT_LEN);
	{
		uint8_t t14[PUBLIC_FLOORS_LIMIT_TIME_LEN];
		int ti;
		const char *p;

		p = recv_floors_limit_time;
		if (!p)
			return -1;
		for (ti = 0; ti < PUBLIC_FLOORS_LIMIT_TIME_LEN; ti++)
		{
			if (p[ti] < '0' || p[ti] > '9')
			{
				printf("floors limit time need 14 digits at EEPROM write\r\n");
				return -1;
			}
			t14[ti] = (uint8_t)p[ti];
		}
		WriteRawBytes(t14, PUBLIC_FLOORS_LIMIT_TIME_ADDR, PUBLIC_FLOORS_LIMIT_TIME_LEN);
		memcpy(g_floors_limit_time, t14, (size_t)PUBLIC_FLOORS_LIMIT_TIME_LEN);
		g_floors_limit_time[PUBLIC_FLOORS_LIMIT_TIME_LEN] = '\0';
	}

	Floor_AuthCheck_limit(elevator_tail5_limit);
	Bsp_SetBeep(1);
	return 1;
}

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
	MakeTimestamp14FromDS3231(&ct, now14);
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

static int Cmd_Permission(char received_data[], const char order_type_meta[8], const char card_number_meta[32], int uart_port)
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

	recv_cmd = s_recv_csv_field[0];
	recv_arg_id = s_recv_csv_field[1];
	recv_arg_count = s_recv_csv_field[2];
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

// 新增黑名单
int BlackUserListAdd(char received_data[])
{
	// 制作待加签信息
	json = BuildCvjJsonFromValidData12(valid_data);

	if (json)
	{
		// printf("Built JSON: %s\r\n", json_local);
		//  对结果进行sha1换算
		sha1_hash(json, hash_str);
		// printf("SHA1 = %s\n", hash_str_local);
		//  验签
		if (VerifySignature(hash_str, valid_data))
		{
			// printf("\r\nadd---ok\n");

			// 执行命令
			AddBlacklistIds_FromPacket(valid_data);
			// printf("add %d ID to Blacklist\n", i_local);
		}
		else
		{
			// printf("add---fail\n");
			return -1; // 验签失败
		}
	}
	else
	{
		// printf("BuildJsonFromReceived failed\r\n");
		return -2; // 构建JSON失败
	}

	return 1;
}

// 删除黑名单
int BlackUserListDel(char received_data[])
{
	// 制作待加签信息
	json = BuildCvjJsonFromValidData12(valid_data);

	if (json)
	{
		// printf("Built JSON: %s,%s\r\n", json, valid_data);
		//  对结果进行sha1换算
		sha1_hash(json, hash_str);
		// printf("SHA2 = %s\n", hash_str);

		// 验签
		if (VerifySignature(hash_str, valid_data))
		{
			// printf("Build--ok\n");

			// 执行命令
			DelBlacklistIds_FromPacket(valid_data);
			// printf("del %d ID Blacklist\n", i);
		}
		else
		{
			// printf("BlackUserListDel--fail\n");
			return -1;
		}
	}
	else
	{
		// printf("BuildJsonFromReceived failed\r\n");
		return -2;
	}

	return 1;
}
// 命令解析
int CommContrl(char received_data[], const char order_type_meta[8], const char card_number_meta[32], int uart_port)
{
	int ret;
	char cmd_ch;

	// 从 valid_data 中提取的 ID 字符串
	// const char *id = ExtractIdFromValidData(valid_data);
	// printf("ID: %s\n", id);
	//----------------------------------------------------------------------------------------------------------------------------------------------------------

	// unsigned comma_n;
	// 命令类型
	cmd_ch = received_data[0];
	(void)order_type_meta;
	(void)card_number_meta;

	if (cmd_ch == '1') // 设备参数设置（CSV 首列即为字符 '1'，并非非法）
	{
		ret = Cmd_Setting(received_data);
		if (ret == 1)
		{
			/* RTC 已写入；EEPROM 与复位在 4G AT 全部成功后由 main 完成 */
			printf("cmd setting ok, wait 4G AT then EEPROM & reboot\r\n");
		}
		return ret;
	}
	if (cmd_ch == '2') // 权限访问
	{
		ret = Cmd_Permission(received_data, order_type_meta, card_number_meta, uart_port);
		return ret;
	}
	if (cmd_ch == '3') // 黑名单
	{
		// printf("qr csv: invalid cmd char '%c'\r\n", cmd_ch);

		return -1;
	}
	if (cmd_ch == '6') // 设置时间及漂移时间
	{
		// printf("qr csv: invalid cmd char '%c'\r\n", cmd_ch);
		Cmd_Set_Time(received_data);
		return -1;
	}

	if (cmd_ch == '7') // 设置常开：CSV 4 列 cmd,hex,YYYYMMDDHHmmss,sig
	{
		return Cmd_Set_OpenLimit(received_data);
	}
	printf("no cmd matched for cmd char '%c'\r\n", cmd_ch);
	return 0;
}

// 清空所有黑名单
int BlackUserListClear(char received_data[])
{
	char user[40];
	char pass[40];
	int ok;

	// printf("BlackUserListClear--%s\n", valid_data);
	if (sscanf(valid_data, "6,%39[^,],%39[^,]", user, pass) == 2)
	{
		// printf("BlackUserListClear-- %s,%s\n", user, pass);
		/* 验证账号密码（本地字符串比较） */
		ok = VerifyDeviceCredentials(user, pass);
		if (ok)
		{
			clearPublicKeySpace();
			return 1;
		}
		else
		{
			// printf("cmd6: err\r\n");
			return -1;
		}
	}
	else
	{
		return -1;
	}
}
