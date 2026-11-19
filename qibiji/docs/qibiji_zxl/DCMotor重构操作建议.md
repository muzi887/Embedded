# 直流电机（闸门）模块重构操作建议（qibiji_zxl）

> **起点**：《ADC重构操作建议.md》已完成（先做 ADC）。`qibiji_zxl` 现有 KEY / TIM3 / DEBUG_UART / LCD / ADC 五个模块，`main.c` 跑按键 + LCD 页面 + 背光 + ADC 采样。没有 `HARDWARE/DCMOTOR/`、没有 `HARDWARE/GATEIO/`、没有 `armReg`、没有限位开关 / 编码器、没有 4G。
>
> **v2 历史**：在"继电器 + 联锁 + 堵转 + 超时兜底"基础上接入限位与编码器，代码全部塞在 `DCMOTOR` 模块里。
>
> **v4**：把限位/编码器从电机模块里**拆成独立硬件层 `GATEIO`**，对齐原工程 `dc_motor.c` 与 `switch.c` 的边界。`GATEIO` 独占 PD7 / PB3 / PB4 / PB0，负责初始化、10ms 采样、消抖和编码器边沿计数；`DCMOTOR` 只消费查询结果做决策，不再直接读引脚、不再判电平和边沿。硬件选型/接线/电气匹配归到 §10，`armReg` 字段落地归到 §11。
>
> **v5（本版）**：加入**自动全行程校准**（`DCMOTOR_StartCalibration()`）——复用现有状态机，下行找下限位、上行到上限位时把全程编码器脉冲自动记为 `s_max_turns`。**只存 RAM、不碰 EEPROM**。校准逻辑集中在 §6 的代码和 §9 ⑫；§12.3 的"手动测脉冲"降级为校准失败时的备选。
>
> **目标**：把原工程 `qibiji/HARDWARE/DC_MOTOR/dc_motor.c`（决策/继电器）和 `qibiji/HARDWARE/SWITCH/switch.c`（限位 + 编码器采样）搬进来，拆成 `HARDWARE/DCMOTOR/dcmotor.c/.h` 与 `HARDWARE/GATEIO/gate_io.c/.h` 两个模块。
>
> **怎么用这份文件**：第 3、4 节是 `GATEIO` 的完整代码；第 5、6 节是 `DCMOTOR` 的完整代码；第 7 节改 `main.c`；第 8 节改 Keil 工程；第 9 节讲两模块的机制与数据流；第 10 节是限位/编码器硬件选型与接线；第 11 节是 `armReg` 逐字段落地；第 12 节上板验证；第 13 节与原工程逐项对照；第 14 节约束和**安全提醒**；第 15 节验证记录。

---

## 1. 本轮做什么、不做什么

**做**：

- **新增 `HARDWARE/GATEIO/`（硬件层）**：自己的引脚定义、自己的 `.c/.h`；负责 PD7/PB3/PB4/PB0 的 GPIO 初始化（含 JTAG 释放）、10ms 采样、限位 2 次一致消抖、编码器 A/B 双通道下降沿计数。对外只暴露 `GATEIO_IsOpenLimit()`、`GATEIO_IsCloseLimit()`、`GATEIO_GetPulseCount()`、`GATEIO_PopPulseDelta()`。
- **新增 `HARDWARE/DCMOTOR/`（决策层）**：继电器只剩 PD1（主电）/ PD3（方向）；PD4/PD5（485 侧限位控制 `V01_CTRL`/`V02_CTRL`）已划归 RS485，宏在 `rs485.h`、初始化在 `RS485_Init()`；请求/执行分离、边沿驱动；电流零点联锁、堵转断电、30s 超时；根据 `GATEIO` 的限位与脉冲增量维护开度。
- **限位语义**：上限位 PD7（全开）、下限位 PB3（全关），低电平有效；到站强制停机并把开度归一到 100% / 0%（选型与接线见 §10）。
- **编码器语义**：PB4（A）/ PB0（B），低电平有效；`GATEIO` 只数下降沿，`DCMOTOR` 按运动方向积分成 `s_count`，再换算 0–100% 开度。
- **自动全行程校准（v5 新增）**：`DCMOTOR_StartCalibration()` 复用现有状态机——先下行找下限位（`s_count=0`），再上行到上限位，把全程脉冲记为 `s_max_turns`（§9 ⑫）。结果存 RAM，掉电不保持；校准期屏蔽普通按键请求，堵转 / 联锁 / 超时任一发生即判失败。
- **`armReg` 落地**：与电机/行程相关的字段逐项映射到 `GATEIO` / `DCMOTOR` / ADC / LCD（§11）；无硬件的字段明确不做。
- 单次运行超过最长时长强制停（安全兜底，现在降级为"限位失效时的第二道保险"；校准时它兼作"找不到限位"的失败判据）。

**不搬**（明确排除，新工程没有对应硬件 / 模块）：

| 项 | 为什么不搬 |
|---|---|
| `armReg` 里的远程 / 电池 / 太阳能 / GPRS / 时间 / 报警字段 | 无对应硬件或模块；与电机/行程相关的字段已在 §11 逐项落地，其余明确不做 |
| `dcmotor_auto()` 的 EEPROM 存储 / `dcmotor_auto_stop()` 回位 | v5 已做**自动校准**本体（复用状态机、RAM 存 `s_max_turns`）；但原工程 `auto_stop()` 的"标定后再下降回全关"这段、以及写 EEPROM 掉电保持，仍不搬——前者对算开度非必需，后者无 EEPROM 模块 |
| `remoteflag` | 只是"远程改过目标"的标志位，等价语义已由 `DCMOTOR_IsTargetActive()` 表达；t3/t4 到位判断由目标闭环本体取代 |
| `EventPostMotorStart/Stop()` / `PropertyReport_SetPending()` | 没有 GPRS；停止原因用 getter 暴露，不上报 |
| `dcmotor_keycheck()` 读全局 `KeyNum` | 新工程按键走事件分发；改由 `main.c` 的 `APP_DispatchKey()` 显式调 `DCMOTOR_Request()`（限位拦截放在 Request 里，和原工程 keycheck 的限位门控等价） |
| `KeyLed_Ctl()` | 按键 LED 属于 LED 模块，不归电机 |
| `JudeLocked()` 卡转判断 | 原工程这段函数体整段被注释 = 死代码，不搬（堵转已由 ADC 电流检测覆盖） |
| `para_read()` / `paraToEeprom()` / `Eeprom_Wpara()` | 没有 24C02 / EEPROM 参数区；全行程脉冲数改为自动校准（v5）或手动设定，均只存 RAM、掉电不保持 |
| `setReg()` / `getReg()` / `setRegMul()` | 原工程只有声明、没有实现也没有调用；寄存器地址访问只是设想，不搬 |

---

## 2. 模块划分与数据流（先看懂这张图）

```
main.c
  ├─ 10ms 节拍: GATEIO_Tick()    采样 PD7/PB3/PB4/PB0，限位消抖，编码器数边沿
  ├─ 10ms 节拍: DCMOTOR_Tick()   读 GATEIO 的限位/脉冲增量，累计运行时长
  ├─ 10ms 节拍: ADC_Tick()
  ├─ 主循环:    DCMOTOR_Process()  联锁 + 堵转 + 限位拦截 + 边沿驱动继电器
  └─ 按键:      DCMOTOR_Request()  写"想去哪"

依赖方向（谁 include 谁）：
DCMOTOR ──调用──> GATEIO   只读查询：限位状态、脉冲增量
DCMOTOR ──调用──> ADC      零点联锁、堵转标志、运行通知
GATEIO  ──不依赖──> DCMOTOR / ADC
```

**为什么这样切**：

1. **对齐原工程边界**：原工程 `dc_motor.c` 管继电器和状态机，`switch.c` 管限位 + 编码器采样。新工程保留同一条缝，把 `switch.c` 的职责抽成 `GATEIO`，职责更明确。
2. **引脚与电气细节收口**：PD7/PB3 的有效电平、PB3/PB4 的 JTAG 释放、消抖次数、编码器边沿极性，全部只出现在 `gate_io.c`。现场把 NPN 接近开关换成 PNP 或机械开关，只改这一个文件，不碰电机决策。
3. **可独立验证**：短接 PD7 到 GND 就能看 `GATEIO_IsOpenLimit()` 变 1，转编码器就能看 `GATEIO_GetPulseCount()` 增，不需要给电机上电。
4. **接口最小、无环形依赖**：`GATEIO` 只暴露 4 个查询函数，不持有方向、不持有开度；`DCMOTOR` 只 include `gate_io.h` / `adc.h` 两个硬件头，不出现 PD7/PB3/PB4/PB0 的引脚宏，也不反向被 `GATEIO` 调用。

**一个关键约定**：`GATEIO` **不知道闸门在往哪走**。它只回答"限位现在通没通、又来了几个脉冲"。方向由 `DCMOTOR` 的 `s_dir` / `s_state` 解释——这是"离开限位"判断和"脉冲加减"都必须放在决策层的原因。

---

## 3. 第 1 步：新建 `HARDWARE/GATEIO/gate_io.h`

新目录 `qibiji_zxl/HARDWARE/GATEIO/` 需要你手动建。先放头文件：

```c
#ifndef __GATE_IO_H
#define __GATE_IO_H

/* ---------------------------------------------------------------------------
 * Gate hardware I/O (limit switches + rotary encoder) for qibiji_zxl
 *
 * Owns every pin that senses the gate:
 *   PD7 = IN1, fully-open limit  (active low, internal pull-up)
 *   PB3 = IN2, fully-close limit (active low, internal pull-up, JTAG TDO)
 *   PB4 = CODE1, encoder channel A (active low, JTAG nTRST)
 *   PB0 = CODE2, encoder channel B (active low, ADC input pin)
 *
 * Responsibilities: clock/GPIO init, JTAG release, 10 ms sampling, 2-sample
 *   debounce on both limits, falling-edge counting on both encoder channels.
 * Not responsible: direction, opening percentage, motor decisions. This
 *   module does not know which way the gate moves; DCMOTOR interprets that.
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"

/* ---- pin map: the only place these pins are defined ---- */
#define GATEIO_OPEN_LIMIT_PORT   GPIOD
#define GATEIO_OPEN_LIMIT_PIN    GPIO_Pin_7
#define GATEIO_OPEN_LIMIT_BIT    7u

#define GATEIO_CLOSE_LIMIT_PORT  GPIOB
#define GATEIO_CLOSE_LIMIT_PIN   GPIO_Pin_3
#define GATEIO_CLOSE_LIMIT_BIT   3u

#define GATEIO_ENC_A_PORT        GPIOB
#define GATEIO_ENC_A_PIN         GPIO_Pin_4
#define GATEIO_ENC_A_BIT         4u

#define GATEIO_ENC_B_PORT        GPIOB
#define GATEIO_ENC_B_PIN         GPIO_Pin_0
#define GATEIO_ENC_B_BIT         0u

#define GATEIO_OPEN_LIMIT_LEVEL()   PDin(GATEIO_OPEN_LIMIT_BIT)
#define GATEIO_CLOSE_LIMIT_LEVEL()  PBin(GATEIO_CLOSE_LIMIT_BIT)
#define GATEIO_ENC_A_LEVEL()        PBin(GATEIO_ENC_A_BIT)
#define GATEIO_ENC_B_LEVEL()        PBin(GATEIO_ENC_B_BIT)

/* two equal samples at 10 ms = 20..30 ms of stable level */
#define GATEIO_DEBOUNCE_TICKS    2u

void     GATEIO_Init(void);      /* call once, before DCMOTOR_Init() */
void     GATEIO_Tick(void);      /* call once per 10 ms tick */

/* Debounced "at limit" flags: 1 = limit tripped (active low on the pin). */
uint8_t  GATEIO_IsOpenLimit(void);
uint8_t  GATEIO_IsCloseLimit(void);

/* Cumulative falling edges on A and B, saturates at 65535. */
uint16_t GATEIO_GetPulseCount(void);

/* Falling edges since the previous call; reading clears the accumulator. */
uint8_t  GATEIO_PopPulseDelta(void);

#endif
```

**接口说明**：

| 函数 | 谁调、何时调 | 作用 |
|---|---|---|
| `GATEIO_Init()` | `main()` 里一次，**必须在 `DCMOTOR_Init()` 之前** | 开 GPIOB/GPIOD/AFIO 时钟、关 JTAG 留 SWD、四个脚配 IPU、用实际电平初始化消抖与边沿状态 |
| `GATEIO_Tick()` | 10ms 节拍里，**必须在 `DCMOTOR_Tick()` 之前** | 采 4 个脚；限位做 2 次一致消抖；编码器检测下降沿并累加 |
| `GATEIO_IsOpenLimit()` | 决策层 / 调试 | 消抖后的上限位，1=全开到位 |
| `GATEIO_IsCloseLimit()` | 决策层 / 调试 | 消抖后的下限位，1=全关到位 |
| `GATEIO_GetPulseCount()` | 校准 / 调试 | 上电以来累计边沿数（绝对量，不随方向减） |
| `GATEIO_PopPulseDelta()` | `DCMOTOR_Tick()` 每 10ms 一次 | 取走"上次调用以来新增的边沿数"，并清零 |

---

## 4. 第 2 步：新建 `HARDWARE/GATEIO/gate_io.c`

```c
#include "gate_io.h"

/* ===========================================================================
 * Gate hardware I/O implementation
 *
 * Ported from qibiji/HARDWARE/SWITCH/switch.c:
 *   SWITCH_Init()  -> GATEIO_Init()
 *   JudeDir()      -> limit part of GATEIO_Tick()
 *   JudeCount()    -> encoder part of GATEIO_Tick()
 *
 * Changes vs original:
 *   - limit levels are debounced (2 equal samples) before being reported;
 *   - encoder keeps a cumulative count plus a per-tick delta, so the motor
 *     layer never touches pins or decides edge polarity;
 *   - no direction / opening / motor state in this module.
 * =========================================================================== */

static uint8_t  s_open_stable;   /* stable raw level, 1 = released */
static uint8_t  s_open_cand;
static uint8_t  s_open_cnt;

static uint8_t  s_close_stable;
static uint8_t  s_close_cand;
static uint8_t  s_close_cnt;

static uint8_t  s_enc_a_prev;
static uint8_t  s_enc_b_prev;
static uint16_t s_pulse_count;
static uint8_t  s_pulse_delta;

static void GATEIO_Debounce(uint8_t raw, uint8_t *cand, uint8_t *stable,
                            uint8_t *cnt)
{
    if (raw != *cand)
    {
        *cand = raw;             /* changed: this is sample 1 of the window */
        *cnt = 1u;
        return;
    }

    if (*cnt < GATEIO_DEBOUNCE_TICKS)
    {
        (*cnt)++;
        if (*cnt >= GATEIO_DEBOUNCE_TICKS)
        {
            *stable = *cand;     /* two equal samples: commit the new level */
        }
    }
}

void GATEIO_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOD |
                           RCC_APB2Periph_AFIO, ENABLE);

    /* PB3/PB4 are shared with JTAG (TDO / nTRST). Disable JTAG but KEEP SWD
     * (PA13/PA14) so flashing and debugging still work. */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = GATEIO_OPEN_LIMIT_PIN;
    GPIO_Init(GATEIO_OPEN_LIMIT_PORT, &gpio);

    gpio.GPIO_Pin = GATEIO_CLOSE_LIMIT_PIN | GATEIO_ENC_A_PIN |
                    GATEIO_ENC_B_PIN;
    GPIO_Init(GPIOB, &gpio);

    /* seed debounce state with the real pin levels, so a low level present at
     * power-up is not reported as a fresh edge / fresh limit event */
    s_open_stable  = (GATEIO_OPEN_LIMIT_LEVEL()  != 0u) ? 1u : 0u;
    s_open_cand    = s_open_stable;
    s_open_cnt     = 0u;
    s_close_stable = (GATEIO_CLOSE_LIMIT_LEVEL() != 0u) ? 1u : 0u;
    s_close_cand   = s_close_stable;
    s_close_cnt    = 0u;

    s_enc_a_prev  = (GATEIO_ENC_A_LEVEL() != 0u) ? 1u : 0u;
    s_enc_b_prev  = (GATEIO_ENC_B_LEVEL() != 0u) ? 1u : 0u;
    s_pulse_count = 0u;
    s_pulse_delta = 0u;
}

void GATEIO_Tick(void)
{
    uint8_t cur;

    /* ---- limit switches: debounce, then expose as clean levels ---- */
    cur = (GATEIO_OPEN_LIMIT_LEVEL() != 0u) ? 1u : 0u;
    GATEIO_Debounce(cur, &s_open_cand, &s_open_stable, &s_open_cnt);

    cur = (GATEIO_CLOSE_LIMIT_LEVEL() != 0u) ? 1u : 0u;
    GATEIO_Debounce(cur, &s_close_cand, &s_close_stable, &s_close_cnt);

    /* ---- encoder: falling edge on either channel = one pulse ---- */
    cur = (GATEIO_ENC_A_LEVEL() != 0u) ? 1u : 0u;
    if ((s_enc_a_prev != 0u) && (cur == 0u))
    {
        if (s_pulse_count < 0xFFFFu)
            s_pulse_count++;
        if (s_pulse_delta < 0xFFu)
            s_pulse_delta++;
    }
    s_enc_a_prev = cur;

    cur = (GATEIO_ENC_B_LEVEL() != 0u) ? 1u : 0u;
    if ((s_enc_b_prev != 0u) && (cur == 0u))
    {
        if (s_pulse_count < 0xFFFFu)
            s_pulse_count++;
        if (s_pulse_delta < 0xFFu)
            s_pulse_delta++;
    }
    s_enc_b_prev = cur;
}

uint8_t GATEIO_IsOpenLimit(void)
{
    return (s_open_stable == 0u) ? 1u : 0u;
}

uint8_t GATEIO_IsCloseLimit(void)
{
    return (s_close_stable == 0u) ? 1u : 0u;
}

uint16_t GATEIO_GetPulseCount(void)
{
    return s_pulse_count;
}

uint8_t GATEIO_PopPulseDelta(void)
{
    uint8_t d = s_pulse_delta;

    s_pulse_delta = 0u;
    return d;
}
```

**实现要点**：

- **消抖**：只有连续 `GATEIO_DEBOUNCE_TICKS`（2）次采样都等于候选值，才更新 `stable`。一次跳变到"稳定"大约 20–30ms，对闸门限位足够，且能压住机械开关弹跳和接近开关边缘抖动。
- **编码器不做消抖**：编码器脉冲本身就是窄脉冲，再消抖会丢计数；10ms 一次边沿检测天然拒绝短于一个采样周期的毛刺。
- **上电种子**：`GATEIO_Init()` 用真实电平初始化 `s_enc_a_prev/s_enc_b_prev` 和限位稳定值，避免上电瞬间的低电平被误判成一个边沿或一次"刚到位"。
- **增量而非全局位置**：`s_pulse_count` 是绝对累计量，`s_pulse_delta` 是给决策层消费的增量。`DCMOTOR` 不需要知道绝对数，也就不必和 `GATEIO` 共享全局变量。

---

## 5. 第 3 步：新建 `HARDWARE/DCMOTOR/dcmotor.h`

```c
#ifndef __DCMOTOR_H
#define __DCMOTOR_H

/* ---------------------------------------------------------------------------
 * DC motor (gate) control module for qibiji_zxl
 *
 * Scope: relay drive (PD1 main power, PD3 direction), request/executed state
 *        separation with edge-triggered action, current-zero interlock from
 *        the ADC module, stall stop, max-run-time fallback, limit reaction
 *        and opening feedback.
 *
 * Hardware boundary: limit switches and encoder pins live in gate_io.c.
 *        This module only calls GATEIO_IsOpenLimit(), GATEIO_IsCloseLimit()
 *        and GATEIO_PopPulseDelta(); it never touches those pins directly.
 *
 * Auto calibration: DCMOTOR_StartCalibration() drives DOWN to the close limit,
 *        then UP to the open limit, and stores the full-travel pulse count in
 *        s_max_turns (RAM only, no EEPROM). Reuses the existing state machine.
 *
 * Not included (no hardware wired in qibiji_zxl): GPRS event reporting,
 *        EEPROM persistence. Opening is computed from s_max_turns, which is
 *        either auto-calibrated or set manually (DCMOTOR_SetMaxTurns()).
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"

#define MOTOR_UP     0x01u
#define MOTOR_DOWN   0x02u
#define MOTOR_STOP   0x03u

/* auto-calibration result codes (DCMOTOR_GetCalResult) */
#define CAL_RESULT_IDLE     0u
#define CAL_RESULT_RUNNING  1u
#define CAL_RESULT_OK       2u
#define CAL_RESULT_FAIL     3u

/* stop reason, exposed for debug / display only */
typedef enum
{
    MOTOR_STOP_NONE = 0,
    MOTOR_STOP_MANUAL,
    MOTOR_STOP_STALL,
    MOTOR_STOP_TIMEOUT,
    MOTOR_STOP_INTERLOCK,
    MOTOR_STOP_LIMIT_OPEN,    /* reached fully-open limit (PD7 via GATEIO) */
    MOTOR_STOP_LIMIT_CLOSE    /* reached fully-close limit (PB3 via GATEIO) */
} motor_stop_reason_t;

void                DCMOTOR_Init(void);
void                DCMOTOR_Request(uint8_t state);   /* set target state */
void                DCMOTOR_Process(void);            /* call each main-loop turn */
void                DCMOTOR_Tick(void);               /* call once per 10 ms tick */
uint8_t             DCMOTOR_GetState(void);
uint8_t             DCMOTOR_GetDir(void);
motor_stop_reason_t DCMOTOR_GetLastStopReason(void);
void                DCMOTOR_SetMaxRunMs(uint16_t ms);

/* opening feedback (limit state is queried from GATEIO directly) */
uint16_t            DCMOTOR_GetOpening(void);   /* opening 0..100 percent */
uint16_t            DCMOTOR_GetCount(void);     /* baseline-relative pulses */
void                DCMOTOR_SetMaxTurns(uint16_t turns); /* pulses for full travel */

/* auto calibration (RAM only) */
void    DCMOTOR_StartCalibration(void);   /* ignored unless STOP + ADC ready */
uint8_t DCMOTOR_IsCalibrating(void);      /* 1 while calibration owns the motor */
uint8_t DCMOTOR_GetCalResult(void);       /* CAL_RESULT_* */

/* target opening control (closed loop on encoder feedback) */
uint8_t  DCMOTOR_SetTarget(uint16_t target);  /* 1 = accepted, 0 = rejected */
void     DCMOTOR_CancelTarget(void);
uint16_t DCMOTOR_GetTarget(void);             /* 0..100 */
uint8_t  DCMOTOR_IsTargetActive(void);        /* 1 = driving to a target */

#endif
```

**接口说明**：

| 函数 | 谁调、何时调 | 作用 |
|---|---|---|
| `DCMOTOR_Init()` | `main()` 里一次，**在 `GATEIO_Init()` 之后** | 配继电器 GPIO、全断、状态清零、用 `GATEIO` 当前限位初始化标志 |
| `DCMOTOR_Request(state)` | `APP_DispatchKey()` 里，按键事件到来时 | 只写"想去哪"；**已到限位时拒绝往该方向开**（防顶死）；**同时取消未完成的目标开度**，手动优先 |
| `DCMOTOR_SetTarget(t)` | 平台 `setValue` 下发时（`GPRS_SetValue`） | 设目标开度 0–100，返回 1=接受；未校准（`s_max_turns==0`）、正在校准、越界都返回 0，调用方据此回 409 |
| `DCMOTOR_CancelTarget()` / `DCMOTOR_GetTarget()` / `DCMOTOR_IsTargetActive()` | 调试 / 显示 / 上层判断 | 取消目标、读目标值、查目标闭环是否在跑 |
| `DCMOTOR_Process()` | 主循环每轮 | 做联锁 + 收堵转 + **目标开度闭环** + 限位拦截 + 边沿执行（真正驱动继电器） |
| `DCMOTOR_Tick()` | 10ms 节拍里，**在 `GATEIO_Tick()` 之后** | 读限位/脉冲增量、维护 `s_count`/`s_opening`、累计运行时长、超时停机 |
| `DCMOTOR_GetState/GetDir/GetLastStopReason()` | 调试 / 显示 | 只读：当前状态、方向、上次停机原因 |
| `DCMOTOR_GetOpening()` | 调试 / 显示（本轮不改 LCD） | 开度百分比 0–100 |
| `DCMOTOR_GetCount()` | 校准 / 调试 | 相对基准的脉冲计数 `s_count` |
| `DCMOTOR_SetMaxTurns(turns)` | 初始化后调一次（校准成功时由模块自动调，见下） | 设全行程脉冲数；默认 0=未设定，此时中途开度显示 0，只有全开限位处显示 100 |
| `DCMOTOR_SetMaxRunMs(ms)` | 可选，初始化后 | 改最长运行时长，默认 30000ms（30s）；校准时兼作"找不到限位"的失败判据 |
| `DCMOTOR_StartCalibration()` | 设置页"校准开关"触发（见 §7.3.1） | 启动自动校准；**仅在 STOP 且 ADC 已 ready 时有效**，否则直接忽略。校准期间 `DCMOTOR_Request()` 被屏蔽 |
| `DCMOTOR_IsCalibrating()` | 调试 / 显示 | 校准是否正在进行（1=电机被校准接管） |
| `DCMOTOR_IsCalibrated()` | 调试 / 显示 | 是否已校准（`s_max_turns != 0`）；zxl 未做 EEPROM，上电为 0，校准成功后置位。供 LCD 校准状态行显示"已校准" |
| `DCMOTOR_GetCalResult()` | 校准结束后查询 | 返回 `CAL_RESULT_IDLE/RUNNING/OK/FAIL` |

> **拿到全行程脉冲数的两种方式**：① 调 `DCMOTOR_StartCalibration()` 自动跑一遍（推荐，v5）；② 照 §12.3 手动测再 `DCMOTOR_SetMaxTurns()`（校准失败时的备选）。两者最终都是写 `s_max_turns`，效果一样。

> **和 v2 的接口差异**：`DCMOTOR_IsAtOpen()/IsAtClose()` 已删除。限位状态现在只有一个权威来源——`GATEIO_IsOpenLimit()/IsCloseLimit()`；电机层不再转发一份，避免两个标志不同步。

---

## 6. 第 4 步：新建 `HARDWARE/DCMOTOR/dcmotor.c`

```c
#include "dcmotor.h"
#include "gate_io.h"
#include "adc.h"
#include "delay.h"

/* ===========================================================================
 * DC motor implementation
 *
 * Ported from qibiji/HARDWARE/DC_MOTOR/dc_motor.c (relays + state machine).
 * Limit/encoder sampling moved to gate_io.c (was SWITCH/switch.c).
 *
 * Kept:
 *  - relay sequence: set direction first, delay 5ms, then main power on
 *  - stop only drops main power (direction relay untouched)
 *  - request (state) vs executed (old) separation, action only on edge
 *  - limit switches force stop; encoder pulses adjust a baseline count
 * Removed: armReg, remote flag, key-check (keys arrive via explicit Request),
 *  GPRS events / property report, EEPROM persistence, auto-stop return travel.
 * Added: current-zero interlock + stall stop via ADC, max-run fallback,
 *  opening percentage from encoder count / full-travel pulse count,
 *  auto full-travel calibration (RAM only, reuses the existing state machine).
 * =========================================================================== */

/* ---- relay outputs (GPIOD); sensors belong to GATEIO ---- */
#define RLY_MAIN_ON()    GPIO_SetBits(GPIOD, GPIO_Pin_1)
#define RLY_MAIN_OFF()   GPIO_ResetBits(GPIOD, GPIO_Pin_1)
#define RLY_DIR_UP()     GPIO_SetBits(GPIOD, GPIO_Pin_3)
#define RLY_DIR_DOWN()   GPIO_ResetBits(GPIOD, GPIO_Pin_3)
/* PD4/PD5 (V01_CTRL / V02_CTRL) are 485-side limited-control outputs and
 * belong to the RS485 module now -- do NOT re-add them here. */

#define MAX_RUN_MS_DEFAULT  30000u   /* 30s fallback if limit switch fails */

/* internal calibration phases */
#define CAL_PHASE_IDLE        0u
#define CAL_PHASE_SEEK_CLOSE  1u
#define CAL_PHASE_SEEK_OPEN   2u

static uint8_t  s_state;        /* requested state */
static uint8_t  s_old;          /* executed state */
static uint8_t  s_dir;          /* last physical direction */
static motor_stop_reason_t s_stop_reason;
static uint16_t s_max_run_ms;
static uint32_t s_run_ms;       /* ms spent in current run */
static uint8_t  s_stall_pending;/* stall seen, to be reported on STOP edge */

/* ---- limit latch + encoder position (replaces FLAG_OPENALL/CLOSEALL) ---- */
static uint8_t  s_at_open;      /* 1 = fully-open limit latched */
static uint8_t  s_at_close;     /* 1 = fully-close limit latched */
static uint16_t s_count;        /* encoder pulses from the close baseline */
static uint16_t s_max_turns;    /* pulses for full travel, 0 = not set */
static uint16_t s_opening;      /* opening percent 0..100 */

/* ---- auto calibration state ---- */
static uint8_t  s_cal_active;   /* 1 = calibration owns the motor */
static uint8_t  s_cal_phase;    /* CAL_PHASE_* */
static uint8_t  s_cal_result;   /* CAL_RESULT_* */

/* ---- target opening ---- */
static uint16_t s_target;        /* requested opening 0..100 */
static uint8_t  s_target_active; /* 1 = target loop owns the direction */

static void DCMOTOR_RelayInit(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    /* relay outputs: PD1 main + PD3 direction only.
     * PD4/PD5 are configured by RS485_Init() (485-side limit control). */
    gpio.GPIO_Pin   = GPIO_Pin_1 | GPIO_Pin_3;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &gpio);

    RLY_MAIN_OFF();
    RLY_DIR_DOWN();
}

static void DCMOTOR_DriveUp(void)
{
    RLY_DIR_UP();
    delay_ms(5);                /* direction relay must settle before main power */
    RLY_MAIN_ON();
    s_dir = MOTOR_UP;
}

static void DCMOTOR_DriveDown(void)
{
    RLY_DIR_DOWN();
    delay_ms(5);                /* same settle time before main power */
    RLY_MAIN_ON();
    s_dir = MOTOR_DOWN;
}

static void DCMOTOR_DriveStop(void)
{
    RLY_MAIN_OFF();             /* only main power; direction relay unchanged */
}

static void DCMOTOR_UpdateOpening(void)
{
    uint16_t c;

    if (s_max_turns == 0u)
    {
        /* not calibrated yet: opening is only meaningful at the limits */
        s_opening = (s_at_open != 0u) ? 100u : 0u;
        return;
    }

    c = s_count;
    if (c > s_max_turns)
        c = s_max_turns;

    s_opening = (uint16_t)(((u32)c * 100u) / s_max_turns);
}

/* Read the debounced limit levels from GATEIO and maintain the latches.
 * GATEIO is direction-agnostic; the "leaving the limit" decision needs s_dir,
 * which is why it lives here and not in the hardware layer. */
static void DCMOTOR_UpdateLimits(void)
{
    if (GATEIO_IsOpenLimit() != 0u)
    {
        s_at_open = 1u;         /* just reached / still at fully open */
        if (s_max_turns != 0u)
            s_count = s_max_turns;
    }
    else if ((s_at_open != 0u) && (s_dir == MOTOR_DOWN))
    {
        s_at_open = 0u;         /* started moving away, downwards */
        s_count = s_max_turns;  /* resync counter to full travel */
    }

    if (GATEIO_IsCloseLimit() != 0u)
    {
        s_at_close = 1u;        /* just reached / still at fully closed */
        s_count = 0u;           /* opening baseline = 0 */
    }
    else if ((s_at_close != 0u) && (s_dir == MOTOR_UP))
    {
        s_at_close = 0u;        /* started moving away, upwards */
        s_count = 0u;           /* resync counter to bottom baseline */
    }
}

/* Consume the edge delta produced by GATEIO since the last tick. The sign
 * comes from the requested state, exactly like the original JudeCount(). */
static void DCMOTOR_ConsumePulses(void)
{
    uint8_t n;
    uint8_t i;

    n = GATEIO_PopPulseDelta();
    if ((s_at_open != 0u) || (s_at_close != 0u))
        return;

    for (i = 0u; i < n; i++)
    {
        if (s_state == MOTOR_DOWN)
        {
            if (s_count > 0u)
                s_count--;
        }
        else if (s_state == MOTOR_UP)
        {
            /* max_turns==0 means "not set yet": let it count freely so the
             * full-travel value can be measured (see section 12.3). */
            if ((s_max_turns == 0u) || (s_count < s_max_turns))
                s_count++;
        }
    }
}

/* Finish calibration: clear ownership, latch the result code.
 * Does not touch s_state - caller sets the motor state (normally STOP). */
static void s_CalFinish(uint8_t result)
{
    s_cal_active = 0u;
    s_cal_phase = CAL_PHASE_IDLE;
    s_cal_result = result;
}

void DCMOTOR_Init(void)
{
    s_state = MOTOR_STOP;
    s_old = MOTOR_STOP;
    s_dir = MOTOR_STOP;
    s_stop_reason = MOTOR_STOP_NONE;
    s_max_run_ms = MAX_RUN_MS_DEFAULT;
    s_run_ms = 0u;
    s_stall_pending = 0u;
    s_count = 0u;
    s_max_turns = 0u;
    s_opening = 0u;

    s_cal_active = 0u;
    s_cal_phase = CAL_PHASE_IDLE;
    s_cal_result = CAL_RESULT_IDLE;

    s_target = 0u;
    s_target_active = 0u;

    DCMOTOR_RelayInit();

    /* GATEIO_Init() must have run first; seed the latches so a gate parked at
     * a limit does not need a 10 ms tick before Request() starts guarding. */
    s_at_open  = GATEIO_IsOpenLimit();
    s_at_close = GATEIO_IsCloseLimit();
}

/* Start calibration only from rest with a valid ADC zero point.
 * Clears the old baseline first so the up-run can be measured freely. */
void DCMOTOR_StartCalibration(void)
{
    if (s_cal_active != 0u)
        return;
    if (s_state != MOTOR_STOP)
        return;
    if (ADC_CurrentIsReady() == 0u)
        return;

    s_count = 0u;
    s_max_turns = 0u;
    s_stop_reason = MOTOR_STOP_NONE;
    s_run_ms = 0u;
    s_cal_phase = CAL_PHASE_SEEK_CLOSE;
    s_cal_result = CAL_RESULT_RUNNING;
    s_cal_active = 1u;
    s_target_active = 0u;   /* calibration takes over the motor */
    s_state = MOTOR_DOWN;   /* internal set: bypasses Request guard */
}

uint8_t DCMOTOR_IsCalibrating(void) { return s_cal_active; }
uint8_t DCMOTOR_GetCalResult(void)  { return s_cal_result; }

void DCMOTOR_Request(uint8_t state)
{
    /* refuse to drive into an already-tripped limit (same as the original
     * keycheck's "FLAG_OPENALL != 1 / FLAG_CLOSEALL != 1" guard). Without this,
     * a held UP key at the top would keep re-issuing UP and chatter the relay. */
    if ((state == MOTOR_UP)   && (s_at_open  != 0u)) return;
    if ((state == MOTOR_DOWN) && (s_at_close != 0u)) return;

    if ((state == MOTOR_UP) || (state == MOTOR_DOWN) || (state == MOTOR_STOP))
    {
        s_target_active = 0u;   /* manual command wins over a pending target */
        s_state = state;
    }
}

/* Advance calibration at the end of Process(), after edge handling, so the
 * limit STOP already happened and s_old is synced. Phase switches set s_state
 * but not s_old, so the next Process turn fires the drive edge naturally. */
static void s_CalibrationProcess(void)
{
    if (s_cal_active == 0u)
        return;

    if ((s_cal_phase == CAL_PHASE_SEEK_CLOSE) && (s_at_close != 0u))
    {
        s_count = 0u;                    /* close limit = zero baseline */
        s_cal_phase = CAL_PHASE_SEEK_OPEN;
        s_stop_reason = MOTOR_STOP_NONE;
        s_state = MOTOR_UP;              /* reverse and start measuring */
    }
    else if ((s_cal_phase == CAL_PHASE_SEEK_OPEN) && (s_at_open != 0u))
    {
        s_max_turns = s_count;           /* full travel measured */
        s_state = MOTOR_STOP;
        s_CalFinish(CAL_RESULT_OK);
    }
}

void DCMOTOR_Process(void)
{
    /* 1) current-zero interlock: zero point not ready -> force stop */
    if (ADC_CurrentIsReady() == 0u)
    {
        s_state = MOTOR_STOP;
        s_old = MOTOR_STOP;      /* sync old: no phantom stop event on ready */
        s_stop_reason = MOTOR_STOP_INTERLOCK;
        if (s_cal_active != 0u)
            s_CalFinish(CAL_RESULT_FAIL);
        ADC_SetMotorRunning(0u);
        DCMOTOR_DriveStop();
        return;
    }

    /* 2) consume stall flag raised by ADC */
    if (ADC_PopStall() != 0u)
    {
        s_stall_pending = 1u;
        s_state = MOTOR_STOP;    /* request stop; reason set on edge below */
        if (s_cal_active != 0u)
            s_CalFinish(CAL_RESULT_FAIL);
        ADC_SetMotorRunning(0u);
        DCMOTOR_DriveStop();     /* immediate physical cut, do not wait */
    }

    /* 3) target-opening loop: choose direction from the encoder feedback.
     *    Runs before the limit interlock so a limit still wins. */
    if ((s_target_active != 0u) && (s_cal_active == 0u))
    {
        if (s_opening == s_target)
        {
            s_target_active = 0u;      /* arrived */
            s_state = MOTOR_STOP;
        }
        else if (s_opening < s_target)
        {
            if (s_state != MOTOR_UP)
                s_state = MOTOR_UP;
        }
        else
        {
            if (s_state != MOTOR_DOWN)
                s_state = MOTOR_DOWN;
        }
    }

    /* 4) limit interlock: never keep driving into a tripped limit.
     *    Reason is set here; the STOP edge below will not overwrite it
     *    (it only defaults to MANUAL when reason is still NONE). */
    if ((s_at_open != 0u) && (s_state == MOTOR_UP))
    {
        s_state = MOTOR_STOP;
        s_stop_reason = MOTOR_STOP_LIMIT_OPEN;
        s_opening = 100u;
    }
    else if ((s_at_close != 0u) && (s_state == MOTOR_DOWN))
    {
        s_state = MOTOR_STOP;
        s_stop_reason = MOTOR_STOP_LIMIT_CLOSE;
        s_opening = 0u;
    }

    /* 5) edge-triggered action */
    if (s_old != s_state)
    {
        switch (s_state)
        {
        case MOTOR_UP:
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_NONE;
            ADC_SetMotorRunning(1u);    /* freeze ADC zero tracking while running */
            DCMOTOR_DriveUp();
            break;
        case MOTOR_DOWN:
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_NONE;
            ADC_SetMotorRunning(1u);
            DCMOTOR_DriveDown();
            break;
        case MOTOR_STOP:
        default:
            ADC_SetMotorRunning(0u);    /* allow zero tracking after stop */
            if (s_stall_pending != 0u)
            {
                s_stop_reason = MOTOR_STOP_STALL;
                s_stall_pending = 0u;
            }
            else if (s_stop_reason == MOTOR_STOP_NONE)
            {
                s_stop_reason = MOTOR_STOP_MANUAL;
            }
            s_run_ms = 0u;
            s_target_active = 0u;    /* any stop cancels a pending target */
            DCMOTOR_DriveStop();
            break;
        }
        s_old = s_state;
    }

    s_CalibrationProcess();     /* advance calibration after the drive edge */
}

/* -------------------------------------------------------------------------
 * Target opening (closed loop)
 * ------------------------------------------------------------------------- */

uint8_t DCMOTOR_SetTarget(uint16_t target)
{
    if (target > 100u)
        return 0u;
    if (s_cal_active != 0u)
        return 0u;
    if (s_max_turns == 0u)
        return 0u;              /* not calibrated: opening value is meaningless */

    s_target = target;
    s_target_active = 1u;
    return 1u;
}

void DCMOTOR_CancelTarget(void)      { s_target_active = 0u; }
uint16_t DCMOTOR_GetTarget(void)     { return s_target; }
uint8_t  DCMOTOR_IsTargetActive(void){ return s_target_active; }

void DCMOTOR_Tick(void)
{
    DCMOTOR_UpdateLimits();     /* GATEIO levels -> latches, baseline sync */
    DCMOTOR_ConsumePulses();    /* GATEIO edge delta -> s_count */
    DCMOTOR_UpdateOpening();    /* s_count / s_max_turns -> s_opening */

    if ((s_state == MOTOR_UP) || (s_state == MOTOR_DOWN))
    {
        s_run_ms += 10u;
        if (s_run_ms >= (u32)s_max_run_ms)
        {
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_TIMEOUT;
            s_state = MOTOR_STOP;
            if (s_cal_active != 0u)
                s_CalFinish(CAL_RESULT_FAIL);
            ADC_SetMotorRunning(0u);
            DCMOTOR_DriveStop();    /* immediate cut on timeout */
        }
    }
}

uint8_t DCMOTOR_GetState(void)  { return s_state; }
uint8_t DCMOTOR_GetDir(void)    { return s_dir; }
motor_stop_reason_t DCMOTOR_GetLastStopReason(void) { return s_stop_reason; }
void DCMOTOR_SetMaxRunMs(uint16_t ms) { s_max_run_ms = ms; }
uint16_t DCMOTOR_GetOpening(void) { return s_opening; }
uint16_t DCMOTOR_GetCount(void)   { return s_count; }

void DCMOTOR_SetMaxTurns(uint16_t turns)
{
    s_max_turns = turns;
    DCMOTOR_UpdateOpening();
}
```

> **和 host 桩验证版的差异**：`delay_ms(5)` 在 host 验证时被桩成空循环（host 没有 SysTick）；**上面文档代码用的是真机 `delay_ms(5)`**，这是对的——`main()` 里已在 `DCMOTOR_Init()` 前调了 `delay_init()`。代码块顶部已包含 `#include "delay.h"`。

> **校准期间的手动键**：当前 `DCMOTOR_Request()` 没有拦截 `s_cal_active`，校准中按上/下键会改写 `s_state`（`s_cal_active` 仍为 1，`s_CalibrationProcess()` 继续按相位推进）。若希望校准期间完全屏蔽手动键，在函数首行加 `if (s_cal_active != 0u) return;`。

> **Init 的差别**：当前 `DCMOTOR_Init()` 没有显式清零 `s_count` 与 `s_cal_active/s_cal_phase/s_cal_result`，靠静态变量零初始化兜底，与文档写法等价。

---

## 7. 第 5 步：改 `USER/main.c`

模块拆开以后，`main.c` 要**多接一条硬件层的初始化与节拍**。结构仍然很小：两次 Init、两个 Tick、一个 Process。

### 7.1 加 include（放在 `#include "adc.h"` 之后）

```c
#include "gate_io.h"
#include "dcmotor.h"
```

### 7.2 `main()` 里加 Init，顺序不能反

```c
    LCD_PAGES_Init();
    ADC1_Init();
    GATEIO_Init();                     /* hardware layer: GPIO + JTAG + seeds */
    DCMOTOR_Init();                    /* decision layer: relays off, state reset */
    DCMOTOR_SetMaxTurns(0u);           /* start uncalibrated; run calibration or measure (12.3) */
    TIM3_10ms_Init();
```

**顺序**：`GATEIO_Init()` → `DCMOTOR_Init()` → `DCMOTOR_SetMaxTurns()` → `TIM3_10ms_Init()`。`DCMOTOR_Init()` 会读 `GATEIO` 的限位状态；反过来会读到静态零值，把两个限位都当成"已到位"，上电后 UP/DOWN 全被 `Request()` 拒绝。

### 7.3 `APP_DispatchKey()` 里把按键映射成电机动作

在原函数的 `LCD_BACKLIGHT_Kick();` 之后加一段：

```c
    LCD_PAGES_Process(key);
    LCD_BACKLIGHT_Kick();

    /* only on the home page, same gating as the original PageSta==0 */
    if (LCD_PAGES_IsHome())
    {
        if (key == KEY_UP)
            DCMOTOR_Request(MOTOR_UP);
        else if (key == KEY_DOWN)
            DCMOTOR_Request(MOTOR_DOWN);
        else if (key == KEY_STOP)
            DCMOTOR_Request(MOTOR_STOP);
    }
```

**限位拦截不用在这里写**——已经放进 `DCMOTOR_Request()` 内部了（到上限位时 UP 请求被吞掉，到下限位时 DOWN 请求被吞掉），这样不管请求来自按键还是以后的 4G，都统一拦住。

**这里用到的 `LCD_PAGES_IsHome()` 已经存在**（`lcd_pages.c` / `lcd_pages.h` 里已提供），本轮不用改 LCD 模块，直接调用即可。它只判断当前页是不是 `PAGE_HOME`，不涉及任何显示逻辑。

#### 7.3.1 校准触发（v5 新增，复用 `AUTO_FLAG` 作一次性标志）

LCD 设置页第 3 行用 `LCD_PARAM_AUTO_FLAG`（0/1）作一次性触发标志：光标停该行 → STOP 进编辑 → UP 把它置 1。`main()` 每轮检测这个标志即可触发校准——不用加按键，触发语义和原工程 `autoFlag` 一致；该行的**显示**已单独改成电机真实状态（见 LCD 文档 §5.2/§5.4）。在 `DCMOTOR_Process();` 之前加：

```c
    /* calibration trigger: set-page "auto" flag acts like a one-shot */
    if (LCD_PAGES_GetParam(LCD_PARAM_AUTO_FLAG) != 0u)
    {
        LCD_PAGES_SetParam(LCD_PARAM_AUTO_FLAG, 0u);   /* consume flag */
        DCMOTOR_StartCalibration();   /* ignores call if not STOP / ADC not ready */
    }
```

**为什么先把标志清 0 再启动**：`StartCalibration()` 只在 STOP 且 ADC ready 时有效；若此刻不满足（比如电机正在跑），这次请求被忽略、标志也已清掉——避免用户以为"排上队了"结果过一会儿突然开始校准。要重发就在设置页再置一次。若你希望"排队等条件满足再校准"，可改成只在 `DCMOTOR_GetCalResult()` 返回 `OK/FAIL` 后再清标志，但那样语义更复杂，本版从简。

**注意**：`LCD_PARAM_AUTO_FLAG` 只是一次性触发标志（被这里消费后清 0），**不是**该行的显示值。设置页第 3 行现在显示电机真实状态：`DCMOTOR_IsCalibrating()` → 校准中、`DCMOTOR_IsCalibrated()`（`s_max_turns != 0`）→ 已校准、否则 → 未校准（见 LCD 文档 §5.2/§5.4）。因为状态会在无按键时变化，`main()` 每轮还要调一次 `LCD_PAGES_Refresh()`——放在本触发块之后、`DCMOTOR_Process()` 之前（见 §7.4）。

### 7.4 主循环两处：节拍里加两个 Tick，循环里加 Process

```c
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();
            ADC_Tick();
            GATEIO_Tick();          /* sample first: limits + encoder edges */
            DCMOTOR_Tick();         /* then consume: states/delta + run time */
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);

        /* calibration trigger from set-page auto flag (see 7.3.1) */
        if (LCD_PAGES_GetParam(LCD_PARAM_AUTO_FLAG) != 0u)
        {
            LCD_PAGES_SetParam(LCD_PARAM_AUTO_FLAG, 0u);
            DCMOTOR_StartCalibration();
        }

        LCD_PAGES_Refresh();        /* redraw rows whose state changes w/o a key
                                       (e.g. calibration status: 未校准/校准中/已校准) */
        DCMOTOR_Process();          /* interlock + stall + limits + relay edge */
```

**顺序不能反**：`DCMOTOR_Tick()` 读的是 `GATEIO_Tick()` 本拍刚更新的限位与增量。先跑 `DCMOTOR_Tick()` 会永远晚 10ms 拿到数据，虽然功能上勉强能跑，但时序语义就错了。

**必须放在同一个 `if (Tick_10ms_Take())` 里**：`Tick_10ms_Take()` 是消费式取标志，写两个独立 `if` 会导致第二处几乎取不到节拍。

### 7.5 改完后的 `main.c` 全貌（核对用）

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

volatile key_code_t g_last_key = KEY_NONE;

static const char * const s_key_name[] =
{
    "NONE", "UP", "DOWN", "STOP", "SET", "CANCEL"
};

static void APP_DispatchKey(key_code_t key)
{
    const char *name;

    g_last_key = key;

    if((key >= KEY_NONE) && (key <= KEY_CANCEL))
        name = s_key_name[key];
    else
        name = "UNKNOWN";

    DEBUG_UART_SendString("KEY: ");
    DEBUG_UART_SendString(name);
    DEBUG_UART_SendString("\r\n");

    LCD_PAGES_Process(key);
    LCD_BACKLIGHT_Kick();

    if (LCD_PAGES_IsHome())
    {
        if (key == KEY_UP)
            DCMOTOR_Request(MOTOR_UP);
        else if (key == KEY_DOWN)
            DCMOTOR_Request(MOTOR_DOWN);
        else if (key == KEY_STOP)
            DCMOTOR_Request(MOTOR_STOP);
    }
}

int main(void)
{
    key_code_t key;

    NVIC_Configuration();
    DEBUG_UART_Init();
    KEY_Init();
    delay_init();
    LCD12864_Init();
    LCD_BACKLIGHT_Init();
    LCD_PAGES_Init();
    ADC1_Init();
    GATEIO_Init();                  /* new: hardware layer */
    DCMOTOR_Init();                 /* new: decision layer */
    DCMOTOR_SetMaxTurns(0u);        /* new: measure full travel first */
    TIM3_10ms_Init();

    DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
    DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

    while (1)
    {
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();
            ADC_Tick();
            GATEIO_Tick();          /* new: sample first */
            DCMOTOR_Tick();         /* new: then consume */
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);

        /* new: calibration trigger from set-page auto flag */
        if (LCD_PAGES_GetParam(LCD_PARAM_AUTO_FLAG) != 0u)
        {
            LCD_PAGES_SetParam(LCD_PARAM_AUTO_FLAG, 0u);
            DCMOTOR_StartCalibration();
        }

        LCD_PAGES_Refresh();        /* new: redraw rows changing w/o a key (calibration status) */
        DCMOTOR_Process();          /* new */
    }
}
```

---

## 8. 第 6 步：改 `USER/SRTUV10.uvprojx`

**本轮新增两个目录、4 个文件**：`HARDWARE/GATEIO/gate_io.c/.h` 与 `HARDWARE/DCMOTOR/dcmotor.c/.h`。

### 8.1 IncludePath（约第 338 行）追加两个目录

```xml
<IncludePath>..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\DEBUG_UART;..\HARDWARE\KEY;..\HARDWARE\TIMER;..\HARDWARE\LCD12864;..\HARDWARE\ADC;..\HARDWARE\GATEIO;..\HARDWARE\DCMOTOR</IncludePath>
```

### 8.2 HARDWARE 组 `<Files>` 末尾追加 4 个节点

```xml
            <File>
              <FileName>gate_io.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\GATEIO\gate_io.c</FilePath>
            </File>
            <File>
              <FileName>gate_io.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\GATEIO\gate_io.h</FilePath>
            </File>
            <File>
              <FileName>dcmotor.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\DCMOTOR\dcmotor.c</FilePath>
            </File>
            <File>
              <FileName>dcmotor.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\DCMOTOR\dcmotor.h</FilePath>
            </File>
```

`stm32f10x_gpio.c`、`stm32f10x_usart.c` 工程已编入（JTAG remap 用 GPIO 库函数），不用加。改完 uvprojx 关工程重开再 Build。

---

## 9. 机制讲解与数据流

**① 请求 vs 已执行（`s_state` ≠ `s_old`）**——原工程最核心的约定：
- `DCMOTOR_Request()` 只写 `s_state`（"想去哪"）；`DCMOTOR_Process()` 比较 `s_old` 和 `s_state`，**只有不等时才驱动继电器**，然后 `s_old = s_state` 提交；
- 好处：多个来源（按键 / 堵转 / 超时 / 限位）随便写状态不会造成 GPIO 抖动，动作收敛到一处。

**② 继电器时序**：先方向、延时 5ms、再主电。这个 5ms 是硬件保护——换向继电器触点要稳定，主电通了再切方向等于带载拉弧。`DCMOTOR_DriveStop()` 只断主电、不动方向继电器，保留最后方向 `s_dir`（限位"离开判断"要用它）。

**③ 电流零点联锁**：`ADC_CurrentIsReady()==0` 时强制 STOP、断电并 `return`。既让零点能采到（电机强制静止），也保证零点不准时不许带电运行。

**④ 堵转停机**：ADC 连续 200ms 过流置标志；`ADC_PopStall()` 取到 → 立即物理断电，再走边沿把原因记成 `STALL`。

**⑤ 硬件层的采样链（`GATEIO_Tick()`）**：

```
10ms 节拍
  ├─ PD7 采样 → 2 次一致 → s_open_stable → GATEIO_IsOpenLimit()
  ├─ PB3 采样 → 2 次一致 → s_close_stable → GATEIO_IsCloseLimit()
  ├─ PB4 上次高/本次低 → s_pulse_count++ / s_pulse_delta++
  └─ PB0 上次高/本次低 → s_pulse_count++ / s_pulse_delta++
```

- **只做电平判定和边沿计数**，不判断"这次低电平是到位还是离开"；
- **消抖是逐通道独立的**：两个限位各有一套 candidate/count/stable，互不影响；
- **脉冲增量最多 2/拍**（A、B 各一个下降沿），`s_pulse_delta` 用 `uint8_t` 足够。

**⑥ 限位链路（`GATEIO` 电平 + `DCMOTOR` 方向）**：
- `DCMOTOR_UpdateLimits()` 每 10ms 读一次 `GATEIO_IsOpenLimit()/IsCloseLimit()`；
- 上限位有效 → `s_at_open=1`；无效且 `s_dir==MOTOR_DOWN`（确实在往下走）→ 清 0，同时 `s_count=s_max_turns` 把开度基准拉回满；
- 下限位有效 → `s_at_close=1`、`s_count=0`；无效且 `s_dir==MOTOR_UP` → 清 0、`s_count=0`；
- **为什么方向判断不能下沉到 GATEIO**：`GATEIO` 只看电平，若它自己清标志，传感器抖动或上电时序都会误清；方向是电机层的事实，只有它知道"这次离开是真的在走"。

**⑦ 限位停机与防顶死（两道闸）**：
- 第一道在 `DCMOTOR_Request()`：已经全开时拒绝 UP、全关时拒绝 DOWN——和原工程 keycheck 的 `FLAG_OPENALL != 1` 门控等价。按住键不放也不会反复下发启动请求。
- 第二道在 `DCMOTOR_Process()`：万一运行中到位（比如请求发出后才到站），`s_at_open && state==UP` → 强制 STOP，原因记 `LIMIT_OPEN`，开度归 100%；下限位同理记 `LIMIT_CLOSE`、开度归 0%。

**⑧ 编码器链路（`GATEIO` 数边沿，`DCMOTOR` 定方向）**：
- `GATEIO_Tick()` 只在"上次高、这次低"时给 `s_pulse_delta` 加 1，A/B 各算一个；
- `DCMOTOR_ConsumePulses()` 每 10ms 取走增量，按 `s_state` 决定加减：UP 加、DOWN 减；到限位后本拍增量直接丢弃；
- 再用 `s_opening = s_count * 100 / s_max_turns` 换算开度（用 u32 中间量防溢出）；
- **A/B 相序不参与判向**：本版沿用电机的请求方向，不比较两相先后。电机被手动盘动、断电溜车或 A/B 接反时，中途计数方向可能错；到限位仍会归基准。

**⑨ 全行程脉冲数 `s_max_turns`**：这是开度换算的分母（`s_opening = s_count*100/s_max_turns`）。v5 起可以自动标定（`DCMOTOR_StartCalibration()`，见 ⑫），也可手动设定（`DCMOTOR_SetMaxTurns()` / §12.3）。设 0 时中途开度显示 0、全开限位处仍显示 100，限位停机照常工作（限位不依赖脉冲数）。**只存 RAM，掉电不保持**——原工程写 EEPROM 那部分没搬。

**⑩ JTAG 复用脚的坑（最容易翻车的地方）**：PB3（JTAG-TDO）和 PB4（JTAG-nTRST）默认被 JTAG 占用，直接配成普通输入是读不到正确电平的。`GATEIO_Init()` 里先开 AFIO 时钟、再 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)` 关掉 JTAG——注意这是 **disable JTAG、保留 SWD**（PA13/PA14），所以 ST-Link 下载调试不受影响。别用 `GPIO_Remap_SWJ_Disable`（那个连 SWD 也关了，会造成下载困难）。

**⑪ 停机原因**（`motor_stop_reason_t`）：`NONE` / `MANUAL`（停机键）/ `STALL`（堵转）/ `TIMEOUT`（30s 兜底）/ `INTERLOCK`（零点联锁）/ `LIMIT_OPEN` / `LIMIT_CLOSE`（限位）。只在 getter 里暴露，本轮不上报 GPRS。若堵转和限位同时发生，边沿分支里堵转优先（都属合理停机）。

**⑫ 自动校准状态机（v5）**——复用现有状态机，没开新文件、没加新定时器：

```
触发：LCD 设置页"校准开关"置 1 → main.c 调 StartCalibration()
      （仅在 s_state==STOP 且 ADC ready 时生效，否则忽略；校准期 Request 被屏蔽）

  s_state=MOTOR_DOWN（内部直设，绕过 Request 限位门控）
        │  下行找下限位
        ▼
  s_at_close==1 ──> s_count=0，phase=SEEK_OPEN，s_state=MOTOR_UP
        │  上行到全开，ConsumePulses 因 max_turns==0 自由累加 s_count
        ▼
  s_at_open==1 ───> s_max_turns=s_count，CalResult=OK，停机
```

四个关键点：

1. **不新写电机驱动**：校准只写 `s_state`，真正的继电器动作仍由 `DCMOTOR_Process()` 的边沿逻辑完成——和按键、堵转、超时写的是同一个变量，动作收敛到一处（呼应 ①）。
2. **换向靠"改 state 不改 old"**：`s_CalibrationProcess()` 在 Process 末尾（边沿处理之后）执行，此时限位已触发 STOP、`s_old` 已同步。它改 `s_state=UP` 但**不动 `s_old`**，于是下一轮 Process 自然检测到边沿、执行 `DriveUp()`。这个顺序不能挪到边沿处理之前。
3. **`s_count` 复用了"未标定时自由计数"的现有逻辑**：`ConsumePulses()` 里 `if (s_max_turns==0 || s_count<s_max_turns) s_count++`——`StartCalibration()` 先把 `s_max_turns` 清 0，正好让上行脉冲无封顶地累加。
4. **失败即中止**：校准期间一旦联锁（`ADC_CurrentIsReady()==0`）、堵转（`ADC_PopStall()`）、超时（30s 找不到限位）任一发生，立即 `s_CalFinish(CAL_RESULT_FAIL)` 并停机，不写 `s_max_turns`。结果通过 `DCMOTOR_GetCalResult()` 查询。

> **和原工程 `dcmotor_auto()` 的关系**：原工程是个 6 步状态机（下降找下限位 → 上行记脉冲 → 再下降回全关），还写 EEPROM。本版砍掉"再下降回全关"（对算开度非必需）和 EEPROM（无模块），只保留"下行归零 → 上行记满"两步，故代码量小很多。

---

## 10. 限位与编码器硬件（GATEIO 层）

**归属说明**：选型、接线、电气匹配都是 `GATEIO` 的职责，所以统一放在这一节。`DCMOTOR` 的文档不再重复硬件细节，只引用 §10。

### 10.1 限位器件选型

| 类型 | 典型器件 | 输出 | 优点 | 局限 / 注意 |
|---|---|---|---|---|
| 机械行程开关 / 微动开关 | 欧姆龙 D4N、V-15 类 | 干接点（NO/NC/COM） | 便宜、无需供电、触点容量大、失效直观 | 触点弹跳（`GATEIO` 已做 2 次消抖，仍建议 RC）、机械磨损、重复精度 mm 级、需 IP65 外壳 |
| 电感式接近开关 | LJ12A3-4-Z/BX（NPN NO） | 三线 NPN/PNP，6–36V | 无触点、寿命长、抗油污、重复精度高 | 只认金属撞块；需独立供电 + 电平转换/光耦；检测距离 2–8mm、响应 1–2ms |
| 磁簧 / 霍尔 | 干簧管、A3144 | 干接点 / 开集电极 | 非接触、防水好做 | 干簧怕振动与触点弹跳；霍尔要 5V 供电和上拉 |
| 光电限位（槽型 / 对射 / 反射） | EE-SX670（槽型）、E3Z（对射） | NPN/PNP 开集电极 | 响应快（<1ms）、重复精度高、非接触 | 怕灰尘、水雾、强光；对射需两侧安装对准；要供电与电平转换 |
| 绝对值位置传感器 | SSI/RS485/模拟量 | 总线/模拟 | 免撞块、直接给位置 | 接口和成本高，本版不采用 |

**本版按"低电平有效"设计**，所以优先选 **NPN 开集电极输出（到位时拉到 GND）** 的接近/光电开关，或 **干接点对地闭合** 的机械开关——传感器只负责"拉低"，3.3V 由 MCU 侧上拉提供，接线最简单。

### 10.2 编码器类型与选型

| 类型 | 输出 | 分辨率 | 与本工程的适配性 |
|---|---|---|---|
| 增量式旋转编码器（AB 两相） | 开集电极 / 推挽 / 差分 | PPR × 1~4 | **本版采用**：PB4=A、PB0=B，下降沿计数 |
| 单相脉冲编码器 / 霍尔测速 | 单路脉冲 | 1 脉冲/转 或齿数 | 只能计数不能判向；本版靠电机指令定方向，勉强可用但不推荐 |
| 绝对值编码器（SSI/RS485/模拟） | 总线 / 模拟 | 高 | 需额外接口与协议，本版不支持 |
| 电位器 / 角度传感器 | 0–3.3V 模拟 | 连续 | 要占 ADC 通道；本工程 ADC 已用于电流采样，不采用 |

选增量式时看四个参数：**PPR（每转脉冲数）**、**输出形式**（NPN 开集电极 / 推挽 / 差分线驱动）、**供电电压**（5/12/24V）、**轴径与安装方式**。PPR 不是越大越好：10ms 轮询下轴转速上限 = **15/PPR 转/秒**（工程值，推导见 10.4），PPR=100 时约 9rpm，PPR=600 时只剩 1.5rpm。所以本版推荐 **PPR=100、编码器装在减速箱输出轴**（§10.6 的 D4/D6）；要上 200–600 PPR，得先把边沿检测改成外部中断或定时器编码器模式。

**本版的计数倍率是 2×PPR/圈，不是四倍频**：`GATEIO` 对 A、B 两相各数下降沿，一个正交周期贡献 2 个脉冲（正转加、反转减由 `DCMOTOR` 决定）。若按四倍频选型会把分辨率算高一倍，采购前先按 2×PPR 折算全行程脉冲数。

### 10.3 引脚与电气匹配

| 信号 | 引脚 | 配置 | 原工程对应 | 备注 |
|---|---|---|---|---|
| 上限位（全开） | PD7 | IPU 输入，低有效 | `UP_LIMIT = PDin(7)`（IN1） | 空闲脚，无复用冲突 |
| 下限位（全关） | PB3 | IPU 输入，低有效 | `DN_LIMIT = PBin(3)`（IN2） | **JTAG TDO**，由 `GATEIO_Init()` 关 JTAG 留 SWD |
| 编码器 A 相 | PB4 | IPU 输入，低有效 | `CODE1 = PBin(4)`（IN3） | **JTAG nTRST**，同上 |
| 编码器 B 相 | PB0 | IPU 输入，低有效 | `CODE2 = PBin(0)`（IN4） | **ADC 通道脚，非 5V 容忍** |

内部上拉按数据手册是 **30 / 40 / 50kΩ（最小/典型/最大）**，只适合"传感器把引脚拉到地"的场合。长线或干扰环境下建议**外部 4.7–10kΩ 上拉到 3.3V**，限位脚对地并 100nF（RC≈0.47–1ms，叠加在 2 次消抖上，总响应约 20–30ms）。

**引脚电气事实（已定案，不用再查）**

| 问题 | 结论 | 依据 |
|---|---|---|
| PB0 是不是 5V 容忍 | **不是**。PB0/PB1 在引脚定义表里没有 `FT` 标记，主功能是 `ADC12_IN8/ADC12_IN9`，属模拟输入脚 | STM32F103 高密度数据手册 Table 5；本工程把 PB0 当编码器 B 相 |
| PB3/PB4 能不能当普通 GPIO | 能，但**必须先关 JTAG**：PB3=JTDO、PB4=NJTRST，复位后归 JTAG 复用；两脚带 `FT` 标记 | 数据手册 Table 5；`GPIO_Remap_SWJ_JTAGDisable`（stm32f10x_gpio.h） |
| PD7 复位后默认电平 | **浮空输入**，电平由外部接线决定（GPIO 端口复位值 `CRL/CRH = 0x44444444`）；PD7 不属于调试脚/晶振脚 | RM0008 GPIO 章节 |
| 本工程里四根线的默认电平 | `GATEIO_Init()` 之后全部 `IPU`，**空闲 = 高**；限位开关或开集输出把脚拉低即"到位" | `gate_io.c` 的 `GPIO_Init(..., GPIO_Mode_IPU)` + `GATEIO_IsOpenLimit()` 判低 |
| 关 JTAG 后还能下载调试吗 | 能。只关 JTAG-DP、保留 SW-DP，PA13/PA14 仍可 SWD | 库宏注释："JTAG-DP Disabled and SW-DP Enabled" |
| 内部上拉有多强 | 30/40/50kΩ，3.3V 下约 66–110µA，只能驱动短线和开集输出 | 数据手册 I/O 静态特性 |
| 换板要改几处 | 四根线全部集中在 `gate_io.h` 的 4 个宏，换板只改这 4 行，`dcmotor.c` 不动 | 本次模块拆分的目的 |

三种常见接法的处理：

1. **干接点（机械开关 / 干簧管）**：一端接引脚、一端接 GND，闭合 = 低。长线加 RC（10k + 100nF）；`GATEIO` 已做 2 次一致消抖，机械弹跳一般能压住。
2. **NPN 三线传感器（12/24V 供电，OUT 为开集电极）**：OUT 接引脚、传感器 GND 与 MCU 共地，内部/外部上拉即可。但**先确认 OUT 关断时的耐压和漏电流**；若传感器用 24V 供电，输出关断可能带 24V，必须用光耦（PC817/EL357，高速可选 6N137）隔离，不能直连。
3. **PNP 三线传感器（到位输出高）**：与本版"低有效"相反。要么加 NPN 三极管/光耦反相，要么改 `GATEIO_IsOpenLimit()/IsCloseLimit()` 的有效电平判断（改 `gate_io.c`，不影响电机层）。

编码器输出形式对应：

- **NPN 开集电极**（最常见）：OUT 接引脚，内部/外部上拉到 3.3V，低有效，和本版代码一致。建议外部 2.2–4.7kΩ 上拉（内部 40k 太弱，长线边沿会变缓）。
- **推挽 5V**：PB4 可 5V 容忍，但 **PB0 不行**，必须分压（如 10k/20k）或光耦；两路用同一方案，避免相位差被改变。
- **差分线驱动（RS-422，A+/A−、B+/B−）**：抗干扰最好，但不能直连 MCU，需 26LS32/AM26LV32 类接收器转成单端 3.3V。

**电压红线**：

- **别混淆 PD4/PD5**：`V01_CTRL/V02_CTRL` 是 485 侧限位控制用的**输出**，归 `RS485`（宏在 `rs485.h`、`RS485_Init()` 里配成推挽并置 1），不是限位输入；限位输入只有 PD7 / PB3，归 `GATEIO`。
- **PB0 没有 5V 容忍**（见上表），5V 编码器不能直连；PD7/PB3/PB4 虽有 `FT` 标注，本设计也统一 3.3V，避免一半 5V 一半 3.3V 的混接。任何情况下都不要把 12/24V 直接接到 GPIO。
- 任何非 3.3V 传感器都建议"共地 + 光耦/电平转换"或"独立隔离电源"，24V 工业接近开关尤其如此。

### 10.4 采样率、消抖与最高脉冲频率

`GATEIO_Tick()` 每 **10ms** 执行一次：

- **限位**：2 次一致才更新，跳变响应约 20–30ms。对闸门这种大惯量机械足够，且能压住弹跳/抖动。若安全等级要求高，不要只依赖软件限位，见 10.5 的硬线安全链。
- **编码器**：不做消抖，检测"上次高、这次低"。低电平必须**至少跨过一次采样**才能被看到，高电平也必须跨过一次才能形成下一次下降沿；单通道可靠计数上限约 **50Hz（周期 20ms，高低各 10ms）**，工程上按 **≤30Hz** 留余量。
- **换算公式（可直接套）**：单通道上限 `f = 50Hz`（理论）/ `30Hz`（工程）；计数是 2×PPR/圈，所以轴转速上限 `n = f / (2×PPR)` 转/秒，即 **25/PPR（理论）、15/PPR（工程）**。例：PPR=100 → 0.15 转/秒（9rpm）；PPR=600 → 0.025 转/秒（1.5rpm）；PPR=100 配 30rpm → 50Hz，正好踩在理论上限，工程上必然丢脉冲。
- **结论（定案）**：本版 10ms 轮询只适用于 **PPR≤100 且编码器装在减速箱输出轴** 这一组合（§10.6 的 D4/D6）。要更高分辨率、或编码器必须装在电机轴（几千 rpm、边沿 kHz 级），必须把 `GATEIO` 的边沿检测改成**外部中断**（PB0/PB4 都可配 EXTI）或**定时器输入捕获 / 编码器接口模式**——这是选型阶段就要算清的，不是调参能解决的。

### 10.5 安装、失效安全与抗干扰

- **撞块位置**：限位动作点要在机械行程末端之前，留 10–20mm 越程，保证触停后不顶死；上、下限位各用一个撞块，不要共用。
- **断线失效**：本版是"低 = 到位、断线 = 高 = 未到位"，**断线时这层保护会消失**（和原工程一致）。安全等级要求高时，应把机械限位做成 **NC 常闭触点串进电机主电回路**的硬保护，软件限位只作辅助；或改成 NC 接法并反转 `GATEIO` 的有效电平。
- **冗余**：重要闸门可做双上限位（按失效安全选串联/并联），并保留"限位 + 30s 超时 + 堵转"三层保护（本版三层都有）。
- **编码器安装**：装在闸门轴/卷筒轴（低转速、少打滑）比装在电机轴（高转速、经减速箱）更利于 10ms 采样；联轴器要防打滑和回差，否则每次换向都有几个脉冲的误差。
- **编码器基准**：`DCMOTOR` 的 `s_count` 只在**下限位触发**时清 0、**上限位时**同步为 `s_max_turns`。因此**上电时闸门应在全关**（或先按 DOWN 撞一次下限位），否则 `s_count=0` 而门不在底部，开度是错的。原工程靠 EEPROM 记 `gate_kaidu`，本版没有（是否补上见 §10.6 的 D11）。
- **抗干扰**：电机是感性负载，换向和断电瞬间干扰强。编码器线用**屏蔽双绞**，屏蔽层单端接地，与电机动力线分开走线；编码器电源入口加 100nF + 10µF，信号可加 RC（4.7k + 100nF，τ≈0.47ms，仍满足 30Hz 上限）；长线或强干扰场合用**差分编码器 + 接收器**或**光耦**（6N137 类，10Mbps 足够）；继电器线圈加续流二极管/RC 吸收（硬件侧，本版代码不管）。
- **限位防护**：户外选 IP65 以上、带防尘罩的接近/光电开关；光电避开阳光直射和积水，定期擦镜头。

### 10.6 定案结论与需你拍板的清单

**已经定案（依据：芯片数据手册 / 标准库 / 本仓库与原工程代码，不再算"待确认"）**

| # | 结论 | 依据 |
|---|---|---|
| A1 | PB0/PB1 **不是** 5V 容忍脚 | 高密度数据手册引脚表无 `FT` 标记，主功能为 `ADC12_IN8/IN9` |
| A2 | PB3/PB4 是 `FT` 脚，但复位后归 JTAG（JTDO/NJTRST），必须 remap 才能当 GPIO | 数据手册 Table 5 + `GPIO_Remap_SWJ_JTAGDisable` |
| A3 | PD7 复位后浮空；四脚初始化后一律 IPU、空闲高、低有效 | GPIO 复位值 `0x44444444` + `gate_io.c` |
| A4 | 内部上拉 30/40/50kΩ，只能驱动短线/开集输出 | 数据手册 I/O 静态特性 |
| A5 | 限位消抖 2 拍 ≈ 20–30ms；编码器 10ms 轮询单通道 ≤50Hz（理论）/30Hz（工程）；计数 2×PPR/圈 | 代码 + 采样定理（见 10.4 公式） |
| A6 | "低 = 到位"是代码事实，不是假设 | `GATEIO_IsOpenLimit()` 读消抖后的低电平 |
| A7 | 四根线集中在 `gate_io.h` 的 4 个宏，换板只改 4 行 | 本次模块拆分设计 |
| A8 | 原工程 `Eeprom_Wpara()` 的分支标签（0x0004/0x0006）与实际写入字节地址（0x0008/0x000A）对不上，属标签写错；读写两端用同一字节地址，数据本身没坏 | 原工程 `reg.c` 代码 |
| A9 | `range_target` 在原工程确有写入（GPRS/串口屏）与读取（`dcmotor_ctrl()` 到位判断），不是死字段；本工程已用 `DCMOTOR_SetTarget()` 落地闭环（见 §6 目标开度） | 原工程 `reg.c` / `gprs_JSON.c` |

**需要你拍板的 12 项（每项给了推荐默认值，照着做就能开工）**

| # | 事项 | 推荐默认 | 为什么选它 |
|---|---|---|---|
| D1 | 限位器件类型 | 槽型光电（NPN 开集、NO）或微动开关（干接点对地），IP65 | 与代码"低有效"完全一致，零改动；非接触寿命长，机械开关最便宜、失效直观 |
| D2 | 限位供电 | 3.3V，与 MCU 同域 | 免电平转换/光耦；PD7 虽有 FT 标注，同域最稳。若必须 24V 供电，必须串光耦 |
| D3 | 限位常态触点 | NO（到位拉低） | 与现有代码一致，断线失效由 D10 兜底。若要求"断线即停"，改 NC 并把 `gate_io.c` 的有效电平取反（只改 1 处） |
| D4 | 编码器 PPR | 100 PPR | 2×PPR=200 计数/圈；10ms 轮询允许轴速 0.15 转/秒（9rpm），与闸门轴匹配；再高就得换中断计数 |
| D5 | 编码器输出形式/供电 | NPN 开集 + 3.3V 上拉 | PB0 非 5V 容忍，5V 推挽必须分压/光耦；开集靠上拉，天然 3.3V |
| D6 | 编码器安装位置 | 减速箱输出轴/卷筒轴，刚性联轴器 | 转速低，10ms 够采样；装电机轴是 kHz 边沿，必丢脉冲；联轴器要防打滑/回差 |
| D7 | A/B 相序 | 不敏感，默认上行时 A 超前 B | 本版只计数不判向；接反只影响中途开度单调性，限位归基准不受影响 |
| D8 | Z 相 / 四倍频 / EXTI | 都不做 | 10ms 轮询已够；四倍频会把 2×PPR 的分辨率算错一倍 |
| D9 | 外部上拉/RC | 限位 10kΩ+100nF；编码器线长 >0.5m 时 4.7kΩ+100nF | 内部 40kΩ 太弱，长线边沿缓、易受电机干扰；RC≤1ms 不影响 30Hz 上限 |
| D10 | 硬线安全链 | 建议加：NC 限位触点串进主电回路（或独立急停） | 软件消抖要 20–30ms，且 MCU 可能死机/复位，软件限位不能当安全级保护；增量成本只有一对触点 |
| D11 | EEPROM 掉电保持 | 建议做：存 `s_max_turns` + `s_count`（AT24C02 已有） | 免每次上电重新校准/回零；写入策略：停机且计数稳定、变化 ≥2 个计数、间隔 ≥5min，寿命足够 |
| D12 | 电机主供电电压 | 24V（电机允许的前提下） | 同功率电流小、线损小，3500mA 堵转阈值余量更大；固件不感知电压，只控制继电器通断 |

拍板后对应的改动面：

- 选 D3 的 NC 方案 → 只改 `gate_io.c` 里限位有效电平 1 处；
- 选 D8 的 EXTI 方案 → 重写 `GATEIO_Tick()` 的边沿部分，对外接口不变；
- 选 D11 → 在 `DCMOTOR_Init()` 之前加 EEPROM 读恢复，`s_opening` 由两个值算出，不必单独存。

---

## 11. 原工程 `armReg` 字段与本模块的落地

### 11.1 `armReg` 是什么

`armReg` 定义在 `qibiji/HARDWARE/REG/reg.h`，是**全局参数/状态结构体**（`Register armReg;`，reg.c 第 11 行），不是 STM32 外设寄存器：

- 它把电机状态、开度、限位标志、电流/电池/太阳能、GPRS、时间、报警全部塞进一个 `u16` 结构体；
- 配套一套 EEPROM 参数区（AT24C02）和 `para_read()/eepromToPara()/paraToEeprom()/Eeprom_Wpara()`；
- `reg.h` 还声明了 `setReg()/getReg()/setRegMul()`，但**没有实现、没有调用**——"按寄存器地址访问"只是设想；
- `initReg()` 整个函数体只有一句有效语句：`armReg.motor_on_off = 3`（`MOTOR_STOP`），其余是注释。

本节按"`armReg` = 这个参数结构体"来写。如果你指的是**外设寄存器映射**（GPIO/ADC/TIM 的寄存器位），那属于芯片层文档，不在本模块范围；本模块只通过标准库函数和 `PDin/PBin` 位带宏访问硬件。

### 11.2 控制、状态与行程字段

| 字段 | 含义 | 原工程谁读写 | 新工程落地 |
|---|---|---|---|
| `distance_local` | 1=远程 / 本地控制 | 无任何读写（死字段） | 不做；本工程没有远程/本地切换 |
| `motor_on_off` | 电机请求状态（`MOTOR_UP/DOWN/STOP`） | 写：`initReg()`、`dcmotor_keycheck()`、`dcmotor_auto()`、`dcmotor_ctrl()`、`dcmotor_stop()`；读：`dcmotor_keycheck()` 的停止门控、`JudeKaidu()` 的停机判断 | `s_state` + `DCMOTOR_Request()` / `DCMOTOR_GetState()`；同时删掉了与 `sys_moter_state` 的双份状态 |
| `up_flag` | 全开标志 | 无任何读写（死字段；实际用全局 `FLAG_OPENALL`） | `GATEIO_IsOpenLimit()`（消抖后的硬件事实）+ `DCMOTOR` 内部 `s_at_open` 锁存 |
| `down_flag` | 全关标志 | 无任何读写（实际用 `FLAG_CLOSEALL`） | `GATEIO_IsCloseLimit()` + `s_at_close` |
| `up_err_flag` / `down_err_flag` | 上升/下降错误 | 无任何读写 | 不做；停机原因用 `motor_stop_reason_t` 表达 |
| `range_target` | 目标开度 0–100% | 写：`gprs_JSON.c`（远程）、`HARDWARE/LCD-uart/lcd.c`（旧串口屏，写 300/0，量纲与 0–100 不一致）；读：`dcmotor_ctrl()` 的 t3/t4 到位判断 | **已落地**：`DCMOTOR_SetTarget()` + `DCMOTOR_Process()` 第 3 步闭环，到目标自动 STOP |
| `gate_maxturns` | 全行程脉冲数 | 写：`JudeDir()` 自动校准、`eepromToPara()` 从 EEPROM 恢复；读：`JudeCount()` 封顶、`ChangeReality()` 算开度、`dcmotor_ctrl()` 目标判断 | `s_max_turns`：`DCMOTOR_StartCalibration()` 自动标定（v5）或 `DCMOTOR_SetMaxTurns()` 手动设；不写 EEPROM |
| `gate_kaidu` | 开度 0–100% | 写：`ChangeReality()`、`dcmotor_ctrl()` 限位归一 0/100、`eepromToPara()` 恢复；读：`Dispaly.c`/`DispalyPara.c`、`property_report.c`、`event_pub.c`、`gprs_JSON.c` | `s_opening` + `DCMOTOR_GetOpening()` |
| `gate_kaidu_count` | 开度对应的脉冲计数（掉电保存） | 写：`JudeKaidu()` 动完存 EEPROM、`Eeprom_Wpara()` 写 0x000C；读：`eepromToPara()` 恢复成 `Num_count` | `s_count` + `DCMOTOR_GetCount()`；`GATEIO_GetPulseCount()` 是绝对累计量，两者不是同一个东西 |
| `gate_upper` / `gate_lower` | 闸门上限值 / 下限值 | 只在 `reg.c` 里被注释掉，未启用 | 不做；上下限由限位开关硬件给出 |
| `gate_range_adc` | 闸门行程 ADC 值 | 无任何读写 | 不做；本工程不用电位器测行程 |

### 11.3 参数、测量与环境字段

| 字段 | 含义 | 原工程谁读写 | 新工程落地 |
|---|---|---|---|
| `motor_warning_curr_upper` | 堵转电流阈值（mA，默认 3500） | 写：LCD 参数页、GPRS JSON、`eepromToPara()`；读：ADC 过流判断 | ADC 模块：`ADC_SetStallThresholdMa()` / `ADC_STALL_MA_DEFAULT`；LCD 的 `LCD_PARAM_STALL_010A` 用 0.1A 显示 |
| `gate_maxheight` | 闸门全开高度（mm，默认 1000） | 写：LCD 参数页、GPRS JSON、EEPROM 0x0006；读：LCD 显示 | 新工程对应 LCD 的 `LCD_PARAM_GATE_MAX`（仅 RAM，无 EEPROM）；不进 `DCMOTOR`，不参与开度计算 |
| `motor_curr` | 电机电流（mA） | 写：`adc.c`；读：`Dispaly.c`、`property_report.c` | `ADC_GetCurrentMa()`；`DCMOTOR` 不保存电流副本 |
| `bat_vol` | 电池电压 | 原工程基本未用（旧 ADC 注释、485 注释） | 新 ADC 有 `ADC_GetBatMv()`；不恢复 `armReg` 字段，也不进电机 |
| `solar_vol` | 太阳能电压 | 原工程基本未用（注释） | 新 ADC 有 `ADC_GetSolarMv()`；同上 |
| `bat_curr` / `bat_cap` / `bat_state` | 电池电流/容量/状态 | 只在 485 里整段注释 | 不做；无电池管理 |
| `solar_curr` | 太阳能电流 | 只在 485 里注释 | 不做 |
| `temperature` | 环境温度 | 只在 485 里注释 | 不做；无温度传感器 |
| `gprs_state` | GPRS 状态 | 无任何读写 | 不做；无 4G |
| `reportT` | 上报间隔（秒） | 写：`sbf20_app.c`（485 配置）；读：`timer.c` 上报节拍 | 不做；无上报链路 |

### 11.4 时间、报警与其他

| 字段 | 含义 | 原工程谁读写 | 新工程落地 |
|---|---|---|---|
| `years` / `months` / `days` / `hours` / `minutes` / `second` | RTC 时间 | 无任何读写（死字段） | 不做；新工程没有 RTC 对时 |
| `warning` | 报警码 | 无任何读写 | 不做；改用 `motor_stop_reason_t` |
| `change` | 变化标志 | 只在 `adc.c` 里注释 | 不做 |

### 11.5 EEPROM 映射（原工程有、本版不搬）

原工程把 5 个参数放在 AT24C02 参数区（`eeprom_buf[10]`，前 20 字节）的第 2–6 个字：

| 字下标 | 字节地址 | 字段 | 原工程行为 |
|---|---|---|---|
| 2 | 0x0004 | `motor_warning_curr_upper` | 上电读；旧 0.1A 编码自动迁移成 mA；LCD/GPRS 可改 |
| 3 | 0x0006 | `gate_maxheight` | 上电读；LCD/GPRS 可改 |
| 4 | 0x0008 | `gate_maxturns` | 自动校准后写入；上电读 |
| 5 | 0x000A | `gate_kaidu` | 动完写入；上电读，并用 `count*100/maxturns` 重算 |
| 6 | 0x000C | `gate_kaidu_count` | `JudeKaidu()` 每 500ms 检查一次：停机且计数连续两次不变时写入；上电恢复成 `Num_count` |

本版**没有 EEPROM**：`s_max_turns` 由自动校准（`DCMOTOR_StartCalibration()`）或手动 `DCMOTOR_SetMaxTurns()` 设定，`s_count` 上电从 0 开始。以后做掉电保持时，最少需要持久化 `s_max_turns` 与 `s_count` 两个值，并在 `DCMOTOR_Init()` 里恢复；`s_opening` 可由两者算出，不必单独存。

### 11.6 为什么不再保留全局 `armReg`

1. **读写方太散**：中断、主循环、LCD、GPRS 都能改同一个字段，出了问题很难追。新模块用 `static` 私有状态 + getter，写入收敛到 `Request()/Process()/Tick()`。
2. **约一半字段没有硬件**：电池、太阳能、温度、GPRS、时间、报警在 `qibiji_zxl` 都不存在；整结构体搬过来只是背债。
3. **双份状态已经证明会不同步**：原工程同时维护 `sys_moter_state` 和 `armReg.motor_on_off`，两处都要写。新模块只保留一份 `s_state`。
4. **参数各有归属**：堵转阈值归 ADC，高度/校准开关归 LCD，全行程脉冲数归 `DCMOTOR`——不需要一个"什么都装"的结构体。

### 11.7 本节定案与待拍板项

**定案**

1. `range_target` 在原工程不是死字段（GPRS/串口屏写、`dcmotor_ctrl()` 读），本工程用 `DCMOTOR_SetTarget()` + `DCMOTOR_Process()` 第 3 步闭环落地；手动 `DCMOTOR_Request()`、任何 STOP、校准启动都会取消未完成的目标。
2. `gate_maxheight` 归 LCD 的 `LCD_PARAM_GATE_MAX`，不参与控制；`DCMOTOR` 只认脉冲数 `s_max_turns`，两者通过 §12.3 实测换算，不强行绑定。
3. 原工程 `Eeprom_Wpara()` 的分支标签与实际字节地址不一致是**标签写错**（见 §10.6 A8），数据没有被破坏；本版不搬。
4. 是否补 EEPROM 掉电保持，见 §10.6 的 **D11**。做的话只需持久化 `s_max_turns`、`s_count`，`s_opening` 可由两者算出，不必单独存。

---

## 12. 上板验证

### 12.1 安全前置（务必先做）

**上电先别急着按 UP/DOWN。** 先确认：

1. 继电器接线正确——PD1 是主电、PD3 是方向，别接反；
2. `DCMOTOR_Init()` 后两个继电器都是断的（万用表量 PD1/PD3，或调试器看）；
3. **限位传感器电平**：用手靠近/离开 PD7、PB3 的接近开关，用 `GATEIO_IsOpenLimit()/GATEIO_IsCloseLimit()` 或调试器看 GPIOD.7 / GPIOB.3 电平是否随接近变低、离开变高。电平不对就先别装机，检查 IPU 和接线；
4. **编码器电平**：转编码器轴，看 PB4/PB0 是否高低跳变，`GATEIO_GetPulseCount()` 是否增加；
5. ADC 零点标定完成后（`ADC_CurrentIsReady()==1`，约上电 320ms）才允许电机动——代码里的联锁会挡住前 320ms。

### 12.2 预期现象

| 操作 | 预期 | 对应代码 |
|---|---|---|
| 上电 0–320ms 按 UP | 电机不动（联锁挡住） | `ADC_CurrentIsReady()==0` 分支 |
| 320ms 后首页按 UP | 开闸：PD3 先高，5ms 后 PD1 高 | `DCMOTOR_DriveUp()` |
| 运行中按 DOWN | 换向：PD3 拉低，5ms 后 PD1 重新高 | 边沿 → `DCMOTOR_DriveDown()` |
| 运行中按 STOP | 只断 PD1，PD3 保持；原因 `MANUAL` | `DCMOTOR_DriveStop()` |
| 设置页按 UP/DOWN | 电机不动（只改参数） | `LCD_PAGES_IsHome()` 门控 |
| **闸门升到顶触发 PD7** | 自动停；原因 `LIMIT_OPEN`；`GetOpening()==100` | Process 限位拦截 |
| **顶住后继续按住 UP** | 继电器不反复吸合（Request 拦截） | `DCMOTOR_Request()` 限位门控 |
| **从顶部按 DOWN** | 电机下降，离开限位后 `GATEIO_IsOpenLimit()` 变 0，`s_count` 同步为满 | `DCMOTOR_UpdateLimits()` 离开判断 |
| **闸门降到底触发 PB3** | 自动停；原因 `LIMIT_CLOSE`；`GetOpening()==0` | Process 限位拦截 |
| **转编码器（设好 max_turns 后）** | UP 时 `GetCount()` 增、`GetOpening()` 升；DOWN 时减 | `GATEIO_PopPulseDelta()` + `DCMOTOR_ConsumePulses()` |
| 人为卡住闸门使电流超阈值 200ms | 立即断电；原因 `STALL` | ADC 标志 → Process |
| 按住 UP 超过 30s（模拟限位失效） | 强制停；原因 `TIMEOUT` | `DCMOTOR_Tick()` |
| **设置页置"校准开关"=1** | 闸门先下降到下限位、再上升到上限位停机；`GetCalResult()==OK` | §12.3 方式 A + `s_CalibrationProcess()` |
| **校准途中按任意键** | 按键无效（被屏蔽），校准继续 | `DCMOTOR_Request()` 的 `s_cal_active` 门控 |
| **校准找不到限位（>30s）** | 停机；`GetCalResult()==FAIL`，`s_max_turns` 不变 | `DCMOTOR_Tick()` 超时 → `s_CalFinish(FAIL)` |

### 12.3 标定全行程脉冲数 `s_max_turns`

编码器开度要准，得先知道"从全关走到全开一共多少个脉冲"。**优先用自动校准（v5）**：

**方式 A：自动校准（推荐）**

1. 确认两个限位都正常（§12.1 第 3 步）、ADC 已 ready、电机静止；
2. LCD 设置页把"校准开关"置 1（或调 `DCMOTOR_StartCalibration()`）；
3. 观察闸门动作：**先自动下降到全关（PB3）→ 短暂停 → 再上升到全开（PD7）→ 停**；
4. 查 `DCMOTOR_GetCalResult()`：返回 `CAL_RESULT_OK` 即成功，`s_max_turns` 已自动写入，开度可直接使用；返回 `CAL_RESULT_FAIL` 说明中途堵转/联锁/超时（30s 没找到限位），**不会写** `s_max_turns`，需排查后重来；
5. 再手动跑一遍全程，看 `DCMOTOR_GetOpening()` 是否在 0–100 线性变化、到顶 100、到底 0。

> ⚠️ 校准时闸门会先**自动往下走**找下限位——确保下行路径无人和障碍物。校准期间按键被屏蔽（防误触打断），要中止只能断电。

**方式 B：手动测（校准失败时的备选）**

1. 保持 `DCMOTOR_SetMaxTurns(0u)`（未设定，计数不封顶）；
2. 手动把闸门降到全关（触发 PB3），`DCMOTOR_GetCount()` 应显示 0；
3. 按住 UP 让闸门一路升到全开（触发 PD7），读 `DCMOTOR_GetCount()`——这个数就是全行程脉冲数（限位触发那一拍可能少计 1–2 个边沿，来回多测两次取稳定值）；也可以直接读 `GATEIO_GetPulseCount()` 的增量，两者在本流程里一致；
4. 把读到的值填进 `main.c` 的 `DCMOTOR_SetMaxTurns(实际值)`，重新编译；
5. 同方式 A 第 5 步复验。

> 两种方式都只写 RAM，**掉电后 `s_max_turns` 归 0、需要重新标定或回填**——这是没搬 EEPROM 的代价。若读数不稳定（抖动多计），可先接受近似值；真要精确需给编码器加 RC 滤波，或把 `GATEIO` 的边沿检测改成"连续 N 拍确认"（会牺牲最高计数频率，本版未加，保持和原工程一致的简单下降沿检测）。

### 12.4 出问题时的排查

| 症状 | 首查 | 次查 |
|---|---|---|
| 限位/编码器读数一直是高（不触发） | `GATEIO_Init()` 是否执行了 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable)`（PB3/PB4 被 JTAG 占着） | 引脚接错、IPU 未配、传感器是 NPN/PNP 需外部上拉/下拉 |
| 限位到了不停 | `DCMOTOR_Process()` 是否在主循环每轮跑；`GATEIO_IsOpenLimit()` 是否为 1 | 传感器有效电平是不是低（本版代码固定低有效，PNP 需反相或改一处）；消抖窗口是否还没满 |
| 到顶后继电器反复吸合 | `DCMOTOR_Request()` 里的限位门控是否加了 | `s_at_open` 是否在抖动（传感器抖动超过消抖窗口） |
| 中途开度一直是 0 | `s_max_turns` 是否还是 0（未设定） | `DCMOTOR_GetCount()` 是否在增、`GATEIO_GetPulseCount()` 是否在增 |
| 开度不准 / 到顶不是 100 | §12.3 测的脉冲数是否偏小 | max_turns 与 count 是否同一方向标定 |
| 一上电继电器就吸合 | `DCMOTOR_Init()` 是否在 TIM3 启动前跑完；PD1 极性 | 继电器是高电平吸合（`PDout=1` 通电）还是低电平，按实际模块接线核对 |
| 换向打火 | `delay_ms(5)` 是否生效（`delay_init()` 顺序） | 方向继电器接线 |
| SWD 下载连不上 | 是否误用了 `GPIO_Remap_SWJ_Disable`（应保留 SWD） | 复位后重新连接 |
| 编译报 `undefined symbol ADC_*` | ADC 模块是否先做完、`adc.c` 是否进工程 | IncludePath 是否含 `..\HARDWARE\ADC` |
| 编译报 `undefined symbol GATEIO_*` | `gate_io.c` 是否加进工程 | IncludePath 是否含 `..\HARDWARE\GATEIO` |
| 置了校准开关但没反应 | 电机是否在跑 / ADC 是否 ready（`StartCalibration()` 只在 STOP+ready 时生效） | 标志是否被 main.c 及时清掉、是否真的调到 `StartCalibration()` |
| 校准报 FAIL | 下行/上行是否撞到限位（`GATEIO_IsOpenLimit/IsCloseLimit()`）；是否 30s 超时 | 限位传感器电平、编码器接线；中途是否堵转 |
| 校准 OK 但开度仍不对 | `GetCount()` 与 `s_max_turns` 是否同一方向标定 | 校准上行时编码器计数是否正常（§12.1 第 4 步） |

---

## 13. 与原工程逐项对照

| 原工程 | 原行为 | 新工程 | 差异说明 |
|---|---|---|---|
| `dc_motor.c` + `switch.c` | 一个管决策/继电器，一个管限位/编码器采样 | `DCMOTOR` + `GATEIO` 两个模块 | 边界对齐，引脚与电气细节归 `GATEIO` |
| `sys_moter_state` / `_old` | 请求 / 已执行，全局 | `s_state` / `s_old`，模块私有 | 语义一致，去掉全局广播 |
| `armReg.motor_on_off` | 与 `sys_moter_state` 双份状态 | 只保留 `s_state` | 消除不同步 |
| `dcmotor_up/down()` | PD3 方向 + delay 5ms + PD1 主电 | 同 | 时序保留（硬件保护） |
| `dcmotor_stop()` | 只断 PD1、留方向 | 同 | 一致 |
| `Dcmotor_Process()` 联锁 | `!ADC_CurrentIsReady()` → 强制 STOP | 同 | 一致，含 `s_old` 同步 |
| ADC 堵转 | ADC 直接调 `dcmotor_request_stop(STALL)` | `ADC_PopStall()` 取标志后断电 | 解耦，见 §2 |
| `dcmotor_keycheck()` 的限位门控 | `FLAG_OPENALL/CLOSEALL != 1` 才响应 UP/DOWN | `DCMOTOR_Request()` 里同样门控 | 位置变了（Request 内），语义一致 |
| 限位停机 t1/t2 | `FLAG_OPENALL/CLOSEALL` + 方向 | `s_at_open/s_at_close` + Process 拦截 | 标志私有化；停机原因细化为 LIMIT_OPEN/CLOSE |
| `switch.c` 的 `SWITCH_Init()` | `main.c` 里调用，初始化 GPIO + JTAG 释放 | `GATEIO_Init()`，`main.c` 里一次性调用 | 初始化职责不变，只是独立成模块；采样从 TIM2 中断挪到 10ms 节拍 |
| 限位检测 `JudeDir()` | TIM2 5ms 中断，`UP_LIMIT`=PD7 / `DN_LIMIT`=PB3，直接判电平 | `GATEIO_Tick()` 10ms，同引脚，2 次一致消抖 | 5ms→10ms；新增 20–30ms 消抖；方向判断移到 `DCMOTOR` |
| 编码器 `JudeCount()` | TIM2 5ms 中断，CODE1=PB4 / CODE2=PB0 下降沿，`Num_count` 增减 | `GATEIO_Tick()` 10ms 数边沿 → `DCMOTOR_ConsumePulses()` 按方向积分 | 采样 5ms→10ms；边沿计数与方向积分拆到两层 |
| `ChangeReality()` | 每次脉冲后 `Num_count*100/gate_maxturns` | `DCMOTOR_UpdateOpening()` 每 10ms 用 `s_count/s_max_turns` 算 | 合并进 Tick；去掉 `autoFlag` 分支；`s_count` 语义同 `Num_count` |
| `gate_maxturns` 自动校准 | `dcmotor_auto()` 6 步状态机跑到底标定 + EEPROM | `DCMOTOR_StartCalibration()` 两步（下行归零→上行记满），复用现有状态机 | v5 已做自动校准本体（§9 ⑫）；砍掉标定后"再下降回全关"和 EEPROM，结果只存 RAM |
| `autoFlag` 校准触发 | 全局标志触发 `dcmotor_auto()` | LCD 设置页"校准开关" `LCD_PARAM_AUTO_FLAG` → `main.c` 调 `DCMOTOR_StartCalibration()` | 触发点换成已有 LCD 参数，不改 LCD 模块 |
| `armReg` 的 `up_flag/down_flag` | 死字段，实际用 `FLAG_OPENALL/CLOSEALL` | `GATEIO_IsOpenLimit()/IsCloseLimit()` + `DCMOTOR` 锁存 | 单一权威来源 |
| `armReg.gate_kaidu` | 全局变量，多处读写 | `s_opening` + `DCMOTOR_GetOpening()` | 全局→getter |
| `JudeLocked()` 卡转 | 函数体整段注释（死代码） | 不搬 | 堵转已由 ADC 电流覆盖 |
| `EventPost*` / `PropertyReport` | GPRS 事件 + 属性上报 | 无 | 没有 4G，以后接 |
| `setReg/getReg/setRegMul` | 只有声明，无实现无调用 | 不搬 | 原工程未落地的设想 |

---

## 14. 约束与安全提醒

- **C90**（`<uC99>0</uC99>`）：变量块首声明；`gate_io.c/.h`、`dcmotor.c/.h` 注释全 ASCII。
- **`dcmotor.c` 已含 `#include "delay.h"`**：代码块顶部写好了（用了 `delay_ms(5)`），不用手动补。
- **依赖是单向的**：`dcmotor.c` include `gate_io.h` / `adc.h`；`gate_io.c` 不 include `dcmotor.h` / `adc.h`；`adc.c` 也不 include `dcmotor.h`。别加反向调用，否则又回到全局耦合。
- **初始化顺序不能反**：`GATEIO_Init()` → `DCMOTOR_Init()` → `DCMOTOR_SetMaxTurns()` → `TIM3_10ms_Init()`。`DCMOTOR_Init()` 要读 `GATEIO` 的限位种子。
- **节拍顺序不能反**：`GATEIO_Tick()` → `DCMOTOR_Tick()`，每 10ms 一次，且和 `KEY_Tick()` / `ADC_Tick()` 共用同一个 `if (Tick_10ms_Take())`。
- **不新增定时器 / 中断**：复用 TIM3 10ms 节拍，两个模块都没有 `TIMx_IRQHandler`。
- **`Process` 和 `Tick` 职责不同**：`Process()` 主循环每轮调（快速响应按键/堵转/限位停机），`Tick()` 只在 10ms 节拍里调（消费 `GATEIO` 状态/增量 + 累计时长），别合并、别互换位置。
- **⚠️ JTAG 脚复用**：PB3/PB4 默认被 JTAG 占用，必须 `GPIO_Remap_SWJ_JTAGDisable`（关 JTAG、留 SWD）。千万别用 `GPIO_Remap_SWJ_Disable`，否则 SWD 也关掉、下载会变得很麻烦。用了 remap 后 PB3/PB4 不能再当 JTAG 功能脚。
- **⚠️ 硬件电气前提见 §10.3 / §10.6**：已定案的是"四脚 IPU、空闲高、低有效"、PB0 非 5V 容忍、PB3/PB4 归 JTAG、2×PPR 计数、10ms 下轴速上限 15/PPR 转/秒；还需要你拍板的是器件类型、供电、PPR、安装位置、安全链、EEPROM（§10.6 的 D1–D12）。接线前先把那张表定下来。
- **⚠️ 限位现在有软件消抖**：`GATEIO` 做 2 次一致确认（20–30ms）。这能压住弹跳，但也意味着**限位响应比 v2 慢 20–30ms**；对闸门够用，但不要把它当安全级急停——高安全等级要加 NC 硬线安全链（§10.5）。
- **⚠️ 编码器采样率是 10ms**：原工程 5ms 中断扫编码器，新工程只有 10ms 节拍。**若编码器脉冲宽度小于 10ms（高速旋转时可能发生），会漏脉冲导致开度偏小**。闸门是慢速机械，正常行程几十秒，一般问题不大；若发现开度异常，需改用外部中断或定时器输入捕获来数脉冲。
- **⚠️ 30s 超时现在只是第二道保险**：接了限位后，正常停机靠限位（`LIMIT_OPEN/CLOSE`）；30s 超时保留，用于限位失效（传感器坏、线断）时防止无限转烧毁。它仍不是行程控制——若你的闸门正常全行程接近/超过 30s，会在没到头时被强制停，用 `DCMOTOR_SetMaxRunMs()` 设一个比正常行程略大的值。
- **⚠️ 开度是软件估算值**：`s_opening` 由编码器脉冲 / `s_max_turns` 算出（分母可自动校准或手动设），断电重启后 `s_max_turns` 归 0、开度从 0 开始，不记忆实际闸位（原工程有 EEPROM 存 `gate_kaidu`）。若需断电保持真实开度，属后续 EEPROM 轮的工作。
- **⚠️ 首次上电位置**：`s_count` 以全关为 0 基准。上电时闸门不在全关，开度就是错的；先按 DOWN 撞一次下限位再使用，或直接跑一次自动校准。
- **⚠️ 校准会主动驱动电机**：`DCMOTOR_StartCalibration()` 触发后闸门先下行再上行，全程约一个来回。确认轨道无人无物再触发；校准期间按键被屏蔽，只能断电中止。
- **⚠️ 校准只在静止 + ADC ready 时启动**：运行途中调用会被直接忽略（且触发标志已被 main.c 清掉），不会排队。这点和原工程 `autoFlag` 可能不同——若你需要"排队执行"，见 §7.3.1 的改法。
- **⚠️ 校准结果只存 RAM**：`s_max_turns` 掉电归 0，开度要重新标定（自动或手动回填）。原工程存 EEPROM 那部分本版没搬。
- **校准调用点要在边沿处理之后**：`s_CalibrationProcess()` 必须放在 `DCMOTOR_Process()` 末尾（`s_old=s_state` 之后），靠"改 state 不改 old"让下一轮自然产生驱动边沿。挪到前面会导致换向那一步丢边沿、电机不动。

---

## 15. 验证记录

- **v4 代码为静态推导**：限位/编码器拆分成 `GATEIO` 后按用户要求未落地验证代码（不改工程源码、不在临时目录写桩测试）。逻辑对照原工程 `dc_motor.c` / `switch.c` 逐条推导，关键点已在上文标注（限位拦截防顶死、消抖后电平、编码器增量与方向积分、JTAG 释放、开度 u32 防溢出、限位停机 reason 不被边沿覆盖）。
- **v5 校准逻辑已做 host 桩验证**：在 `.workbuddy\tmp\calib_test\` 建桩（`gate_io` / `adc` / 标准库最小桩），把 §6 的校准版 `dcmotor.c`（`delay_ms(5)` 桩成空）与测试驱动一起用 `gcc -std=c90 -Wall -Wextra -pedantic` 编译——**零警告零错误**，**23 项行为断言全部通过**（RUN_EXIT=0）。覆盖：Init 后 IDLE、非 STOP / ADC 未 ready 时拒绝启动、正常两步校准（下行归零→上行记满，`s_count` 精确等于注入脉冲、到顶开度精确 100）、校准期屏蔽外部请求、堵转 / 联锁 / 超时三种失败中止、失败后可重启。这是桩验证，不是 Keil 真机编译。
- **§10 的电气事实与 §11 的字段映射已定案**：PB0 非 5V 容忍、PB3/PB4 的 JTAG 归属、PD7 复位浮空、初始化后 IPU 空闲高、内部上拉 30–50kΩ、10ms 采样上限与 2×PPR 计数、`armReg` 各字段读写方与 EEPROM 映射，全部按芯片数据手册/标准库/原工程代码核实后写进 §10.3、§10.6、§11.2–§11.7。**剩下只有 §10.6 的 D1–D12 需要你拍板**，表里已给推荐默认值与理由。
- **待你完成**：① Keil 点 Build 确认编译/链接（尤其 `GPIO_PinRemapConfig`、`PDin/PBin` 宏是否可用）；② 严格按 §12.1 安全前置上板；③ 按 §12.3 方式 A 跑自动校准确认 `GetCalResult()==OK`（失败则用方式 B 手动标定）；④ 按 §12.2 逐项验证限位停机、编码器开度、防顶死、校准触发与按键屏蔽。
- 若 Keil 报 `PDin/PBin` 未定义，确认 `sys.h`（`SYSTEM/sys`）已被 include（经 `gate_io.h` → `sys.h` 引入，位带宏在 sys.h 里）。

### 15.1 实施提醒：工程里 `gate_io.c` 与本文档的两处不一致（2026-11-17 补充）

以下两条是**实际工程文件** `qibiji_zxl/HARDWARE/GATEIO/gate_io.c` 相对本文档 §4 参考代码的偏差。本文档本身自洽（§4 的函数名与时钟配置都是对的），但按工程现状编译会出问题，实施时请对照修正：**本文档不改，改的是你工程里那份 .c**（下面给出确切改法）。

1. **函数名不一致（会导致链接失败）**：工程 `gate_io.c:52` 定义的是 `static void GATEIO_RelayInit(void)`，而 `gate_io.h:47` 声明、`main.c:77` 调用的都是 `GATEIO_Init()`。名字对不上，且 `static` 使外部无法调用 → Keil 报 `L6218E: Undefined symbol GATEIO_Init`。
   - 修法（推荐，只动一处）：把 `gate_io.c:52` 的 `static void GATEIO_RelayInit(void)` 改成 `void GATEIO_Init(void)`——去掉 `static`、名字对齐 `.h` 与 `main.c`。
   - 说明：`RelayInit`（继电器初始化）这个命名词不达意——GATEIO 管的是限位/编码器输入，没有继电器；继电器归 DCMOTOR（那边确有 `DCMOTOR_RelayInit()`）。多半是照 DCMOTOR 套命名或模板复制时带错的。

2. **漏开 AFIO 时钟（PB3/PB4 可能读不到正确电平）**：工程 `gate_io.c:56` 的时钟使能只有 `RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOB`，但第 60 行紧接着调了 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`。标准库要求重映射前先开 AFIO 时钟——本文档 §4 与 `RCC_APB2Periph_AFIO` 都在，原工程 `switch.c:34` 也开了，唯独工程这份 `.c` 漏了。
   - 修法：给第 56 行的 `RCC_APB2PeriphClockCmd(...)` 补上 `| RCC_APB2Periph_AFIO`。
   - 症状：若 PB3（下限位）/PB4（编码器 A）读数一直不对，先查这一行。

> 这两条都是工程落地时被改坏/漏改造成的，不是设计变更；逻辑与本文档 §4、§9 一致，按上面改完即可，无需改 `.h` 或 `main.c`。

---

## 16. 继电器边界与不拆决策（原《继电器重构操作建议.md》并入）

> 本节回答一个问题：做 RS485 时，继电器要不要从 `DCMOTOR` 里拆出来单独做一个模块？
>
> **结论（已拍板）**：**不拆**。继电器继续留在 `DCMOTOR`，不新建 `RLY` 层。继电器没有独立的业务含义，它只是电机决策的执行末端。本节把"边界到底在哪""为什么不拆"讲清楚；当前工程必须修的两处（`gate_io.c` 的 `GATEIO_Init` 命名与 AFIO 时钟）已在**本文档 §15.1**给出，不在此重复。
>
> **仅改工程里 `gate_io.c` 的那两行（见 §15.1），不动设计、不动其它模块，不新建任何继电器模块。**

### 16.1 继电器实际由哪些脚驱动

| 引脚 | 作用 | 有效方式 | 归属 |
|---|---|---|---|
| **PD1** | 主电继电器（给电机供电） | 高电平吸合 | DCMOTOR |
| **PD3** | 方向继电器（正/反转） | 高=开闸方向、低=关闸方向 | DCMOTOR |

> **PD4/PD5 已不在本表**：`V01_CTRL`/`V02_CTRL`（485 侧限位控制输出）原先放在 `DCMOTOR` 里，现已划归 RS485——宏定义在 `rs485.h`，初始化在 `RS485_Init()`（推挽、置 1），`DCMOTOR` 不再引用这两位。详见《RS485重构操作建议.md》§3、§6、§7。

继电器驱动宏（在 `DCMOTOR` 里，照抄原工程时序）：

```c
#define RLY_MAIN_ON()    GPIO_SetBits(GPIOD, GPIO_Pin_1)
#define RLY_MAIN_OFF()   GPIO_ResetBits(GPIOD, GPIO_Pin_1)
#define RLY_DIR_UP()     GPIO_SetBits(GPIOD, GPIO_Pin_3)
#define RLY_DIR_DOWN()   GPIO_ResetBits(GPIOD, GPIO_Pin_3)
```

初始化在 `DCMOTOR_RelayInit()`（由 `DCMOTOR_Init()` 调用），只配 PD1/PD3 两个输出、全部断开（PD4/PD5 由 `RS485_Init()` 负责）。动作时序是"先切方向 → `delay_ms(5)` → 再合主电"，这个 5ms 是换向继电器触点稳定的硬件保护，不能省（见本文档 §9 ②）。停机只断主电（PD1），保留方向继电器状态（限位"离开判断"要用）。

### 16.2 最容易搞混的：GPIOD 上这几个"邻居"各归谁

PD1~PD6 都在 GPIOD，但分属三个不同职责。**这张表是边界的全部**：

| 引脚 | 名字 | 方向 | 归属模块 | 一句话作用 |
|---|---|---|---|---|
| PD1 | 主电继电器 | 输出 | DCMOTOR | 给电机供电的总开关 |
| PD3 | 方向继电器 | 输出 | DCMOTOR | 决定电机正反转 |
| PD4 | V01_CTRL | 输出 | **RS485** | 485 侧限位控制（**不是限位输入**），`RS485_Init()` 置 1 |
| PD5 | V02_CTRL | 输出 | **RS485** | 485 侧限位控制（**不是限位输入**），`RS485_Init()` 置 1 |
| **PD6** | RS485 方向 | 输出 | **RS485** | 收发切换：高=发送、低=接收 |

两个高频误区：

1. **PD4/PD5 是"输出"不是"输入"**：它们是 485 侧限位控制输出 `V01_CTRL/V02_CTRL`，归 `RS485`（宏在 `rs485.h`，`RS485_Init()` 里配置），`DCMOTOR` 已不再引用。真正的限位**输入**是 PD7（上限位）和 PB3（下限位），归 `GATEIO`。别看到"限位"俩字就把 PD4/PD5 塞进 GATEIO，也别塞回 DCMOTOR。
2. **PD6 归 RS485，不归继电器**：虽然它是 GPIOD 上的方向控制、名字像"方向继电器"，但它是 RS485 收发器的方向脚，和电机方向继电器（PD3）完全两回事。做 RS485 时它归 `RS485` 模块管（见《RS485重构操作建议.md》§3）。

一句话记法：**PD1/PD3 是电机继电器，归 DCMOTOR；PD4/PD5 是 485 侧限位控制输出、PD6 是串口方向，都归 RS485。**

### 16.3 为什么不拆独立 RLY 层（决策依据）

拆不拆，看两条：有几个消费者、时序是否会被割裂。当前两条都不支持拆：

**① 只有一个消费者**：继电器唯一的服务对象就是电机。按键、限位、堵转、超时、校准——这些看起来是不同来源，但它们都不直接操作继电器，而是先汇成"电机意图"（`DCMOTOR` 的 `s_state`），再由 `DCMOTOR_Process()` 统一变成继电器动作。拆出 `RLY` 层，它也只是被 `DCMOTOR` 一个模块调用，多一层转发而已。

**② 时序必须贴着决策**：换向要"先方向、等 5ms、再主电"，停机要"只断主电、留方向"，限位拦截要在驱动前判断。这些时序和电机状态机强耦合。如果拆开，`DCMOTOR` 得把"方向"和"主电"两个动作分别下发给 `RLY`，中间那 5ms 延时和状态判断放在哪一层都会别扭——放 `RLY` 里它就得懂电机语义，放 `DCMOTOR` 里那 `RLY` 就只剩几个 GPIO 宏，成了空壳。

**③ 拆开的现实代价**：多一个初始化函数要排顺序（继电器必须先于电机动作初始化）、多一层接口、多一处可能配错的地方。而收益（"解耦"）在当前单一消费者下兑现不了。

> 什么时候才该拆？当出现**第二个消费者**时——比如将来有个独立的"急停/安全"模块也要操作主电继电器，那时把继电器提到公共层才划算。目前没有，就不拆。

### 16.4 工程落地两处修正

即 §15.1 的两条：① `gate_io.c:52` 的 `static void GATEIO_RelayInit(void)` 改成 `void GATEIO_Init(void)`（去 `static`、对齐 `.h` 与 `main.c`，否则链接报 `L6218E`）；② 第 56 行时钟补 `| RCC_APB2Periph_AFIO`（否则 PB3/PB4 读数不对）。**改这两行即可，无需改 `.h`、`main.c`，也不新建模块。**

### 16.5 验证清单（改完后）

继电器部分本身在本文档 §15 已做 host 桩验证（v5 校准版 23 项断言全过），无需重做。本轮只需确认 §15.1 的工程改动生效：

1. Keil 点 Build，`L6218E: Undefined symbol GATEIO_Init` 消失（§15.1 ① 修复生效）；
2. 上电后 `DCMOTOR_Init()` 跑完，万用表量 PD1（主电）、PD3（方向）都处于断开状态；
3. 确认 PB3（下限位）、PB4（编码器 A）读数正常（§15.1 ② 修复生效）——触发下限位能被 `GATEIO` 采到；
4. 按一次开闸：PD3 先变化、`delay_ms(5)` 后 PD1 吸合；按停止：只断 PD1、PD3 保持；
5. 做 RS485 时确认 PD6 独立于以上继电器脚——发帧期间 PD6 为高、收帧期间为低，继电器不受影响；同时确认 PD4/PD5 由 `RS485_Init()` 置高、`DCMOTOR` 全程不碰（边界见 §16.2、《RS485重构操作建议.md》§3）。

### 16.6 边界对照与待办

**一句话边界表**：

| 职责 | 归属 | 引脚/接口 |
|---|---|---|
| 电机继电器（主电/方向） | DCMOTOR | PD1 / PD3 |
| 485 侧限位控制输出 | RS485 | PD4 / PD5（`V01_CTRL` / `V02_CTRL`） |
| 限位输入 + 编码器 | GATEIO | PD7 / PB3 / PB4 / PB0 |
| RS485 收发方向 | RS485 | PD6 |
| RS485 数据收发 | RS485 | PC10(TX) / PC11(RX) |

**待办**：

1. 按 §15.1 修 `gate_io.c` 两处（不改设计、不新建模块）；
2. 按 §16.5 清单 Build + 上板验证；
3. 继续做 RS485：见《RS485重构操作建议.md》，注意 PD6 边界；
4. **将来若出现第二个继电器消费者**（如独立急停模块），再重新评估是否上提为公共 `RLY` 层——那时在本节续写，现在不做。

---

> 配套文档到此完整：《ADC重构操作建议.md》《RS485重构操作建议.md》与本文档（含 §16 继电器边界）。电机和 ADC 都没有接 LCD 显示（本轮不改显示内容）——把电流、开度、限位状态显示在 LCD 上，属于后续"真数据接线"的独立需求，到时再单独生成建议。
