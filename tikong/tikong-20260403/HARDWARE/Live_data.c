#include "Live_data.h"
#include "blackList.h"

char final_json_str[2048];
DS3231_Time currentTime2; // 存放时间结构体
uint16_t length;
char *json_str;
/////////////////////1/////////////////////////
long long messageId;
const char *requestId = "1";
long long requestTimestamp = 1755141246512;
const char *code = "200";
const char *msg = "SUCCESS";

/////////////////////2/////////////////////////
char *key2_1 = "qr_code_report";
char *key2_2 = "qr_code_data";
char *key2_3 = "qr_code_time";
char *key2_4 = "handle_result";
char *dachuanzi = "sagaesihngaspodifjmaowegjaiso;kgawopegj";
long long value = 1755141246516;
char *action = "放行";

char *key3_1 = "signal_strength";
uint8_t value3 = 2;
long long requestTimestamp_4 = 1755141246516;
char *items[] =
    {
        "80be43906e724fd38a58148b5f883df9",
        "21c3ac8b3e4548539710ec04a98b3bfe"};
int items_count;
long long requestTimestamp_5 = 1755141246516;
long long requestTimestamp_6 = 1755141246516;
long long requestTimestamp_7 = 1755141246516;

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

// 密钥更新回执
uint8_t Seventh_Reply(long long rxmessageId, const char *rxrequestId, long long rxrequestTimestamp)
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
  //printf("start GetNetTime\n");
  sprintf(json_str,
          "3,{"
          "\"messageId\":\"1761205012087\","
          "\"requestId\":\"1\""
          "}");
  // 通过USART3发送JSON字符串
  length = strlen(json_str);
  USART3_SendData((uint8_t *)json_str, length);
}
