#include "property_report.h"
#include "gprs_at.h"     
#include "dcmotor.h"
#include "adc.h"
#include <stdio.h>
#include <string.h>
#include "uart_queue.h"
#include "timer.h"
#include "net_liveness.h"   
#include "gprs_rtc.h"

#define PROP_FORCE_DEBOUNCE_MS 300u

static uint8_t  s_pending = 0u;
static uint16_t s_last_opening = 0xFFFFu;
static uint32_t s_last_change_tick = 0u;
static uint32_t s_last_force_tick = 0u;
static uint32_t s_min_interval_ms = PROP_REPORT_MIN_INTERVAL_MS;

/* build the body field string (no outer braces) */
uint8_t PropertyReport_BuildSnapshot(char *buf, uint32_t buf_size)
{
    int n;
    n = snprintf(buf, buf_size,
                 "\"Opening\":%u,\"MotorCurrent\":%.3f",
                 (unsigned)DCMOTOR_GetOpening(),
                 ADC_GetCurrentMa() / 1000.0f);
    return (n > 0 && (uint32_t)n < buf_size) ? 1u : 0u;
}

uint8_t Send_property(const char *para_buf)
{
    char json_str[UART_TX_JSON_LEN];
    char id_buf[24];
    uint32_t sec;
    uint16_t ms;

    if (para_buf == NULL)
        return 0u;

    /* 13 位 id = unix 秒(10) + 毫秒(3)，与 timeSync 一致。
       用 %lu/%03u 拼接，避免 64 位返回值和 %lld 带来的兼容问题。 */
    sec = GPRS_RTC_GetUnixSec();
    ms  = GPRS_RTC_GetMsOfSec();
    ms  = GPRS_RTC_GetMsOfSec();
    if (sec == 0u)          /* RTC 未同步：保证 id 非 0、非负 */
        sec = 1u;
    (void)snprintf(id_buf, sizeof(id_buf), "%lu%03u",
                   (unsigned long)sec, (unsigned)ms);

    (void)snprintf(json_str, sizeof(json_str),
             "%u,{\"id\":\"%s\",\"productKey\":\"%s\",\"deviceKey\":\"%s\","
             "\"type\":\"property\",\"body\":{%s}}",
             TOPIC_PROPERTY_PUB,
             id_buf,
             GPRS_PRODUCT_KEY, GPRS_DEVICE_KEY,
             para_buf);
    return UartTx_Push_JSON(json_str, (uint16_t)strlen(json_str), "", NULL,
                            UART_TX_JSON_TIMEOUT_MS, 0u);
}

void PropertyReport_Force(void)
{
    char para[PROP_SNAPSHOT_BUF_SIZE];
    if ((uint32_t)(g_sys_ms - s_last_force_tick) < PROP_FORCE_DEBOUNCE_MS)
        return;
    s_last_force_tick = g_sys_ms;
    if (PropertyReport_BuildSnapshot(para, sizeof(para)))
    {
        (void)Send_property(para);
        s_last_opening = DCMOTOR_GetOpening();
    }
}

void PropertyReport_OnOpeningChange(void)
{
    uint16_t curr = DCMOTOR_GetOpening();
    uint16_t diff;
    if (s_last_opening == 0xFFFFu) { s_last_opening = curr; return; }
    diff = (curr >= s_last_opening) ? (curr - s_last_opening) : (s_last_opening - curr);
    if (diff < PROP_OPENING_THRESHOLD) return;
    if ((uint32_t)(g_sys_ms - s_last_change_tick) < s_min_interval_ms) return;
    {
        char para[PROP_SNAPSHOT_BUF_SIZE];
        if (PropertyReport_BuildSnapshot(para, sizeof(para)))
        {
            (void)Send_property(para);
            s_last_opening = curr;
            s_last_change_tick = g_sys_ms;
        }
    }
}

void PropertyReport_SetMinIntervalMs(uint16_t ms) { s_min_interval_ms = (uint32_t)ms; }

void PropertyReport_SetPending(void) { s_pending = 1u; }

void PropertyReport_Process(void)
{
    if (!gprs_is_ready() || DCMOTOR_IsCalibrating() || NetLiveness_IsWaitingTimeSync())
        return;
    if (!s_pending)
        return;
    s_pending = 0u;
    if (DCMOTOR_GetState() == MOTOR_STOP)
        PropertyReport_Force();
    else
        PropertyReport_OnOpeningChange();
}