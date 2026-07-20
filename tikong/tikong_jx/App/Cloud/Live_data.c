#include "Live_data.h"
#include "blackList.h"
#include <stdio.h>
#include "data_handler.h"
#include "pass_crypto.h"
#include "rtc.h"
#include "app_config.h"

/* qrCodeData / cardData 可容纳完整权限串（≤MAX_RECEIVE_LEN）+ JSON 包头与 handleResult 字段 */
#define QR_REPLY_JSON_CAPACITY (MAX_RECEIVE_LEN + 512)

RTC_Time currentTime2; // 存放时间结构体

CmdResult g_result;

// rs485 透传回执
uint8_t First_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp)
{
  char json_str[512] = {'\0'};
  uint8_t ret = 0;
  u16 length;
  sprintf(json_str,
          "2,{"
          "\"messageId\":%lld,"
          "\"requestId\":\"%s\","
          "\"requestTimestamp\":%lld,"
          "\"body\":{"
          "\"code\":\"200\","
          "\"msg\":\"SUCCESS\","
          "\"data\":{"
          "\"result\":\"240A0ACC010001010F00F40D\""
          "}"
          "}"
          "}",
          rxmessageId, rxrequestId, rxrequestTimestamp);

  //printf("Generated JSON:\n%s\n", json_str);

  // 通过USART3发送JSON字符串
  length = strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);

  return ret;
}

// 提取黑名单回执
uint8_t Four_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp)
{
  char json_str[2900] = {'\0'};
  uint8_t err = 0;
  u16 length, i = 0, j = 0, k = 0;
  char str[2610] = {'\0'};

  j = 0;
  k = 0;
  for (i = 0; i < MAX_ARRAY_SIZE; i++)
  {
    if (dataArray[i].isEmpty == 0x00) // 黑名单
    {
      err = 0;
      for (k = 0; k < 10; k++)
      {
        if (dataArray[i].data[k] == '\0')
        {
          err = 1;
          break;
        }
      }

      if (err == 0)
      {
        str[j++] = '"';
        for (k = 0; k < 10; k++)
        {
          str[j++] = dataArray[i].data[k];
        }
        str[j++] = '"';
        str[j++] = ',';
      }
    }
  }
  if (j >= 1)
    str[j - 1] = '\0';

  sprintf(json_str,
          "2,{"
          "\"messageId\":%lld,"
          "\"requestId\":\"%s\","
          "\"requestTimestamp\":%lld,"
          "\"body\":{"
          "\"code\":\"200\","
          "\"msg\":\"SUCCESS\","
          "\"body\":["
          "%s"
          "]"
          "}"
          "}",
          rxmessageId, rxrequestId, rxrequestTimestamp, str);

  //printf("Generated JSON:\n%s\n", json_str);

  // 通过USART3发送JSON字符串
  length = strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);

  return 0;
}

// 增加黑名单回执
uint8_t Fifth_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp)
{
  char json_str[512] = {'\0'};
  u16 length;

  sprintf(json_str,
          "2,{"
          "\"messageId\":%lld,"
          "\"requestId\":\"%s\","
          "\"requestTimestamp\":%lld,"
          "\"body\":{"
          "\"code\":\"200\","
          "\"msg\":\"SUCCESS\""
          "}"
          "}",
          rxmessageId, rxrequestId, rxrequestTimestamp);

  //printf("Generated JSON:\n%s\n", json_str);

  // 通过USART3发送JSON字符串
  length = strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);

  return 0;
}

// 减少黑名单回执
uint8_t Sixth_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp)
{
  char json_str[512] = {'\0'};
  u16 length;

  sprintf(json_str,
          "2,{"
          "\"messageId\":%lld,"
          "\"requestId\":\"%s\","
          "\"requestTimestamp\":%lld,"
          "\"body\":{"
          "\"code\":\"200\","
          "\"msg\":\"SUCCESS\""
          "}"
          "}",
          rxmessageId, rxrequestId, rxrequestTimestamp);

  //printf("Generated JSON:\n%s\n", json_str);

  // 通过USART3发送JSON字符串
  length = strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);

  return 0;
}

// 获取时间
void GetNetTime(void)
{
  u16 length = 0;
  char json_str[512] = {'\0'};
  printf("start GetNetTime\r\n");
  sprintf(json_str,
          "1,{"
          "\"messageId\":\"1761205012087\","
          "\"requestId\":\"1\""
          "}");
  // 通过USART3发送JSON字符串
  length = strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
}

// 呼梯回执
uint8_t call_elevator_Reply(const char *messageId, const char *requestId, const char *requestTimestamp)
{
  char json_str[512] = {'\0'};
  u16 length;
  /* 缺字段时用 "0"，避免出现 "messageId":,"requestTimestamp":, 等非法 JSON */
  const char *mid = (messageId && messageId[0]) ? messageId : "0";
  const char *rid = (requestId && requestId[0]) ? requestId : "";
  const char *rts = (requestTimestamp && requestTimestamp[0]) ? requestTimestamp : "0";

  (void)snprintf(json_str, sizeof(json_str),
                   "3,{"
                   "\"messageId\":%s,"
                   "\"requestId\":\"%s\","
                   "\"requestTimestamp\":%s,"
                   "\"body\":{"
                   "\"code\":\"200\","
                   "\"msg\":\"SUCCESS\","
                   "\"error\":\"\","
                   "\"errorDescription\":\"\""
                   "}"
                   "}",
                   mid, rid, rts);

  length = (u16)strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
  printf("call_elevator_Reply sent: %s\r\n", json_str);
  return 0;
}
// 开门回执
uint8_t call_open_door_Reply(const char *messageId, const char *requestId, const char *requestTimestamp)
{
  char json_str[512] = {'\0'};
  u16 length;
  /* 缺字段时用 "0"，避免出现 "messageId":,"requestTimestamp":, 等非法 JSON */
  const char *mid = (messageId && messageId[0]) ? messageId : "0";
  const char *rid = (requestId && requestId[0]) ? requestId : "";
  const char *rts = (requestTimestamp && requestTimestamp[0]) ? requestTimestamp : "0";

  (void)snprintf(json_str, sizeof(json_str),
                   "3,{"
                   "\"messageId\":%s,"
                   "\"requestId\":\"%s\","
                   "\"requestTimestamp\":%s,"
                   "\"body\":{"
                   "\"code\":\"200\","
                   "\"msg\":\"SUCCESS\","
                   "\"error\":\"\","
                   "\"errorDescription\":\"\""
                   "}"
                   "}",
                   mid, rid, rts);

  length = (u16)strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
  printf("call_open_door_Reply sent: %s\r\n", json_str);
  return 0;
}
// 读取设备配置回执
uint8_t call_readsetting_Reply(const char *messageId, const char *requestId, const char *requestTimestamp)
{
  char json_str[512] = {'\0'};
  char device_time14[15];
  u16 length;
  /* 缺字段时用 "0"，避免出现 "messageId":,"requestTimestamp":, 等非法 JSON */
  const char *mid = (messageId && messageId[0]) ? messageId : "0";
  const char *rid = (requestId && requestId[0]) ? requestId : "";
  const char *rts = (requestTimestamp && requestTimestamp[0]) ? requestTimestamp : "0";
  // ParseDeviceModeRlyTimes();
  RtcChip_GetTime(&currentTime2);
  MakeTimestamp14(&currentTime2, device_time14);
  (void)snprintf(json_str, sizeof(json_str),
                   "3,{"
                   "\"messageId\":%s,"
                   "\"requestId\":\"%s\","
                   "\"requestTimestamp\":%s,"
                   "\"body\":{"
		              "\"deviceAccount\": \"%s\","
		              "\"devicePassword\": \"%s\","
	              	"\"deviceType\": \"%s\","
	              	"\"deviceCode\": \"%s\","
              		"\"seedPassword\": \"%s\","
	               	"\"deviceTime\": \"%s\","
	               	"\"doorConfig\": [{"
                   "\"relayNumber\": \"0\","
                   "\"defaultEnableTime\": %d},{"
                   "\"relayNumber\": \"1\","
                   "\"defaultEnableTime\": %d}]"
                   "}"
                   "}",
                   mid, rid, rts, g_device_id, g_device_password, g_device_type, g_device_code, g_device_public_Key, device_time14, rly_1_time, rly_2_time);

  length = (u16)strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
  printf("call_readsetting_Reply sent: %s\r\n", json_str);
  return 0;
}
void card_Reply(const char *messageId, const char *requestTimestamp, CmdResult result,const char *re_data,const char *card_Number){
  static char json_str[QR_REPLY_JSON_CAPACITY];
  u16 length;
    RTC_Time now;
  char out14[15];
  const char *emsg = result.msg ? result.msg : "";
  const char *eerror = result.error ? result.error : "";

  RtcChip_GetTime(&now);
  MakeTimestamp14(&now, out14);
  snprintf(json_str, sizeof(json_str),
					 "2,{"
					 "\"messageId\": %s,"
					 "\"body\": [{"
					 "\"key\": \"card_unvarnished_transmission\","
					 "\"ts\": %s,"
					 "\"info\": [{"
					 "\"key\": \"cardData\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"cardTime\","
					 "\"value\": %s"
           "}, {"
					 "\"key\": \"cardNumber\","
					 "\"value\": \"%s\""
					 "}, {"
					  "\"key\": \"handleResult\","	 
            "\"value\": {"
			    	"\"code\": %d,"
			    	"\"msg\": \"%s\","
			    	"\"error\": \"%s\","
				    "\"errorDescription\": \"\"}"  
					 "}]"
					 "}]"
					 "}",
					 messageId, requestTimestamp, re_data, out14, card_Number, result.code, emsg, eerror);
            length = (u16)strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
  printf("card_Reply_Reply sent: %s\r\n", json_str);
}
void pwd_Reply(const char *messageId, const char *requestTimestamp, int passwordData ,CmdResult result){
  char json_str[512] = {'\0'};
  u16 length;
  RTC_Time now;
  char out14[15];

  const char *emsg = result.msg ? result.msg : "";
  const char *eerror = result.error ? result.error : "";
  RtcChip_GetTime(&now);
  MakeTimestamp14(&now, out14);
  snprintf(json_str, sizeof(json_str),
					 "2,{"
					 "\"messageId\": %s,"
					 "\"body\": [{"
					 "\"key\": \"password_unvarnished_transmission\","
					 "\"ts\": %s,"
					 "\"info\": [{"
					 "\"key\": \"passwordData\","
					 "\"value\": %d"
					 "}, {"
					 "\"key\": \"passwordTime\","
					 "\"value\": %s"
					 "}, {"
					  "\"key\": \"handleResult\","	 
            "\"value\": {"
			    	"\"code\": \"%d\","
			    	"\"msg\": \"%s\","
			    	"\"error\": \"%s\","
				    "\"errorDescription\": \"\"}"  
					 "}]"
					 "}]"
					 "}",
					 messageId, requestTimestamp,passwordData,out14, result.code,emsg,eerror);
            length = (u16)strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
  printf("call_pwd_Reply sent: %s\r\n", json_str);
}
void qr_Reply(const char *messageId, const char *requestTimestamp, CmdResult result,const char *re_data)
{
  static char json_str[QR_REPLY_JSON_CAPACITY];
  u16 length;
  RTC_Time now;
  char out14[15];
  const char *emsg = result.msg ? result.msg : "";
  const char *eerror = result.error ? result.error : "";

  RtcChip_GetTime(&now);
  MakeTimestamp14(&now, out14);
  snprintf(json_str, sizeof(json_str),
					 "2,{"
					 "\"messageId\": %s,"
					 "\"body\": [{"
					 "\"key\": \"qr_code_unvarnished_transmission\","
					 "\"ts\": %s,"
					 "\"info\": [{"
					 "\"key\": \"qrCodeData\","
					 "\"value\": \"%s\""
					 "}, {"
					 "\"key\": \"qrCodeTime\","
					 "\"value\": %s"
					 "}, {"
					  "\"key\": \"handleResult\","	 
            "\"value\": {"
			    	"\"code\": %d,"
			    	"\"msg\": \"%s\","
			    	"\"error\": \"%s\","
				    "\"errorDescription\": \"\"}"  
					 "}]"
					 "}]"
					 "}",
					 messageId, requestTimestamp, re_data, out14, result.code, emsg,eerror);
  length = (u16)strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
  		g_result.code = 200;
		g_result.msg = "";
  printf("call_qr_Reply sent: %s\r\n", json_str);
}
