#include "cmd.h"
#include "app_run.h"
#include "sha1.h"
#include "time.h"
#include "data_handler.h"
#include "rtc.h"
#include "pass_setting.h"
#include "pass_permission.h"

extern volatile u8 Elevator_AuthCheck_Flag; // 电梯权限检查标志
char *json;									// 存放待加签json
char *valid_data;							// 存放干净的数据
static char hash_str[80];							// 存放sha1结果
// char order_type[8];
// char card_Number[32];

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
int CommControl(char received_data[], const char order_type_meta[8], const char card_number_meta[32], int uart_port)
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
