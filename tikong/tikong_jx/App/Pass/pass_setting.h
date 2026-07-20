#ifndef PASS_SETTING_H
#define PASS_SETTING_H

#include "rtc.h"
#include "eeprom.h"
#include <stdio.h>

extern char g_mqtt_addr[PUBLIC_MQTT_ADDR_LEN + 1];
extern char g_mqtt_productKey[PUBLIC_MQTT_PRODUCTKEY_LEN + 1];
extern char g_mqtt_deviceKey[PUBLIC_MQTT_DEVICEKEY_LEN + 1];
extern char g_mqtt_deviceSecret[PUBLIC_MQTT_DEVICESECRET_LEN + 1];

/** EEPROM 常开限位截止时间串（14 位 YYYYMMDDhhmmss + NUL） */
extern char g_floors_limit_time[PUBLIC_FLOORS_LIMIT_TIME_LEN + 1];

int Cmd_Setting(char received_data[]);
void Cmd_Setting_SnapshotForEeprom(void);
void Cmd_Setting_WriteKeysToEeprom(void);
void Cmd_Setting_WriteKeysToEeprom_sim(void);
void Cmd_Setting_OnAtSequenceDone(void);
void Cmd_Setting_OnAtSequenceAbort(void);
int Cmd_Set_Time(char received_data[]);
int Cmd_Set_OpenLimit(char received_data[]);
void copy_field_snap(char *dst, size_t dst_sz, const char *src);
int qr_hex_char_to_nibble(char c);
void qr_split_s_received_by_comma(const char *src);
int parseYMDHMS(const char *s, RTC_Time *t);

#endif
