# GPRS（4G 上云）模块重构操作建议 · 阶段三

> 原文出自《[GPRS重构操作建议_阶段二起.md](./GPRS重构操作建议_阶段二起.md)》§8，现独立成篇。
> 阶段一→二→三→四有强依赖，必须按顺序实施。

## 目标

加心跳探活（解决"假连接"）、开度变化/停机属性上报、事件上报。这一阶段让设备从"能收命令"变成"能主动汇报 + 能自愈"。

**新增文件**：`net_liveness.h/.c`、`property_report.h/.c`、`event_pub.h/.c`。

## 1. `net_liveness.h/.c` —— 心跳探活 + 假连接自愈

**它解决什么问题**：`gprs_is_ready()` 只证明"模块回 AT 正常"，不证明"平台收得到"。MQTT 会话早断了模块照样回 AT，两端都不认为掉线——这就是"假连接"。心跳每 120s 发一帧极简 `"2,{}"`（4 字节，无信封无 id），平台回一帧 `"{}"`，收到就证明双向通道真的通。

`net_liveness.h`（完整，参数集中在这里）：

```c
#ifndef __NET_LIVENESS_H
#define __NET_LIVENESS_H

#include "gprs_config.h"
#include "cJSON.h"

#define NET_LIVENESS_INTERVAL_MS (120UL * 1000UL)   /* heartbeat period */
#define NET_LIVENESS_MISS_THRESHOLD 3u              /* 3 misses -> recovery */
#define NET_LIVENESS_L1_MIN_INTERVAL_MS (15UL * 60UL * 1000UL)
#define NET_LIVENESS_TSFAIL_THRESHOLD 3u            /* waiting-phase timesync fails */

void NetLiveness_Process(void);
void NetLiveness_OnAck(void);
uint8_t NetLiveness_IsAck(cJSON *root);
uint8_t NetLiveness_IsServerConfirmed(void);
void NetLiveness_OnTimeSyncOk(void);
uint8_t NetLiveness_IsWaitingTimeSync(void);
void NetLiveness_OnTimeSyncFail(void);
void NetLiveness_ResetState(void);

#endif
```

**函数关系速查**：两组内部完全独立的通道，`Process` 是唯一主动发包者，其余都是被动回调或查询：

| 通道 | 方向 | 主驱动 | 包识别 | 成功回调 | 失败回调 | 查询接口 |
|---|---|---|---|---|---|---|
| 心跳保活 | 设备→平台→设备 | `Process` 定时发 ping | `IsAck` 识别 ack 包 | `OnAck` | Process 内部 miss 计数 | `IsServerConfirmed` |
| 时钟同步 | 设备→平台→设备 | `Process` 周期触发 | 由 JSON 分发器按 type 判定 | `OnTimeSyncOk` | `OnTimeSyncFail`（超时） | `IsWaitingTimeSync` |

调用者分工：`main` 主循环 → 只调 `Process`；`gprs_json.c ParseProcess` → 调 `IsAck` 和 `OnTimeSyncOk`；`gprs_at.c` 状态机 → 只读查询，重连时调 `ResetState` 一把清掉所有内部标志。

`net_liveness.c`（机制完整，替换 `sys_ms`→`g_sys_ms`、日志→`GPRS_Printf`）：

```c
#include "net_liveness.h"
#include "gprs.h"
#include "gprs_4g.h"
#include "gprs_at.h"
#include "uart_queue.h"
#include <string.h>

#define NET_LIVENESS_FRAME     "2,{}"
#define NET_LIVENESS_FRAME_LEN 4u
#define NET_LIVENESS_TX_TIMEOUT_MS 300u

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
    s_wait_timesync = 0u;
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
```

**心跳回执要接进 `GPRS_JSON_ParseProcess()`**：在 `gprs_json.c` 的 `GPRS_JSON_ParseProcess()` 里，`cJSON_Parse` 之后、`GPRS_ValidateServerJson()` **之前**，插入：

```c
        if (NetLiveness_IsAck(root))
        {
            NetLiveness_OnAck();
            g_json_hdr.id[0] = '\0';   /* ack has no id: clear stale id */
            cJSON_Delete(root);
            continue;
        }
```

> **顺序不能反**：平台对极简心跳只回 `{}`，它没有 `id`/`productKey`，过不了严格信封校验。所以心跳识别必须在校验之前。这也是原工程的明确设计。

> **首帧门槛为什么存在**：心跳一旦通了 `s_miss` 就清零，会掩盖校时失败。校时失败会让之后所有上报带错时间戳，比"晚几分钟上线"糟得多。所以顺序硬性固定：3 条 AT → 校时 → 心跳。**阶段三单独验证时**，`s_wait_timesync=1` 会挡住心跳，需要在 NORMAL 后临时手动调一次 `NetLiveness_OnTimeSyncOk()` 放行，否则心跳永远不发。阶段四落地后这条门槛由 `GPRS_ClockSync` 自动打开：NORMAL → `GPRS_ClockSync_OnNormal()`（热身 2 s）→ 发 `sys.timeSync` → 收到 `request_reply` 里 `OnReply()` 写完 DS1302 后调 `NetLiveness_OnTimeSyncOk()`，**不需要再手动放行**。

## 2. `property_report.h/.c` —— 属性上报

**职责**：开度变化 ≥2% 或电机停机时，把当前全量属性打成一条 `property` JSON 上报。

数据从新工程 getter 取，替换原工程的 `armReg`：

| JSON 字段 | 原工程取值 | 新工程取值 |
|---|---|---|
| `Opening` | `armReg.gate_kaidu` | `DCMOTOR_GetOpening()` |
| `MotorCurrent`（A） | `armReg.motor_curr / 1000.0f` | `ADC_GetCurrentMa() / 1000.0f` |

`property_report.h`：

```c
#ifndef __PROPERTY_REPORT_H
#define __PROPERTY_REPORT_H

#include "gprs_config.h"

#define PROP_OPENING_THRESHOLD 2u
#define PROP_REPORT_MIN_INTERVAL_MS 1000u
#define PROP_SNAPSHOT_BUF_SIZE 256u

void PropertyReport_SetPending(void);  /* called when opening changes (motor layer) */
void PropertyReport_Process(void);     /* main loop */
void PropertyReport_Force(void);       /* force report, e.g. on motor stop */
uint8_t PropertyReport_BuildSnapshot(char *buf, uint32_t buf_size);
uint8_t Send_property(const char *para_buf);

#endif
```

`property_report.c`：

```c
#include "property_report.h"
#include "gprs_at.h"         /* DCMOTOR_IsCalibrating via dcmotor.h */
#include "dcmotor.h"
#include "adc.h"
#include <stdio.h>
#include <string.h>
#include "uart_queue.h"
#include "gprs_rtc.h"        /* GPRS_RTC_NowMs() for the uplink id */
#include "net_liveness.h"    /* NetLiveness_IsWaitingTimeSync() */

static uint8_t  s_pending = 0u;
static uint16_t s_last_opening = 0xFFFFu;
static uint32_t s_last_change_tick = 0u;
static uint32_t s_last_force_tick = 0u;
#define PROP_FORCE_DEBOUNCE_MS 300u

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
    int64_t ts;

    if (para_buf == NULL)
        return 0u;

    /* same 13-digit ms id as the timeSync request, so every uplink shares one
       id scheme (stage 4 brings the DS1302 backend; before that this is just
       the local RTC value) */
    ts = GPRS_RTC_NowMs();
    (void)snprintf(id_buf, sizeof(id_buf), "%lld", (long long)ts);

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
    if ((uint32_t)(g_sys_ms - s_last_change_tick) < PROP_REPORT_MIN_INTERVAL_MS) return;
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

void PropertyReport_SetPending(void) { s_pending = 1u; }

void PropertyReport_Process(void)
{
    /* gate #1 link down | gate #2 calibrating | gate #3 timeSync not yet ok:
       the first property uplink must wait for sys.timeSync, otherwise the id
       is built from a garbage DS1302 value (observed as a negative id). */
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
```

> **接线**：`PropertyReport_SetPending()` 要在开度变化时被调用——原工程在 `switch.c` 的 `ChangeReality()` 里调。新工程要在 `DCMOTOR` 更新开度的地方（`DCMOTOR_UpdateOpening()` 或 `DCMOTOR_Tick()` 里检测到开度变化处）加一句 `PropertyReport_SetPending()`。**这属于跨模块接线，本轮只在这里点明位置，不铺开改 DCMOTOR 文档**。

> **对时门槛**：`PropertyReport_Process()` 第三个条件 `NetLiveness_IsWaitingTimeSync()` 让首帧属性上报等 `sys.timeSync` 成功后再发。因为 id 由 `GPRS_RTC_NowMs()` 生成，对时完成前 DS1302 是上电垃圾值，曾观测到 `id:"-286297738"` 这种负 id 帧。对时失败/卡住时不会永久阻塞——`net_liveness.c` 会走 hard reset 4G 自愈、重连后重新对时，行为与首帧心跳的对时门槛一致。

## 3. `event_pub.h/.c` —— 事件上报

事件类型与停止原因沿用原工程枚举（阶段三只需电机启停/停机事件的骨架）：

```c
#ifndef __EVENT_PUB_H
#define __EVENT_PUB_H

#include "gprs_config.h"

typedef enum
{
    EVT_MOTOR_START_UP, EVT_MOTOR_START_DOWN, EVT_MOTOR_STOP,
    EVT_LIMIT_OPEN, EVT_LIMIT_CLOSE, EVT_ALARM, EVT_REMOTE_REACH, EVT_MAX
} EvtType;

typedef enum
{
    STOP_REASON_UNKNOWN = 0, STOP_REASON_TARGET_REACH, STOP_REASON_LIMIT_OPEN,
    STOP_REASON_LIMIT_CLOSE, STOP_REASON_MANUAL, STOP_REASON_STALL,
    STOP_REASON_EMERGENCY_STOP
} MotorStopReason;

uint8_t EventPostSimple(EvtType evt);
uint8_t EventPostMotorStop(MotorStopReason reason, uint16_t opening);
uint8_t EventPostMotorStall(uint16_t motor_current_ma);

#endif
```

`event_pub.c`（核心：一个通用发送函数 + 三个入口）：

```c
#include "event_pub.h"
#include "gprs_at.h"
#include "dcmotor.h"
#include <stdio.h>
#include <string.h>

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
```

> **事件触发点**：原工程在 `dc_motor.c` / `switch.c` 的各状态迁移处调 `EventPost*`。新工程要在 `DCMOTOR` 的对应点补调用（电机启动/停机/限位/堵转/到位）。同样属于跨模块接线，**本轮只点明，不铺开**。

### 3.1 事件触发点已落地 + 上板验证（2026-11-21 补充）

上面"只点明，不铺开"的触发点接线**已经完成并上板验证通过**：

- 调用位置（`qibiji_zxl/HARDWARE/DCMOTOR/dcmotor.c`）：366 行 `EventPostSimple(EVT_MOTOR_START_UP)`、373 行 `EventPostSimple(EVT_MOTOR_START_DOWN)`、390 行 `EventPostMotorStop(s_MapStopReason(...), s_opening)`、392 行 `EventPostMotorStall(s_stall_ma)`；
- 7.1 的前置条件均已满足：`event_pub.c` 已 Add 进 Keil 工程（`event_pub.o` 存在）、`#include "event_pub.h"` 已加；
- 原工程 `event_motor_up_posted` 等去重标志**不需要**：新工程 `if (s_old != s_state)` 本身就是边沿触发；
- 旧工程 `switch.c:194/221` 的限位事件原本就是注释掉的，未补（维持决策）。

上板日志（`TOPIC_EVENT_PUB=4`）：

```
send json-->4,{"id":"365860",...,"type":"event","body":{"eventCode":"motor.start.up","output":{}}}
send json-->4,{"id":"395880",...,"type":"event","body":{"eventCode":"motor.stop","output":{"reason":6,"opening":0}}}
```

`gprs_is_ready()` + `DCMOTOR_IsCalibrating()` 双闸正常（心跳释放后才发事件）。`reason:6` 当时是超时保护停机（`MOTOR_STOP_TIMEOUT → STOP_REASON_EMERGENCY_STOP`），字段映射正确。细节见《DCMotor重构操作建议.md》§17.2/§17.4。

## 4. 阶段三接线与验证

`main.c`：

```c
    /* per main-loop iteration, after GPRS_AT_StateMachine() */
    NetLiveness_Process();
    PropertyReport_Process();
```

并在 `gprs_json.c` 的 Parse 里加入 §1 的心跳识别和阶段四的对时分支占位。

**验证清单**：

1. 进 NORMAL 后（临时放行 timesync 门槛），调试口每 120s 出现 `[LIVE] heartbeat ...`，平台回 `{}` 后出现 `heartbeat ack ok`。
2. 断开平台（不回 `{}`），连续 3 次后 `[LIVE] recovery: hard reset 4G module`，模块重连。
3. 开闸让开度变化 ≥2%，看到 `up/property/post` 帧，`Opening` 字段与 `DCMOTOR_GetOpening()` 一致。
4. 停机时看到一次强制上报（`motor.stop` 事件 + 属性）。
5. 用 `NetLiveness_IsServerConfirmed()` 可在将来驱动 LCD 的"平台已确认在线"指示（本轮不改 LCD）。
