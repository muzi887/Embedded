#include "gprs_at.h"
#include "gprs_config.h"
#include "gprs_4g.h"
#include "gprs_port.h"
#include "uart_queue.h"
#include <string.h>
#include "timer.h"
#include "gprs_json.h"

/*==================== Link state machine states ====================*/
#define ST_OFFLINE      1u  /* wait for LINKA up */               /* wait for LINKA up */
#define ST_AT_PUSH      2u  /* enqueue 3 init AT commands */      /* enqueue 3 init AT commands */
#define ST_AT_WAIT      3u  /* wait for AT replies / retries */   /* wait for AT replies / retries */
#define ST_NORMAL       4u  /* link ready for business JSON */    /* link ready for business JSON */
#define ST_AT_DELAY     5u  /* short retry delay after fail */    /* short retry delay after fail */
#define ST_AT_BACKOFF   6u  /* long backoff after max retries */  /* long backoff after max retries */

/*==================== State machine timing ====================*/
#define AT_MAX_RETRY          3u                      /* AT init rounds before backoff */       /* AT init rounds before backoff */
#define AT_RETRY_MS           10000u                  /* delay between AT rounds */             /* delay between AT rounds */
#define AT_BACKOFF_MS         (30UL * 60UL * 1000UL)  /* 30 min backoff */                      /* 30 min backoff */
#define CSQ_PERIOD_MS         (10UL * 60UL * 1000UL)  /* periodic CSQ period in NORMAL */       /* periodic CSQ period in NORMAL */
#define OFFLINE_PROBE_MS      5000u                   /* 离线时主动探询模块的周期 */
#define LINK_DOWN_DEBOUNCE_MS 2000u                   /* LINKA low this long = down */          /* LINKA low this long = down */
#define LINK_UP_DEBOUNCE_MS   500u                    /* LINKA high this long = up */           /* LINKA high this long = up */
#define OFFLINE_HARD_RESET_MS 60000u                  /* offline this long -> hard reset 4G */  /* offline this long -> hard reset 4G */
#define RESET_MIN_INTERVAL_MS (5UL * 60UL * 1000UL)   /* min gap between hard resets */         /* min gap between hard resets */

/*==================== Public link / AT result flags ====================*/
volatile uint8_t g_atflag_online = 0u;
volatile uint8_t g_at_flag_iccid = 0u;
volatile uint8_t g_at_flag_mqttcid = 0u;
volatile uint8_t g_at_flag_csq = 0u;

/*==================== State machine runtime state ====================*/
static uint8_t  s_state = ST_OFFLINE;
static uint8_t  s_offline_entry = 1u;
static uint8_t  s_link_down_occurred = 0u;
static uint8_t  s_retry_cnt = 0u;
static uint32_t s_fail_tick = 0u;
static uint32_t s_backoff_tick = 0u;
static uint32_t s_csq_tick = 0u;

/*==================== LINKA debounce state ====================*/
static uint8_t  s_link_state = 0u;
static uint8_t  s_link_raw_prev = 0u;
static uint32_t s_link_change_tick = 0u;
static uint32_t s_offline_since = 0u;

/**
 * @brief Record the 4G module boot banner as seen on UART.
 * Called by GPRS_AT_RxPump() when the banner is detected; sets the flag that
 * lets the state machine leave ST_OFFLINE and start AT initialization.
 */
void GPRS_AT_OnBanner(void)
{
    g_atflag_online = 1u;
    GPRS_Printf("[GPRS] module banner seen -> atflag_online=1\r\n");
}

/**
 * @brief Debounce the LINKA (network-status) input and detect a link-down edge.
 * Low must persist LINK_DOWN_DEBOUNCE_MS to confirm down; high must persist
 * LINK_UP_DEBOUNCE_MS to confirm up.
 * @return 1 on a confirmed falling edge (link just went down), 0 otherwise
 */
static uint8_t s_link_debounce(void)
{
    uint8_t raw = GPRS_LINKA ? 1u : 0u;
    if (raw != s_link_raw_prev)
    {
        s_link_raw_prev = raw;
        s_link_change_tick = g_sys_ms;
        return 0u;
    }
    if (raw)
    {
        if (!s_link_state && (uint32_t)(g_sys_ms - s_link_change_tick) >= LINK_UP_DEBOUNCE_MS)
            s_link_state = 1u;
    }
    else
    {
        if (s_link_state && (uint32_t)(g_sys_ms - s_link_change_tick) >= LINK_DOWN_DEBOUNCE_MS)
        {
            s_link_state = 0u;
            return 1u;   /* confirmed falling edge */
        }
    }
    return 0u;
}

/*==================== AT response callbacks ====================*/
/** @brief AT callback: set g_at_flag_iccid when the reply contains "+ICCID". */
static void s_on_iccid(char *data, uint16_t len)
{
    (void)len;
    if (strstr(data, "+ICCID") != NULL)
        g_at_flag_iccid = 1u;
    GPRS_Printf("[GPRS] ICCID: %.*s\r\n", (int)len, data); 
}
/** @brief AT callback: set g_at_flag_mqttcid on "+MQTTCID"; device-id parse goes here later. */
static void s_on_mqttcid(char *data, uint16_t len)
{
    (void)len;
    if (strstr(data, "+MQTTCID") != NULL)
        g_at_flag_mqttcid = 1u;
    /* device id parsing from +MQTTCID can be added here when EEPROM is available */
    GPRS_Printf("[GPRS] MQTTCID: %.*s\r\n", (int)len, data); 
}
/** @brief AT callback: set g_at_flag_csq when the reply contains "+CSQ". */
static void s_on_csq(char *data, uint16_t len)
{
    if (strstr(data, "+CSQ") != NULL)
        g_at_flag_csq = 1u;
    GPRS_Printf("[GPRS] CSQ: %.*s\r\n", (int)len, data);
}

/**
 * @brief 离线探询：LINKA 为低时状态机不会发任何 AT。
 * 第一轮一次性连发 4 条 MQTT 配置查询（趁 banner 到 link up 的约 11 秒窗口），
 * 第二轮再做原来的 ICCID/MQTTCID/CSQ 探测。
 * 内容由 GPRS_AT_RxPump 的 "[GPRS] RX[..]" 原样打印，回调传 NULL 即可。
 */
static void s_offline_probe(void)
{
    static uint8_t  s_probe_idx = 0u;
    static uint32_t s_probe_tick = 0u;

    if ((uint32_t)(g_sys_ms - s_probe_tick) < OFFLINE_PROBE_MS)
        return;
    if (!UartTx_IsEmpty())          /* 队列里还有在途项，下一轮再说 */
        return;

    s_probe_tick = g_sys_ms;
    GPRS_Printf("[GPRS] probe %u: pc4=%u pc5=%u banner=%u\r\n",
                (unsigned)s_probe_idx,
                (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_4),
                (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5),
                (unsigned)g_atflag_online);

    if (s_probe_idx == 0u)
    {
        /* 一轮把配置全查完：队列深度 8，四条一起入队，
           由 TX 队列的"一发一收"机制串行发出 */
        (void)UartTx_Push_AT(GPRS_AT_MQTTSVR, GPRS_AT_MQTTSVR_LEN,
                             "+MQTTSVR", NULL, 1500u, 0u);
        (void)UartTx_Push_AT(GPRS_AT_MQTTMOD, GPRS_AT_MQTTMOD_LEN,
                             "+MQTTMOD", NULL, 1500u, 0u);
        (void)UartTx_Push_AT(GPRS_AT_MQTTSUBTP, GPRS_AT_MQTTSUBTP_LEN,
                             "+MQTTSUBTP", NULL, 1500u, 0u);
        (void)UartTx_Push_AT(GPRS_AT_MQTTPUBTP, GPRS_AT_MQTTPUBTP_LEN,
                             "+MQTTPUBTP", NULL, 1500u, 0u);
    }
    else
    {
        (void)UartTx_Push_AT(GPRS_AT_MQTTCID, GPRS_AT_MQTTCID_LEN,
                             "+MQTTCID", s_on_mqttcid, 2000u, 0u);
        (void)UartTx_Push_AT(GPRS_AT_ICCID, GPRS_AT_ICCID_LEN,
                             "+ICCID", s_on_iccid, 2000u, 0u);
        (void)UartTx_Push_AT(GPRS_AT_CSQ, GPRS_AT_CSQ_LEN,
                             "+CSQ", s_on_csq, 2000u, 0u);
    }

    s_probe_idx = (uint8_t)((s_probe_idx + 1u) % 2u);
}

/**
 * @brief Drive the six-state link / AT-init state machine (call every loop).
 * OFFLINE waits for LINKA up; PUSH enqueues ICCID/MQTTCID/CSQ; WAIT checks the
 * replies with short-retry / long-backoff; NORMAL periodically resends CSQ.
 * A confirmed link-down resets all flags and drops in-flight TX items.
 */
void GPRS_AT_StateMachine(void)
{
    if (s_link_debounce() && (s_state != ST_OFFLINE))
    {
        s_state = ST_OFFLINE;
        s_offline_entry = 1u;
        s_offline_since = g_sys_ms;
        g_atflag_online = 0u;
        s_link_down_occurred = 1u;
        g_at_flag_iccid = 0u;
        g_at_flag_mqttcid = 0u;
        g_at_flag_csq = 0u;
        s_retry_cnt = 0u;
        UartTx_Queue_Init();   /* drop in-flight AT/JSON items */
        GPRS_ClockSync_Reset();  /* drop in-flight timeSync */
        GPRS_Printf("[GPRS] Link down confirmed -> State1\r\n");
    }

    switch (s_state)
    {
    case ST_OFFLINE:
        if (s_offline_entry)
        {
            s_offline_entry = 0u;
            GPRS_Printf("[GPRS] State1: waiting (link=%u atflag=%u down=%u)\r\n",
                        (unsigned)s_link_state, (unsigned)g_atflag_online,
                        (unsigned)s_link_down_occurred);
        }
        s_offline_probe();   /* 不等 LINKA：先把模块的真实回包抓出来 */
        if (s_link_state && (g_atflag_online || s_link_down_occurred))
        {
            s_link_down_occurred = 0u;
            s_state = ST_AT_PUSH;
            GPRS_Printf("[GPRS] State1->2: link up, start AT init\r\n");
            break;
        }
        if (((uint32_t)(g_sys_ms - s_offline_since) >= OFFLINE_HARD_RESET_MS) &&
            GPRS_4G_ResetAllowed(RESET_MIN_INTERVAL_MS))
        {
            UartTx_Queue_Init();
            GPRS_ClockSync_Reset();  /* drop in-flight timeSync */   
            GPRS_4G_Reset();   
            s_offline_since = g_sys_ms;
            g_atflag_online = 0u;
            s_link_down_occurred = 1u;
            g_at_flag_iccid = 0u;
            g_at_flag_mqttcid = 0u;
            g_at_flag_csq = 0u;
            s_retry_cnt = 0u;
            GPRS_Printf("[GPRS] State1: offline long, hard reset 4G\r\n");
        }
        break;

    case ST_AT_PUSH:
        g_at_flag_iccid = 0u;
        g_at_flag_mqttcid = 0u;
        g_at_flag_csq = 0u;
        UartTx_Push_AT(GPRS_AT_ICCID, GPRS_AT_ICCID_LEN, "+ICCID",
                       s_on_iccid, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);
        UartTx_Push_AT(GPRS_AT_MQTTCID, GPRS_AT_MQTTCID_LEN, "+MQTTCID",
                       s_on_mqttcid, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);
        UartTx_Push_AT(GPRS_AT_CSQ, GPRS_AT_CSQ_LEN, "+CSQ",
                       s_on_csq, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);
        s_state = ST_AT_WAIT;
        GPRS_Printf("[GPRS] State2->3: 3 AT queued\r\n");
        break;

    case ST_AT_WAIT:
        if (UartTx_IsEmpty())
        {
            if (g_at_flag_iccid && g_at_flag_mqttcid && g_at_flag_csq)
            {
                s_state = ST_NORMAL;
                s_retry_cnt = 0u;
                GPRS_Printf("[GPRS] State3->4: AT init done, NORMAL\r\n");
                GPRS_ClockSync_OnNormal();   /* issue first timeSync request */
                // GPRS_SendLivenessPing(); 
            }
            else
            {
                s_retry_cnt++;
                if (s_retry_cnt >= AT_MAX_RETRY)
                {
                    s_backoff_tick = g_sys_ms;
                    s_state = ST_AT_BACKOFF;
                    GPRS_Printf("[GPRS] State3->6: max retry, wait 30min\r\n");
                }
                else
                {
                    s_fail_tick = g_sys_ms;
                    s_state = ST_AT_DELAY;
                    GPRS_Printf("[GPRS] State3->5: fail #%u, retry in 10s\r\n",
                                (unsigned)s_retry_cnt);
                }
            }
        }
        break;

    case ST_AT_DELAY:
        if ((uint32_t)(g_sys_ms - s_fail_tick) >= AT_RETRY_MS)
            s_state = ST_AT_PUSH;
        break;

    case ST_AT_BACKOFF:
        if ((uint32_t)(g_sys_ms - s_backoff_tick) >= AT_BACKOFF_MS)
        {
            s_retry_cnt = 0u;
            s_state = ST_AT_PUSH;
        }
        break;

    case ST_NORMAL:
        if (s_csq_tick == 0u)
            s_csq_tick = g_sys_ms;     /* start 10-min period on first NORMAL */
        if ((uint32_t)(g_sys_ms - s_csq_tick) >= CSQ_PERIOD_MS)
        {
            s_csq_tick = g_sys_ms;
            UartTx_Push_AT(GPRS_AT_CSQ, GPRS_AT_CSQ_LEN, "+CSQ",
                           s_on_csq, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);
        }
        break;

    default:
        s_state = ST_OFFLINE;
        break;
    }
}

/** @brief Return 1 when the link is in ST_NORMAL (global TX gate). */
uint8_t gprs_is_ready(void)
{
    return (s_state == ST_NORMAL) ? 1u : 0u;
}

/**
 * @brief Move staged UART frames into the RX queue and detect the boot banner.
 * Takes at most a few frames per call; the take+append is briefly IRQ-guarded
 * so an ISR cannot overwrite a staging slot mid-copy.
 */
void GPRS_AT_RxPump(void)
{
    char *buf;
    uint16_t len;
    uint8_t limit;
    for (limit = 0u; limit < 4u; limit++)
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (!GPRS_PORT_RxStageTake(&buf, &len))
        {
            __set_PRIMASK(primask);
            break;
        }
        UartRx_Append(buf, len);
        __set_PRIMASK(primask);

        /* GPRS_Printf's internal buffer is 256 bytes: a long AT reply such as
           +MQTTPUBTP (428 B) would be cut off. Print in 200-byte chunks. */
        if (len > 200u)
        {
            uint16_t off = 0u;
            GPRS_Printf("[GPRS] RX[%u] +++\r\n", (unsigned)len);
            while (off < len)
            {
                uint16_t n = (uint16_t)(len - off);
                if (n > 200u)
                    n = 200u;
                GPRS_Printf("%.*s", (int)n, buf + off);
                off = (uint16_t)(off + n);
            }
            GPRS_Printf("\r\n--- end ---\r\n");
        }
        else
        {
            GPRS_Printf("[GPRS] RX[%u] %.*s\r\n", (unsigned)len, (int)len, buf);
        }
    }
    UartRx_Extract();

    if (g_gprs_banner_seen)
    {
        g_gprs_banner_seen = 0u;
        GPRS_AT_OnBanner();
    }
}
