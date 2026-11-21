#include "event_pub.h"
#include "gprs_at.h"
#include "dcmotor.h"
#include <stdio.h>
#include <string.h>
#include "uart_queue.h"
#include "timer.h"

static const char *s_evt_code(EvtType evt)
{
    switch (evt)
    {
    case EVT_MOTOR_START_UP:   return "motor.start.up";
    case EVT_MOTOR_START_DOWN: return "motor.start.down";
    case EVT_MOTOR_STOP:       return "motor.stop";
    case EVT_LIMIT_OPEN:       return "limit.open";
    case EVT_LIMIT_CLOSE:      return "limit.close";
    case EVT_ALARM:            return "alarm";
    case EVT_REMOTE_REACH:     return "remote.reach";
    default:                   return "unknown";
    }
}

static uint8_t s_post(const char *event_code, const char *para_json)
{
    char json_str[UART_TX_JSON_LEN];
    if (!gprs_is_ready() || DCMOTOR_IsCalibrating())
        return 0u;
    (void)snprintf(json_str, sizeof(json_str),
             "%u,{\"id\":\"%u\",\"productKey\":\"%s\",\"deviceKey\":\"%s\","
             "\"type\":\"event\",\"body\":{\"eventCode\":\"%s\",\"output\":%s}}",
             TOPIC_EVENT_PUB, (unsigned)g_sys_ms, GPRS_PRODUCT_KEY,
             GPRS_DEVICE_KEY, event_code,
             (para_json && para_json[0]) ? para_json : "{}");
    return UartTx_Push_JSON(json_str, (uint16_t)strlen(json_str), "", NULL,
                            UART_TX_JSON_TIMEOUT_MS, 0u);
}

uint8_t EventPostSimple(EvtType evt)
{
    return s_post(s_evt_code(evt), "{}");
}
uint8_t EventPostMotorStop(MotorStopReason reason, uint16_t opening)
{
    char para[64];
    (void)snprintf(para, sizeof(para),
                   "{\"reason\":%d,\"opening\":%u}", (int)reason, (unsigned)opening);
    return s_post(s_evt_code(EVT_MOTOR_STOP), para);
}
uint8_t EventPostMotorStall(uint16_t motor_current_ma)
{
    char para[64];
    (void)snprintf(para, sizeof(para),
                   "{\"code\":\"stall\",\"currentMa\":%u}", (unsigned)motor_current_ma);
    return s_post(s_evt_code(EVT_ALARM), para);
}