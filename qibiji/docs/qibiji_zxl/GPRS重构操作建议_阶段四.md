# GPRS（4G 上云）模块重构操作建议 · 阶段四

> 原文出自《[GPRS重构操作建议_阶段二起.md](./GPRS重构操作建议_阶段二起.md)》§9，现独立成篇。
> 阶段四依赖阶段三（心跳门槛 `s_wait_timesync=1` 必须在本阶段放行）。**可按需裁剪**——暂时不需要 OTA，只做对时 + 配置即可。

## 目标

补齐对时（RTC 校准）、远程配置、版本号上报、OTA 框架。这一阶段依赖 EEPROM/Flash，**可以按需裁剪**——如果暂时不需要 OTA，只做对时 + 配置即可。

## 1. 对时（sys.timeSync）

**作用**：从平台拿 `serverTime`（13 位毫秒），写进 DS1302 硬件 RTC，让所有上报带正确时间戳。

> **完整实现已独立成篇**：[DS1302重构操作建议.md](./DS1302重构操作建议.md)。
> 那里包含驱动拷贝清单与三处已知缺陷修正、`gprs_rtc.h/.c` 全部源码（unix ↔ BCD ↔ 时区 ↔ 周计算）、对时状态机完整代码、验证清单。本节只保留要点。

### 四条要点

1. **发**：进 `ST_NORMAL` 后**先热身 2s**，再发一轮 `request`（`requestCode="sys.timeSync"`，id 用 13 位时间戳）。模块 `AT+CACHEN=OFF` 不缓存上行，发早了会被丢。热身由 `GPRS_ClockSync_OnNormal()` 起计时，`Process()` 到点才 `Start()`。
2. **收**：回执 `type="request_reply"`，`body.output.serverTime` 是 13 位毫秒，**必须用 `cJSON_GetNumberValue()` 读成 double**（`valueint` 会溢出）。写入 RTC 后调 `NetLiveness_OnTimeSyncOk()` 放行首帧心跳。
3. **失败**（修掉原工程 F1）：`body.code != 0` 时也要 `NetLiveness_OnTimeSyncFail()` 并重试，否则心跳被锁死 24 小时。重试节奏 15/30/60s，每次换新 id。
4. **通道未确认时不烧重试次数**：任何一帧下行（心跳 ack 也算）都会调 `GPRS_ClockSync_OnDownlink()` 把 `s_clock_chan_ok` 置 1，之后才走 15/30/60 正式重试；在此之前按 60s 一帧单发探测，最多 5 次后交给 `NetLiveness` 的 `timeSync stuck` 看门狗。这样即使整轮都撞在模块重连窗口里，也不会 105 秒就 `give up`。

### 实际使用的代码

以下是当前磁盘上的真实源码（`qibiji_zxl/HARDWARE/GPRS/gprs_json.c` L13–27 常量/状态 + L227–406 七个函数），与 [DS1302重构操作建议.md](./DS1302重构操作建议.md) §8 所载状态机一致；RTC 驱动抽象层（`GPRS_RTC_NowMs/Set` 等）在该独立文档里，本节只列对时状态机本身。

```c
/* ===== qibiji_zxl/HARDWARE/GPRS/gprs_json.c : 对时状态机（实际源码） ===== */

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
    s_clock_warmup_pending = 1u;     /* 打开热身计时标志 */
    s_clock_warmup_tick = g_sys_ms;  /* 记下进入 NORMAL 的时刻 */
    GPRS_Printf("[RTC] warm-up %ums before first timeSync\r\n",
                (unsigned)GPRS_CLOCK_WARMUP_MS);
}

/* 标记下行通道已验证，退出探测模式 */
void GPRS_ClockSync_OnDownlink(void)
{
    if (s_clock_chan_ok != 0u)
        return;

    s_clock_chan_ok = 1u; /* 使 Process() 从"60 秒一帧探测、不消耗重试次数"切换到"15/30/60 秒正式重试" */
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

/* 针对 sys.timeSync 回执的处理函数：取出 serverTime 写 DS1302，放行首帧心跳 */
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

    NetLiveness_OnTimeSyncOk();      /* release first heartbeat 放行首帧心跳 */
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
```

**调用点（全部已在磁盘上接通）**：

| 函数 | 位置 | 触发时机 / 作用 |
|---|---|---|
| `GPRS_ClockSync_Process()` | `USER/main.c:130` | 主循环每轮调度，跑热身→探测→15/30/60 重试→give up 状态机 |
| `GPRS_ClockSync_OnNormal()` | `HARDWARE/GPRS/gprs_at.c:248` | `ST_AT_WAIT` 三条 AT 都完成、进 `ST_NORMAL` 时：Reset + 起 2s 热身 |
| `GPRS_ClockSync_Reset()` | `gprs_at.c:187` | 确认掉线 → 回 State1：丢弃在途对时 |
| `GPRS_ClockSync_Reset()` | `gprs_at.c:213` | 离线超时硬复位 4G 前：丢弃在途对时 |
| `GPRS_ClockSync_OnDownlink()` | `gprs_json.c:144` | `GPRS_JSON_ParseProcess` 解析出任意一帧有效下行 → `s_clock_chan_ok=1`，退出探测模式 |
| `GPRS_ClockSync_OnReply(root)` | `gprs_json.c:158` | 收到 `type="request_reply"` → 取 `body.output.serverTime` 写 DS1302，`NetLiveness_OnTimeSyncOk()` 放行首帧心跳 |

> `gprs_json.c` 文件头部已 `#include "gprs_rtc.h"`、`#include "net_liveness.h"`；`GPRS_RTC_NowMs()` 在对时完成前返回的是 DS1302 上电值，仅用于生成不重复的请求 id（不影响正确性，id 只作匹配），真正的时间戳门槛由阶段三的 `NetLiveness_IsWaitingTimeSync()` 把关。

### 为什么要有心跳门槛

心跳一旦通了 `s_miss` 就清零，会掩盖校时失败。校时失败会让之后所有上报带错时间戳，比"晚几分钟上线"糟得多。顺序硬性固定：3 条 AT → 校时 → 心跳。

### 依赖

- `GPRS_RTC_*` 四个接口来自本阶段新增的抽象层（见独立文档 §6/§7）；
- `GPRS_RTC_GetUnixSec()` 在未校时时返回 0，调用方必须判断，不能把 1970 年的时间戳发给平台。

## 2. 远程配置 / 版本号 / ReSet

- `getConfig` / `config`：原工程读写 `armReg.motor_warning_curr_upper`、`gate_maxheight` 并存 EEPROM。新工程无 EEPROM → 阶段四若要做，先用 RAM 变量 + 编译期初值，落在 `gprs_config.c` 里；持久化列为后续。
- `getFirmware`：回一个编译期版本号宏 `GPRS_FW_VERSION`，不依赖 OTA_Info。
- `ReSet`：阶段二已实现（延后复位），阶段四无需额外改动。

## 3. OTA（框架占位，可不做）

原工程 OTA 依赖 `24CXX`（存 OTA_Info）+ `STMFLASH`（写固件）+ A1/A2 双区 + 复位向量跳转，是独立的大需求。本版**不实现下载**，只保留 `setUpdateValue` 的回执框架（回 `{"updateFlag":...}`，标志位存 RAM）。真要做 OTA 时单独生成建议，不塞进本轮。

## 4. 阶段四验证

1. 对时：平台回 `request_reply` + `serverTime` 后，调试口打印解析出的时间，`NetLiveness_IsWaitingTimeSync()` 变 0、首帧心跳发出。
2. 对时业务失败（`code!=0`）：确认会重试而不是锁死（F1 已修）。
3. `getFirmware` → 回版本号；`getConfig` → 回当前配置项。
