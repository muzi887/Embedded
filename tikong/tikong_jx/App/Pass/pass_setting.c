#include "pass_setting.h"
#include "uart3_at_sequence.h"
#include "data_handler.h"
#include "app_run.h"
#include "sha1.h"
#include "pass_crypto.h"
#include "floor_ctrl.h"
#include "rly.h"
#include "timer.h"
#include "pass_csv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* data 段为 CSV：固定 8 个逗号，即 9 列 */
#define QR_RECV_CSV_COMMA_N 8u
#define QR_RECV_CSV_COL_N (QR_RECV_CSV_COMMA_N + 1u)

static char *recv_device_id;		  // 设备账号
static char *recv_device_password;	  // 设备密码
static char *recv_device_type;		  // 设备类型
static char *recv_device_code;		  // 设备编码
static char *recv_device_public_key; // 加密密钥
static char *recv_sys_time;		  // 系统时间
static char *recv_floors_limit;	  // 限定楼层
static char *recv_floors_limit_time; // 限定时间
static char *recv_drift_time;		  // 漂移时间
static char *recv_device_mode;		  // 设备模式

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

/* MQTT 连接参数（由 Cmd_Setting 解析后写入，供 uart3_at_sequence 读取） */
char g_mqtt_addr[PUBLIC_MQTT_ADDR_LEN + 1];
char g_mqtt_productKey[PUBLIC_MQTT_PRODUCTKEY_LEN + 1];
char g_mqtt_deviceKey[PUBLIC_MQTT_DEVICEKEY_LEN + 1];
char g_mqtt_deviceSecret[PUBLIC_MQTT_DEVICESECRET_LEN + 1];

static char *recv_cmd;
static char *recv_arg_signature; // 签名

char g_floors_limit_time[PUBLIC_FLOORS_LIMIT_TIME_LEN+1];

static char hash_str[80];							// 存放sha1结果

int Cmd_Setting(char received_data[])
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

int Cmd_Set_Time(char received_data[])
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

	/* 验签：将最后一个逗号后的字段替换为设备密钥后计算 SHA1 前10位 */
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

int Cmd_Set_OpenLimit(char received_data[])
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

	MakeTimestamp14(&now, now14);

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

int qr_hex_char_to_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
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


