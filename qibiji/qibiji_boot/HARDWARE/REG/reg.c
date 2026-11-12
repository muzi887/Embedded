#include "reg.h"
#include "delay.h"
#include "24cxx.h"
#include "boot.h"
#include "display.h"   /* OTA_ERR_* 错误类型（液晶提示用） */
#include <stdio.h>
#include <string.h>

/* MQTT 身份参数：初值取兜底值，EEPROM 槽位有效时由 MQTT_ReadKeysFromEeprom() 覆盖 */
char g_mqtt_productKey[MQTT_KEY_STR_LEN] = MQTT_PRODUCTKEY_DEFAULT;
char g_mqtt_deviceKey[MQTT_KEY_STR_LEN] = MQTT_DEVICEKEY_DEFAULT;

/* 槽位首字节必须是可打印 ASCII：0x00（空）与 0xFF（未写）都视为无效 */
static u8 MqttKeyUsable(const char *buf)
{
	u8 c = (u8)buf[0];
	return ((c >= 0x20u) && (c < 0x7Fu)) ? 1u : 0u;
}

/* 从 24C02 读取 productKey / deviceKey（与 A1 应用同地址、同长度）。
   平台会校验「消息体 productKey/deviceKey == 主题里的 productKey/deviceKey」，
   不一致直接判失败并且不回包（IotAccessProcessor.ValidateEnvelope）。
   所以 B 区发出的 putVersion/getFirmware/otaError 必须带 A1 应用同一套身份，
   否则现场表现就是"putVersion 发出后一直 request timeout"。 */
void MQTT_ReadKeysFromEeprom(void)
{
	char pk[MQTT_KEY_STR_LEN];
	char dk[MQTT_KEY_STR_LEN];

	memset(pk, 0, sizeof(pk));
	memset(dk, 0, sizeof(dk));
	AT24CXX_Read(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)pk, MQTT_KEY_EEPROM_LEN);
	AT24CXX_Read(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)dk, MQTT_KEY_EEPROM_LEN);
	pk[MQTT_KEY_EEPROM_LEN] = '\0';
	dk[MQTT_KEY_EEPROM_LEN] = '\0';

	if (MqttKeyUsable(pk))
		snprintf(g_mqtt_productKey, sizeof(g_mqtt_productKey), "%s", pk);
	if (MqttKeyUsable(dk))
		snprintf(g_mqtt_deviceKey, sizeof(g_mqtt_deviceKey), "%s", dk);

	printf("mqtt id: pk=%s, dk=%s\r\n", g_mqtt_productKey, g_mqtt_deviceKey);
}

void AT24CXX_ReadOTAInfo(void)
{
	memset(&OTA_Info,0,OTA_INFOCB_SIZE);  //清空OTA_Info结构体清零
	if(AT24CXX_Check()) 
	{
		/* 24C02 自检/读写失败（芯片或 I2C 总线异常）：升级状态无法落盘，
		   下载好的固件也没法靠 OTA_flag 引导，所以这里显式关闭联网 OTA，
		   让设备直接运行 A1。原来只 memset 成 0 属于「静默失效」，
		   现场看不出原因；版本串也与 paraToEeprom() 的 "v0.0.0" 不一致。 */
		OTA_Info.OTA_flag    = OTA_A1_FLAG;
		OTA_Info.OTA_size    = 0;
		OTA_Info.UPDATE_flag = 0;
		g_ota_lcd_err  = OTA_ERR_EEPROM;   /* 让 main 在 LCD 就绪后提示“存储异常” */
		g_ota_lcd_hold = 0;                /* 持久性错误：只显示不等待 */
		sprintf(OTA_Info.OTA_ver,"v0.0.0");
		printf("24C02 check fail: OTA disabled, run A1 (OTA_ver = %s)\n",OTA_Info.OTA_ver);
	}
	else//成功，读取24C02数据
	{                         
		AT24CXX_Read(OTA_ADDR,(uint8_t *)&OTA_Info,OTA_INFOCB_SIZE);        //从24C02读取数据，存储到OTA_Info结构体
		printf("OTA_ver = %s,%x,%d\n",OTA_Info.OTA_ver,OTA_Info.OTA_flag,OTA_Info.UPDATE_flag);
		MQTT_ReadKeysFromEeprom();   /* 同时取出 platform 需要的 productKey / deviceKey */
	}
}

//写入默认值
void paraToEeprom(void)
{
	 memset(&OTA_Info,0,OTA_INFOCB_SIZE);
     OTA_Info.OTA_flag = OTA_A1_FLAG;					// 标志位默认值，表示当前A区是最新的
	 OTA_Info.UPDATE_flag=1;							// 0 无需ota ，1 需要联网ota
	 sprintf(OTA_Info.OTA_ver,"v0.0.0");
	 printf("set default para\r\n");
	 printf("OTA_ver = %s,%x\n",OTA_Info.OTA_ver,OTA_Info.OTA_flag);
	 AT24CXX_Write(OTA_ADDR,(uint8_t *)&OTA_Info,OTA_INFOCB_SIZE);        //写入标志位
}
