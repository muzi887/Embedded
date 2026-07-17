#ifndef __LIVE_DATA_R_H
#define __LIVE_DATA_R_H
#include "sys.h"
#include "cJSON.h"
#include "usart.h"
#include "eeprom.h"
#include "blackList.h"
#include "Live_data.h"
#include "iic.h"
#include "DS3231.h"
#include "usart2.h"
#include "delay.h"
#include "data_handler.h"


extern int caseNumber;
/* 根据旧版 cJSON库没有提供 cJSON_IsString，所以自己定义了一个 */
#ifndef cJSON_IsString
/* cJSON 类型的低8位是基础类型，高位可能带标志位，所以用 & 0xFF 屏蔽 */
#define cJSON_IsString(item)  ((item) && (((item)->type & 0xFF) == cJSON_String))
#endif

extern DS3231_Time currentTime; // 存放时间结构体
extern char *json1;
extern long long messageId111;
//extern long long ts1;
extern long long qr_code_time;
extern char result1[RS485_REC_LEN * 2 + 1];
extern char g_device_publicKey[PUBLIC_KEY_LEN+1];
/////////////////////1/////////////////////////
extern long long messageId;
extern const char *requestId;
extern long long requestTimestamp;
extern const char *code;
extern const char *msg;
extern const char *result;

/////////////////////2/////////////////////////
extern char *key2_1;
extern char *key2_2;
extern char *key2_3;
extern char *key2_4;
extern char *dachuanzi;
extern long long value;
extern char *action;
/////////////////////3/////////////////////////
extern char *key3_1;
extern uint8_t value3;
 /////////////////////4/////////////////////////
extern char *requestId_4r;  /////注意requestId和返回requestId不同
extern long long requestTimestamp_4;
extern char *code_4;
extern char *msg_4;
extern char* items[];
extern int items_count;
/////////////////////5/////////////////////////
extern char *requestId_5r;  /////注意requestId和返回requestId不同
extern long long messageId_5;
extern long long requestTimestamp_5;
extern char *code_5;
extern char *msg_5;		
/////////////////////6/////////////////////////
extern char *requestId_6r;  /////注意requestId和返回requestId不同
extern long long messageId_6;
extern long long requestTimestamp_6;
extern char *code_6;
extern char *msg_6;		
/////////////////////7/////////////////////////
extern char *requestId_7r;  /////注意requestId和返回requestId不同
extern long long messageId_7;
extern long long requestTimestamp_7;
extern char *code_7;
extern char *msg_7;




int parse_rs485_json(cJSON* root);
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
