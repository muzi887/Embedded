// 头文件
#include "main.h"
#include "usart.h"
#include "gprs.h"
#include <stdio.h>
#include <string.h>
#include "crc16.h"
#include "delay.h"
#include "boot.h"
#include "ProgressBar.h"
#include "display.h"
u8 rxparagate = 0; // 接收到的闸门开度
u8 atflag = 0;	   // at可查询
extern int pub_version_flag;
OtaPara OtaParas;
CJsonPara CParas;
char clientid[bClientid];
u8 process = 0;
extern OTA_InfoCB OTA_Info; // 存储在24C02内的OTA信息回调的结构体

// OTA下载上下文（跨包保留）
static u32 s_dl_addr = ST32_A2_SADDR;
static u32 s_dl_lastaddr = ST32_A2_SADDR;
static u16 s_dl_last_offset = 0;
static u8 s_dl_crc_retry_cnt = 0;

void gprs_download_context_reset(void)
{
	s_dl_addr = ST32_A2_SADDR;
	s_dl_lastaddr = ST32_A2_SADDR;
	s_dl_last_offset = 0;
	s_dl_crc_retry_cnt = 0;
}

//-------------------------GPRS接收数据处理-----------------------------------//
// 由于使用mqtt模块接收数据，暂不考虑数据的完整性问题，直接处理接收到的数据
/*
数据格式：
   {JSON字符串} + {X字节二进制数据} + {2字节CRC}
	↑              ↑                  ↑
   pos           pos+1            pos+1+Size
*/
int gprs_rx_datas()
{
	char *pos;
	char UART_RX2_BUF[1024] = {0};
	u16 i = 0, u2rxcnt = 0;
	cJSON *root;
	char cjsonStr[1024] = {'\0'};
	u16 cjson_len = 0;
	u16 message_len = 0;
	int validate_result;
	int txsta;
	int rx_ret = GPRS_RX_RET_PARSE_FAILED;
	if (Usart2_Rec_Cnt == 0)
		return GPRS_RX_RET_NO_DATA;

	__disable_irq();
	u2rxcnt = Usart2_Rec_Cnt;
	if (u2rxcnt > sizeof(UART_RX2_BUF))
		u2rxcnt = sizeof(UART_RX2_BUF);
	Usart2_Rec_Cnt = 0;
	for (i = 0; i < u2rxcnt; i++)
	{
		UART_RX2_BUF[i] = DMA_Rece_Buf2[i];
		DMA_Rece_Buf2[i] = 0;
	}
	__enable_irq();

	if (SYS_state == OTA_STATE_WAIT_NET)
	{
		if (str_contains(UART_RX2_BUF, "OK"))
		{
			SYS_state = OTA_STATE_AT_OK;
			printf("Received AT OK\r\n");
			return GPRS_RX_RET_AT_OK;
		}
		else
			return GPRS_RX_RET_NO_DATA;
	}
	if (SYS_state == OTA_STATE_AT_OK) // at模块启动后，等待网络上线还需要一段时间，这期间会收到模块的型号等，暂不处理。
	{
				// 判断是否为+MQTTCID 响应
				pos = strstr(UART_RX2_BUF, "+MQTTCID:");
				if (pos != NULL)
				{
					u16 cid_start = (u16)(pos - UART_RX2_BUF);
					u16 cid_value_start = (u16)(cid_start + 9);
					u16 cid_value_end = (u16)(cid_start + 19); // 包含最后一位
					u16 k = 0;
					if (cid_value_end >= u2rxcnt)
					{
						printf("MQTTCID frame too short\r\n");
						return GPRS_RX_RET_NO_DATA;
					}
					for (k = cid_value_start; k <= cid_value_end; k++)
					{
						if (UART_RX2_BUF[k] < '0' || UART_RX2_BUF[k] > '9')
						{
							printf("MQTTCID contains non-digit\r\n");
							return GPRS_RX_RET_NO_DATA;
						}
					}
					// 设备编号
					clientid[0] = pos[9];
					clientid[1] = pos[10];
					clientid[2] = pos[11];
					clientid[3] = pos[12];
					clientid[4] = pos[13];
					clientid[5] = pos[14];
					clientid[6] = pos[15];
					clientid[7] = pos[16];
					clientid[8] = pos[17];
					clientid[9] = pos[18];
					clientid[10] = pos[19];
					clientid[11] = '\0';

					printf("Received MQTT CID: %s\r\n", clientid);
					SYS_state = OTA_STATE_MQTTCID_OK;
					return GPRS_RX_RET_MQTTCID_OK;
				}
				else
				{
					printf("Received data while waiting for network, but no +MQTTCID found\r\n");
					return GPRS_RX_RET_NO_DATA;
				}

		}
		if (u2rxcnt <= 2 || UART_RX2_BUF[2] != '{')
		{
			return GPRS_RX_RET_PARSE_FAILED;
		}
		pos = find_json_end_by_bracket_matching(UART_RX2_BUF + 2, u2rxcnt - 2);

		if (pos == NULL)
		{ // 接收到JSON
			printf("s->c back JSON validation failed with end or start is error --\r\n");
			return GPRS_RX_RET_PARSE_FAILED;
		}

		cjson_len = (u16)(pos - (UART_RX2_BUF + 2) + 1);
		if (cjson_len >= sizeof(cjsonStr))
		{
			printf("json payload too long\r\n");
			return GPRS_RX_RET_PARSE_FAILED;
		}
		memcpy(cjsonStr, UART_RX2_BUF + 2, cjson_len);
		cjsonStr[cjson_len] = '\0';
		root = cJSON_Parse(cjsonStr);
		if (root == NULL)
		{
			printf("cJSON_Parse failed\r\n");
			return GPRS_RX_RET_PARSE_FAILED;
		}
		validate_result = validate_server_reply_json(root);
		if (validate_result != 0)
		{
			printf("s->c back JSON validation failed with error code: %d\n", validate_result);
			cJSON_Delete(root);
			return validate_result;
		}
		// json检查完整性完成，继续
		// 如果是版本请求回复
		/*
		1,{"type":"cmd","back":"true","cliendid":"74000000004","CmdName":"version","para":{"version":"v1.0.5"},"random":"200001010000801000"}
		{"type":"back","back":"false","Cliendid":"74000000004","CmdName":"version","para":{"version":"v1.0.5","size":49476,"upgrade":0},"random":"201001018400801000"}
		*/
		if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "version") == 0) // 设备需要更新的信息
		{
			txsta = cjson_upgrade(root);
			// JSON错误
			if (txsta == 1)
			{
				// ota_step3();
				progressbar_Init();
				printf("version: Firmware upgrade started, requesting first data block\r\n");
				rx_ret = GPRS_RX_RET_START_UPDATE; // 开始更新
			}
			else if (txsta == 0)
			{
				// ota_step1();
				printf("version: Version is same or upgrade not needed\r\n");
				// ProgressBar_Update(100);
				// delay_ms(100);
				rx_ret = GPRS_RX_RET_NO_UPDATE; // 无需下载，直接跳转
			}
			else
			{
				rx_ret = txsta; // 透传错误码
			}
		}
		// 如果是下载数据回复
		else if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "download") == 0) // 下载
		{
			message_len = (u16)(u2rxcnt - (u16)((pos + 1) - UART_RX2_BUF));
			txsta = cjson_download_reply(root, pos + 1, message_len);
			if (txsta == 1)
			{
				printf("download: Firmware download completed, ready to restart\r\n");
				rx_ret = GPRS_RX_RET_DOWNLOAD_DONE; // 整体下载完成，准备重启
			}
			else if (txsta == 0)
			{
				printf("download: Data block written successfully, continue downloading next block\r\n");
				rx_ret = GPRS_RX_RET_DOWNLOAD_CONTINUE; // 单一包数据传输成功
			}
			else
			{
				rx_ret = txsta; // 透传错误码
			}
		}
		cJSON_Delete(root);
		return rx_ret;
	}

	//-------------------------CJSON 命令 ------------------------------------
	// 固件升级中，只有客户端主动发送请求，服务器才会回应数据这一种情况，不存在服务器主动发送数据给客户端的情况。

	//------服务器 -> 客户端----------
	// 固件文件信息，及是否允许升级
	u8 cjson_upgrade(cJSON * root)
	{
		cJSON *para, *size, *ver, *upgrade;
		u8 gradeflag = 0;
		int result;
		size_t ver_len;

		// 先验证格式，确保para字段存在且正确  -1~-10
		//	result = validate_para_json_object1(root);
		//	if (result < 0)
		//	{
		//		printf("s->c back JSON validation failed with error code <para>: %d\n", result);
		//		return result;
		//	}

		para = cJSON_GetObjectItem(root, "para");
		size = cJSON_GetObjectItem(para, "size");
		ver = cJSON_GetObjectItem(para, "version");
		upgrade = cJSON_GetObjectItem(para, "upgrade");

		// if ((cJSON_IsString(ver) && (ver->valuestring != NULL)) && (cJSON_IsNumber(size)) && (cJSON_IsNumber(upgrade)))
		// {
		// gradeflag = cJSON_GetNumberValue(upgrade);

		// 判断是否需要升级
		if (cJSON_GetNumberValue(upgrade) && (strcmp((char *)OTA_Info.OTA_ver, ver->valuestring) != 0))
		{
			OtaParas.size = cJSON_GetNumberValue(size);
			memset(OtaParas.ver, 0, 32);
			ver_len = strlen(ver->valuestring);
			strncpy(OtaParas.ver, ver->valuestring, (ver_len < 31) ? ver_len : 31);
			OtaParas.ver[31] = '\0'; // 确保字符串结束

			if ((OtaParas.size % 512) != 0)
			{
				OtaParas.count = OtaParas.size / 512 + 1;
				OtaParas.remaining = OtaParas.size % 512; // 最后一帧字节数
			}
			else
			{
				OtaParas.count = OtaParas.size / 512;
				OtaParas.remaining = 512;
			}

			printf("upgrade is start --  OtaParas.size = %d, %d\r\n", OtaParas.size, OtaParas.count);
			// 开始更新
			// ota_step1();
			process = OtaParas.count;
			ProgressBar_Update(0);
			delay_ms(100); // 延迟100ms

			cjson_pub_getbin(0);
			// 需要升级
			return 1;
		}
		// 无需升级
		return 0;
	}
	// 应答设备端请求的bin文件数据
	u8 cjson_download_reply(cJSON * root, char *message, u16 message_len)
	{
		cJSON *para, *bOffset, *bsize;
		char *pos;
		u16 Offset = 0, Size = 0;
		u16 crc = 0;
		int result;
		size_t ota_ver_len;

		u16 i, j;

		u16 orig_size;
		// 先验证格式，确保para字段存在且正确 -11~-18
		//	result = validate_para_json_object2(root);
		//	if (result < 0)//error type -1~-6
		//	{
		//		printf("s->c back JSON validation failed with error code <para>: %d\n", result);
		//		return result;
		//	}
		// 获得参数
		para = cJSON_GetObjectItem(root, "para");
		bOffset = cJSON_GetObjectItem(para, "offset");
		bsize = cJSON_GetObjectItem(para, "size");

		// 转换数据类型
		Offset = cJSON_GetNumberValue(bOffset);
		Size = cJSON_GetNumberValue(bsize);
		if ((u32)Size + 2 > message_len)
		{
			printf("download packet too short: need %u, have %u\r\n", (u16)(Size + 2), message_len);
			return GPRS_RX_ERR_PACKET_SHORT;
		}

		printf("Offset = %d,size = %d\r\n", Offset, Size);
		//		if(Offset < (OtaParas.count-1))
		//			cjson_pub_getbin(Offset+1);
		//
		//		return 0;
		// 提取校验码
		crc = CRC16_CCITT_FALSE((u8 *)&message[0], Size);
		// printf("crc = --%x , %x , %x , %x , %x , %x\r\n", crc, message[0], message[1], message[511], message[512], message[513]);
		//  crc校验
		if (((crc >> 8) & 0xff) != message[Size + 1] || (crc & 0xff) != message[Size]) // 校验失败，重新请求
		{
			// 每次产生错误请求，先清零错误标志
			if (s_dl_last_offset != Offset)
			{
				s_dl_crc_retry_cnt = 0;
				s_dl_last_offset = Offset;
			}
			else
			{
				s_dl_crc_retry_cnt++;
			}
			if (s_dl_crc_retry_cnt < 5)
				cjson_pub_getbin(Offset);
			else
				s_dl_crc_retry_cnt = 5;
			printf("download data error %d\n", s_dl_crc_retry_cnt);
			return GPRS_RX_ERR_CRC_RETRY_EXCEEDED;
		}

		orig_size = Size; // 保存原始字节数用于打印
		if ((Size % 2) != 0)
			Size = Size / 2 + 1;
		else
			Size = Size / 2;

		// 写flash A2区
		printf("write A2 zone-- %d --\r\n", Offset);
		s_dl_addr += Size * 2;
		// printf("read A2 addr-- %d --%d\r\n", s_dl_addr,s_dl_lastaddr);
		// test_mod
		STMFLASH_Write(s_dl_lastaddr, (u16 *)&message[0], Size);
		s_dl_lastaddr = s_dl_addr;
		// 打印message数组
		/*	printf("message array: ");
			for (i = 0; i < orig_size + 2; i++) {
				printf("%02x ", (u8)message[i]);
			}
			printf("\r\n");*/

		if (Offset < (OtaParas.count - 1)) // 写满字节数512
		{
			cjson_pub_getbin(Offset + 1);
			ProgressBar_Update(Offset * 100 / process);
			delay_ms(200); // 延迟100ms
			printf("request next data --\r\n");
		}
		else if (Offset == (OtaParas.count - 1)) // 最后一帧
		{
			// 存储信息
			// 更新版本号、标志位、文件大小
			gprs_download_context_reset();

			memset(&OTA_Info.OTA_ver, 0, 32);
			ota_ver_len = strlen(OtaParas.ver);
			strncpy((char *)OTA_Info.OTA_ver, OtaParas.ver, (ota_ver_len < 31) ? ota_ver_len : 31);
			OTA_Info.OTA_ver[31] = '\0';	   // 确保字符串结束
			OTA_Info.OTA_size = OtaParas.size; // 总字节 例如47600
			printf("OTA_size = %d \r\n ", OTA_Info.OTA_size);
			// test_mod
			OTA_Info.OTA_flag = OTA_A2_FLAG;

			if (AT24CXX_Write(OTA_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE))
			{
				printf("write ota info to eeprom error --\r\n");
				return GPRS_RX_ERR_EEPROM_WRITE;
			}

			printf("update is ok --\r\n");
			ProgressBar_Update(100);
			// delay_ms(100);
			// NVIC_SystemReset(); // 设备重启
			return 1;
		}

		return 0;
	}

	//------ 客户端 -> 服务器----------
	// 版本号上报
	void cjson_pub_version(void)
	{
		const char *cid = (clientid[0] != '\0') ? clientid : "74000000004";
		memset(CParas.type, 0, bType);
		memset(CParas.back, 0, bBack);
		memset(CParas.CmdName, 0, bCmdName);
		memset(CParas.random, 0, bRandom);
		snprintf(CParas.type, bType, "%s", "cmd");
		snprintf(CParas.back, bBack, "%s", "true");
		snprintf(CParas.clientid, bClientid, "%s", cid);
		snprintf(CParas.CmdName, bCmdName, "%s", "version");
		generate_random_string(CParas.random);
		// printf("c->s --begin\r\n");
		gprs_send_datas(SVERSION, CParas);
		// printf("c->s --end\r\n");
		pub_version_flag = 0;
	}
	// 请求bin文件块序号
	void cjson_pub_getbin(u16 offset)
	{ // TODO 设备号的设定逻辑不完整。
		const char *cid = (clientid[0] != '\0') ? clientid : "74000000004";
		memset(CParas.type, 0, bType);
		memset(CParas.back, 0, bBack);
		snprintf(CParas.clientid, bClientid, "%s", cid);
		memset(CParas.CmdName, 0, bCmdName);
		memset(CParas.random, 0, bRandom);
		snprintf(CParas.type, bType, "%s", "cmd");
		snprintf(CParas.back, bBack, "%s", "true");
		snprintf(CParas.CmdName, bCmdName, "%s", "download");
		generate_random_string(CParas.random);
		CParas.offset = offset;
		gprs_send_datas(UPGRADE, CParas);
	}

	// 工具函数，判断json合法性
	//---------------------------------
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
	 * -26: cliendid字段不存在
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
	int validate_server_reply_json(cJSON * root)
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
		if (strcmp(item->valuestring, "back") != 0)
		{
			printf("Error: type field value is not 'back', got: %s\n", item->valuestring);
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
		if (strcmp(item->valuestring, "version") != 0 &&
			strcmp(item->valuestring, "download") != 0)
		{
			printf("Error: CmdName field value is not 'version' or 'download', got: %s\n",
				   item->valuestring);
			return -29;
		}

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
		if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "version") == 0)
		{
			// version命令：para必须包含version, size, upgrade字段
			cJSON *ver = cJSON_GetObjectItem(item, "version");
			cJSON *size = cJSON_GetObjectItem(item, "size");
			cJSON *upgrade = cJSON_GetObjectItem(item, "upgrade");

			if (ver == NULL || !cJSON_IsString(ver) || ver->valuestring == NULL)
			{
				printf("Error: para.version field not found or not a string\n");
				return -32;
			}
			if (size == NULL || !cJSON_IsNumber(size))
			{
				printf("Error: para.size field not found or not a number\n");
				return -33;
			}
			// 检查size字段是否是整数（不是浮点数）
			size_value = cJSON_GetNumberValue(size);
			if (size_value != (int)size_value)
			{
				printf("Error: para.size field is not an integer (got: %f)\n", size_value);
				return -34;
			}
			if (upgrade == NULL || !cJSON_IsNumber(upgrade))
			{
				printf("Error: para.upgrade field not found or not a number\n");
				return -35;
			}
			// 检查upgrade字段是否是整数（不是浮点数）
			upgrade_value = cJSON_GetNumberValue(upgrade);
			if (upgrade_value != (int)upgrade_value)
			{
				printf("Error: para.upgrade field is not an integer (got: %f)\n", upgrade_value);
				return -36;
			}
		}
		else if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "download") == 0)
		{
			// download命令：para必须包含offset, size字段
			cJSON *offset = cJSON_GetObjectItem(item, "offset");
			cJSON *size = cJSON_GetObjectItem(item, "size");

			if (offset == NULL || !cJSON_IsNumber(offset))
			{
				printf("Error: para.offset field not found or not a number\n");
				return -37;
			}
			// 检查offset字段是否是整数（不是浮点数）
			offset_value = cJSON_GetNumberValue(offset);
			if (offset_value != (int)offset_value)
			{
				printf("Error: para.offset field is not an integer (got: %f)\n", offset_value);
				return -38;
			}
			if (size == NULL || !cJSON_IsNumber(size))
			{
				printf("Error: para.size field not found or not a number\n");
				return -39;
			}
			// 检查size字段是否是整数（不是浮点数）
			size_value = cJSON_GetNumberValue(size);
			if (size_value != (int)size_value)
			{
				printf("Error: para.size field is not an integer (got: %f)\n", size_value);
				return -40;
			}
		}

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

	/**
	 * @brief 使用括号匹配算法找到完整JSON的结束位置（简化版）
	 * @param start JSON开始位置（指向'{'）
	 * @param max_len 最大搜索长度
	 * @return JSON结束位置的指针（指向'}'），失败返回NULL
	 *
	 * 注意：此函数仅根据 '{' 和 '}' 的深度匹配，不处理字符串内的括号
	 * 适用于字符串中只包含一个JSON对象的情况
	 *
	 * 示例：
	 *   char *json_start = strstr(buffer, "{");
	 *   char *json_end = find_json_end_by_bracket_matching(json_start, 512);
	 *   if (json_end != NULL) {
	 *       int json_len = json_end - json_start + 1;  // 包含结束的'}'
	 *   }
	 */
	char *find_json_end_by_bracket_matching(char *start, u16 max_len)
	{
		int depth = 0; // 括号深度
		char *p = start;
		char *end = start + max_len;

		// 检查起始字符是否为 '{'
		if (start == NULL || *p != '{')
		{
			return NULL; // 不是JSON对象开始
		}

		// 从第一个 '{' 开始计数
		depth = 1;
		p++; // 跳过第一个 '{'

		// 遍历字符串，只根据 '{' 和 '}' 的深度匹配
		while (p < end && *p != '\0')
		{
			if (*p == '{')
			{
				depth++; // 遇到 '{' 深度加1
			}
			else if (*p == '}')
			{
				depth--; // 遇到 '}' 深度减1
				if (depth == 0)
				{
					return p; // 深度归零，返回指向'}'的指针
				}
			}
			p++;
		}
		return NULL; // 未找到完整的JSON（深度未归零或超出范围）
	}

	/**
	 * @brief 检查字符串中是否包含指定的子字符串
	 * @param str 要搜索的字符串
	 * @param substr 要查找的子字符串
	 * @return 1-找到, 0-未找到
	 */
	u8 str_contains(const char *str, const char *substr)
	{
		if (str == NULL || substr == NULL)
		{
			return 0; // 参数无效
		}

		if (strstr(str, substr) != NULL)
		{
			return 1; // 找到子字符串
		}

		return 0; // 未找到
	}

	/**
	 * @brief 检查字符串中是否包含 "WH-LTE-7S1"
	 * @param str 要搜索的字符串
	 * @return 1-包含, 0-不包含
	 */
	u8 str_contains_wh_lte_7s1(const char *str)
	{
		return str_contains(str, "WH-LTE-7S1");
	}

	///**
	// * @brief 检查para字段中的JSON对象格式
	// * @param root cJSON根对象
	// * @return 0-符合条件, 负数-错误代码
	// *
	// * 注意：para字段本身是JSON对象，不是字符串
	// *
	// * 错误代码定义：
	// * -1: para字段不存在
	// * -2: para字段不是JSON对象类型
	// * -3: version字段不存在或不是字符串
	// * -4: size字段不存在或不是整数
	// * -5: upgrade字段不存在或不是整数
	// */
	// int validate_para_json_object1(cJSON *root)
	//{
	//	cJSON *para_obj;
	//	cJSON *version_item;
	//	cJSON *size_item;
	//	cJSON *upgrade_item;
	//	double size_value;
	//	double upgrade_value;

	//	// 1. 检查para字段是否存在
	//	para_obj = cJSON_GetObjectItem(root, "para");
	//	if (para_obj == NULL)
	//	{
	//		printf("Error: version para field not found\n");
	//		return -1;
	//	}

	//	// 2. 检查para字段是否是JSON对象类型
	//	if (!cJSON_IsObject(para_obj))
	//	{
	//		printf("Error: version para field is not a JSON object\n");
	//		return -2;
	//	}

	//	// 3. 检查version字段（必须是字符串）
	//	version_item = cJSON_GetObjectItem(para_obj, "version");
	//	if (version_item == NULL)
	//	{
	//		printf("Error: version para.version field not found\n");
	//		return -3;
	//	}
	//	if (!cJSON_IsString(version_item) || version_item->valuestring == NULL)
	//	{
	//		printf("Error: version para.version field is not a string\n");
	//		return -4;
	//	}

	//	// 4. 检查size字段（必须是整数）
	//	size_item = cJSON_GetObjectItem(para_obj, "size");
	//	if (size_item == NULL)
	//	{
	//		printf("Error: version para.size field not found\n");
	//		return -5;
	//	}
	//	if (!cJSON_IsNumber(size_item))
	//	{
	//		printf("Error: version para.size field is not a number\n");
	//		return -6;
	//	}
	//	// 检查是否是整数（不是浮点数）
	//	size_value = cJSON_GetNumberValue(size_item);
	//	if (size_value != (int)size_value)
	//	{
	//		printf("Error: version para.size field is not an integer (got: %f)\n", size_value);
	//		return -7;
	//	}

	//	// 5. 检查upgrade字段（必须是整数）
	//	upgrade_item = cJSON_GetObjectItem(para_obj, "upgrade");
	//	if (upgrade_item == NULL)
	//	{
	//		printf("Error: version para.upgrade field not found\n");
	//		return -8;
	//	}
	//	if (!cJSON_IsNumber(upgrade_item))
	//	{
	//		printf("Error: version para.upgrade field is not a number\n");
	//		return -9;
	//	}
	//	// 检查是否是整数（不是浮点数）
	//	upgrade_value = cJSON_GetNumberValue(upgrade_item);
	//	if (upgrade_value != (int)upgrade_value)
	//	{
	//		printf("Error: version para.upgrade field is not an integer (got: %f)\n", upgrade_value);
	//		return -10;
	//	}

	//	// 所有检查通过
	//	return 0;
	//}

	///**
	// * @brief 校验para字段是JSON对象，且size和offset是整数
	// * @param root cJSON根对象
	// * @return 0-校验通过, 其他值-错误代码
	// *
	// * 错误代码定义：
	// * -1: para字段不存在
	// * -2: para字段不是JSON对象
	// * -3: para.size字段不存在或不是数字
	// * -4: para.size字段不是整数
	// * -5: para.offset字段不存在或不是数字
	// * -6: para.offset字段不是整数
	// */
	// int validate_para_json_object2(cJSON *root)
	//{
	//	cJSON *para_item;
	//	cJSON *size_item;
	//	cJSON *offset_item;
	//	double size_value;
	//	double offset_value;

	//	// 1. 检查para字段是否存在
	//	para_item = cJSON_GetObjectItem(root, "para");
	//	if (para_item == NULL)
	//	{
	//		printf("Error: download para field not found\n");
	//		return -11;
	//	}

	//	// 2. 检查para字段是否是JSON对象
	//	if (!cJSON_IsObject(para_item))
	//	{
	//		char *para_str = cJSON_Print(para_item);
	//		if (para_str != NULL)
	//		{
	//			printf("Error: download para field is not a JSON object, para_item: %s\n", para_str);
	//			free(para_str);  // 释放cJSON_Print分配的内存
	//		}
	//		else
	//		{
	//			printf("Error: download para field is not a JSON object\n");
	//		}
	//		return -12;
	//	}

	//	// 3. 检查size字段是否存在且是数字
	//	size_item = cJSON_GetObjectItem(para_item, "size");
	//	if (size_item == NULL)
	//	{
	//		printf("Error: download para.size field not found\n");
	//		return -13;
	//	}
	//	if (!cJSON_IsNumber(size_item))
	//	{
	//		printf("Error: download para.size field is not a number\n");
	//		return -14;
	//	}

	//	// 4. 检查size字段是否是整数
	//	size_value = cJSON_GetNumberValue(size_item);
	//	if (size_value != (int)size_value)
	//	{
	//		printf("Error: download para.size field is not an integer, got: %f\n", size_value);
	//		return -15;
	//	}

	//	// 5. 检查offset字段是否存在且是数字
	//	offset_item = cJSON_GetObjectItem(para_item, "offset");
	//	if (offset_item == NULL)
	//	{
	//		printf("Error: download para.offset field not found\n");
	//		return -16;
	//	}
	//	if (!cJSON_IsNumber(offset_item))
	//	{
	//		printf("Error: download para.offset field is not a number\n");
	//		return -17;
	//	}

	//	// 6. 检查offset字段是否是整数
	//	offset_value = cJSON_GetNumberValue(offset_item);
	//	if (offset_value != (int)offset_value)
	//	{
	//		printf("Error: download para.offset field is not an integer, got: %f\n", offset_value);
	//		return -18;
	//	}

	//	// 所有检查通过
	//	return 0;
	//}