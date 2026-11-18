#ifndef __GPRS_JSON_H
#define __GPRS_JSON_H

#include "gprs.h"

/* ---------------------------------------------------------------------------
 * GPRS 上行 JSON 构造与发送 —— 回复 / 主动上报 / 心跳
 *
 * 职责：GPRS_ReplyServices() / GPRS_ReplyServicesShort() 构造服务回执；
 *       GPRS_Publish() 通用属性发布；
 *       GPRS_SendLivenessPing() 心跳 ping。
 *       内部统一走 UartTx_Push_JSON() 发往队列。
 * --------------------------------------------------------------------------- */

void GPRS_JSON_ParseProcess(void);

/* service_reply: code=0 success; output is a JSON object string like "{\"open\":true}" */
void GPRS_ReplyServices(int code, const char *message, const char *output);
/* short-lived variant used by ReSet/stopGate */
void GPRS_ReplyServicesPush(int code, const char *message, const char *output, uint16_t timeout_ms);

/* command handlers */
void GPRS_SetValue(int value);
void GPRS_StopGate(void);

#endif