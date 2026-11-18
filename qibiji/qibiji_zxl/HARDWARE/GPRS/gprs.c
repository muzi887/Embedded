#include "gprs.h"
#include "gprs_port.h"
#include "gprs_at.h"
#include "uart_queue.h"
#include <string.h>
#include "timer.h"

GprsMsgHeader  g_json_hdr;
GprsMsgService g_json_service;

static volatile uint8_t s_reset_pending = 0u;
static uint32_t         s_reset_req_tick = 0u;

const char *GPRS_ValidateServerJson(cJSON *root)
{
  cJSON *id, *type, *pk, *dk;
  if (root == NULL)
      return "null root";
  if (!cJSON_IsObject(root))
      return "not object";
  id = cJSON_GetObjectItem(root, "id");
  type = cJSON_GetObjectItem(root, "type");
  pk = cJSON_GetObjectItem(root, "productKey");
  dk = cJSON_GetObjectItem(root, "deviceKey");
  if (!cJSON_IsString(id) || !cJSON_IsString(type))
      return "missing id/type";
  if (!cJSON_IsString(pk) || !cJSON_IsString(dk))
      return "missing productKey/deviceKey";
  /* identity check can be added here when persistent config is available */
  return NULL;
}

void GPRS_RxDatas(void)
{
  /* stage-1 pump already fills rx queue; banner handling lives there.
      Hook kept here so stage 3 can add network-liveness bookkeeping. */
}

void GPRS_Reset_Request(void)
{
    s_reset_pending = 1u;
    s_reset_req_tick = g_sys_ms;
}

void GPRS_Reset_Process(void)
{
  uint32_t elapsed;
  if (!s_reset_pending)
      return;
  elapsed = (uint32_t)(g_sys_ms - s_reset_req_tick);
  /* wait until service_reply is actually dequeued (queue empty), with 6s fallback */
  if (!((elapsed >= GPRS_RESET_MIN_DELAY_MS) &&
        (UartTx_IsEmpty() || elapsed >= GPRS_RESET_MAX_WAIT_MS)))
      return;
  /* IWDG_Feed();  -- add if your new project enables the watchdog */
  NVIC_SystemReset();
}

void GPRS_Process(void)
{
  GPRS_RxDatas();
  GPRS_JSON_ParseProcess();
  GPRS_AT_StateMachine();
  UartTx_Queue_Process(g_sys_ms);
  GPRS_Reset_Process();
  /* stage 3 adds: liveness, clock sync, property report here */
}