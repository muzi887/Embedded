#ifndef __GPRS_H
#define __GPRS_H

#include "gprs_config.h"
#include "cJSON.h"

/* ---------------------------------------------------------------------------
 * GPRS 业务层入口 —— JSON 信封 + 身份校验 + 下行解析分发
 *
 * 职责：ValidateServerJson() 检查业务信封四要素；
 *       GPRS_JSON_ParseProcess() 解析下行帧、识别心跳 ack、
 *       按 type/serviceCode 分发到各 service handler。
 *
 * 备注：本文件只定义"收到了什么怎么分"，不定义"收到后做什么"
 *       （handler 实现在各业务 .c 里）。
 * --------------------------------------------------------------------------- */

#define JSON_HDR_ID_LEN          64u
#define JSON_HDR_TYPE_LEN        24u
#define JSON_HDR_SERVICECODE_LEN 16u

/* last legal downlink envelope -- copied out before cJSON tree is freed */
typedef struct
{
  char id[JSON_HDR_ID_LEN];
  char type[JSON_HDR_TYPE_LEN];
} GprsMsgHeader;

typedef struct
{
  char serviceCode[JSON_HDR_SERVICECODE_LEN];
} GprsMsgService;

extern GprsMsgHeader  g_json_hdr;
extern GprsMsgService g_json_service;

/* seven-step pipeline entry -- call once per main loop */
void GPRS_Process(void);

/* reset deferred until service_reply has been sent */
#define GPRS_RESET_REPLY_TIMEOUT_MS 300u
#define GPRS_RESET_MIN_DELAY_MS     1000u
#define GPRS_RESET_MAX_WAIT_MS      6000u
void GPRS_Reset_Request(void);
void GPRS_Reset_Process(void);

/* internal steps (exposed for stage 3/4 hooks) */
void GPRS_RxDatas(void);
void GPRS_JSON_ParseProcess(void);

/* server JSON envelope validation: NULL = OK, otherwise error reason */
const char *GPRS_ValidateServerJson(cJSON *root);

#endif