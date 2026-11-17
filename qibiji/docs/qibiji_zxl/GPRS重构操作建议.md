# GPRS（4G 上云）模块重构操作建议（qibiji_zxl）

> **起点**：`qibiji_zxl` 现在有 KEY / TIM3 / DEBUG_UART / LCD / ADC / DCMOTOR / GATEIO 几个模块，`main.c` 跑轮询 + 显示 + 电机。没有 USART2 的 4G 链路，没有 cJSON / malloc / DS1302 / 24CXX，没有全局 `armReg` 寄存器黑板，也没有全局毫秒计数 `sys_ms`。
>
> **目标**：把原工程那条真正在跑的对外链路——4G 模块内 MQTT + 私有 JSON——搬进新工程，做成一个独立模块 `HARDWARE/GPRS/`。**用新工程的 getter（`DCMOTOR_GetOpening()` / `ADC_GetCurrentMa()` 等）替代 `armReg`**，不搬全局黑板。整条链路按**四个阶段**落地，每阶段都能单独编译、单独上板验证。
>
> **怎么用这份文件**：第 1、2 节先讲这块链路在系统里干什么、原工程由哪些文件组成（很短，务必先看）；第 3 节是原工程依赖到新工程的裁剪映射表（**这是重构的核心决策**）；第 4 节是模块划分与四阶段路线图；第 5 节是必须先补的两块地基（毫秒时基 + 串口收发缓冲）；第 6～9 节是四个阶段的完整代码与改动；第 10 节改 `main.c`；第 11 节改 Keil 工程；第 12 节机制讲解；第 13 节是原工程静态发现的坑（新版怎么避开）；第 14 节是与原工程对照、约束和待办。
>
> **实施顺序**：这是一条链，**阶段有强依赖**，必须按 1→2→3→4 走。阶段一是地基（不接云也能验证 AT 通了）；阶段二打通"收命令→执行→回执"最小闭环；阶段三加保活与上报；阶段四做对时/配置/OTA。本轮不改 LCD 显示内容。

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
| ② 解：取帧、提 JSON、信封校验、type 分发、执行命令、回执入队 | `Parse_Process()` | `gprs_json.c` | 2 |
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
| `armReg.range_target` | 远程目标开度 | **新工程 DCMOTOR 无"目标开度"概念**——见下文专门说明 |
| `armReg.motor_warning_curr_upper` | 堵转电流阈值（配置写） | 本轮不搬配置写，固定为编译期常量或后续 EEPROM |
| `armReg.gate_maxheight` | 闸门最大行程（配置写） | 同上 |
| `armReg.reportT` | 上报周期（配置） | 固定常量（本版不上报周期兜底） |
| `autoFlag` | 自动校准中标志 | `DCMOTOR_IsCalibrating()` |
| `rxparagate` | 收到的开度值 | 保留为 `gprs_json.c` 内局部 `static u8` |

**`setValue` 落地要专门讲清楚**：原工程 `SettingValue()` 直接写 `armReg.range_target` 和 `sys_moter_state = MOTOR_UP/DOWN`，电机驱动层再去逼近目标。新工程 DCMOTOR 是**请求式**接口（`DCMOTOR_Request(MOTOR_UP/MOTOR_DOWN/MOTOR_STOP)`），没有"设目标开度后自动逼近"这一层。所以阶段二有两个选择：

- **方案 A（推荐，改动最小）**：`setValue` 只做"点动"语义——收到目标开度，比较 `DCMOTOR_GetOpening()`，发一次对应方向的 `DCMOTOR_Request()`；到位判断交给现有逻辑。**协议上回执仍回 success**，但要注意这与原工程"设目标后自动走到"的语义有差异，需平台侧知悉。
- **方案 B（语义对齐，改动大）**：给 DCMOTOR 增加 `DCMOTOR_SetTarget(u16)` 和目标逼近状态机。这超出本轮"只做 GPRS"的范围，**列为后续待办**，本轮不铺开。

本轮代码按**方案 A** 写，并在 `SettingValue()` 处用注释标出方案 B 的接入点。

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
            s_stage_full[i] = 0u;
            *buf = s_stage[i];
            *len = s_stage_len[i];
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

> **与原工程的差异**：原工程中断里还维护 `Usart2_Rec_Cnt` / `UART2_RX_Complete`（给蓝牙 AT 旁路用）。新工程 USART2 归 4G 专用，**这两个标志不需要**，删掉即可。banner 检测保留——它是"模块串口已就绪"的唯一主动证据（详见 §12.1）。波特率这里示例写 115200，原工程 `uart2_init(bound)` 的实参要按你模块实际配置核对（常见 115200 或 9600）。

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
}

void GPRS_4G_Reset(void)
{
    /* all recovery paths share this timeline; blocking ~1s, runs in main loop */
    s_last_reset_ms = g_sys_ms;
    s_reset_ever = 1u;
    GPRS_RST_ON();
    delay_ms(1000);
    GPRS_RST_OFF();
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
        GPRS_Printf("[GPRS] RX[%u]\r\n", (unsigned)len);
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
    GPRS_4G_Init();         /* module reset + LINKA EXTI */
    GPRS_PORT_Init(115200); /* USART2 to 4G module: check your module's actual baud */
    UartTx_Queue_Init();
    UartRx_Queue_Init();
```

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
void GPRS_ParseProcess(void);

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
    /* IWDG_Feed();  -- add if your new project enables the watchdog */
    NVIC_SystemReset();
}

void GPRS_Process(void)
{
    GPRS_RxDatas();
    GPRS_ParseProcess();
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

void GPRS_ParseProcess(void);

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
    GPRS_ReplyServicesPush(0, "success", "{\"success\": true}",
                           GPRS_RESET_REPLY_TIMEOUT_MS);
}

/* ---- parse & dispatch ---- */
void GPRS_ParseProcess(void)
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
        GPRS_ParseProcess();             /* stage 2: was commented out */
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

## 8. 阶段三：保活与上报

**目标**：加心跳探活（解决"假连接"）、开度变化/停机属性上报、事件上报。这一阶段让设备从"能收命令"变成"能主动汇报 + 能自愈"。

**新增文件**：`net_liveness.h/.c`、`property_report.h/.c`、`event_pub.h/.c`。

### 8.1 `net_liveness.h/.c` —— 心跳探活 + 假连接自愈

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

**心跳回执要接进 `Parse_Process()`**：在 `gprs_json.c` 的 `GPRS_ParseProcess()` 里，`cJSON_Parse` 之后、`GPRS_ValidateServerJson()` **之前**，插入：

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

> **首帧门槛为什么存在**：心跳一旦通了 `s_miss` 就清零，会掩盖校时失败。校时失败会让之后所有上报带错时间戳，比"晚几分钟上线"糟得多。所以顺序硬性固定：3 条 AT → 校时 → 心跳。**阶段三先保留这个门槛**（`s_wait_timesync=1`），如果暂时不做阶段四的对时，需要临时在 NORMAL 后手动调一次 `NetLiveness_OnTimeSyncOk()` 放行心跳，否则心跳永远不发——这是阶段三单独验证时的注意点。

### 8.2 `property_report.h/.c` —— 属性上报

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
    /* id = timestamp placeholder; stage 4 replaces with DS1302-derived unix ms */
    if (para_buf == NULL)
        return 0u;
    (void)snprintf(json_str, sizeof(json_str),
             "%u,{\"id\":\"%u\",\"productKey\":\"%s\",\"deviceKey\":\"%s\","
             "\"type\":\"property\",\"body\":{%s}}",
             TOPIC_PROPERTY_PUB,
             (unsigned)g_sys_ms,          /* temporary id; stage 4 uses RTC */
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
    if (!gprs_is_ready() || DCMOTOR_IsCalibrating())
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

### 8.3 `event_pub.h/.c` —— 事件上报

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

### 8.4 阶段三接线与验证

`main.c`：

```c
    /* per main-loop iteration, after GPRS_AT_StateMachine() */
    NetLiveness_Process();
    PropertyReport_Process();
```

并在 `gprs_json.c` 的 Parse 里加入 §8.1 的心跳识别和阶段四的对时分支占位。

**验证清单**：

1. 进 NORMAL 后（临时放行 timesync 门槛），调试口每 120s 出现 `[LIVE] heartbeat ...`，平台回 `{}` 后出现 `heartbeat ack ok`。
2. 断开平台（不回 `{}`），连续 3 次后 `[LIVE] recovery: hard reset 4G module`，模块重连。
3. 开闸让开度变化 ≥2%，看到 `up/property/post` 帧，`Opening` 字段与 `DCMOTOR_GetOpening()` 一致。
4. 停机时看到一次强制上报（`motor.stop` 事件 + 属性）。
5. 用 `NetLiveness_IsServerConfirmed()` 可在将来驱动 LCD 的"平台已确认在线"指示（本轮不改 LCD）。

---

## 9. 阶段四：对时 / 配置 / 版本 / OTA（按需，最后做）

**目标**：补齐对时（RTC 校准）、远程配置、版本号上报、OTA 框架。这一阶段依赖 EEPROM/Flash，**可以按需裁剪**——如果暂时不需要 OTA，只做对时 + 配置即可。

### 9.1 对时（sys.timeSync）

**作用**：从平台拿 `serverTime`（13 位毫秒），写进 RTC，让所有上报带正确时间戳。原工程写 DS1302；新工程没有 DS1302，需要一个抽象层。

`gprs_rtc.h`（抽象层）：

```c
#ifndef __GPRS_RTC_H
#define __GPRS_RTC_H

#include "gprs_config.h"

/* set RTC from unix seconds (0 = use software fallback) */
void GPRS_RTC_Set(uint32_t unix_sec);
/* get current unix seconds and ms-within-second */
uint32_t GPRS_RTC_GetUnixSec(void);
uint16_t GPRS_RTC_GetMsOfSec(void);

#endif
```

本版给**软件桩实现**（不依赖硬件 RTC）：上电基准 0 + `g_sys_ms` 偏移。将来板上若有 DS1302，把这两个函数改成读写芯片即可，对时逻辑不用动。

对时逻辑（放进 `gprs_json.c`）三条要点（照搬原工程，避开 §13 F1）：

1. **发**：进 NORMAL 热身 2s 后发一轮 `request` 帧（`requestCode="sys.timeSync"`，id 用新时间戳）。模块开 `AT+CACHEN=OFF` 不缓存上行，发早了会被丢——所以必须等 MQTT 会话建好。
2. **收**：平台回 `type="request_reply"`，`body.output.serverTime` 是 13 位毫秒。解析后 `GPRS_RTC_Set(serverTime/1000)`，然后调 `NetLiveness_OnTimeSyncOk()` 放行首帧心跳。
3. **失败处理**（**这里修掉 F1**）：`body.code != 0`（业务失败）时，也要 `NetLiveness_OnTimeSyncFail()` 并重试，否则心跳被锁死 24 小时。超时重试节奏 15s/30s/60s，每次换**新的 id**（避免平台去重吃掉重发）。

### 9.2 远程配置 / 版本号 / ReSet

- `getConfig` / `config`：原工程读写 `armReg.motor_warning_curr_upper`、`gate_maxheight` 并存 EEPROM。新工程无 EEPROM → 阶段四若要做，先用 RAM 变量 + 编译期初值，落在 `gprs_config.c` 里；持久化列为后续。
- `getFirmware`：回一个编译期版本号宏 `GPRS_FW_VERSION`，不依赖 OTA_Info。
- `ReSet`：阶段二已实现（延后复位）。

### 9.3 OTA（框架占位，可不做）

原工程 OTA 依赖 `24CXX`（存 OTA_Info）+ `STMFLASH`（写固件）+ A1/A2 双区 + 复位向量跳转，是独立的大需求。本版**不实现下载**，只保留 `setUpdateValue` 的回执框架（回 `{"updateFlag":...}`，标志位存 RAM）。真要做 OTA 时单独生成建议，不塞进本轮。

### 9.4 阶段四验证

1. 对时：平台回 `request_reply` + `serverTime` 后，调试口打印解析出的时间，`NetLiveness_IsWaitingTimeSync()` 变 0、首帧心跳发出。
2. 对时业务失败（`code!=0`）：确认会重试而不是锁死（F1 已修）。
3. `getFirmware` → 回版本号；`getConfig` → 回当前配置项。

---

## 10. 改 `main.c`（全貌核对）

把四阶段全部接上后，`main.c` 的相关部分应是这样（在 DCMotor 文档 §7.5 那版基础上叠加 GPRS）：

```c
#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"
#include "lcd_backlight.h"
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
    LCD12864_Init();
    LCD_BACKLIGHT_Init();
    LCD_PAGES_Init();
    ADC1_Init();
    GATEIO_Init();
    DCMOTOR_Init();
    DCMOTOR_SetMaxTurns(0u);
    TIM3_10ms_Init();

    /* ---- GPRS init (after TIM3, since g_sys_ms ticks from it) ---- */
    GPRS_4G_Init();
    GPRS_PORT_Init(115200);   /* verify your module baud */
    UartTx_Queue_Init();
    UartRx_Queue_Init();

    DEBUG_UART_SendString("qibiji_zxl GPRS ready\r\n");

    while (1)
    {
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
         *   GPRS_RxDatas -> GPRS_ParseProcess -> GPRS_AT_StateMachine
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

下行帧：DMA → 乒乓暂存槽 → 发送队列的接收环形缓冲 → 8 项帧队列 → `Parse_Process()` 消费。上行帧：业务层 → 发送队列（8 项）→ 一发一收状态机 → USART2。**中间只有 `Parse_Process()` 把两边连起来**（收到命令→执行→回执入队→发送）。

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
| **F1（高）** | 对时业务失败 `code!=0` 时直接 return，心跳被锁死 24h | §9.1 明确：`code!=0` 也要 `OnTimeSyncFail()` + 重试 |
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
| 时基 | 全局 `sys_ms`（TIM2） | `g_sys_ms`（TIM3 每 10ms +10，§5.1） |
| 日志 | `printf` + `MY4G_printf` | `GPRS_Printf()`（§5.3） |
| 身份 | EEPROM 读 productKey/deviceKey | 编译期常量（§5.4），将来换读取函数 |
| JSON 库 | cJSON + mymalloc | cJSON 原样拷，先用标准 malloc |
| 对时 RTC | DS1302 | `gprs_rtc.h` 软件桩（§9.1） |
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
7. **看门狗**：若新工程启用 IWDG，`GPRS_Reset_Process()` 复位前要 `IWDG_Feed()`（代码里已注释标出）。
8. **对时回执分支**：阶段四的 `request_reply` 分发尚未写进 `GPRS_ParseProcess()`，做阶段四时补。

### 14.4 配套文档

本文件与《ADC重构操作建议.md》（电流/电压 getter 来源）、《DCMotor重构操作建议.md》（开度/状态 getter、校准、§16 继电器边界）配套。《RS485重构操作建议.md》本轮**暂不实施**（用户决定），其与 GPRS 无关——GPRS 走 USART2 的 4G 模块，RS485 走 UART4，两者独立。
DOCEOF
echo done
