#include "gprs.h"
#include "usart.h"
#include "uart_queue.h"
#include "property_report.h"
#include <stdio.h>
#include <string.h>
#include "crc16.h"
#include "delay.h"

u8 rxparagate = 0; // 接收到的闸门开度
u8 remoteflag = 0;
static u32 gprs_clock_period_tick = 0;
#define GPRS_CLOCK_PERIOD_MS (36000 * 1UL * 60 * 1000) /* 状态4下周期 CLOCK 间隔（36000分钟） */

CJsonEquipment Cjsonequipment;
OTA_InfoCB OTA_Info; // 存储在24C02内的OTA信息回调的结构体

static u32 gprs_periodic_last_tick = 0;
static u8 clock_just_entered = 1; /* 离线时置1，进入 NORMAL 时立即发一次 */

static void Gprs_PeriodicPush_Process(void)
{
	if (!gprs_is_ready())
	{
		clock_just_entered = 1; /* 离线时置位，下次进入 NORMAL 立即发送 */
		return;
	}
	/* 进入时立即发一次，之后每隔 GPRS_CLOCK_PERIOD_MS 发送（逻辑同 gprs_AT 中 just_entered） */
	if (clock_just_entered || (sys_ms - gprs_clock_period_tick) >= GPRS_CLOCK_PERIOD_MS)
	{
		clock_just_entered = 0;
		gprs_clock_period_tick = sys_ms;
		Send_cjson_clock();
	}
}

//-------------GPRS数据处理------------------
void Gprs_Process(void)
{
	//gprs_check(); /* 检查在线 LED */

	/* 1. 接收：DMA 数据追加、帧提取入队 */
	gprs_rx_datas();

	/* 2. 用户程序：从统一接收队列取帧，按内容分发 */
	Parse_Process();

	/* 3. GPRS 状态机：离线→AT 初始化→正常工作 */
	gprs_state_machine();

	/* 4. 定时 JSON 推送（gateproperty） */
	Gprs_PeriodicPush_Process();

	/* 5. 发送状态机驱动 */
	UartTx_Queue_Process(sys_ms);
}

//------------GPRS接收数据处理-----------------------------------//
void gprs_rx_datas(void)
{
	u16 u2rxcnt;
	char tmp[1025];

	if (Usart2_Rec_Cnt == 0)
		return;

	u2rxcnt = Usart2_Rec_Cnt;
	if (u2rxcnt > 1024)
		u2rxcnt = 1024;
	memcpy(tmp, DMA_Rece_Buf2, u2rxcnt);

	MY4G_printf("[GPRS] DMA_Rece_Buf2[%d]: %.*s\n", u2rxcnt, u2rxcnt, DMA_Rece_Buf2);

	tmp[u2rxcnt] = '\0';

	if (strstr(tmp, "WH-LTE-7S1") != NULL)
	{
		atflag_online = 1;
		Usart2_Rec_Cnt = 0;
		return;
	}
	UartRx_Append(DMA_Rece_Buf2, u2rxcnt);
	Usart2_Rec_Cnt = 0;
	UartRx_Extract();
}

//------------解析分发 Parse_Process-------------------//
void Parse_Process(void)
{
	char frame[UART_RX_ITEM_MAX];
	u16 len;
	char *pos;
	u8 order_type = 0;

	char json_buf[UART_TX_JSON_LEN];
	char *json_start;
	u16 json_len;

	cJSON *root;
	int validate_result;
	cJSON *type;

	while (!UartRx_IsEmpty())
	{
		/*
		 * 1. 从接收队列取出一帧数据
		 * 2. 判断内容：如果包含 JSON 则按 JSON 处理，否则按 AT 回执处理
		 *    - JSON 处理：提取 JSON 字符串，判断是那种类型json，命令还是回执
		 *    -命令调用 handleMessage
		 * 	  -回执调用 UartTx_OnRecv
		 *    - AT 回执处理：调用 UartTx_OnRecv 处理当前等待的 AT 指令


		*/
		if (UartRx_Pop(frame, sizeof(frame), &len))
		{
			// 过滤掉混合情况：优先提取 JSON 处理
			extract_json_from_string(frame, len, &json_start, &json_len);
			if (json_len != 0) // json
			{
				memcpy(json_buf, json_start, json_len);
				json_buf[json_len] = '\0';

				// handleMessage(out_buf, json_len);
				root = cJSON_Parse(json_buf);
				// 验证json是否合法

				validate_result = validate_server_reply_json(root);

				if (validate_result != 0)
				{
					printf("s->c back JSON validation failed with error code: %d\n", validate_result);
					cJSON_Delete(root);
					return;
				}

				// 解析数据
				type = cJSON_GetObjectItem(root, "type");
				if (strcmp(type->valuestring, "cmd") == 0) // 接收到服务器发来的（命令）
				{
					cjson_cmd(root);
				}
				else if (strcmp(type->valuestring, "back") == 0) // 接收到服务器发送的（设备数据回执）
				{
					UartTx_OnRecv(json_buf, json_len, 2);
				}
				// return 1;
				cJSON_Delete(root); // 释放空间
			}
			else // at
			{
				// 发送队列有等待的指令，从统一接收队列中依次取出每一帧
				UartTx_OnRecv(frame, len, 1);
			}
		}
	}
}

//------------CJSON 命令 ------------------------------------
// 设备命令处理
u8 cjson_cmd(cJSON *root)
{
	cJSON *back, *CmdName, *random;
	u8 txreply = 0; // 客户端是否需要回执 1需要 2不需要
	u8 txsta = 0;

	back = cJSON_GetObjectItem(root, "back");
	CmdName = cJSON_GetObjectItem(root, "CmdName");
	random = cJSON_GetObjectItem(root, "random");

	// if (random && (random->type == cJSON_String) && (random->valuestring != NULL))
	// {
	// memset(CParas.random, 0, bRandom);
	// strncpy(CParas.random, random->valuestring, strlen(random->valuestring));
	// }
	// else
	// {
	// 	MY4G_printf("Warning: random not found or not a string\n");
	// 	return 1;
	// }

	// if (back && (back->type == cJSON_String) && (back->valuestring != NULL))
	// {
	if (strcmp(back->valuestring, "true") == 0)
		txreply = 1;
	else if (strcmp(back->valuestring, "false") == 0)
		txreply = 0;

	if (strcmp(CmdName->valuestring, "enquiry") == 0) // 刷新状态
	{
		txsta = reply_cjson_enquiry(root, txreply);
	}
	else if (strcmp(CmdName->valuestring, "setting") == 0) // 设置闸门开度
	{
		txsta = reply_cjson_setting(root, txreply);
	}
	else if (strcmp(CmdName->valuestring, "version") == 0) // 获取版本号
	{
		txsta = reply_cjson_version(root, txreply);
	}
	else if (strcmp(CmdName->valuestring, "update") == 0) // 设置更新使能标志
	{
		txsta = reply_cjson_update(root, txreply);
	}
	else
	{
		MY4G_printf("Warning: CmdName not exist\n");
		return 1;
	}

	return txsta;
}

//------ 客户端 -> 服务器 -------响应回复（主动）------这里一部分，在属性，事件，报警也有---
// 发送clock命令json字符串
#define PARA_CLOCK_JSON_BUF_SIZE 32
void Send_cjson_clock(void)
{
	char json_str[512] = {'\0'};
	char random_buf[32];
	char para_buf[PARA_CLOCK_JSON_BUF_SIZE] = "{\"operation\":\"getclock\"}";
	/* 进入时立即发一次，之后每隔 36000 分钟向发送队列添加 clock 查询 */
	// if (gprs_clock_period_tick==0xffff || (sys_ms - gprs_clock_period_tick) >= GPRS_CLOCK_PERIOD_MS)
	// {
	generate_random_string(random_buf);
	printf("--Send_cjson_clock--\r\n");
	generateJsonString(json_str, sizeof(json_str), "cmd", "false", Cjsonequipment.clientid, "clock", random_buf, para_buf);

	//		gprs_clock_period_tick = sys_ms;
	 //printf("Send_cjson_clock-->%s\r\n",json_str);
	UartTx_Push_JSON(json_str, sizeof(json_str), random_buf, reply_cjson_clock, UART_TX_JSON_TIMEOUT_MS, 0);
	//	}
}
//------ 客户端 -> 服务器 -------响应回复（非主动）---------
// 装配状态回执的json字符串
#define PARA_ENQUIRY_JSON_BUF_SIZE 256
void enquiry_para_to_json(const EnquiryPara *p, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size,
			 "{"
			 "\"MotorCurrent\":%0.2f,"
			 "\"MotorVoltage\":%0.2f,"
			 "\"BatteryCurrent\":%0.2f,"
			 "\"BatteryVoltage\":%0.2f,"
			 "\"Traffic\":%0.2f,"
			 "\"LastTime\":\"%s\","
			 "\"Opening\":%u"
			 "}",
			 p->MotorCurrent, p->MotorVoltage, p->BatteryCurrent, p->BatteryVoltage,
			 p->Traffic, p->LastTime, p->Opening);
}
// 装配state的json字符串
#define PARA_STATE_JSON_BUF_SIZE 128
void state_para_to_json(const StatePara *p, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size,
			 "{"
			 "\"state\":\"%s\""
			 "}",
			 p->state);
}
// 装配version的json字符串
#define PARA_VERSION_JSON_BUF_SIZE 128
void version_para_to_json(const VersionPara *p, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size,
			 "{"
			 "\"version\":\"%s\""
			 "}",
			 p->version);
}
// 总装json函数
void generateJsonString(char *buf, size_t buf_size, const char *type, const char *back, const char *clientid, const char *CmdName, const char *random, const char *para)
{
	snprintf(buf, buf_size,
			 "%d,{"
			 "\"type\":\"%s\","
			 "\"back\":\"%s\","
			 "\"clientid\":\"%s\","
			 "\"CmdName\":\"%s\","
			 "\"para\":%s,"
			 "\"random\":\"%s\""
			 "}",
			 Topic_Pub, type, back, clientid, CmdName, para, random);
}

u8 reply_cjson_update(cJSON *root, u8 reply)
{
	cJSON *random_obj, *para_obj, *enable_obj;
	// const char *type = "back";
	// const char *back = "false";
	// const char *CmdName = "setting";
	char json_str[512] = {'\0'};
	const char *random;

	StatePara para_state;
	char para_buf[PARA_STATE_JSON_BUF_SIZE] = {'\0'};

	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	/* enable 在 para 内：{"para":{"enable":true}} */
	para_obj = cJSON_GetObjectItem(root, "para");
	enable_obj = (para_obj != NULL) ? cJSON_GetObjectItem(para_obj, "enable") : NULL;
	if (cJSON_IsBool(enable_obj))
	{
		if (cJSON_IsTrue(enable_obj))
		{
			OTA_Info.UPDATE_flag = 1; // 0 无需ota ，1 需要联网ota
			OTA_Enable=1;
			printf("set OTA_Info.UPDATE_flag=1 \r\n");
			AT24CXX_Write(OTA_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
		}
		else
		{
			OTA_Info.UPDATE_flag = 0; // 0 无需ota ，1 需要联网ota
			OTA_Enable=0;
			printf("set OTA_Info.UPDATE_flag=0 \r\n");
			AT24CXX_Write(OTA_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
		}
	}

	if (reply)
	{
		sprintf(para_state.state, "ok");
		state_para_to_json(&para_state, para_buf, sizeof(para_buf));
		generateJsonString(json_str, sizeof(json_str), "back", "false", Cjsonequipment.clientid, "update", random, para_buf);

		printf("reply_cjson_update-->%s\r\n", json_str);
		UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);
	}
	return 0;
}

// 回执  -获取系统时间，解析 para.time 写入 DS1302
//  数据格式：{"type":"back","back":"false","clientid":"xxx","CmdName":"clock","para":{"time":"2026-03-13 19:28:49"},"random":"xxx"}
void reply_cjson_clock(char *data, u16 len)
{
	cJSON *root, *para, *time_obj;
	const char *time_str;
	int year, month, day, hour, min, sec;
	u8 time_bcd[7];

	if (data == NULL || len == 0)
		return;

	/* 确保可解析：data 在 Parse_Process 中已 null 结尾，直接解析 */
	root = cJSON_Parse(data);
	if (root == NULL)
		return;

	para = cJSON_GetObjectItem(root, "para");
	if (para == NULL)
	{
		cJSON_Delete(root);
		return;
	}
	time_obj = cJSON_GetObjectItem(para, "time");
	if (time_obj == NULL || !cJSON_IsString(time_obj))
	{
		cJSON_Delete(root);
		return;
	}
	time_str = time_obj->valuestring;
	if (time_str == NULL)
	{
		cJSON_Delete(root);
		return;
	}

	if (sscanf(time_str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6)
	{
		cJSON_Delete(root);
		return;
	}
	cJSON_Delete(root);

	/* 范围校验 */
	if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
		hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
		return;

	/* 十进制转 BCD：DS1302 顺序为 秒/分/时/日/月/星期/年 */
	time_bcd[0] = (u8)(((sec / 10) << 4) | (sec % 10));
	time_bcd[1] = (u8)(((min / 10) << 4) | (min % 10));
	time_bcd[2] = (u8)(((hour / 10) << 4) | (hour % 10));
	time_bcd[3] = (u8)(((day / 10) << 4) | (day % 10));
	time_bcd[4] = (u8)(((month / 10) << 4) | (month % 10));
	time_bcd[5] = (u8)(simple_weekday(year, month, day));
	time_bcd[6] = (u8)((((year % 100) / 10) << 4) | ((year % 100) % 10));

	ds1302_settime(time_bcd);
}
// 回执 - 查询设备版本号
u8 reply_cjson_version(cJSON *root, u8 reply)
{
	cJSON *random_obj;
	// const char *type = "back";
	// const char *back = "false";
	// const char *CmdName = "setting";
	char json_str[512] = {'\0'};
	const char *random;

	VersionPara para_version;
	char para_buf[PARA_VERSION_JSON_BUF_SIZE] = {'\0'};

	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	if (reply)
	{
		snprintf(para_version.version, sizeof(para_version.version), "%s", (const char *)OTA_Info.OTA_ver);
		version_para_to_json(&para_version, para_buf, sizeof(para_buf));
		generateJsonString(json_str, sizeof(json_str), "back", "false", Cjsonequipment.clientid, "version", random, para_buf);

		UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);
	}
	return 0;
}
// 回执 - 查询状态
u8 reply_cjson_enquiry(cJSON *root, u8 reply)
{
	EnquiryPara para;
	char para_buf[PARA_ENQUIRY_JSON_BUF_SIZE] = {'\0'};
	char json_str[512] = {'\0'};

	cJSON *random_obj;
	const char *random;
	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	/* 按预定结构体赋值 */
	para.MotorCurrent = g_adc_val[0] / 100.0f;
	para.MotorVoltage = g_adc_val[2] / 10.0f + 0.3f;
	para.BatteryCurrent = 15.52f;
	para.BatteryVoltage = g_adc_val[2] / 10.0f + 0.3f;
	para.Traffic = 15.52f;
	get_lasttime(para.LastTime);
	para.Opening = (u8)armReg.gate_kaidu;

	enquiry_para_to_json(&para, para_buf, sizeof(para_buf));
	MY4G_printf("[GPRS] 3\r\n");
	generateJsonString(json_str, sizeof(json_str), "back", "false", "xxx", "enquiry", random, para_buf);
	MY4G_printf("[GPRS] 2\r\n");
	UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);

	return 0;
}
// 回执 - 设置闸门开度
u8 reply_cjson_setting(cJSON *root, u8 reply)
{
	cJSON *para, *opening, *random_obj;
	// const char *type = "back";
	// const char *back = "false";
	// const char *CmdName = "setting";
	char json_str[512] = {'\0'};
	const char *random;

	StatePara para_state;
	char para_buf[PARA_STATE_JSON_BUF_SIZE] = {'\0'};

	para = cJSON_GetObjectItem(root, "para");
	opening = cJSON_GetObjectItem(para, "valve_opening");
	rxparagate = cJSON_GetNumberValue(opening);

	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	if (rxparagate != armReg.gate_kaidu)
	{
		armReg.range_target = rxparagate;

		// printf("armReg =%d, %d\n",armReg.range_target,armReg.range_reality);

		if ((armReg.range_target < armReg.gate_kaidu) && FLAG_CLOSEALL != 1 && autoFlag == 0)
		{
			sys_moter_state = MOTOR_DOWN;
			remoteflag = 1;
			// if(armReg.range_target >=2)
			// 	armReg.range_target += 2;
		}
		else if ((armReg.range_target > armReg.gate_kaidu) && FLAG_OPENALL != 1 && autoFlag == 0)
		{
			sys_moter_state = MOTOR_UP;
			remoteflag = 1;
			// if(armReg.range_target >=2)
			// 	armReg.range_target -= 2;
		}
	}

	if (reply)
	{
		sprintf(para_state.state, "ok");
		state_para_to_json(&para_state, para_buf, sizeof(para_buf));
		generateJsonString(json_str, sizeof(json_str), "back", "false", Cjsonequipment.clientid, "setting", random, para_buf);

		UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);
	}
	return 0;
}

// 工具函数------------------------------

// 判断json合法性
/**
 * @brief 验证JSON是否符合服务器回执格式
 * @param root cJSON根对象
 * @return 0-符合条件, 其他值-错误代码
 *
 * 错误代码定义：
 * -21: type字段不存在
 * -22: type字段不是字符串
 * -23: type字段值不是"back"
 * -24: back字段不存在
 * -25: back字段不是字符串
 * -26: clientid字段不存在
 * -27: CmdName字段不存在
 * -28: CmdName字段不是字符串
 * -29: CmdName字段值不是"version"或"download"
 * -30: para字段不存在
 * -31: para字段不是对象
 * -32: para.version字段不存在或不是字符串
 * -33: para.size字段不存在或不是数字
 * -34: para.size字段不是整数
 * -35: para.upgrade字段不存在或不是数字
 * -36: para.upgrade字段不是整数
 * -37: para.offset字段不存在或不是数字
 * -38: para.offset字段不是整数
 * -39: para.size字段不存在或不是数字（download命令）
 * -40: para.size字段不是整数（download命令）
 * -41: random字段不存在
 */

// 这部分代码考虑在服务器端验证，后期客户端代码取消。
int validate_server_reply_json(cJSON *root)
{
	cJSON *item;
	double size_value;
	double upgrade_value;
	double offset_value;

	// 1. 检查type字段
	item = cJSON_GetObjectItem(root, "type");
	if (item == NULL)
	{
		printf("Error: type field not found\n");
		return -21;
	}
	if (!cJSON_IsString(item) || item->valuestring == NULL)
	{
		printf("Error: type field is not a string\n");
		return -22;
	}
	if (strcmp(item->valuestring, "back") != 0 && strcmp(item->valuestring, "cmd") != 0)
	{
		printf("Error: type field value is not 'back' or 'cmd', got: %s\n", item->valuestring);
		return -23;
	}

	// 2. 检查back字段
	item = cJSON_GetObjectItem(root, "back");
	if (item == NULL)
	{
		printf("Error: back field not found\n");
		return -24;
	}
	if (!cJSON_IsString(item) || item->valuestring == NULL)
	{
		printf("Error: back field is not a string\n");
		return -25;
	}
	// back字段值可以是"true"或"false"，这里只检查是否存在

	// 3. 检查clientid字段
	item = cJSON_GetObjectItem(root, "clientid");
	if (item == NULL)
	{
		printf("Error: clientid field not found\n");
		return -26;
	}
	// clientid可以是字符串或数字，这里只检查是否存在

	// 4. 检查CmdName字段
	item = cJSON_GetObjectItem(root, "CmdName");
	if (item == NULL)
	{
		printf("Error: CmdName field not found\n");
		return -27;
	}
	if (!cJSON_IsString(item) || item->valuestring == NULL)
	{
		printf("Error: CmdName field is not a string\n");
		return -28;
	}
	// if (strcmp(item->valuestring, "version") != 0 &&
	// 	strcmp(item->valuestring, "download") != 0)
	// {
	// 	printf("Error: CmdName field value is not 'version' or 'download', got: %s\n",
	// 		   item->valuestring);
	// 	return -29;
	// }

	// 5. 检查para字段（必须是对象）
	item = cJSON_GetObjectItem(root, "para");
	if (item == NULL)
	{
		printf("Error: para field not found\n");
		return -30;
	}
	if (!cJSON_IsObject(item))
	{
		printf("Error: para field is not an object\n");
		return -31;
	}

	// 6. 根据CmdName验证para字段的内容
	// if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "version") == 0)
	// {
	// 	// version命令：para必须包含version, size, upgrade字段
	// 	cJSON *ver = cJSON_GetObjectItem(item, "version");
	// 	cJSON *size = cJSON_GetObjectItem(item, "size");
	// 	cJSON *upgrade = cJSON_GetObjectItem(item, "upgrade");

	// 	if (ver == NULL || !cJSON_IsString(ver) || ver->valuestring == NULL)
	// 	{
	// 		printf("Error: para.version field not found or not a string\n");
	// 		return -32;
	// 	}
	// 	if (size == NULL || !cJSON_IsNumber(size))
	// 	{
	// 		printf("Error: para.size field not found or not a number\n");
	// 		return -33;
	// 	}
	// 	// 检查size字段是否是整数（不是浮点数）
	// 	size_value = cJSON_GetNumberValue(size);
	// 	if (size_value != (int)size_value)
	// 	{
	// 		printf("Error: para.size field is not an integer (got: %f)\n", size_value);
	// 		return -34;
	// 	}
	// 	if (upgrade == NULL || !cJSON_IsNumber(upgrade))
	// 	{
	// 		printf("Error: para.upgrade field not found or not a number\n");
	// 		return -35;
	// 	}
	// 	// 检查upgrade字段是否是整数（不是浮点数）
	// 	upgrade_value = cJSON_GetNumberValue(upgrade);
	// 	if (upgrade_value != (int)upgrade_value)
	// 	{
	// 		printf("Error: para.upgrade field is not an integer (got: %f)\n", upgrade_value);
	// 		return -36;
	// 	}
	// }
	// else if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "download") == 0)
	// {
	// 	// download命令：para必须包含offset, size字段
	// 	cJSON *offset = cJSON_GetObjectItem(item, "offset");
	// 	cJSON *size = cJSON_GetObjectItem(item, "size");

	// 	if (offset == NULL || !cJSON_IsNumber(offset))
	// 	{
	// 		printf("Error: para.offset field not found or not a number\n");
	// 		return -37;
	// 	}
	// 	// 检查offset字段是否是整数（不是浮点数）
	// 	offset_value = cJSON_GetNumberValue(offset);
	// 	if (offset_value != (int)offset_value)
	// 	{
	// 		printf("Error: para.offset field is not an integer (got: %f)\n", offset_value);
	// 		return -38;
	// 	}
	// 	if (size == NULL || !cJSON_IsNumber(size))
	// 	{
	// 		printf("Error: para.size field not found or not a number\n");
	// 		return -39;
	// 	}
	// 	// 检查size字段是否是整数（不是浮点数）
	// 	size_value = cJSON_GetNumberValue(size);
	// 	if (size_value != (int)size_value)
	// 	{
	// 		printf("Error: para.size field is not an integer (got: %f)\n", size_value);
	// 		return -40;
	// 	}
	// }

	// 7. 检查random字段
	item = cJSON_GetObjectItem(root, "random");
	if (item == NULL)
	{
		printf("Error: random field not found\n");
		return -41;
	}
	// random可以是字符串或数字，这里只检查是否存在

	return 0; // 所有检查通过
}
// 时间格式化
void get_lasttime(char *output)
{
	char time[7];
	ds1302_dataread(time);
	sprintf(output, "20%02d-%02d-%02d %02d:%02d:%02d", //%04d
			time[6], time[4], time[3], time[2], time[1], time[0]);
}

// 生成随机字符串
void generate_random_string(char *output)
{
	char time[7];
	ds1302_dataread(time);
	sprintf(output, "20%02d%02d%02d%02d%02d%02d1000", //%04d
			time[6], time[4], time[3], time[2], time[1], time[0]);
	// rand() % 10000);
}