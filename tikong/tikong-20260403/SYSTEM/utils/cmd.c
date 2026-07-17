#include "cmd.h"
#include "sha1.h"
#include "time.h"
#include "usart3.h"
#include "usart2.h"
#include "data_handler.h"
#include <string.h>

extern DS3231_Time currentTime; // 存放时间结构体
char *json;						// 存放待加签json
char *valid_data;				// 存放干净的数据
char hash_str[80];				// 存放sha1结果

// 新增黑名单
int DoCmd1(char received_data[])
{
	//int i;
	long long ts1;
	long long qr_code_time;
	long long messageId111;
	char jsonStr[2048]; // 分配足够大的空间

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

			// 上报事件
			DS3231_GetTime(&currentTime);
			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			// TODO JSON1
			// json1 = BuildCvjJsonFromValidData12_1(valid_data_local, hash_str_local);
			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
			snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 1"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
			// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
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
int DoCmd2(char received_data[])
{
	//int i;
	long long ts1;
	long long qr_code_time;
	long long messageId111;
	char jsonStr[2048]; // 分配足够大的空间

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

			// 上报事件
			DS3231_GetTime(&currentTime);
			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			// json1 = BuildCvjJsonFromValidData12_1(valid_data, hash_str);
			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
			snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 1"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
			// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
		}
		else
		{
			// printf("DoCmd2--fail\n");
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
// 梯控权限访问
int DoCmd3(char received_data[])
{
	long long ts1;
	long long qr_code_time;
	long long messageId111;
	char jsonStr[2048]; // 分配足够大的空间

	char hex_field[32];	 /* 保存提取的14个HEX字符 */
	uint8_t floors7[11]; /* 转换后的字节数组 */
	int ok;
//	uint8_t check;
	int len;
	char last14[15];

	uint8_t frame[FRAME_MAX_LEN];
	uint16_t chardan = 0xFFFF; /* 举例 */
	uint8_t chartihao = 0xFF;  /* 举例 */

	int i, found = 0; // 标记是否找到数据

	// 从 valid_data 中提取的 ID 字符串
	const char *id = ExtractIdFromValidData(valid_data);
	// printf("ID: %s\n", id);

	// id黑名单验证
	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		// 使用 strncmp 比较前 10 字节（假设每个 UUID 长度为 10）
		if ((strncmp((char *)dataArray[i].data, id, (DATA_SIZE - 1)) == 0) && (dataArray[i].isEmpty == 0))
		{
			found = 0;
			break; // 结束循环
		}
		else
			found = 1; // 未找到数据
	}
	if (found)
	{
		// printf("BlackList-ok\r\n");
		//  制作加签数据
		json = BuildCvjJsonFromValidData3(valid_data, hash_str);
		if (json)
		{
			// printf("Built JSON: %s\r\n", json);
		}
		else
		{
			// printf("BuildJsonFromReceived failed\r\n");
			return -1;
		}

		// 对结果进行sha1换算
		sha1_hash(json, hash_str);
		// printf("SHA1 = %s\n", hash_str);

		// 验签
		if (VerifySignature(hash_str, valid_data))
		{
			// printf("json1:%s", json1);
			//  时间检验
			DS3231_GetTime(&currentTime);
			if (CheckValidPeriod_WithNow(valid_data, &currentTime))
			{
				// 制作指令
				// 提取 22 字符 HEX，生成梯控板指令
				if (sscanf(valid_data, "%*[^,],%*[^,],%*[^,],%*[^,],%*[^,],%22[^,]", hex_field) == 1)
				{
					len = strlen(hex_field);

					/* 这里做字节反转 */
					reverse_bytes(hex_field, 22);

					/* 取后14个字符，代表1-56层 */
					if (len > 14)
						strcpy(last14, hex_field + len - 14);
					else
						strcpy(last14, hex_field);

					ok = hex14_to_bytes7(hex_field, floors7);
					if (ok)
					{
						/* 构造帧（此时 len 是当前帧长度，如 26/27） */
						/* 单层A2指令-直达，多层A1指令-开放权限 */
						if (IsSingleFloor(floors7))
						{
							build_frame_A2(chardan, chartihao, floors7, frame, &len);
						}
						else
							build_frame_A1(chardan, chartihao, floors7, frame, &len);

						/* === 追加校验 + 0xFA === */
						{
							/* 注意：在块首声明变量，兼容 ARMCC C90 */
							uint8_t checksum;

							checksum = sum_checksum(frame, len); /* 对 frame[0..len-1] 做和 */
							// printf("And Check = %02X\n", checksum);
							if (len + 2 <= FRAME_MAX_LEN)
							{
								/* 确保缓冲足够 */
								frame[len++] = checksum; /* 追加校验字节 */
								frame[len++] = 0xFA;	 /* 追加结束符 0xFA */
							}
							else
							{
								// printf("frame 缓冲不足，无法追加校验和 0x0D！\r\n");
								return -2;
							}

							// printf("Final length=%d\n", len);
						}
					}
					else
					{
						return -3;
					}
				}
				else
				{
					return -4;
				}
				RS485_SendData(frame, 21);

				// 上报事件
				DS3231_GetTime(&currentTime);
				messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
				ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
				// json1 = BuildCvjJsonFromValidData3_1(valid_data, hash_str, id);
				qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
				snprintf(jsonStr, sizeof(jsonStr),
						 "1,{"
						 "\"messageId\": %lld,"
						 "\"body\": [{"
						 "\"key\": \"qr_code_report\","
						 "\"ts\": %lld,"
						 "\"info\": [{"
						 "\"key\": \"qr_code_data\","
						 "\"value\": \"%s\""
						 "}, {"
						 "\"key\": \"qr_code_time\","
						 "\"value\": %lld"
						 "}, {"
						 "\"key\": \"handle_result\","
						 "\"value\": 1"
						 "}]"
						 "}]"
						 "}",
						 messageId111, ts1, received_data, qr_code_time);
				// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
				USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
			}
			else
			{
				// printf("currentTime--FAIL\r\n");
				return -5;
			}
		}
		else
		{
			// printf("验签失败\n");
			return -6;
		}
	}
	else
	{
		// printf("on the blacklist\r\n");
		return -7;
	}
	return 1;
}
// 更新公钥
int DoCmd4(char received_data[])
{
	long long ts1;
	long long qr_code_time;
	long long messageId111;
	char jsonStr[2048]; // 分配足够大的空间
	char id4[40];
	char user[40];
	char pass[40];
	char pk_new[PUBLIC_KEY_LEN]; // 新的密钥
	//int i,
  int		 ok;

	// printf("cmd4 publicKey update\r\n");
	if (sscanf(valid_data, "4,%39[^,],%39[^,],%39[^,],%39[^,\r\n]", id4, user, pass, pk_new) == 4)
	{
		/* 验证账号密码（本地字符串比较） */
		ok = VerifyDeviceCredentials(user, pass);
		if (ok)
		{
			/* 更新设备公钥 */
			SetDevicePublicKey(pk_new);
			// printf("cmd4: update ok: %s\r\n", GetDevicePublicKey());

			// 上报事件
			DS3231_GetTime(&currentTime);
			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
			snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 1"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
			// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
		}
		else
		{
			// printf("cmd4: err\r\n");
			return -1;
		}
	}
	else
	{
		// printf("cmd4: err decode\r\n");
		return -2;
	}
	return 1;
}
// 更新时间
int DoCmd5(char received_data[])
{
	long long ts1;
	long long qr_code_time;
	long long messageId111;
	char jsonStr[2048]; // 分配足够大的空间
	char username[50], password[50], timestamp[15];
	int items11;
//	char user[40];
	//char pass[40];

	// printf("命令是更新时间\r\n");
	// printf("%s", valid_data);

	// 使用 sscanf 提取字段
	items11 = sscanf(valid_data, "5,%49[^,],%49[^,],%14[^!]",
					 username, password, timestamp);

	if (items11 == 3)
	{
		// printf("username: %s\n", username);
		// printf("password: %s\n", password);
		// printf("timestamp: %s\n", timestamp);

		// 与原始全局变量进行比较
		if ((strcmp(username, g_device_user) && strcmp(password, g_device_pass)) == 0)
		{
			int year, month, date, hour, minute, second;
			struct tm time; // 声明 struct tm 类型
			// 使用 sscanf 解析时间
			sscanf(timestamp, "%4d%2d%2d%2d%2d%2d", &year, &month, &date, &hour, &minute, &second);

			currentTime.year = year - 2000;
			currentTime.month = month;
			currentTime.date = date;
			currentTime.dayOfWeek = 2;
			currentTime.hour = hour;
			currentTime.minute = minute;
			currentTime.second = second;
			// 计算星期几
			memset(&time, 0, sizeof(time)); // 初始化结构体
			time.tm_year = year - 1900;		// tm_year 以1900为基准
			time.tm_mon = month - 1;		// tm_mon 以0为基准
			time.tm_mday = date;
			time.tm_hour = hour;
			time.tm_min = minute;
			time.tm_sec = second;

			currentTime.dayOfWeek = time.tm_wday == 0 ? 7 : time.tm_wday; // tm_wday 0=Sunday, 1=Monday, ..., 6=Saturday

			// 调用 DS3231_SetTime 设置时间
			DS3231_SetTime(&currentTime);

			DS3231_GetTime(&currentTime);
			delay_ms(1000);
			// printf("20%02d-%02d-%02d %02d:%02d:%02d\n",
			// currentTime.year, currentTime.month, currentTime.date,
			// currentTime.hour, currentTime.minute, currentTime.second);

			// 上报事件
			DS3231_GetTime(&currentTime);
			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
			snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 1"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
			// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
		}
		else
		{
			return -1;
		}
	}
	else
	{
		return -2;
	}
	return 1;
}

// 清空所有黑名单
int DoCmd6(char received_data[])
{
	long long ts1;
	long long qr_code_time;
	long long messageId111;
	char jsonStr[2048]; // 分配足够大的空间

	char user[40];
	char pass[40];
	int ok;

	// printf("DoCmd6--%s\n", valid_data);
	if (sscanf(valid_data, "6,%39[^,],%39[^,]", user, pass) == 2)
	{
		// printf("DoCmd6-- %s,%s\n", user, pass);
		/* 验证账号密码（本地字符串比较） */
		ok = VerifyDeviceCredentials(user, pass);
		if (ok)
		{
			clearPublicKeySpace();

			// 上报事件
			DS3231_GetTime(&currentTime);
			messageId111 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			ts1 = (long long)DS3231_To_MillisTimestamp(&currentTime);
			qr_code_time = (long long)DS3231_To_MillisTimestamp(&currentTime);
			snprintf(jsonStr, sizeof(jsonStr),
					 "1,{"
					 "\"messageId\": %lld,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_report\","
					 "\"ts\": %lld,"
					 "\"info\": [{"
					 "\"key\": \"qr_code_data\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qr_code_time\","
					 "\"value\": %lld"
					 "}, {"
					 "\"key\": \"handle_result\","
					 "\"value\": 1"
					 "}]"
					 "}]"
					 "}",
					 messageId111, ts1, received_data, qr_code_time);
			// printf("\r\nGenerated JSON with prefix:\n%s\n", jsonStr);
			USART3_SendData((uint8_t *)jsonStr, strlen(jsonStr));
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
