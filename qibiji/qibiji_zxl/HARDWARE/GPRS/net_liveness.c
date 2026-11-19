#include "net_liveness.h"
#include "gprs.h"
#include "gprs_4g.h"
#include "gprs_at.h"
#include "uart_queue.h"
#include <string.h>
#include "timer.h"

#define NET_LIVENESS_FRAME          "2,{}"
#define NET_LIVENESS_FRAME_LEN      4u
#define NET_LIVENESS_TX_TIMEOUT_MS  300u

static uint8_t  s_pending = 0u;       /* heartbeat sent, awaiting platform ack */
static uint8_t  s_ever_sent = 0u;
static uint8_t  s_miss = 0u;          /* consecutive un-acked heartbeats */
static uint32_t s_last_send_ms = 0u;
static uint32_t s_last_ack_ms = 0u;
static uint8_t  s_ever_acked = 0u;
static uint8_t  s_wait_timesync = 1u; /* hold first heartbeat until timeSync ok */
static uint8_t  s_wait_logged = 0u;
static uint32_t s_wait_start_ms = 0u;
static uint32_t s_wait_tick_ms = 0u;
static uint8_t  s_ts_fail = 0u;
static uint8_t  s_ts_fast = 1u;
static uint8_t  s_ts_throttle_logged = 0u;

uint8_t NetLiveness_IsAck(cJSON *root)
{
    if (root == NULL || !cJSON_IsObject(root))
        return 0u;
    return (cJSON_GetArraySize(root) == 0) ? 1u : 0u;
}

void NetLiveness_OnAck(void)
{
    if (!s_pending)
        return;
    s_pending = 0u;
    s_miss = 0u;
    s_last_ack_ms = g_sys_ms;
    s_ever_acked = 1u;
    GPRS_Printf("[LIVE] heartbeat ack ok (miss=%u)\r\n", (unsigned)s_miss);
}

void NetLiveness_OnTimeSyncOk(void)
{
    if (!s_wait_timesync)
        return;
    s_wait_timesync = 0u; /* 清 0 之后，下一次`NetLiveness_Process()` 才会开始计时、发`2,{}` 。 */
    s_wait_logged = 0u;
    s_ts_fail = 0u;
    s_ts_fast = 1u;
    s_ts_throttle_logged = 0u;
    GPRS_Printf("[LIVE] timeSync ok -> first heartbeat released\r\n");
}

uint8_t NetLiveness_IsWaitingTimeSync(void) { return s_wait_timesync; }
uint8_t NetLiveness_IsTimeSyncFast(void) { return (s_wait_timesync && s_ts_fast) ? 1u : 0u; }

void NetLiveness_OnTimeSyncFail(void)
{
    if (s_wait_timesync && s_ts_fail < 0xFFu)
        s_ts_fail++;
}

uint8_t NetLiveness_IsServerConfirmed(void)
{
    if (!s_ever_acked)
        return 0u;
    return ((uint32_t)(g_sys_ms - s_last_ack_ms) < (2UL * NET_LIVENESS_INTERVAL_MS)) ? 1u : 0u;
}

void NetLiveness_ResetState(void)
{
    s_pending = 0u;
    s_ever_sent = 0u;
    s_wait_timesync = 1u;   /* re-arm the timesync gate on every re-connect */
    s_wait_logged = 0u;
    s_ts_fast = 1u;
    s_ts_throttle_logged = 0u;
    /* s_miss deliberately kept -- clearing it causes reset storms */
}

static void s_send(void)
{
    if (!UartTx_Push_JSON(NET_LIVENESS_FRAME, NET_LIVENESS_FRAME_LEN, "", NULL,
                          NET_LIVENESS_TX_TIMEOUT_MS, 0u))
    {
        GPRS_Printf("[LIVE] tx queue full, heartbeat skipped\r\n");
        return;
    }
    s_pending = 1u;
}

/**
 * @brief Send one liveness ping "2,{}" immediately, bypassing the 120 s period.
 * Diagnostic helper: verifies the downlink quickly after entering NORMAL.
 * It also arms the normal pending state, so a returning "{}" is reported as
 * "[LIVE] heartbeat ack ok" through NetLiveness_OnAck().
 */
void GPRS_SendLivenessPing(void)
{
    if (!UartTx_Push_JSON(NET_LIVENESS_FRAME, NET_LIVENESS_FRAME_LEN, "", NULL,
                          NET_LIVENESS_TX_TIMEOUT_MS, 0u))
    {
        GPRS_Printf("[LIVE] forced ping dropped: queue full\r\n");
        return;
    }
    s_pending     = 1u;
    s_ever_sent   = 1u;
    s_last_send_ms = g_sys_ms;
    GPRS_Printf("[LIVE] forced ping sent: 2,{}\r\n");
}

static void s_tick(void)
{
    /* first-heartbeat gate: no ack of timeSync -> no heartbeat, no miss counting */
    if (s_wait_timesync)
    {
        if (!s_wait_logged)
        {
            s_wait_logged = 1u;
            s_wait_start_ms = g_sys_ms;
            s_wait_tick_ms = g_sys_ms;
            GPRS_Printf("[LIVE] first heartbeat held: waiting sys.timeSync ok\r\n");
        }
        else if ((uint32_t)(g_sys_ms - s_wait_tick_ms) >= 60000UL)
        {
            s_wait_tick_ms = g_sys_ms;
            GPRS_Printf("[LIVE] still waiting timeSync ok (%us)\r\n",
                        (unsigned)((g_sys_ms - s_wait_start_ms) / 1000UL));
        }
        /* waiting-phase self-heal: timesync stuck -> hard reset module */
        if (s_ts_fail >= NET_LIVENESS_TSFAIL_THRESHOLD)
        {
            if (GPRS_4G_ResetAllowed(NET_LIVENESS_L1_MIN_INTERVAL_MS))
            {
                s_ts_fail = 0u; s_ts_fast = 1u; s_ts_throttle_logged = 0u;
                UartTx_Queue_Init();
                s_pending = 0u;
                GPRS_4G_Reset();
                GPRS_Printf("[LIVE] timeSync stuck -> hard reset 4G\r\n");
            }
            else
            {
                s_ts_fast = 0u;   /* fall back to 60s probe cadence */
                if (!s_ts_throttle_logged)
                {
                    s_ts_throttle_logged = 1u;
                    GPRS_Printf("[LIVE] timeSync stuck but reset throttled\r\n");
                }
            }
        }
        return;
    }

    if (s_ever_sent && (uint32_t)(g_sys_ms - s_last_send_ms) < NET_LIVENESS_INTERVAL_MS)
        return;

    if (s_pending)
    {
        s_pending = 0u;
        if (s_miss < 0xFFu)
            s_miss++;
        GPRS_Printf("[LIVE] heartbeat not acked, miss=%u\r\n", (unsigned)s_miss);
    }
    s_ever_sent = 1u;
    s_last_send_ms = g_sys_ms;
    s_send();
}

static void s_recover(void)
{
    if (s_miss < NET_LIVENESS_MISS_THRESHOLD)
        return;
    if (!GPRS_4G_ResetAllowed(NET_LIVENESS_L1_MIN_INTERVAL_MS))
        return;
    s_miss = 0u;
    UartTx_Queue_Init();
    s_pending = 0u;
    GPRS_4G_Reset();
    GPRS_Printf("[LIVE] recovery: hard reset 4G module\r\n");
}

void NetLiveness_Process(void)
{
    if (!gprs_is_ready())
    {
        NetLiveness_ResetState();
        return;
    }
    s_tick();
    s_recover();
}