#include "gprs_json.h"
#include "gprs.h"
#include "gprs_config.h"
#include "uart_queue.h"
#include "dcmotor.h"
#include "adc.h"
#include <string.h>
#include <stdio.h>

static char s_rx_frame[UART_RX_ITEM_MAX];
static char s_json_buf[UART_TX_JSON_LEN];

/* ---- build & push a service_reply envelope ---- */
static void s_reply_push(int code, const char *message, const char *output, uint16_t timeout_ms)
{
  char json_str[UART_TX_JSON_LEN];
  const char *msg = (message && message[0]) ? message : "success";
  const char *out = (output && output[0]) ? output : "{}";
  const char *reply_id = g_json_hdr.id[0] ? g_json_hdr.id : "";

  (void)snprintf(json_str, sizeof(json_str),
        "%u,{"
        "\"id\":\"%s\","
        "\"productKey\":\"%s\","
        "\"deviceKey\":\"%s\","
        "\"type\":\"service_reply\","
        "\"body\":{"
        "\"serviceCode\":\"%s\","
        "\"code\":%d,"
        "\"message\":\"%s\","
        "\"output\":%s"
        "}"
        "}",
        TOPIC_SERVICE_REPLY,
        reply_id,
        GPRS_PRODUCT_KEY,
        GPRS_DEVICE_KEY,
        g_json_service.serviceCode,
        code, msg, out);

  GPRS_Printf("replyServices-->%s\r\n", json_str);
  (void)UartTx_Push_JSON(json_str, (uint16_t)strlen(json_str), reply_id, NULL, timeout_ms, 0u);
}

void GPRS_ReplyServicesPush(int code, const char *message, const char *output, uint16_t timeout_ms)
{
  s_reply_push(code, message, output, timeout_ms);
}
void GPRS_ReplyServices(int code, const char *message, const char *output)
{
  s_reply_push(code, message, output, 0u);
}

/* ---- command: setValue ---- */
void GPRS_SetValue(int value)
{
  uint16_t target;
  uint16_t current;

  /* NEW: range check -- original code silently truncated to u8 (value 300 -> 44) */
  if ((value < 0) || (value > 100))
  {
      GPRS_ReplyServices(400, "value out of range [0..100]", "{}");
      return;
  }
  target = (uint16_t)value;
  current = DCMOTOR_GetOpening();          /* was: armReg.gate_kaidu */

  /* NEW: refuse while calibrating -- original replied success but did nothing */
  if (DCMOTOR_IsCalibrating())
  {
      GPRS_ReplyServices(409, "calibrating, command rejected", "{}");
      return;
  }

  /*
    * Stage-2 semantics: one-shot jog request toward the target.
    * Original wrote armReg.range_target and sys_moter_state=MOTOR_UP/DOWN; the
    * new DCMOTOR has no "target + auto-approach" layer. For a true "drive to
    * target" behaviour, add DCMOTOR_SetTarget() later (see section 3.1 plan B)
    * and replace these two lines.
    */
  if (target > current)
      DCMOTOR_Request(MOTOR_UP);
  else if (target < current)
      DCMOTOR_Request(MOTOR_DOWN);

    GPRS_ReplyServices(0, "success", "{}");
}

/* ---- command: stopGate ---- */
void GPRS_StopGate(void)
{
    DCMOTOR_Request(MOTOR_STOP);
    GPRS_ReplyServicesPush(0, "success", "{\"success\": true}", GPRS_RESET_REPLY_TIMEOUT_MS);
}

/* ---- parse & dispatch ---- */
void GPRS_JSON_ParseProcess(void)
{
  uint16_t len;
  char *json_start;
  uint16_t json_len;
  cJSON *root;
  const char *err;
  cJSON *type, *body, *serviceCode, *params, *val;

  while (!UartRx_IsEmpty())
  {
    if (!UartRx_Pop(s_rx_frame, sizeof(s_rx_frame), &len))
        break;

    /* extract the {...} substring; if none, treat frame as an AT reply */
    if (extract_json_from_string(s_rx_frame, len, &json_start, &json_len) != 0 ||
        json_len == 0u)
    {
        (void)UartTx_OnRecv(s_rx_frame, len, 1u);   /* AT reply */
        continue;
    }
    memcpy(s_json_buf, json_start, json_len);
    s_json_buf[json_len] = '\0';

    root = cJSON_Parse(s_json_buf);
    if (root == NULL)
        continue;

    if (NetLiveness_IsAck(root))
    {
        NetLiveness_OnAck();
        g_json_hdr.id[0] = '\0';   /* ack has no id: clear stale id */
        cJSON_Delete(root);
        continue;
    }

    err = GPRS_ValidateServerJson(root);
    if (err != NULL)
    {
        GPRS_Printf("s->c JSON validation failed: %s\r\n", err);
        cJSON_Delete(root);
        continue;                 /* continue, not return: don't stall other frames */
    }

    /* copy envelope fields out BEFORE deleting the tree */
    memset(&g_json_hdr, 0, sizeof(g_json_hdr));
    {
        cJSON *it = cJSON_GetObjectItem(root, "id");
        if (it && cJSON_IsString(it) && it->valuestring)
            strncpy(g_json_hdr.id, it->valuestring, sizeof(g_json_hdr.id) - 1u);
        it = cJSON_GetObjectItem(root, "type");
        if (it && cJSON_IsString(it) && it->valuestring)
            strncpy(g_json_hdr.type, it->valuestring, sizeof(g_json_hdr.type) - 1u);
    }

    type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "service") == 0)
    {
        GPRS_Printf("s->c JSON service\r\n");
        body = cJSON_GetObjectItem(root, "body");

        memset(&g_json_service, 0, sizeof(g_json_service));
        {
            cJSON *it = body ? cJSON_GetObjectItem(body, "serviceCode") : NULL;
            if (it && cJSON_IsString(it) && it->valuestring)
                strncpy(g_json_service.serviceCode, it->valuestring,
                        sizeof(g_json_service.serviceCode) - 1u);
        }
        serviceCode = body ? cJSON_GetObjectItem(body, "serviceCode") : NULL;

        if (serviceCode && cJSON_IsString(serviceCode) && serviceCode->valuestring)
        {
            if (strcmp(serviceCode->valuestring, "setValue") == 0)
            {
                params = cJSON_GetObjectItem(body, "params");
                val = params ? cJSON_GetObjectItem(params, "Value") : NULL;
                if (cJSON_IsNumber(val))
                    GPRS_SetValue((int)cJSON_GetNumberValue(val));
            }
            else if (strcmp(serviceCode->valuestring, "stopGate") == 0)
            {
                GPRS_StopGate();
            }
            else if (strcmp(serviceCode->valuestring, "ReSet") == 0)
            {
                /* reply first, then defer the reset (see GPRS_Reset_Request) */
                GPRS_ReplyServicesPush(0, "success", "{\"ResetFlag\": true}",
                                        GPRS_RESET_REPLY_TIMEOUT_MS);
                GPRS_Reset_Request();
            }
            /* stage 3/4 add: getFirmware / setUpdateValue / getConfig / property_get */
        }
    }
    /* stage 3 adds: request_reply / property_get / config branches here */

    cJSON_Delete(root);   /* ALWAYS free -- valuestring pointers die here */
  }
}