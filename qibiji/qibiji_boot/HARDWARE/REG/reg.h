#ifndef __REG_H
#define __REG_H
#include "sys.h"
 
#define eepromLength	10

/* ---- MQTT 身份参数：24C02 布局与 A1 应用 reg.h 完全一致 ---- */
#define MQTT_PRODUCTKEY_BYTE_ADDR	64
#define MQTT_DEVICEKEY_BYTE_ADDR	96
#define MQTT_KEY_EEPROM_LEN			32						/* 每项槽位字节数 */
#define MQTT_KEY_STR_LEN			(MQTT_KEY_EEPROM_LEN + 1)	/* 32 字符 + '\0' */
#define MQTT_PRODUCTKEY_DEFAULT		"pk001"					/* 槽位空/未写时的兜底值 */
#define MQTT_DEVICEKEY_DEFAULT		"dev001"

extern char g_mqtt_productKey[MQTT_KEY_STR_LEN];
extern char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];

void AT24CXX_ReadOTAInfo(void);
void MQTT_ReadKeysFromEeprom(void);
void paraToEeprom(void);
#endif
