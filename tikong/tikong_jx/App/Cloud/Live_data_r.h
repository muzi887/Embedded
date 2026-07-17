#ifndef __LIVE_DATA_R_H
#define __LIVE_DATA_R_H
#include "sys.h"
#include "cJSON.h"
#include "usart1.h"
#include "eeprom.h"
#include "blackList.h"
#include "Live_data.h"
#include "24cxx.h"
#include "RTC.h"
#include "usart2.h"
#include "delay.h"
#include "data_handler.h"


extern int caseNumber;
/* 根据旧版 cJSON库没有提供 cJSON_IsString，所以自己定义了一个 */
#ifndef cJSON_IsString
/* cJSON 类型的低8位是基础类型，高位可能带标志位，所以用 & 0xFF 屏蔽 */
#define cJSON_IsString(item)  ((item) && (((item)->type & 0xFF) == cJSON_String))
#endif

#define FLOOR_RELAY_NUMBERS_MAX  64

extern int g_floor_relay_numbers[FLOOR_RELAY_NUMBERS_MAX];
extern int g_floor_relay_numbers_count;
extern u8 g_floor_relay_array_has_single_number;
extern int g_floor_relay_single_number_value;

int parse_rs485_json(cJSON* root);
int parse_call_elevator_json(cJSON *root);
int parse_tiqublack_json(cJSON* root);
int parse_addblack_json(cJSON* root);
int parse_delblack_json(cJSON* root);
int parse_upgongyao_json(cJSON* root);
int parse_uptime_json(cJSON *root);

void parseSerialData(const char* data);

// 将十六进制字符转换为数值
uint8_t hexCharToValue(char c);

// 将十六进制字符串转换为字节数组
int hexStringToBytes(const char *hexStr, uint8_t *bytes, int maxBytes);



#endif
