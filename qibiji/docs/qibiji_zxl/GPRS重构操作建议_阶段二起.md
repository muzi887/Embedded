# GPRS（4G 上云）模块重构操作建议 · 文件二（阶段二起）

> **这是文件二**：《GPRS重构操作建议.md》的续篇，从阶段二（§7）开始，直到文末（§14）。
>
> **前置依赖**：必须先完成文件一《GPRS重构操作建议.md》的阶段一（§1–§6：地基、USART2/4G 物理通道、队列、六态状态机、main.c 挂载）。本文件的 JSON 命令闭环全部建立在阶段一的队列与状态机之上——阶段一没通，这里无从落地。
>
> **本文件包含**：阶段二（§7 JSON 解析 + 命令闭环）、§10 改 main.c 全貌、§11 改 Keil 工程、§12 机制讲解与设计取舍、§13 原工程静态发现的坑、§14 与原工程对照/约束/待办。

---

## 7. 阶段二：JSON 解析 + 命令闭环（最小可用系统）

**目标**：平台下发一条 `service` JSON（如 `setValue` / `stopGate`），设备解析出来、执行电机动作、回一条 `service_reply`。这是**整条链路最小可用闭环**——做到这里，云端就能远程控制闸门了。

**新增文件**：`gprs.h`（信封结构 + 全局声明）、`gprs_json.h/.c`（七步总入口 + Parse + 回执 + 复位）、从原工程拷 `cJSON.c/.h` 和 `mymalloc`。**依赖阶段一已完成的队列和状态机**。

### 7.1 拷入 cJSON 与内存池

`cJSON` 是纯软件库，无硬件依赖：

- 把原工程 `qibiji/HARDWARE/cJSON/cJSON.c` 和 `cJSON.h` 原样拷到 `qibiji_zxl/HARDWARE/GPRS/`。
- `cJSON` 默认用标准库 `malloc/free`。原工程配了 `HARDWARE/MALLOC/mymalloc.h`（内存池，避免堆碎片）。若要用它，在 `cJSON.h` 里把 `malloc`/`free` 重定义到内存池，并一并拷 `malloc.c`。**本轮建议先用标准库 malloc**（STM32 上 cJSON 解析一帧 ≤1KB 的 JSON，编译进 `heap` 即可），内存池作为可选项。
- Keil 工程里加入 `cJSON.c` 节点（见 §11）。

> **C90 注意**：原版 cJSON 里有 `//` 注释和 `for(int i...)` 之类写法。原工程既然能在 C90 下编过，说明它那份已经改过——**直接拷原工程那份**，不要从网上重新下载新版。

### 7.2 `gprs.h` —— 信封结构与全局声明

原工程 `gprs.h` 里大量宏和结构体，本版只留 JSON 解析/回执真正用到的部分。

```c
#ifndef __GPRS_H
#define __GPRS_H

#include "gprs_config.h"
#include "cJSON.h"

#define JSON_HDR_ID_LEN          64u
#define JSON_HDR_TYPE_LEN        24u
#define JSON_HDR_SERVICECODE_LEN 16u

/* last legal downlink envelope -- copied out before cJSON tree is freed */
typedef struct
{
    char id[JSON_HDR_ID_LEN];
    char type[JSON_HDR_TYPE_LEN];
} GprsMsgHeader;

typedef struct
{
    char serviceCode[JSON_HDR_SERVICECODE_LEN];
} GprsMsgService;

extern GprsMsgHeader  g_json_hdr;
extern GprsMsgService g_json_service;

/* seven-step pipeline entry -- call once per main loop */
void GPRS_Process(void);

/* reset deferred until service_reply has been sent */
#define GPRS_RESET_REPLY_TIMEOUT_MS 300u
#define GPRS_RESET_MIN_DELAY_MS     1000u
#define GPRS_RESET_MAX_WAIT_MS      6000u
void GPRS_Reset_Request(void);
void GPRS_Reset_Process(void);

/* internal steps (exposed for stage 3/4 hooks) */
void GPRS_RxDatas(void);
void GPRS_JSON_ParseProcess(void);

/* server JSON envelope validation: NULL = OK, otherwise error reason */
const char *GPRS_ValidateServerJson(cJSON *root);

#endif
```

配套 `gprs.c`（只放全局定义和信封校验）：

```c
#include "gprs.h"
#include "gprs_port.h"
#include "gprs_at.h"
#include "uart_queue.h"
#include <string.h>

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
    /* 已启用 IWDG：喂狗点在 GPRS_4G_Init()/GPRS_4G_Reset() 的复位脉冲后
       （见《看门狗重构操作建议》§3.4）；此处是 MCU 软复位，立即生效，无需再喂 */
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
```

> **ReSet 的设计精髓**（务必保留）：回执和复位**解耦**。发送状态机在 `Parse` 之后才跑；如果解析到 `ReSet` 就直接 `NVIC_SystemReset()`，回执还压在队列里没发出去，平台只会记一条"命令超时"。所以这里只登记 `s_reset_pending`，等 `UartTx_IsEmpty()` 为真（回执真发出去了）+ 至少 1s，才真复位；6s 是防止队列卡死的兜底。

### 7.3 `gprs_json.h/.c` —— 解析分发 + 命令落地 + 回执

这是阶段二的主角。先看头文件：

```c
#ifndef __GPRS_JSON_H
#define __GPRS_JSON_H

#include "gprs.h"

void GPRS_JSON_ParseProcess(void);

/* service_reply: code=0 success; output is a JSON object string like "{\"open\":true}" */
void GPRS_ReplyServices(int code, const char *message, const char *output);
/* short-lived variant used by ReSet/stopGate */
void GPRS_ReplyServicesPush(int code, const char *message, const char *output,
                            uint16_t timeout_ms);

/* command handlers */
void GPRS_SetValue(int value);
void GPRS_StopGate(void);

#endif
```

`gprs_json.c` 实现（**核心**，注意 `armReg` 已被 getter 替换）：

```c
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
static void s_reply_push(int code, const char *message, const char *output,
                         uint16_t timeout_ms)
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
    (void)UartTx_Push_JSON(json_str, (uint16_t)strlen(json_str), reply_id,
                           NULL, timeout_ms, 0u);
}

void GPRS_ReplyServicesPush(int code, const char *message, const char *output,
                            uint16_t timeout_ms)
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
    /* NEW: range check -- original code silently truncated to u8 (value 300 -> 44) */
    if ((value < 0) || (value > 100))
    {
        GPRS_ReplyServices(400, "value out of range [0..100]", "{}");
        return;
    }

    /* NEW: refuse while calibrating -- original replied success but did nothing */
    if (DCMOTOR_IsCalibrating())
    {
        GPRS_ReplyServices(409, "calibrating, command rejected", "{}");
        return;
    }

    /* true closed-loop target: DCMOTOR_Process() compares the encoder-derived
       opening with the target every tick and stops on arrival (plan B is now
       the default; it replaces the former one-shot DCMOTOR_Request jog) */
    if (DCMOTOR_SetTarget((uint16_t)value) == 0u)
    {
        GPRS_ReplyServices(409, "not calibrated, target rejected", "{}");
        return;
    }

    /* motion is asynchronous: reply now, the opening progress is reported as
       property frames driven by DCMOTOR_Tick() -> PropertyReport_SetPending() */
    GPRS_ReplyServices(0, "success", "{}");
}

/* ---- command: stopGate ---- */
void GPRS_StopGate(void)
{
    DCMOTOR_Request(MOTOR_STOP);
    GPRS_ReplyServicesPush(0, "success", "{\"success\": true}",
                           GPRS_RESET_REPLY_TIMEOUT_MS);
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

        /* NOTE: stage 3 inserts NetLiveness_IsAck(root) check here, before validate */

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
```

> **三个关键设计（照搬原工程，别改）**：
> 1. **字段先抄后进队**：`cJSON` 的 `valuestring` 指向解析树内部，树一 `cJSON_Delete` 就失效。回执要带回平台的 `id`/`serviceCode`，所以必须先 `strncpy` 到全局 `g_json_hdr`/`g_json_service`，再释放树。
> 2. **校验失败用 `continue` 不用 `return`**：原工程这里是 `return`（见 §13 F6），一帧坏 JSON 会卡住同轮后续帧。新版改成 `continue`，坏帧丢弃、好帧照处理。
> 3. **范围校验 + 校准拒绝是新增的保护**：原工程 `setValue` 把平台下发值强转 `u8`（300→44）且校准中仍回 success（见 §13 F5）。新版加了范围检查和 `DCMOTOR_IsCalibrating()` 拒绝。

### 7.4 阶段二接线与验证

`main.c` 把阶段一注释掉的解析调用打开：

```c
        GPRS_AT_RxPump();
        GPRS_JSON_ParseProcess();             /* stage 2: was commented out */
        GPRS_AT_StateMachine();
        UartTx_Queue_Process(g_sys_ms);
        GPRS_Reset_Process();
```

初始化追加：`UartTx_Queue_Init()` / `UartRx_Queue_Init()` 已在阶段一加过，无需重复。

**验证清单**：

1. 让模块先进 NORMAL（阶段一已验证）。
2. 用串口助手经 4G/或直连模拟下发一帧（把 `<pk>`/`<dk>`/`<id>` 换成实际值）：
   ```
   1,{"id":"t1","productKey":"pk","deviceKey":"dk","type":"service","body":{"serviceCode":"stopGate","commandNo":"c1"}}
   ```
3. 调试口应看到 `s->c JSON service` 和 `replyServices-->`，电机停止。
4. 下发 `setValue` + `{"params":{"Value":30}}` → 电机朝目标点动，回执 success；下发 `Value=300` → 回 `400 value out of range`。
5. 下发 `ReSet` → 先看到回执发出，`≥1s` 后才复位（可用调试口打印的时序观察）。
6. 发一帧缺 `deviceKey` 的坏 JSON → 打印 `validation failed`，但**下一帧好 JSON 仍能被处理**（验证 F6 已修）。

---

## 10. 改 `main.c`（全貌核对）

把阶段一、二接上后，`main.c` 的相关部分应是这样（在 DCMotor 文档 §7.5 那版基础上叠加 GPRS）：

```c
#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"
#include "lcd_backlight.h"
#include "wdg.h"
#include "adc.h"
#include "gate_io.h"
#include "dcmotor.h"
/* GPRS stage 1-3 */
#include "gprs_config.h"
#include "gprs_port.h"
#include "gprs_4g.h"
#include "gprs_at.h"
#include "uart_queue.h"
#include "gprs.h"
#include "gprs_json.h"
#include "net_liveness.h"
#include "property_report.h"
#include "event_pub.h"

int main(void)
{
    NVIC_Configuration();
    DEBUG_UART_Init();
    KEY_Init();
    delay_init();
    WDG_Init();               /* 必须先于 LCD12864_Init()、AT24CXX_Check()、GPRS_4G_Init() 等长阻塞 */
    LCD12864_Init();
    LCD_BACKLIGHT_Init();
    LCD_PAGES_Init();
    ADC1_Init();
    GATEIO_Init();
    DCMOTOR_Init();
    DCMOTOR_SetMaxTurns(0u);
    TIM3_10ms_Init();

    /* ---- GPRS init (after TIM3, since g_sys_ms ticks from it) ---- */
    GPRS_PORT_Init(115200);   /* verify your module baud */
    GPRS_4G_Init();           /* 必须在串口之后：复位后要能收到模块启动 banner */
    UartTx_Queue_Init();
    UartRx_Queue_Init();

    DEBUG_UART_SendString("qibiji_zxl GPRS ready\r\n");

    while (1)
    {
        WDG_Feed();               /* 主循环存活基线：每轮一次，不受节拍条件控制 */
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();
            ADC_Tick();
            GATEIO_Tick();
            DCMOTOR_Tick();
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);

        if (LCD_PAGES_GetParam(LCD_PARAM_AUTO_FLAG) != 0u)
        {
            LCD_PAGES_SetParam(LCD_PARAM_AUTO_FLAG, 0u);
            DCMOTOR_StartCalibration();
        }

        DCMOTOR_Process();

        /* ---- GPRS: every loop, no tick gate (all timeouts use g_sys_ms) ---- */
        GPRS_Process();
        /* GPRS_Process() internally calls:
         *   GPRS_RxDatas -> GPRS_JSON_ParseProcess -> GPRS_AT_StateMachine
         *   -> UartTx_Queue_Process(g_sys_ms) -> GPRS_Reset_Process
         * stage 3 hooks (call here after GPRS_Process, or add inside it):
         */
        NetLiveness_Process();
        PropertyReport_Process();
    }
}
```

> **`g_sys_ms` 初始化顺序**：`GPRS_4G_Init()` 内部用了 `delay_ms()` 和复位脉冲，但不读 `g_sys_ms` 做判断（首次复位不计入节流）。TIM3 要在 GPRS 初始化前启好，这样进主循环时 `g_sys_ms` 已经在走。**注意**：若 watchdog、LCD 等也用了 TIM3，确认 `TIM3_IRQHandler` 只有一份定义。

---

## 11. 改 Keil 工程（`USER/SRTUV10.uvprojx`）

### 11.1 IncludePath 追加一个目录

在 IncludePath（约第 338 行）末尾追加：

```
;.\HARDWARE\GPRS
```

cJSON 若拷进 `HARDWARE/GPRS/`，就不用再加 `HARDWARE\cJSON` 路径；若放在原处，另外加 `;.\HARDWARE\cJSON`。

### 11.2 `HARDWARE` 组追加源文件节点

按阶段逐步加（先加阶段一，Build 通过再加下一阶段）：

| 阶段 | 加入的 .c 文件 |
|---|---|
| 1 | `gprs_port.c`、`gprs_4g.c`、`uart_queue.c`、`gprs_at.c`、`gprs.c` |
| 2 | `gprs_json.c`、`cJSON.c`（+ 可选 `malloc.c`） |
| 3 | `net_liveness.c`、`property_report.c`、`event_pub.c` |
| 4 | `gprs_rtc.c`（+ 若做 OTA：`24cxx.c`、`stmflash.c`） |

节点 XML 格式参照已有文件（`<File><FileName>xxx.c</FileName><FilePath>.\HARDWARE\GPRS\xxx.c</FilePath></File>`）。

### 11.3 中断向量冲突检查（重要）

新工程 `stm32f10x_it.c` 里若已有 `USART2_IRQHandler` 或 `EXTI9_5_IRQHandler` 的空实现，会和 `gprs_port.c` / `gprs_4g.c` 里的定义**重复定义**。处理办法：保留一处——要么把中断函数留在 `gprs_port.c`/`gprs_4g.c`，删掉 `stm32f10x_it.c` 里的空桩；要么把函数体挪到 `stm32f10x_it.c`，在模块里只放处理逻辑函数。**二选一，不能两处都有**。

---

## 12. 机制讲解与设计取舍

### 12.1 四层"在线"证据别混

这是理解整条链路的钥匙，原工程解读稿反复强调：

| 层 | 变量/判据 | 证明什么 | 阶段 |
|---|---|---|---|
| 蜂窝注册 | `GPRS_LINKA`（去抖后） | 模块跟基站连上了 | 1 |
| 串口可对话 | `g_atflag_online`（banner） | 模块启动完、UART 能收发 AT | 1 |
| 业务就绪 | `gprs_is_ready()`（NORMAL） | 三条 AT 都过，可发业务 JSON | 1 |
| 平台可达 | 心跳回执 / `IsServerConfirmed()` | 云端真的收到并回应了 | 3 |

**banner 为什么必须有**：`LINKA` 只证明"模块跟基站注册上了"，但模块串口可能还没启动完。此时若一看见 LINKA 就压三条 AT，模块答不了 → AT 全超时 → 重试耗尽 → 进 30 分钟退避，现场表现是"上电半小时不上线"。banner（模块上电主动吐的 `WH-LTE-7S1`）是 MCU 能拿到的唯一"模块端已就绪"的主动证据。放行条件：`link_state && (atflag_online || link_down_occurred)`。掉线恢复时模块不会再吐 banner，所以留了 `link_down_occurred` 这半边。

### 12.2 收发是两个独立队列

下行帧：DMA → 乒乓暂存槽 → 发送队列的接收环形缓冲 → 8 项帧队列 → `GPRS_JSON_ParseProcess()` 消费。上行帧：业务层 → 发送队列（8 项）→ 一发一收状态机 → USART2。**中间只有 `GPRS_JSON_ParseProcess()` 把两边连起来**（收到命令→执行→回执入队→发送）。

乒乓暂存槽的意义：DMA 是 Normal 模式，IDLE 中断收一帧就重新使能复用同一缓冲。若主循环被长操作占住（Flash 擦写、大量 printf），第二帧会覆盖第一帧——静默丢下行。暂存槽在中断里就把每帧搬走，主循环停顿不再造成覆盖。

### 12.3 为什么回执用 300ms 短驻留

发送队列只有 8 项。一条 JSON 若按 5s 等平台应答，连续几条上报就把队列堵死。所以回执/上报默认"fire and forget"（发完留 300ms 帧间隔就出队），不占队列。代价是平台侧丢失就永久丢失，设备不知道——这是明确取舍。只有真正需要应答的（对时 request）用 5000ms 超时 + 回调。

### 12.4 非 NORMAL 时上行静默丢弃

`UartTx_Push_JSON()` 开头 `if (!gprs_is_ready()) return 0;`，事件/属性入口也用 `gprs_is_ready()` 挡掉。**丢弃不打印、不计数**。因为 NOR Flash 没地方存历史事件。代价是掉线期间发生的一切在云端永久缺失。现场排查"为什么这段没数据"时，只能靠 `[GPRS] State1/3/5/6` 日志反推。

### 12.5 复位节流的统一时间线

所有 4G 模块硬复位（离线恢复 5min、探活/校时自愈 15min、蓝牙重配置）**共用 `gprs_4g.c` 里 `s_last_reset_ms` 一条时间线**。原工程曾因两条路径各记各的，出现"状态机刚复位、90 秒后应用层又复位"。新版照搬这个统一机制。

---

## 13. 原工程静态发现的坑（F1–F10）新版怎么避开

这些是原解读稿里记录的问题。重构是"接着写完"而不是"照抄"，能修的都修了：

| 编号 | 原工程问题 | 新版处理 |
|---|---|---|
| **F1（高）** | 对时业务失败 `code!=0` 时直接 return，心跳被锁死 24h | 阶段四文档明确：`code!=0` 也要 `OnTimeSyncFail()` + 重试 |
| **F2（高）** | 命令已执行但同轮掉线确认清空发送队列，回执发不出 | §7.3 的 ReSet 延后复位已修一类；F2 的"命令轮次撞掉线"窗口仍需现场留意——可考虑把 `UartTx_Queue_Init()` 的清队列移到确认无在途回执时（列为待办） |
| **F3（中）** | `PeriodicPush` 名不副实（只做对时），属性无周期兜底 | 新版拆出独立的 `property_report.c`，不再叫 PeriodicPush；周期兜底仍未做（见待办） |
| **F4（中）** | 非 NORMAL 上行静默丢弃且无告警 | 保留取舍，但 `GPRS_Printf` 日志可见 State 变化（§12.4） |
| **F5（中）** | `setValue` 无条件回 success、u8 截断 | §7.3 加了范围校验 [0,100] + 校准中拒绝 |
| **F6（低）** | 校验失败 `return` 卡住同轮后续帧 | §7.3 改成 `continue` |
| **F7（低）** | `property_get` 复用全局信箱 commandNo 残留 | 阶段四做 property_get 时先 `memset` 再填（已注明） |
| **F8（低）** | 推送失败返回值被忽略，队列满回执静默丢 | 新版保留了返回值，但调用方大多仍不检查（列为待办） |
| **F9（低）** | 心跳回执判据只认空对象 | 保留（原工程评估风险很低） |
| **F10（低）** | `UartTx_OnRecv()` 声明返回 u8 但末尾无 return（UB） | 照抄时要补 `return 0;`（**明确提醒**：原文件 L222 掉出函数末尾） |

> **F10 特别提醒**：`uart_queue.c` 的 `UartTx_OnRecv()` 在非 AT/非 JSON 匹配时会掉出函数末尾，ARMCC 下返回寄存器残值。当前调用方不看返回值所以没暴露，但这是未定义行为。**照抄时在函数末尾补 `return 0u;`**。

---

## 14. 与原工程对照 / 约束 / 待办

### 14.1 逐项对照

| 项 | 原工程 | 新工程（本建议） |
|---|---|---|
| 数据黑板 | 全局 `armReg` | getter：`DCMOTOR_GetOpening()` / `ADC_GetCurrentMa()` / `DCMOTOR_GetState()` |
| 时基 | 全局 `sys_ms`（TIM2） | `g_sys_ms`（TIM3 每 10ms +10，文件一 §5.1） |
| 日志 | `printf` + `MY4G_printf` | `GPRS_Printf()`（文件一 §5.3） |
| 身份 | EEPROM 读 productKey/deviceKey | 编译期常量（文件一 §5.4），将来换读取函数 |
| JSON 库 | cJSON + mymalloc | cJSON 原样拷，先用标准 malloc |
| 对时 RTC | DS1302 | `gprs_rtc.h` 软件桩（见阶段四文档） |
| OTA | 24CXX + STMFLASH + A1/A2 双区 | 不实现，只留回执框架 |
| 蓝牙旁路 | uart3_at_sequence 共享 USART2 | 不搬，USART2 归 4G 专用 |
| 模块拆分 | 7 文件混在 GPRS 目录 | 同左，但按四阶段逐步落地 |

### 14.2 约束（全程遵守）

- **C90 / ASCII 注释**：所有新代码变量块首声明、无 `for(int i...)`、无 `//` 之后的中文（注释用英文）。cJSON 用原工程那份已适配 C90 的。
- **不改任何 `.c`/`.h` 源码（本轮）**：本文件只是建议，未改动工程源码。真正实施时按阶段改。
- **不碰 LCD 显示**：本轮不改 `lcd_pages.c`。心跳/在线状态要不要上屏，是后续"真数据接线"的独立需求。
- **阶段有强依赖**：1→2→3→4 顺序执行，每阶段单独 Build + 上板验证。

### 14.3 待办（本轮明确不做，列出来避免遗漏）

1. **`setValue` 语义**：本轮是"点动一次"（方案 A）。若要"设目标自动走到"，给 DCMOTOR 加 `DCMOTOR_SetTarget()`（方案 B）。
2. **属性周期兜底**：目前只在开度变化/停机时上报，闸门长期不动时平台拿不到属性。可加一个 24h 周期上报（原工程 `PeriodicPush` 本意）。
3. **事件触发点接线**：`EventPost*` / `PropertyReport_SetPending()` 要在 DCMOTOR 状态迁移处补调用（本文件只点明位置）。
4. **F2 窗口**：命令轮次撞掉线确认时回执可能被清队列，需现场观察，必要时调整清队列时机。
5. **F8 队列满**：入队返回值调用方未检查，队列满时回执静默丢。
6. **持久化**：productKey/deviceKey、配置参数、OTA 标志目前无 EEPROM，都用常量/RAM。
7. **看门狗**：已按《看门狗重构操作建议》接入 IWDG。喂狗点在 `GPRS_4G_Init()` / `GPRS_4G_Reset()` 的复位脉冲之后（§3.4）；`GPRS_Reset_Process()` 的 MCU 软复位立即生效，无需额外喂狗，主循环每轮照常喂狗。
8. **对时回执分支**：阶段四的 `request_reply` 分发尚未写进 `GPRS_JSON_ParseProcess()`，做阶段四时补。

### 14.4 配套文档

本文件是《GPRS重构操作建议.md》（文件一，阶段一）的续篇，必须与文件一配套使用；另与《ADC重构操作建议.md》（电流/电压 getter 来源）、《DCMotor重构操作建议.md》（开度/状态 getter、校准、§16 继电器边界）配套。《RS485重构操作建议.md》本轮**暂不实施**（用户决定），其与 GPRS 无关——GPRS 走 USART2 的 4G 模块，RS485 走 UART4，两者独立。
