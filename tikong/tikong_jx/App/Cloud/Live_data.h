#ifndef __LIVE_DATA_H
#define __LIVE_DATA_H

#include "sys.h"
#include "cJSON.h"
#include "usart1.h"   
#include "usart3.h"
#include "eeprom.h"
#include "rtc.h"    

void GetNetTime(void);

uint8_t First_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp);
uint8_t Four_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp);


uint8_t Fifth_Reply(long long rxmessageId,const char *rxrequestId,long long rxrequestTimestamp);
uint8_t Sixth_Reply(long long rxmessageId,const char *rxrequestId,long long rxrequestTimestamp);
uint8_t Seventh_Reply(long long rxmessageId,const char *rxrequestId,long long rxrequestTimestamp);


uint8_t Second_Send(char *dachuanzi);
uint8_t Third_Send(void);

//uint8_t First_Reply(long long messageId, const char *requestId, long long requestTimestamp, 
//                   const char *code, const char *msg, const char *result);
//uint8_t Second_Send(char *dachuanzi);
//uint8_t Third_Send(void);
//uint8_t Four_Reply(long long messageId, const char *requestId_4r, long long requestTimestamp_4, 
////                  const char *code_4, const char *msg_4, 
//                  char *items[], int item_count);
//uint8_t Fifth_Reply(long long messageId,const char *requestId,long long requestTimestamp);
//uint8_t Sixth_Reply(long long messageId,const char *requestId,long long requestTimestamp);
//uint8_t Seventh_Reply(long long messageId,const char *requestId,long long requestTimestamp);
uint8_t call_elevator_Reply(const char *messageId, const char *requestId, const char *requestTimestamp);
uint8_t call_open_door_Reply(const char *messageId, const char *requestId, const char *requestTimestamp);
uint8_t call_readsetting_Reply(const char *messageId, const char *requestId, const char *requestTimestamp);
extern int baud_rate_data;
extern int data_length_1;
extern int data_start_length_1;
extern int data_stop_length_1;
extern char verify_1[2];
extern char data_1[128];
extern char deviceKey_1[32];
extern char key_1[32];
extern char productKey_1[32];
extern char requestId_1[8];
extern long long requestTimestamp_1;
 /////////////////////////2.reply//////////////
extern char code_1[8];           // 状态码，如"200"
extern char msg_1[64];           // 消息内容，如"操作成功"  
extern char messageId_1[32];     // 消息ID，如"1755139316330"
 /////////////////////////3.提取黑名单//////////////
extern char deviceKey_3[32];          // 设备密钥
extern char key_3[32];                // 请求类型  
extern char productKey_3[32];         // 产品密钥
extern char requestId_3[16];          // 请求ID
extern long long requestTimestamp_3;  // 时间戳
 /////////////////////////4.增加黑名单//////////////
extern char deviceKey_4[32];          // 设备密钥
extern char key_4[32];                // 请求类型  
extern char productKey_4[32];         // 产品密钥
extern char requestId_4[16];          // 请求ID
extern long long requestTimestamp_4b;  // 时间戳
extern char qrCodeIds_4[500][11];       // 二维码ID数组
extern int qrCodeIds_count_4;       // 二维码ID数量
/////////////////////////5.减少黑名单//////////////
extern char deviceKey_5[32];          // 设备密钥
extern char key_5[32];                // 请求类型  
extern char productKey_5[32];         // 产品密钥
extern char requestId_5[16];          // 请求ID
extern long long requestTimestamp_5b;  // 时间戳
extern char qrCodeIds_5[30][11];       // 二维码ID数组
extern int qrCodeIds_count_5;     // 二维码ID数量
/////////////////////////6.更新公钥//////////////
extern char deviceKey_6[32];          // 设备密钥
extern char key_6[32];                // 请求类型  
extern char productKey_6[32];         // 产品密钥
extern char requestId_6[16];          // 请求ID
extern long long requestTimestamp_6b;  // 时间戳
extern char publicKey_6[512];         // 公钥

typedef struct CmdResult
{
	int code;
	const char *msg;
	const char *error;
} CmdResult;

extern CmdResult g_result;
void qr_Reply(const char *messageId, const char *requestTimestamp, CmdResult result,const char *re_data);
void card_Reply(const char *messageId, const char *requestTimestamp, CmdResult result,const char *re_data,const char *card_Number);
void pwd_Reply(const char *messageId, const char *requestTimestamp, int passwordData ,CmdResult result);
#endif

