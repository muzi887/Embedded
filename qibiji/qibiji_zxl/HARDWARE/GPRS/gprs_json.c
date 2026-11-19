#include "gprs_json.h"
#include "gprs.h"
#include "gprs_config.h"
#include "uart_queue.h"
#include "dcmotor.h"
#include "adc.h"
#include <string.h>
#include <stdio.h>
#include "timer.h"
#include "gprs_rtc.h"
#include "net_liveness.h"
#include "uart_queue.h"

#define GPRS_CLOCK_MAX_ATTEMPTS     3u
#define GPRS_CLOCK_TX_TIMEOUT_MS    3000u
#define GPRS_CLOCK_WARMUP_MS        2000u    /* let the module finish its session */
#define GPRS_CLOCK_PROBE_MS         60000u   /* probe cadence while downlink unseen */
#define GPRS_CLOCK_PROBE_MAX        5u       /* probes before feeding the watchdog */
static const uint32_t s_clock_wait_ms[GPRS_CLOCK_MAX_ATTEMPTS] = {15000u, 30000u, 60000u};

static uint8_t  s_clock_pending;          /* 1 = a request is in flight */
static uint8_t  s_clock_attempt_idx;      /* 0..2 -> 15/30/60 s */
static uint8_t  s_clock_chan_ok;          /* 1 = at least one downlink seen */
static uint8_t  s_clock_warmup_pending;   /* 1 = waiting out the post-NORMAL warm-up */
static uint8_t  s_clock_probe_cnt;        /* probes issued while channel unconfirmed */
static uint32_t s_clock_attempt_tick;     /* when the in-flight request was issued */
static uint32_t s_clock_warmup_tick;      /* when NORMAL was entered */
static char     s_clock_pending_id[24];   /* in-flight id, kept for matching */

static char s_rx_frame[UART_RX_ITEM_MAX];
static char s_json_buf[UART_TX_JSON_LEN];

static void GPRS_ClockSync_OnReply(cJSON *root);

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

    /* range check -- original code silently truncated to u8 (value 300 -> 44) */
    if ((value < 0) || (value > 100))
    {
        GPRS_ReplyServices(400, "value out of range [0..100]", "{}");
        return;
    }
    target = (uint16_t)value;
    current = DCMOTOR_GetOpening();          /* was: armReg.gate_kaidu */

    /* refuse while calibrating -- original replied success but did nothing */
    if (DCMOTOR_IsCalibrating())
    {
        GPRS_ReplyServices(409, "calibrating, command rejected", "{}");
        return;
    }

    if (DCMOTOR_SetTarget((uint16_t)value) == 0u)
    {
        GPRS_ReplyServices(409, "not calibrated, target rejected", "{}");
        return;
    }

    /* motion is asynchronous: reply now, opening progress goes out as
       property reports driven by DCMOTOR_Tick() -> PropertyReport_SetPending() */
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

    GPRS_ClockSync_OnDownlink();     /* any valid downlink proves the channel */

    if (NetLiveness_IsAck(root))
    {
        NetLiveness_OnAck();
        g_json_hdr.id[0] = '\0';   /* ack has no id: clear stale id */
        cJSON_Delete(root);
        continue;
    }

    type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "request_reply") == 0)
    {
        GPRS_ClockSync_OnReply(root);
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

static void GPRS_ClockSync_IssueAttempt(void)
{
    char json[UART_TX_JSON_LEN];
    char id_buf[24];
    int64_t ts = GPRS_RTC_NowMs();

    snprintf(id_buf, sizeof(id_buf), "%lld", (long long)ts);

    snprintf(json, sizeof(json),
             "%u,{"
             "\"id\":\"%s\","
             "\"productKey\":\"%s\","
             "\"deviceKey\":\"%s\","
             "\"type\":\"request\","
             "\"body\":{\"requestCode\":\"sys.timeSync\"}"
             "}",
             (unsigned)TOPIC_REQUEST_INVOKE, id_buf,
             GPRS_PRODUCT_KEY, GPRS_DEVICE_KEY);

    snprintf(s_clock_pending_id, sizeof(s_clock_pending_id), "%s", id_buf);
    s_clock_attempt_tick = g_sys_ms;
    s_clock_pending = 1u;

    /* 回执由 id 匹配，这里不重发，避免同一 id 被平台去重吃掉 */
    UartTx_Push_JSON(json, (uint16_t)strlen(json), id_buf, NULL,
                     GPRS_CLOCK_TX_TIMEOUT_MS, 0u);
}

void GPRS_ClockSync_Reset(void)
{
    s_clock_pending        = 0u;
    s_clock_attempt_idx    = 0u;
    s_clock_chan_ok        = 0u;    
    s_clock_warmup_pending = 0u;
    s_clock_probe_cnt      = 0u;
    s_clock_pending_id[0]  = '\0';
}

/* 进入 NORMAL 状态时调用的入口函数：重置对时状态并启动 2 秒热身 */
void GPRS_ClockSync_OnNormal(void)  
{
    GPRS_ClockSync_Reset();
    s_clock_warmup_pending = 1u;     /* 2. 打开热身计时标志 */
    s_clock_warmup_tick = g_sys_ms;  /* 3. 记下进入 NORMAL 的时刻 */
    GPRS_Printf("[RTC] warm-up %ums before first timeSync\r\n",
                (unsigned)GPRS_CLOCK_WARMUP_MS);
}

/* 标记下行通道已验证，退出探测模式 */
void GPRS_ClockSync_OnDownlink(void)
{
    if (s_clock_chan_ok != 0u)
        return;

    s_clock_chan_ok = 1u; /* 使`Process()` 从“60 秒一帧探测、不消耗重试次数”切换到“15/30/60 秒正式重试” */
    s_clock_probe_cnt = 0u;
    GPRS_Printf("[RTC] downlink confirmed, normal retry mode\r\n");

    if (s_clock_pending != 0u)
    {
        /* the in-flight probe now starts the normal 15/30/60 clock */
        s_clock_attempt_idx = 0u;
        s_clock_attempt_tick = g_sys_ms;
    }
}

void GPRS_ClockSync_Start(void)
{
    if (s_clock_pending)
        return;
    s_clock_attempt_idx = 0u;
    GPRS_ClockSync_IssueAttempt();
}

/* 针对`sys.timeSync` 回执的处理函数：取出 serverTime 写 DS1302，放行首帧心跳 */
static void GPRS_ClockSync_OnReply(cJSON *root)
{
    cJSON *body   = cJSON_GetObjectItem(root, "body");
    cJSON *code   = body ? cJSON_GetObjectItem(body, "code") : NULL;
    cJSON *output = body ? cJSON_GetObjectItem(body, "output") : NULL;
    cJSON *st     = output ? cJSON_GetObjectItem(output, "serverTime") : NULL;
    double ms;

    if (!cJSON_IsObject(body))
        return;

    s_clock_chan_ok = 1u;      /* a reply is the strongest channel evidence */

    /* F1: business failure must also release the gate, otherwise the first
       heartbeat is locked until the 24h timeout */
    if (cJSON_IsNumber(code) && (int)cJSON_GetNumberValue(code) != 0)
    {
        GPRS_Printf("[RTC] timeSync failed, code=%d\r\n",
                    (int)cJSON_GetNumberValue(code));
        s_clock_pending = 0u;
        NetLiveness_OnTimeSyncFail();
        return;
    }

    /* serverTime is 13 digits: MUST read as double, valueint overflows */
    if (!cJSON_IsNumber(st))
    {
        s_clock_pending = 0u;
        NetLiveness_OnTimeSyncFail();
        return;
    }

    ms = cJSON_GetNumberValue(st);
    if (ms <= 0.0)
    {
        s_clock_pending = 0u;
        NetLiveness_OnTimeSyncFail();
        return;
    }

    GPRS_RTC_Set((uint32_t)(ms / 1000.0)); /* 写 DS1302 */
    s_clock_pending = 0u;
    s_clock_attempt_idx = 0u;

    NetLiveness_OnTimeSyncOk();      /* release first heartbeat 放行首帧心跳*/
    GPRS_Printf("[RTC] timeSync ok\r\n");
}

void GPRS_ClockSync_Process(void)
{
    uint32_t wait_ms;

    /* warm-up after NORMAL: the module may still be attaching / subscribing */
    if (s_clock_warmup_pending != 0u)
    {
        if ((uint32_t)(g_sys_ms - s_clock_warmup_tick) < GPRS_CLOCK_WARMUP_MS)
            return;
        s_clock_warmup_pending = 0u;
        GPRS_ClockSync_Start();
        return;
    }

    if (!s_clock_pending)
        return;

    /* probe mode: nothing has come down on this session yet, so a missing
     *   reply proves nothing -- keep one frame in flight at a 60 s cadence
     *   and do NOT consume the 15/30/60 retry budget. */
    if (s_clock_chan_ok == 0u)
    {
        if ((uint32_t)(g_sys_ms - s_clock_attempt_tick) < GPRS_CLOCK_PROBE_MS)
            return;

        if (s_clock_probe_cnt < 0xFFu)
            s_clock_probe_cnt++;

        if (s_clock_probe_cnt >= GPRS_CLOCK_PROBE_MAX)
        {
            s_clock_probe_cnt = 0u;
            NetLiveness_OnTimeSyncFail();   /* feed the stuck-timeSync watchdog */
        }

        GPRS_Printf("[RTC] channel unconfirmed, re-probe\r\n");
        GPRS_ClockSync_IssueAttempt();
        return;
    }

    /* normal retry: 15 / 30 / 60 s, then give up */
    wait_ms = s_clock_wait_ms[s_clock_attempt_idx];
    if ((uint32_t)(g_sys_ms - s_clock_attempt_tick) < wait_ms)
        return;

    if ((uint8_t)(s_clock_attempt_idx + 1u) >= GPRS_CLOCK_MAX_ATTEMPTS)
    {
        GPRS_Printf("[RTC] timeSync give up after %u attempts\r\n",
                    (unsigned)GPRS_CLOCK_MAX_ATTEMPTS);
        s_clock_pending = 0u;
        s_clock_attempt_idx = 0u;
        NetLiveness_OnTimeSyncFail();
        return;
    }

    s_clock_attempt_idx++;
    GPRS_ClockSync_IssueAttempt();
}