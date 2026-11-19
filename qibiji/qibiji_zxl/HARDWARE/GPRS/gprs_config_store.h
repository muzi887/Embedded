#ifndef __GPRS_CONFIG_STORE_H
#define __GPRS_CONFIG_STORE_H

#include "gprs_config.h"

/* EEPROM 字节地址（各 32 字节槽） */
#define MQTT_PRODUCTKEY_BYTE_ADDR    64u
#define MQTT_DEVICEKEY_BYTE_ADDR     96u
#define MQTT_DEVICESECRET_BYTE_ADDR  128u
#define MQTT_MQTTADDR_BYTE_ADDR      160u
#define MQTT_KEY_EEPROM_LEN          32u
#define MQTT_KEY_STR_LEN             (MQTT_KEY_EEPROM_LEN + 1u)

/* 从 EEPROM 读凭据到指定缓冲；返回 1=槽有效，0=空白 */
uint8_t gprs_store_read_pk(char *buf);
uint8_t gprs_store_read_dk(char *buf);
uint8_t gprs_store_read_secret(char *buf);
uint8_t gprs_store_read_addr(char *buf);

/* 上电统一调：依次读四个槽，无效槽回退到 gprs_config.h 里的默认宏 */
void gprs_config_load(void);

/* 写入：只写空白槽（首字节非 0/0xFF 则跳过，保护真实凭据不被覆盖） */
void gprs_store_write_str(uint16_t addr, const char *s);

/* RAM 中的凭据全局变量（在 gprs_config_store.c 定义，消费端直接引用） */
extern char g_mqtt_productKey[MQTT_KEY_STR_LEN];
extern char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];
extern char g_mqtt_deviceSecret[MQTT_KEY_STR_LEN];
extern char g_mqtt_addr[MQTT_KEY_STR_LEN];

#endif