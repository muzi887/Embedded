# GPRS（4G 上云）模块重构操作建议（qibiji_zxl）

> **起点**：`qibiji_zxl` 现在有 KEY / TIM3 / DEBUG_UART / LCD / ADC / DCMOTOR / GATEIO 几个模块，`main.c` 跑轮询 + 显示 + 电机。没有 USART2 的 4G 链路，没有 cJSON / malloc / DS1302 / 24CXX，没有全局 `armReg` 寄存器黑板，也没有全局毫秒计数 `sys_ms`。
>
> **目标**：把原工程那条真正在跑的对外链路——4G 模块内 MQTT + 私有 JSON——搬进新工程，做成一个独立模块 `HARDWARE/GPRS/`。**用新工程的 getter（`DCMOTOR_GetOpening()` / `ADC_GetCurrentMa()` 等）替代 `armReg`**，不搬全局黑板。整条链路按**四个阶段**落地，每阶段都能单独编译、单独上板验证。
>
> **怎么用这份文件**：本文件（文件一）是**阶段一**：第 1、2 节先讲链路在系统里干什么、原工程由哪些文件组成（很短，务必先看）；第 3 节是原工程依赖到新工程的裁剪映射表（**这是重构的核心决策**）；第 4 节是模块划分与四阶段路线图；第 5 节是必须先补的两块地基（毫秒时基 + 串口收发缓冲）；第 6 节是阶段一的完整代码与改动（物理通道 + 模块上线 + 验证清单）。**阶段二～四（§7–§14：JSON 命令闭环、保活上报、对时/配置/OTA，以及 main.c 全貌、Keil 工程、机制讲解、静态坑、对照待办）已拆到配套文件**《GPRS重构操作建议_阶段二起.md》（文件二）。两个阶段有强依赖，先做完本文件阶段一再做文件二。
>
> **实施顺序**：这是一条链，**阶段有强依赖**，必须按 1→2→3→4 走。阶段一是地基（不接云也能验证 AT 通了）；阶段二打通"收命令→执行→回执"最小闭环；阶段三加保活与上报；阶段四做对时/配置/OTA。本轮不改 LCD 显示内容。（阶段二起见《GPRS重构操作建议_阶段二起.md》。）

---

## 1. 这块链路在系统里干什么（先看这个）

一句话：**它是控制器和云平台之间的"无线网线与懂协议的翻译官"。**

打个比方：控制器是个只会说"开闸/关闸/报开度"的本地设备，云平台在千里之外。4G 模块（WH-LTE-7S1）是一台已经配好 MQTT 的"网关"，MCU 通过串口（USART2）用一套简单的文本命令指挥它——`"1,{...}"` 表示"往主题 1 发这条 JSON"；`"2,{}"` 表示"往主题 2 发个心跳"。云平台下发的东西也从这条串口吐回来，MCU 拆出 JSON 看懂命令，执行完再回一条。

关键认知（来自原工程解读稿）：**Gprs_Process() 不是一个函数，而是一条流水线**。它每轮主循环把七个"轮子"转一遍——

```
收 → 解 → 连 → 报/活 → 发 → 复位 → 可观测
```

它自己不发一个字节、不解析一条报文，只调度。所有跨轮状态都藏在子模块的 `static` 变量里。这是个**非阻塞轮询调度器**（唯一的阻塞是 4G 模块硬复位那 1 秒）。

对应到系统里的位置：

| 步骤 | 谁做 | 在哪 | 阶段 |
|---|---|---|---|
| ① 收：DMA 整帧 → 暂存槽 → 环形缓冲 → 帧队列 | USART2 IDLE 中断 + `UartRx_PumpFromDma()` | `gprs_port.c` / `uart_queue.c` | 1 |
| ② 解：取帧、提 JSON、信封校验、type 分发、执行命令、回执入队 | `GPRS_JSON_ParseProcess()` | `gprs_json.c` | 2 |
| ③ 连：六态链路状态机（离线→AT 初始化→NORMAL） | `gprs_state_machine()` | `gprs_at.c` | 1 |
| ④ 活：心跳探活 + 对时调度 | `NetLiveness_Process()` / 对时 | `net_liveness.c` / `gprs_json.c` | 3 |
| ④ 报：开度变化 / 停机 / 事件上报 | `PropertyReport_*` / `EventPost*` | `property_report.c` / `event_pub.c` | 3 |
| ⑤ 发：发送队列"一发一收"状态机 | `UartTx_Queue_Process()` | `uart_queue.c` | 1 |
| ⑥ 复位：ReSet 延后复位（等回执发出去） | `Gprs_Reset_Process()` | `gprs_json.c` | 2 |
| 地基 | 全局毫秒 `sys_ms` + 4G 模块复位/LINKA | `timer.c`（改）/ `gprs_4g.c` | 1 |

**它测/传的量代表什么**：上下行的 JSON 里带的是闸门开度（0–100%）、电机电流/电压、电池电压/电流、测量值等。本模块的职责是**忠实搬运 + 按协议编解码**，数据从新工程各模块的 getter 现取，不存一份副本。

---

## 2. 原工程现状：7 个协作文件 + 一堆依赖

理解原工程的组成，是这份重构的前提。原工程 `qibiji/HARDWARE/GPRS/` 下是 7 个自研文件，加 `HARDWARE/4G/` 和 USART2 驱动：

| 文件 | 职责 | 本轮对应 |
|---|---|---|
| `gprs.h` | 信封结构体、主题号、各种参数宏、函数声明 | `gprs.h`（裁剪） |
| `gprs_JSON.c` | 七步总入口 + 解析分发 + 回执 + 对时 + 复位 + OTA | `gprs_json.c`（分阶段拆） |
| `gprs_AT.c` | 六态状态机 + LINKA 去抖 + 三条 AT + 回调 | `gprs_at.c` |
| `uart_queue.c/.h` | 发送队列（AT/JSON）+ 接收环形缓冲/帧队列 + JSON 提取 | `uart_queue.c/.h`（基本照搬） |
| `net_liveness.c/.h` | 心跳探活 + 假连接自愈 | `net_liveness.c/.h` |
| `property_report.c/.h` | 属性上报（开度变化/停机强制） | `property_report.c/.h` |
| `event_pub.c/.h` | 事件上报（电机启停/限位/报警） | `event_pub.c/.h` |
| `HARDWARE/4G/4G.c/.h` | 模块 RST 复位 + LINKA 外部中断 + 复位节流 | `gprs_4g.c/.h` |
| `HARDWARE/USART/usart2.c` | PA2/PA3 + DMA1_CH6 + IDLE + 乒乓暂存槽 | `gprs_port.c`（只搬 4G 相关部分） |

**此外还依赖这些库/外设**：`cJSON`（JSON 解析）、`mymalloc`（cJSON 的内存池）、`DS1302`（对时写 RTC）、`24CXX`（OTA 信息存 EEPROM）、`STMFLASH`（OTA 下载写 Flash）、`reg.c` 的 `armReg`（全局数据黑板）、`main.h` 的全局 `sys_ms` / LED / `xinhao` / `ccid` / `order` 显示缓冲。

**最关键的一句**：原工程是真正跑起来的链路（不像 RS485 那样"只问不用"），但它的实现**和 `armReg`、显示缓冲、EEPROM 深度耦合**。重构的功夫主要不在"照搬代码"，而在**把这些耦合点换成新工程的 getter / 局部状态**。

---

## 3. 依赖裁剪映射（重构的核心决策）

这一节是整份文档最重要的表。原工程每依赖一样东西，新工程都要有个说法。

### 3.1 全局 `armReg` → 新工程 getter / 局部状态

| 原工程写法 | 含义 | 新工程替换 |
|---|---|---|
| `armReg.gate_kaidu` | 当前开度（0–100） | `DCMOTOR_GetOpening()` |
| `armReg.motor_curr` | 电机电流（mA×1000） | `(u32)ADC_GetCurrentMa() * 1000` |
| `sys_moter_state` | 电机状态 | `DCMOTOR_GetState()` |
| `armReg.range_target` | 远程目标开度 | `DCMOTOR_SetTarget()`（已落地，见《DCMotor重构操作建议》§6 目标开度闭环） |
| `armReg.motor_warning_curr_upper` | 堵转电流阈值（配置写） | 本轮不搬配置写，固定为编译期常量或后续 EEPROM |
| `armReg.gate_maxheight` | 闸门最大行程（配置写） | 同上 |
| `armReg.reportT` | 上报周期（配置） | 固定常量（本版不上报周期兜底） |
| `autoFlag` | 自动校准中标志 | `DCMOTOR_IsCalibrating()` |
| `rxparagate` | 收到的开度值 | 保留为 `gprs_json.c` 内局部 `static u8` |

**`setValue` 落地（已按方案 B 实现）**：原工程 `SettingValue()` 直接写 `armReg.range_target` 和 `sys_moter_state = MOTOR_UP/DOWN`，电机驱动层再去逼近目标。新工程早期 DCMOTOR 只有**请求式**接口（`DCMOTOR_Request(MOTOR_UP/MOTOR_DOWN/MOTOR_STOP)`），所以曾考虑只做"点动"（方案 A）；现已给 DCMOTOR 补上**目标开度闭环**（方案 B）：

- `DCMOTOR_SetTarget(t)` 设 0–100 目标，`DCMOTOR_Process()` 第 3 步按 `s_opening` 选方向，到位即 STOP；`DCMOTOR_IsCalibrating()` 时拒绝；
- 手动 `DCMOTOR_Request()`、任何 STOP、校准启动都会取消未完成的目标（`s_target_active = 0u`）；
- 因此 `GPRS_SetValue()` 现在只做参数校验 + `DCMOTOR_SetTarget()` + 回执，语义与原工程"设目标后自动走到"一致。

### 3.2 基础件与库

| 原工程依赖 | 新工程怎么办 | 阶段 |
|---|---|---|
| 全局 `sys_ms`（毫秒计数） | **必须补**：在 `timer.c` 的 TIM3 中断里加 `volatile u32 g_sys_ms += 10`（见 §5.1） | 1 |
| `cJSON.c/.h` | 纯软件库，直接拷进工程；`mymalloc` 一并拷（cJSON 默认用 malloc，可切到内存池） | 2 |
| `printf` / `MY4G_printf` 日志 | 统一替换为 `GPRS_PRINTF(...)` → `DEBUG_UART_SendString` 的包装（见 §5.3） | 1 |
| `LED_LINE` 在线灯 | 新工程无 led 模块；阶段一先用 `GPRS_PRINTF` 打印状态代替，不新增 GPIO | 1 |
| `g_mqtt_productKey` / `g_mqtt_deviceKey` / `clientid`（从 EEPROM 读） | 新工程无 EEPROM：先用 `gprs_config.h` 里的**编译期常量宏**占位（见 §5.4） | 1 |
| `DS1302` 对时写 RTC | 阶段四才需要：`gprs_rtc.h` 抽象层，本版给"仅软件计时"桩实现 | 4 |
| `24CXX` / `STMFLASH`（OTA） | 阶段四 OTA 才需要；本版**不实现 OTA 下载**，只保留回执框架占位 | 4 |
| `xinhao` / `ccid` / `order` 显示缓冲 | 新工程不接 LCD；AT 回调里只置标志位，不写显示缓冲 | 1 |
| 蓝牙旁路 `uart3_at_sequence` | 新工程没有，不搬；USART2 全程归 4G 专用 | — |

### 3.3 明确不搬

| 项 | 为什么不搬 |
|---|---|
| `armReg` / `reg.c` 全局黑板 | 新工程用 getter 替代，这是路线图既定方向 |
| `HARDWARE/MODBUS/` / SL651 | 与 GPRS 无关，本工程零实现 |
| OTA 二进制下载整套 | 依赖 EEPROM + Flash 分区 + 复位向量，是独立大需求，阶段四只留接口 |
| 蓝牙近场 AT 配置旁路 | 新工程无蓝牙，USART2 不共享 |

---

## 4. 模块划分与四阶段路线图

```
HARDWARE/GPRS/                        ← 本轮新建目录
├── gprs_config.h      编译期常量（主题号/身份/日志宏/超时参数）
├── gprs_port.h/.c     USART2 + DMA1_CH6 + IDLE + 乒乓暂存槽（阶段1）
├── gprs_4g.h/.c       4G 模块 RST + LINKA + 复位节流（阶段1）
├── uart_queue.h/.c    发送队列 + 接收缓冲/帧队列 + JSON 提取（阶段1）
├── gprs_at.h/.c       六态链路状态机 + 三条 AT（阶段1）
├── gprs.h             信封结构 + 全局声明（阶段2）
├── gprs_json.h/.c     七步总入口 + 解析分发 + 回执 + 复位（阶段2）
├── net_liveness.h/.c  心跳探活 + 自愈（阶段3）
├── property_report.h/.c  属性上报（阶段3）
├── event_pub.h/.c     事件上报（阶段3）
├── cJSON.c/.h         ← 从原工程 HARDWARE/cJSON 原样拷（阶段2）
└── mymalloc.h/malloc.c ← 从原工程 HARDWARE/MALLOC 原样拷（阶段2，可选）
```

**四阶段路线图**（每阶段产出可独立验证）：

| 阶段 | 目标 | 新增/改动文件 | 验证标志 |
|---|---|---|---|
| **1 地基与上线** | 4G 模块启动、注网、AT 初始化过、进 NORMAL | `timer.c`（加 sys_ms）、`gprs_config.h`、`gprs_port`、`gprs_4g`、`uart_queue`、`gprs_at` | 调试口看到 `State1→2→3→4`；发 `AT` 有回执 |
| **2 命令闭环** | 平台下发 setValue/stopGate → 执行 → service_reply | `gprs.h`、`gprs_json`、`cJSON`、`mymalloc`、`main.c` 挂 `Gprs_Process()` | 用串口模拟下发一帧 JSON，能看到 `s->c JSON service` 和 `replyServices-->` |
| **3 保活与上报** | 心跳、开度变化上报、事件上报、掉线自愈 | `net_liveness`、`property_report`、`event_pub` | 每 120s 一帧心跳；开闸后看到 `up/property/post` |
| **4 对时/配置/OTA** | 对时、远程配置、版本号、ReSet、OTA 框架 | `gprs_rtc` 桩、扩展 `gprs_json` 分支 | 对时回 `request_reply`；ReSet 延后复位 |

> **为什么要分阶段**：整条链路一次铺开涉及 ~16 个文件、几千行代码，出了错很难定位。按阶段走，每阶段只引入一个变化、只验证一件事，出问题立即能锁定在哪一层。这也是你一贯的节奏。

---

## 5. 地基：先补两块基础设施（阶段 1 前置）

GPRS 链路有两块地基，新工程都没有，必须先补。它们不属于 GPRS 业务，但是 GPRS 全部超时逻辑的前提。

### 5.1 补全局毫秒计数 `sys_ms`（改 `HARDWARE/TIMER/timer.c`）

原工程所有超时（`sys_ms - xxx >= timeout`）都基于一个全局毫秒计数。新工程的 TIM3 每 10ms 中断一次，正好可以顺手累加。

先在 `timer.h` 加声明：

```c
/* global millisecond counter, incremented by TIM3 ISR every 10 ms */
extern volatile uint32_t g_sys_ms;
```

在 `timer.c` 加定义与累加（找到现有 `TIM3_IRQHandler` / `s_tick_10ms` 置位处）：

```c
volatile uint32_t g_sys_ms = 0;

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        g_sys_ms += 10u;      /* 10 ms per tick -- the base of all GPRS timeouts */
        s_tick_10ms = 1;      /* existing flag, consumed by Tick_10ms_Take() */
    }
}
```

> **为什么用 10ms 累加而不是读 TIM3 计数器**：所有原工程逻辑都假设 `sys_ms` 是单调毫秒数，直接 `+=10` 最省事、最不容易出错。代价是超时精度 ±10ms，对 GPRS 的秒级超时完全够。
>
> **回绕安全**：`g_sys_ms` 是 `u32`，约 49.7 天回绕一次。原工程所有超时判断用无符号差值 `(u32)(now - then)`，回绕时依然正确——新工程保持这个写法即可。

### 5.2 USART2 乒乓暂存槽为什么必须有

原工程在 `usart2.c` 里加乒乓暂存槽，是为了对抗裸机超循环的长阻塞：DMA 是 Normal 模式，IDLE 中断收到一帧就立刻重新使能 DMA 复用同一块缓冲；如果主循环正被长操作占住（EEPROM 写、Flash 擦），第二帧会直接覆盖第一帧——**静默丢下行**。

新工程虽然没有 EEPROM 长写，但 OTA（阶段四）、大量 printf 都可能阻塞主循环。所以这个暂存槽照搬，成本很低、收益很高。实现见 §6.1。

### 5.3 统一日志函数 `GPRS_Printf()`

原工程用 `printf` 和 `MY4G_printf` 混着打日志。新工程统一成一个函数 `GPRS_Printf(const char *fmt, ...)`（内部 `vsnprintf` 到栈缓冲，再交给 `DEBUG_UART_SendString`），所有模块都调它。

> **为什么用函数而不是可变参数宏**：ARMCC C90 默认不支持 `#define LOG(...)` 这种可变参数宏（`__VA_ARGS__` 是 C99 特性）。用函数 + `va_list` 是 C90 里的标准做法，不用改 Keil 编译选项。实现见阶段一的 `gprs_port.c`（§6.2）。

### 5.4 设备身份常量（`gprs_config.h`）

```c
/* device identity -- replace with EEPROM/flash read in a later phase */
#define GPRS_PRODUCT_KEY   "your_product_key"
#define GPRS_DEVICE_KEY    "your_device_key"
#define GPRS_CLIENT_ID     "your_client_id"
```

> 这三串是 MQTT 主题和信封的组成部分，必须和平台侧配置一致。阶段一先用常量占位；将来加 EEPROM 时把这三个宏换成读取函数即可，不影响其它代码。

---

## 6. 阶段一：物理通道 + 模块上线

**目标**：4G 模块能被正确复位、能看到 LINKA 注网、能发三条 AT（ICCID / MQTTCID / CSQ）并收到回执、状态机走到 NORMAL。这一阶段**不碰 JSON、不碰 cJSON、不接云平台业务**——只要 AT 通了就算成功。

**新增文件**：`gprs_config.h`、`gprs_port.h/.c`、`gprs_4g.h/.c`、`uart_queue.h/.c`、`gprs_at.h/.c`。

### 6.1 `gprs_config.h`（全模块共用配置）

把散落的宏集中到一个头文件，后面所有模块都 include 它。

```c
#ifndef __GPRS_CONFIG_H
#define __GPRS_CONFIG_H

#include "stm32f10x.h"
#include "sys.h"

/* ---- device identity (replace with persistent storage later) ---- */
#define GPRS_PRODUCT_KEY   "your_product_key"
#define GPRS_DEVICE_KEY    "your_device_key"
#define GPRS_CLIENT_ID     "your_client_id"

/* ---- MQTT topic slots (module MQTTPUBTP/MQTTSUBTP indices) ---- */
#define TOPIC_SUB           1u
#define TOPIC_PUB           1u
#define TOPIC_PROPERTY_PUB  3u
#define TOPIC_EVENT_PUB     4u
#define TOPIC_SERVICE_REPLY 6u
#define TOPIC_REQUEST_INVOKE 8u

/* ---- AT commands sent to the 4G module ---- */
#define GPRS_AT_ICCID      "usr.cn#AT+ICCID?\r\n"
#define GPRS_AT_ICCID_LEN 18u
#define GPRS_AT_MQTTCID    "usr.cn#AT+MQTTCID?\r\n"
#define GPRS_AT_MQTTCID_LEN 20u
#define GPRS_AT_CSQ        "usr.cn#AT+CSQ?\r\n"
#define GPRS_AT_CSQ_LEN    16u

/* ---- log helper: implemented in gprs_port.c (C90-safe, no variadic macro) ---- */
void GPRS_Printf(const char *fmt, ...);

#endif
```

### 6.2 `gprs_port.h/.c` —— USART2 + DMA + IDLE + 乒乓暂存槽

这是原工程 `usart2.c` 里 **GPRS 相关部分**的搬家。PA2=TX、PA3=RX、DMA1_Channel6、9600 8N1、IDLE 中断。日志函数 `GPRS_Printf` 也放这里。

`gprs_port.h`：

```c
#ifndef __GPRS_PORT_H
#define __GPRS_PORT_H

#include "gprs_config.h"

#define GPRS_RX_STAGE_SLOTS 2u
#define GPRS_DMA_BUF_LEN    512u

/* banner from module after power-on/reset */
extern volatile uint8_t g_gprs_banner_seen;

void GPRS_PORT_Init(uint32_t baud);
void GPRS_PORT_Send(const char *buf, uint16_t len);

/* take one staged frame; returns 1 and fills *buf/*len if available */
uint8_t GPRS_PORT_RxStageTake(char **buf, uint16_t *len);

/* length-bounded substring search (DMA buffer is not NUL-terminated) */
uint8_t GPRS_PORT_BufContains(const char *buf, uint16_t len, const char *needle);

#endif
```

`gprs_port.c`：

```c
#include "gprs_port.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "debug_uart.h"

static char s_dma_buf[GPRS_DMA_BUF_LEN];
static char s_stage[GPRS_RX_STAGE_SLOTS][GPRS_DMA_BUF_LEN];
static volatile uint16_t s_stage_len[GPRS_RX_STAGE_SLOTS];
static volatile uint8_t  s_stage_full[GPRS_RX_STAGE_SLOTS];
static volatile uint8_t  s_stage_write = 0u;
volatile uint8_t g_gprs_banner_seen = 0u;

/* C90-safe printf wrapper: format into a stack buffer, then send via DEBUG_UART */
void GPRS_Printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DEBUG_UART_SendString(buf);
}

uint8_t GPRS_PORT_BufContains(const char *buf, uint16_t len, const char *needle)
{
    uint16_t nlen = (uint16_t)strlen(needle);
    uint16_t i;
    if ((buf == NULL) || (needle == NULL) || (len < nlen) || (nlen == 0u))
        return 0u;
    for (i = 0u; i <= (uint16_t)(len - nlen); i++)
    {
        if (memcmp(buf + i, needle, nlen) == 0)
            return 1u;
    }
    return 0u;
}

void GPRS_PORT_Init(uint32_t baud)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;
    DMA_InitTypeDef dma;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PA2 TX */
    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
    /* PA3 RX */
    gpio.GPIO_Pin = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_DeInit(USART2);
    usart.USART_BaudRate = baud;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart);

    nvic.NVIC_IRQChannel = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 1u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);
    USART_Cmd(USART2, ENABLE);

    DMA_DeInit(DMA1_Channel6);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)s_dma_buf;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = GPRS_DMA_BUF_LEN;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_Medium;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &dma);
    DMA_Cmd(DMA1_Channel6, ENABLE);
}

/* restart DMA after an IDLE frame (Normal mode) */
static void s_dma_restart(void)
{
    DMA_Cmd(DMA1_Channel6, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel6, GPRS_DMA_BUF_LEN);
    DMA_Cmd(DMA1_Channel6, ENABLE);
}

void USART2_IRQHandler(void)
{
    uint16_t len;
    uint8_t slot;

    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        /* clear IDLE: read SR then DR (stm32f10x idiom) */
        (void)USART2->SR;
        (void)USART2->DR;

        DMA_Cmd(DMA1_Channel6, DISABLE);
        len = GPRS_DMA_BUF_LEN - DMA_GetCurrDataCounter(DMA1_Channel6);

        slot = s_stage_write;
        if (!s_stage_full[slot] && (len > 0u))
        {
            memcpy(s_stage[slot], s_dma_buf, len);
            s_stage_len[slot] = len;
            s_stage_full[slot] = 1u;
            s_stage_write = (uint8_t)((slot + 1u) % GPRS_RX_STAGE_SLOTS);

            /* detect module banner in the ISR (length-bounded, no strstr) */
            if (GPRS_PORT_BufContains(s_stage[slot], len, "WH-LTE-7S1"))
                g_gprs_banner_seen = 1u;
        }
        s_dma_restart();
    }
}

uint8_t GPRS_PORT_RxStageTake(char **buf, uint16_t *len)
{
    uint8_t i;
    for (i = 0u; i < GPRS_RX_STAGE_SLOTS; i++)
    {
        if (s_stage_full[i])
        {
            *buf = s_stage[i];
            *len = s_stage_len[i];
            s_stage_full[i] = 0u;
            return 1u;
        }
    }
    return 0u;
}

void GPRS_PORT_Send(const char *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0u; i < len; i++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
            ;
        USART_SendData(USART2, (uint16_t)buf[i]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
        ;
}
```

**为什么 `s_stage_full[i] = 0u` 要放在最后**：这一句等于告诉 ISR"这个槽空出来了"。如果先清标志、再去取 `*buf` / `*len`，ISR 立刻就能把该槽当空槽重新 `memcpy` 下一帧，而调用方手里正指着这块内存——数据会被覆盖。所以顺序固定为：**先取出 `*buf`、`*len`，最后才清 `s_stage_full[i]`**，让"调用方已拿到数据"永远发生在"槽位重新可用"之前。`GPRS_AT_RxPump()` 本身是在 `__disable_irq()` 区间里调用它（`UartRx_Append()` 拷完才恢复），所以这是双保险：即使以后换到别的上下文调用，函数自身的顺序也是安全的。

> **与原工程的差异**：原工程中断里还维护 `Usart2_Rec_Cnt` / `UART2_RX_Complete`（给蓝牙 AT 旁路用）。新工程 USART2 归 4G 专用，**这两个标志不需要**，删掉即可。banner 检测保留——它是"模块串口已就绪"的唯一主动证据（详见文件二《GPRS重构操作建议_阶段二起.md》§12.1）。波特率这里示例写 115200，原工程 `uart2_init(bound)` 的实参要按你模块实际配置核对（常见 115200 或 9600）。

### 6.3 `gprs_4g.h/.c` —— 模块复位 + LINKA

原工程 `HARDWARE/4G/4G.c` 的搬家。引脚：**PC5 = LINKA（注网指示，边沿中断）**、**PC4 = LINKB（本版不用）**、**PA1 = RST（模块复位，高有效）**。

`gprs_4g.h`：

```c
#ifndef __GPRS_4G_H
#define __GPRS_4G_H

#include "gprs_config.h"

#define GPRS_LINKA     GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5)
#define GPRS_RST_ON()  GPIO_SetBits(GPIOA, GPIO_Pin_1)
#define GPRS_RST_OFF() GPIO_ResetBits(GPIOA, GPIO_Pin_1)

void GPRS_4G_Init(void);
void GPRS_4G_Reset(void);
uint8_t GPRS_4G_ResetAllowed(uint32_t min_interval_ms);

#endif
```

`gprs_4g.c`：

```c
#include "gprs_4g.h"
#include "gprs_config.h"
#include "delay.h"
#include "wdg.h"                 /* WDG_Feed()：复位脉冲后喂狗 */

static volatile uint32_t s_last_reset_ms = 0u;
static volatile uint8_t  s_reset_ever = 0u;

uint8_t GPRS_4G_ResetAllowed(uint32_t min_interval_ms)
{
    uint32_t elapsed;
    if (!s_reset_ever)
        return 1u;
    elapsed = (uint32_t)(g_sys_ms - s_last_reset_ms);
    return (elapsed >= min_interval_ms) ? 1u : 0u;
}

void GPRS_4G_Init(void)
{
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef exti;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_AFIO, ENABLE);

    /* PC4 / PC5: LINKA / LINKB, input pull-down */
    gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);

    /* PA1: module reset, push-pull output */
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);

    /* EXTI line 5 on PC5, both edges */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5);
    EXTI_ClearITPendingBit(EXTI_Line5);
    exti.EXTI_Line = EXTI_Line5;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel = EXTI9_5_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2u;
    nvic.NVIC_IRQChannelSubPriority = 3u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    /* power-on reset pulse (not counted in the throttle timeline) */
    GPRS_RST_ON();
    delay_ms(500);
    GPRS_RST_OFF();
    WDG_Feed();                  /* 500ms 复位脉冲后喂狗，见《看门狗重构操作建议》§3.4 */
}

void GPRS_4G_Reset(void)
{
    /* all recovery paths share this timeline; blocking ~1s, runs in main loop */
    s_last_reset_ms = g_sys_ms;
    s_reset_ever = 1u;
    GPRS_RST_ON();
    delay_ms(1000);
    GPRS_RST_OFF();
    WDG_Feed();                  /* 运行期复位 1s，复位脉冲后喂狗 */
    GPRS_4G_Init();
}

void EXTI9_5_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        /* LINKA level is read directly by the state machine's debounce;
           the ISR only needs to clear the flag -- no shared online flag. */
    }
}
```

> **与原工程的差异**：原工程在 EXTI ISR 里写全局 `gprsonlineflag` 供 LCD 显示。新工程状态机自己读 `GPRS_LINKA` 做去抖（更可靠），ISR 不需要维护在线标志。LED 在线指示阶段一先不接。

### 6.4 `uart_queue.h/.c` —— 发送队列 + 接收缓冲

这块基本照搬原工程（纯软件、无硬件耦合），只把 `Usart2_Send` 换成 `GPRS_PORT_Send`、日志换成 `GPRS_Printf`、`sys_ms` 换成 `g_sys_ms`。这里给**完整头文件**；完整 `.c` 可直接照抄原工程 `uart_queue.c`（L1–L653）后做文末那张替换表。

`uart_queue.h`（完整）：

```c
#ifndef __UART_QUEUE_H
#define __UART_QUEUE_H

#include "gprs_config.h"

typedef enum { TX_ITEM_AT, TX_ITEM_JSON } UartTxItemType;
typedef void (*UartTx_AT_Callback)(char *resp, uint16_t len);
typedef void (*UartTx_JSON_Callback)(char *random, uint16_t len);

#define UART_TX_QUEUE_SIZE    8u
#define UART_TX_AT_CMD_LEN    32u
#define UART_TX_AT_RESP_LEN   16u
#define UART_TX_AT_TIMEOUT_MS 1000u
#define UART_TX_AT_MAX_RETRY  2u
#define UART_TX_JSON_LEN      1024u
#define UART_TX_JSON_RANDOM_LEN 20u
#define UART_TX_JSON_FIRE_AND_FORGET_GAP_MS 300u
#define UART_TX_JSON_TIMEOUT_MS 5000u

#define UART_RX_RING_SIZE     3072u
#define UART_RX_QUEUE_SIZE    8u
#define UART_RX_ITEM_MAX      1024u

#define UART_TX_STATE_IDLE    0u
#define UART_TX_STATE_WAITING 1u

void UartTx_Queue_Init(void);
void UartRx_Queue_Init(void);
uint8_t UartTx_Push_AT(const char *cmd, uint8_t cmd_len, const char *resp_key,
                       UartTx_AT_Callback cb, uint16_t timeout_ms, uint8_t max_retry);
uint8_t UartTx_Push_JSON(const char *data, uint16_t len, const char *random,
                         UartTx_JSON_Callback cb, uint16_t timeout_ms, uint8_t max_retry);
void UartTx_Queue_Process(uint32_t tick_ms);
uint8_t UartTx_OnRecv(char *data, uint16_t len, uint8_t strtype);
uint8_t UartTx_IsEmpty(void);

void UartRx_Append(char *data, uint16_t len);
void UartRx_Extract(void);
uint8_t UartRx_Pop(char *out, uint16_t max_len, uint16_t *out_len);
uint8_t UartRx_IsEmpty(void);
void UartRx_PumpFromDma(uint8_t verbose);
extern volatile uint32_t g_uartRx_dropped_bytes;

/* JSON bracket extraction (stage 2) */
int extract_json_from_string(char *buf, uint16_t buf_len,
                             char **out_start, uint16_t *out_len);

#endif
```

`uart_queue.c` 照抄原文件（L1–L653，含 `find_substr` / `find_json_end_by_bracket_matching` / `extract_json_from_string` / 发送状态机 / 接收环形缓冲），然后做这四处替换：

| 原工程写法 | 替换为 |
|---|---|
| `Usart2_Send(...)` | `GPRS_PORT_Send(...)` |
| `MY4G_printf` / `printf` | `GPRS_Printf` |
| 参数 `sys_tick_ms` | 调用处传 `g_sys_ms` |
| `Uart2_RxStageTake(...)` | `GPRS_PORT_RxStageTake(...)` |

> **特别注意**：`UartTx_Push_JSON()` 开头有 `if (!gprs_is_ready()) return 0;` 这个总闸——不在 NORMAL，所有上行 JSON 静默丢弃（不缓存、不重发）。这是原工程的明确取舍，新工程照搬。所以只有进了 NORMAL，JSON 才发得出去。

### 6.5 `gprs_at.h/.c` —— 六态链路状态机

阶段一的核心。基本照搬原工程 `gprs_AT.c` 的状态机与三条 AT 回调，做三处替换：`sys_ms`→`g_sys_ms`、日志→`GPRS_Printf`、显示缓冲（`xinhao`/`ccid`/`order`）→置标志位。

`gprs_at.h`：

```c
#ifndef __GPRS_AT_H
#define __GPRS_AT_H

#include "gprs_config.h"

extern volatile uint8_t g_atflag_online;    /* module banner seen -> UART ready */
extern volatile uint8_t g_at_flag_iccid;
extern volatile uint8_t g_at_flag_mqttcid;
extern volatile uint8_t g_at_flag_csq;

void GPRS_AT_StateMachine(void);
uint8_t gprs_is_ready(void);        /* 1 = NORMAL, the global tx gate */
void GPRS_AT_OnBanner(void);
void GPRS_AT_RxPump(void);          /* move staged frames into rx queue */

#endif
```

`gprs_at.c`（状态机主体）：

```c
#include "gprs_at.h"
#include "gprs_config.h"
#include "gprs_4g.h"
#include "gprs_port.h"
#include "uart_queue.h"
#include <string.h>

#define ST_OFFLINE      1u
#define ST_AT_PUSH      2u
#define ST_AT_WAIT      3u
#define ST_NORMAL       4u
#define ST_AT_DELAY     5u
#define ST_AT_BACKOFF   6u

#define AT_MAX_RETRY          3u
#define AT_RETRY_MS           10000u
#define AT_BACKOFF_MS         (30UL * 60UL * 1000UL)
#define CSQ_PERIOD_MS         (10UL * 60UL * 1000UL)
#define LINK_DOWN_DEBOUNCE_MS 2000u
#define LINK_UP_DEBOUNCE_MS   500u
#define OFFLINE_HARD_RESET_MS 60000u
#define RESET_MIN_INTERVAL_MS (5UL * 60UL * 1000UL)

volatile uint8_t g_atflag_online = 0u;
volatile uint8_t g_at_flag_iccid = 0u;
volatile uint8_t g_at_flag_mqttcid = 0u;
volatile uint8_t g_at_flag_csq = 0u;

static uint8_t  s_state = ST_OFFLINE;
static uint8_t  s_offline_entry = 1u;
static uint8_t  s_link_down_occurred = 0u;
static uint8_t  s_retry_cnt = 0u;
static uint32_t s_fail_tick = 0u;
static uint32_t s_backoff_tick = 0u;
static uint32_t s_csq_tick = 0u;

/* LINKA debounce state */
static uint8_t  s_link_state = 0u;
static uint8_t  s_link_raw_prev = 0u;
static uint32_t s_link_change_tick = 0u;
static uint32_t s_offline_since = 0u;

void GPRS_AT_OnBanner(void)
{
    g_atflag_online = 1u;
    GPRS_Printf("[GPRS] module banner seen -> atflag_online=1\r\n");
}

/* LINKA debounce: low 2s confirms down, high 500ms confirms up */
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

/* ---- AT response callbacks ---- */
static void s_on_iccid(char *data, uint16_t len)
{
    (void)len;
    if (strstr(data, "+ICCID") != NULL)
        g_at_flag_iccid = 1u;
}
static void s_on_mqttcid(char *data, uint16_t len)
{
    (void)len;
    if (strstr(data, "+MQTTCID") != NULL)
        g_at_flag_mqttcid = 1u;
    /* device id parsing from +MQTTCID can be added here when EEPROM is available */
}
static void s_on_csq(char *data, uint16_t len)
{
    (void)len;
    if (strstr(data, "+CSQ") != NULL)
        g_at_flag_csq = 1u;
}

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

uint8_t gprs_is_ready(void)
{
    return (s_state == ST_NORMAL) ? 1u : 0u;
}

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
```

### 6.6 阶段一接线：`main.c`

初始化（放在现有 Init 序列里，`TIM3_10ms_Init()` 之前）：

```c
    GPRS_PORT_Init(115200); /* USART2 to 4G module: check your module's actual baud */
    GPRS_4G_Init();         /* module reset + LINKA EXTI: 必须在串口之后，否则模块启动 banner 会丢 */
    UartTx_Queue_Init();
    UartRx_Queue_Init();
```

> **顺序要求（易错点）**：`GPRS_PORT_Init()` 必须在 `GPRS_4G_Init()` 之前。`GPRS_4G_Init()` 会把 PA1 拉高 500 ms 再释放（经 Q3 反相后等价于 RESET 低有效），模块随即启动并打印 `WH-LTE-7S1` banner；如果此时 USART2 还没初始化，banner 就永久丢失，`atflag_online` 永远为 0，状态机卡在 State1、一条 AT 都不会发。原工程 `main.c` 同样是先 `uart2_init(115200)` 再 `GM4G_Init()`。

主循环里（每轮调用，无节拍门控——GPRS 全部靠 `g_sys_ms` 判超时）：

```c
        GPRS_AT_RxPump();                    /* 1. staged frames -> rx queue */
        /* GPRS_JSON_ParseProcess();         -- stage 2 */
        GPRS_AT_StateMachine();              /* 3. link state machine */
        UartTx_Queue_Process(g_sys_ms);      /* 5. tx state machine */
```

### 6.7 阶段一验证清单

1. Keil Build 通过：确认 `g_sys_ms` 已在 `timer.c` 定义、`EXTI9_5_IRQHandler` / `USART2_IRQHandler` 无重复定义。
2. 上电后调试口看到 `[GPRS] State1: waiting (link=0 ...)`；模块注网后 LINKA 变高 → `State1->2`。
3. 看到 `State2->3: 3 AT queued`，随后能看到模块回的 `+ICCID` / `+MQTTCID` / `+CSQ`。
4. 三条 AT 都过 → `State3->4: NORMAL`；失败 → `State3->5`（10s 重试）或 `State3->6`（30min 退避）。
5. 让 LINKA 变低 2s → `Link down confirmed`，退回 State1。

---

> **配套文件**：阶段二及以后（§7–§14）见《GPRS重构操作建议_阶段二起.md》。
