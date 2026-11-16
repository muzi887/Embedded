# 直流电机（闸门）模块重构操作建议（qibiji_zxl）

> **起点**：《ADC重构操作建议.md》已完成（先做 ADC）。`qibiji_zxl` 现有 KEY / TIM3 / DEBUG_UART / LCD / ADC 五个模块，`main.c` 跑按键 + LCD 页面 + 背光 + ADC 采样。没有 `HARDWARE/DCMOTOR/` 目录，没有 `armReg`、没有限位开关 / 编码器、没有 4G。
>
> **目标**：把原工程 `qibiji/HARDWARE/DC_MOTOR/dc_motor.c` 的电机控制搬进来——继电器驱动、请求/已执行状态分离、边沿触发动作、电流零点联锁、堵转停机、运行时长兜底，做成独立模块 `HARDWARE/DCMOTOR/dcmotor.c/.h`。
>
> **怎么用这份文件**：第 3、4 节是两个新文件的完整代码；第 5 节改 `main.c`；第 6 节改 Keil 工程；第 7～9 节是机制讲解、验证和与原工程的对照；第 10 节是约束和**安全提醒**。

---

## 1. 本轮做什么、不做什么

**做**：继电器 PD1（主电）/ PD3（方向）/ PD4、PD5（485 侧限位控制，沿用原工程初始化）驱动；`DCMOTOR_Request()` 写请求、`DCMOTOR_Process()` 边沿执行；电流零点没标定好就不许动；ADC 报堵转立即断电；单次运行超过最长时长强制停（安全兜底）。

**不搬**（明确排除，新工程没有对应硬件 / 模块）：

| 项 | 为什么不搬 |
|---|---|
| `armReg`（motor_on_off / gate_kaidu / range_target / gate_maxturns） | 整套寄存器结构新工程没有；状态用模块私有变量 + getter |
| 限位开关 `FLAG_OPENALL` / `FLAG_CLOSEALL` | 由 `switch.c` 在 TIM2 中断里根据接近开关刷新；新工程没接限位硬件。**这正是本模块加"最长运行时间"兜底的原因** |
| 编码器开度 `Num_count` / `ChangeReality()` | 同上，没有编码器硬件；开度显示以后接 |
| `dcmotor_auto()` 自动校准行程 / `dcmotor_auto_stop()` | 依赖限位和 `gate_maxturns`，且原工程里 `auto_stop()` 是死代码；不搬 |
| `remoteflag` / 4G 目标开度 t3/t4 | 没有 4G 模块；远程控制以后接 |
| `EventPostMotorStart/Stop()` / `PropertyReport_SetPending()` | 没有 GPRS；停止原因用 getter 暴露，不上报 |
| `dcmotor_keycheck()` 读全局 `KeyNum` | 新工程按键走事件分发；改由 `main.c` 的 `APP_DispatchKey()` 显式调 `DCMOTOR_Request()` |
| `KeyLed_Ctl()` | 按键 LED 属于 LED 模块，不归电机 |

---

## 2. 和 ADC 模块的耦合（先看懂这张图）

两个模块的关系是**单向为主、一个反向标志**：

```
main.c
  ├─ APP_DispatchKey(): UP/DOWN/STOP → DCMOTOR_Request(state)
  ├─ 主循环每轮:        DCMOTOR_Process()
  └─ 10ms 节拍里:       DCMOTOR_Tick()   ADC_Tick()

DCMOTOR ──调用──> ADC
   ├─ ADC_CurrentIsReady()      零点没好 → 强制停机（联锁）
   ├─ ADC_PopStall()            取堵转标志 → 断电
   └─ ADC_SetMotorRunning(0/1)  运行/停止时通知 ADC 冻结/恢复零点跟踪

ADC ──调用──> DCMOTOR :  无
```

- 电机**依赖** ADC 的三个接口（都在《ADC重构操作建议.md》里定义好了）；
- ADC **不依赖**电机——堵转只置标志，电机自己来取。这样两个模块能各自单独验证，不会出现循环依赖。
- `ADC_SetMotorRunning()` 是那个"反向标志"：本质上是电机把自己的运行状态告诉 ADC，用于冻结零点温漂跟踪（防止运行电流抬高零点基线、导致堵转漏报）。方向仍是"电机→ADC"，没有环。

**必须先做 ADC 再做电机**，否则 `DCMOTOR_Process()` 里对 ADC 接口的调用全部 undefined。

---

## 3. 第 1 步：新建 `HARDWARE/DCMOTOR/dcmotor.h`

新目录 `qibiji_zxl/HARDWARE/DCMOTOR/` 需要你手动建。先放头文件：

```c
#ifndef __DCMOTOR_H
#define __DCMOTOR_H

/* ---------------------------------------------------------------------------
 * DC motor (gate) control module for qibiji_zxl
 *
 * Scope: relay drive (PD1 main power, PD3 direction), request/executed state
 *        separation with edge-triggered action, current-zero interlock from
 *        the ADC module, stall stop, and a max-run-time safety fallback.
 *
 * Not included (no hardware wired in qibiji_zxl): limit switches, rotary
 *        encoder / opening feedback, auto-calibration, GPRS event reporting,
 *        EEPROM. Those come when their modules are migrated.
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"

#define MOTOR_UP     0x01u
#define MOTOR_DOWN   0x02u
#define MOTOR_STOP   0x03u

/* stop reason, exposed for debug / display only */
typedef enum
{
    MOTOR_STOP_NONE = 0,
    MOTOR_STOP_MANUAL,
    MOTOR_STOP_STALL,
    MOTOR_STOP_TIMEOUT,
    MOTOR_STOP_INTERLOCK
} motor_stop_reason_t;

void              DCMOTOR_Init(void);
void              DCMOTOR_Request(uint8_t state);   /* set target state */
void              DCMOTOR_Process(void);            /* call each main-loop turn */
void              DCMOTOR_Tick(void);               /* call once per 10 ms tick */
uint8_t           DCMOTOR_GetState(void);
uint8_t           DCMOTOR_GetDir(void);
motor_stop_reason_t DCMOTOR_GetLastStopReason(void);
void              DCMOTOR_SetMaxRunMs(uint16_t ms);

#endif
```

**接口说明**：

| 函数 | 谁调、何时调 | 作用 |
|---|---|---|
| `DCMOTOR_Init()` | `main()` 里一次 | 配 GPIO、继电器全断、状态清零 |
| `DCMOTOR_Request(state)` | `APP_DispatchKey()` 里，按键事件到来时 | 只写"想去哪"，不碰 GPIO；三种状态 `MOTOR_UP/DOWN/STOP` |
| `DCMOTOR_Process()` | 主循环每轮 | 做联锁 + 收堵转 + 边沿执行（真正驱动继电器） |
| `DCMOTOR_Tick()` | 10ms 节拍里 | 累计运行时长，超时强制停 |
| `DCMOTOR_GetState/GetDir/GetLastStopReason()` | 调试 / 显示 | 只读，返回当前状态、方向、上次停机原因 |
| `DCMOTOR_SetMaxRunMs(ms)` | 可选，初始化后 | 改最长运行时长，默认 30000ms（30s） |

---

## 4. 第 2 步：新建 `HARDWARE/DCMOTOR/dcmotor.c`

```c
#include "dcmotor.h"
#include "adc.h"

/* ===========================================================================
 * DC motor implementation
 *
 * Ported from qibiji/HARDWARE/DC_MOTOR/dc_motor.c. Kept:
 *  - relay sequence: set direction first, delay 5ms, then main power on
 *  - stop only drops main power (direction relay untouched)
 *  - request (state) vs executed (old) separation, action only on edge
 * Removed (no hardware in qibiji_zxl): armReg, limit flags, encoder opening,
 *  auto-calibration, remote flag, key-check (keys arrive via explicit Request),
 *  GPRS events / property report.
 * Added: current-zero interlock + stall stop via ADC module, max-run fallback.
 * =========================================================================== */

#define RLY_MAIN_ON()    GPIO_SetBits(GPIOD, GPIO_Pin_1)
#define RLY_MAIN_OFF()   GPIO_ResetBits(GPIOD, GPIO_Pin_1)
#define RLY_DIR_UP()     GPIO_SetBits(GPIOD, GPIO_Pin_3)
#define RLY_DIR_DOWN()   GPIO_ResetBits(GPIOD, GPIO_Pin_3)
#define V01_CTRL         PDout(4)
#define V02_CTRL         PDout(5)

#define MAX_RUN_MS_DEFAULT  30000u   /* 30s safety fallback without limit sw */

static uint8_t  s_state;        /* requested state */
static uint8_t  s_old;          /* executed state */
static uint8_t  s_dir;          /* last physical direction */
static motor_stop_reason_t s_stop_reason;
static uint16_t s_max_run_ms;
static uint32_t s_run_ms;       /* ms spent in current run */
static uint8_t  s_stall_pending;/* stall seen, to be reported on STOP edge */

static void DCMOTOR_RelayInit(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    gpio.GPIO_Pin  = GPIO_Pin_1 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &gpio);

    RLY_MAIN_OFF();
    RLY_DIR_DOWN();
    V01_CTRL = 1u;
    V02_CTRL = 1u;
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

void DCMOTOR_Init(void)
{
    s_state = MOTOR_STOP;
    s_old = MOTOR_STOP;
    s_dir = MOTOR_STOP;
    s_stop_reason = MOTOR_STOP_NONE;
    s_max_run_ms = MAX_RUN_MS_DEFAULT;
    s_run_ms = 0u;
    s_stall_pending = 0u;
    DCMOTOR_RelayInit();
}

void DCMOTOR_Request(uint8_t state)
{
    if ((state == MOTOR_UP) || (state == MOTOR_DOWN) || (state == MOTOR_STOP))
        s_state = state;
}

void DCMOTOR_Process(void)
{
    /* 1) current-zero interlock: zero point not ready -> force stop */
    if (ADC_CurrentIsReady() == 0u)
    {
        s_state = MOTOR_STOP;
        s_old = MOTOR_STOP;      /* sync old: no phantom stop event on ready */
        s_stop_reason = MOTOR_STOP_INTERLOCK;
        ADC_SetMotorRunning(0u);
        DCMOTOR_DriveStop();
        return;
    }

    /* 2) consume stall flag raised by ADC */
    if (ADC_PopStall() != 0u)
    {
        s_stall_pending = 1u;
        s_state = MOTOR_STOP;    /* request stop; reason set on edge below */
        ADC_SetMotorRunning(0u);
        DCMOTOR_DriveStop();     /* immediate physical cut, do not wait */
    }

    /* 3) edge-triggered action */
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
            DCMOTOR_DriveStop();
            break;
        }
        s_old = s_state;
    }
}

void DCMOTOR_Tick(void)
{
    if ((s_state == MOTOR_UP) || (s_state == MOTOR_DOWN))
    {
        s_run_ms += 10u;
        if (s_run_ms >= (u32)s_max_run_ms)
        {
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_TIMEOUT;
            s_state = MOTOR_STOP;
            ADC_SetMotorRunning(0u);
            DCMOTOR_DriveStop();    /* immediate cut on timeout */
        }
    }
}

uint8_t DCMOTOR_GetState(void)  { return s_state; }
uint8_t DCMOTOR_GetDir(void)    { return s_dir; }
motor_stop_reason_t DCMOTOR_GetLastStopReason(void) { return s_stop_reason; }
void DCMOTOR_SetMaxRunMs(uint16_t ms) { s_max_run_ms = ms; }
```

**注意一处和 host 验证版的差异**：host 验证时 `delay_ms(5)` 被桩成空循环（host 没有 SysTick）；**上面文档里的代码用的是真机 `delay_ms(5)`**，这是对的——`main()` 里已经在 `DCMOTOR_Init()` 之前调了 `delay_init()`，`delay_ms()` 可用。别忘了 `dcmotor.c` 顶部要 `#include "delay.h"`（见下面 §5.1 的 include 说明，实际是加在 `.c` 里）。

> **重要**：`dcmotor.c` 里用了 `delay_ms(5)`，所以文件头要加一行 `#include "delay.h"`。上面代码块顶部只写了 `#include "dcmotor.h"` 和 `#include "adc.h"`——**请补上第三行**：
>
> ```c
> #include "delay.h"
> ```
>
> 加在两个 include 之后即可。这是host 桩验证用空循环替代、文档改回真机写法时唯一需要手动补的一行。

---

## 5. 第 3 步：改 `USER/main.c`

### 5.1 加 include（放在 `#include "adc.h"` 之后）

```c
#include "dcmotor.h"
```

### 5.2 `main()` 里加 Init（放在 `ADC1_Init()` 之后、`TIM3_10ms_Init()` 之前）

```c
    LCD_PAGES_Init();
    ADC1_Init();
    DCMOTOR_Init();         /* 本轮新增：GPIO 全关、状态清零 */
    TIM3_10ms_Init();
```

**顺序**：`DCMOTOR_Init()` 要在 `ADC1_Init()` 之后（虽然本模块 Init 不直接调 ADC，但逻辑上先有 ADC 再有机电）；两者都在 `TIM3_10ms_Init()` 之前。`ADC1_Init()` 内部完成硬件初始化后，`ADC_Tick()` 和 `DCMOTOR_Tick()` 才会在 TIM3 启动后被节拍驱动。

### 5.3 `APP_DispatchKey()` 里把按键映射成电机动作

在原函数的 `LCD_BACKLIGHT_Kick();` 之后加一段：

```c
    LCD_PAGES_Process(key);
    LCD_BACKLIGHT_Kick();

    /* 本轮新增：UP 开闸、DOWN 关闸、STOP 停机。
     * 只在首页（默认参数页）响应——和原工程 PageSta==0 的门控等价，
     * 避免设置页调参数时误动作。 */
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

**这里有一个新依赖**：`LCD_PAGES_IsHome()` 是需要在 `lcd_pages.c/.h` 里新增的一个只读函数（判断当前是不是首页 `PAGE_HOME`）。这是本轮**唯一**要动 LCD 模块的地方——加一个 3 行的 getter，不改任何显示逻辑：

在 `lcd_pages.h` 末尾（`#endif` 之前）加：

```c
uint8_t LCD_PAGES_IsHome(void);   /* 1 if current page is home */
```

在 `lcd_pages.c` 末尾加：

```c
uint8_t LCD_PAGES_IsHome(void)
{
    return (s_page == PAGE_HOME) ? 1u : 0u;
}
```

**为什么需要这个门控**：原工程 `dcmotor_keycheck()` 第一行是 `if (PageSta == 0)`——只在默认参数页响应按键。新工程的设置页要用 UP/DOWN 改参数，如果不加这个判断，设置页按 UP 会同时让电机开闸。加一个只读 getter 是最小的解法。如果你暂时不想动 `lcd_pages.c`，也可以先不加门控、直接 `DCMOTOR_Request`，但要清楚设置页按键会误触发电机。

### 5.4 主循环两处：节拍里加 Tick，循环里加 Process

```c
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();
            ADC_Tick();
            DCMOTOR_Tick();         /* 本轮新增：累计运行时长，超时强制停 */
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);

        DCMOTOR_Process();          /* 本轮新增：每轮做联锁+堵转+边沿执行 */
```

**`DCMOTOR_Process()` 放主循环每轮调用，不放节拍里**——边沿执行要尽快响应按键和堵转，主循环跑得比 10ms 节拍快。`DCMOTOR_Tick()` 只在节拍里累计时长，两者职责不同，别合并。

### 5.5 改完后的 `main.c` 全貌（核对用）

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

    if (LCD_PAGES_IsHome())          /* 新增：只在首页响应电机键 */
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
    DCMOTOR_Init();                 /* 新增 */
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
            DCMOTOR_Tick();         /* 新增 */
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);

        DCMOTOR_Process();          /* 新增 */
    }
}
```

---

## 6. 第 4 步：改 `USER/SRTUV10.uvprojx`

### 6.1 IncludePath（约第 338 行）追加 `..\HARDWARE\DCMOTOR`

改后：

```xml
<IncludePath>..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\DEBUG_UART;..\HARDWARE\KEY;..\HARDWARE\TIMER;..\HARDWARE\LCD12864;..\HARDWARE\ADC;..\HARDWARE\DCMOTOR</IncludePath>
```

### 6.2 HARDWARE 组 `<Files>` 末尾追加 2 个节点

```xml
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

`stm32f10x_gpio.c` 工程已编入，不用加。改完 uvprojx 关工程重开再 Build。

---

## 7. 机制讲解

**① 请求 vs 已执行（`s_state` ≠ `s_old`）**——这是原工程最核心的约定，也是本模块照搬的部分：

- `DCMOTOR_Request()` 只写 `s_state`（"想去哪"），按键、以后 4G、堵转、超时都可以写，互不协商；
- `DCMOTOR_Process()` 比较 `s_old`（"上次执行到哪"）和 `s_state`，**只有不等时才驱动继电器**，然后 `s_old = s_state` 提交；
- 好处：多个来源随便写状态不会造成 GPIO 抖动；STOP 键、堵转、超时写的都是同一个请求，动作收敛到一处。

**② 继电器时序**：`dcmotor_up()` 先 `RLY_2H_ON`（方向）、`delay_ms(5)`、再 `RLY_1H_ON`（主电）。这个 5ms 是硬件保护——换向继电器触点要稳定，主电通了再切方向等于带载拉弧，会烧触点。`dcmotor_stop()` 只断主电、不动方向继电器，减少机械动作，也保留最后方向（`s_dir`）。

**③ 电流零点联锁**（原工程 `Dcmotor_Process()` 开头那段）：`ADC_CurrentIsReady()==0` 时强制 `s_state=STOP`、`s_old=STOP`、断电并 `return`，三个子逻辑全跳过。两个目的：让零点一定能采到（电机强制静止），以及零点不准时不许带电运行（否则堵转保护形同虚设）。同步 `s_old` 是为了零点就绪那一轮不补发一条假的停机。

**④ 堵转停机**：ADC 连续 200ms 检测到过流后置标志；本模块 `ADC_PopStall()` 取到 → 立即 `DCMOTOR_DriveStop()` 物理断电（不等主循环后续），再走边沿把停机原因记成 `MOTOR_STOP_STALL`。堵转原因和手动停机用 `DCMOTOR_GetLastStopReason()` 区分。

**⑤ 运行时长兜底**（本模块新增，原工程靠限位）：没有限位开关，按住 UP/DOWN 会一直转到机械卡死。所以 `DCMOTOR_Tick()` 每 10ms 累计 `s_run_ms`，单次运行默认超过 30000ms（30s）强制停机，原因记 `MOTOR_STOP_TIMEOUT`。可用 `DCMOTOR_SetMaxRunMs()` 调整。这是安全兜底，不是正常停机手段——等以后接了限位开关，正常停机仍靠限位。

**⑥ 停机原因**（`motor_stop_reason_t`）：`NONE`（运行中）/ `MANUAL`（本地停机键）/ `STALL`（堵转）/ `TIMEOUT`（超时兜底）/ `INTERLOCK`（零点联锁强制）。只在 getter 里暴露，本轮不上报 GPRS。

---

## 8. 上板验证

### 8.1 安全前置（务必先做）

**上电先别急着按 UP/DOWN。** 先确认：

1. 继电器接线正确——PD1 是主电、PD3 是方向，别接反；
2. `DCMOTOR_Init()` 后两个继电器都是断的（万用表量 PD1/PD3，或用调试器看）；
3. ADC 零点标定完成后（`ADC_CurrentIsReady()==1`，约上电 320ms）才允许电机动——这是代码里的联锁，上板时观察它是否真的挡住了前 320ms 的按键。

### 8.2 预期现象

| 操作 | 预期 | 对应代码 |
|---|---|---|
| 上电 0–320ms 按 UP | 电机不动（联锁挡住） | `ADC_CurrentIsReady()==0` 分支 |
| 320ms 后在首页按 UP | 电机开闸：PD3 先置高，5ms 后 PD1 置高 | `DCMOTOR_DriveUp()` |
| 运行中按 DOWN | 换向：PD3 拉低，5ms 后 PD1 重新置高 | 边沿 → `DCMOTOR_DriveDown()` |
| 按 STOP | 只断 PD1，PD3 保持；`GetLastStopReason()==MANUAL` | `DCMOTOR_DriveStop()` |
| 在设置页按 UP/DOWN | 电机不动（只改参数） | `LCD_PAGES_IsHome()` 门控 |
| 按住 UP 不放超过 30s | 电机强制停；`GetLastStopReason()==TIMEOUT` | `DCMOTOR_Tick()` |
| 人为卡住闸门使电流超阈值 200ms | 立即断电；`GetLastStopReason()==STALL` | ADC 标志 → `DCMOTOR_Process()` |

### 8.3 出问题时的排查

| 症状 | 首查 | 次查 |
|---|---|---|
| 按键电机完全不动 | `ADC_CurrentIsReady()` 是否为 1（联锁挡着） | 是否在首页（`LCD_PAGES_IsHome()`）；`DCMOTOR_Process()` 是否在主循环里 |
| 设置页按 UP 电机也动 | `APP_DispatchKey()` 里的 `LCD_PAGES_IsHome()` 门控是否加了 | `LCD_PAGES_IsHome()` 实现是否正确返回 |
| 一上电继电器就吸合 | `DCMOTOR_Init()` 是否在 TIM3 启动前跑完；PD1 极性 | 继电器是常开还是常闭触点（本代码假设高电平吸合） |
| 换向有误动作 / 打火 | `delay_ms(5)` 是否真的生效（`delay_init()` 是否在 `DCMOTOR_Init()` 之前） | 方向继电器接线 |
| 超时不生效 | `DCMOTOR_Tick()` 是否和 `KEY_Tick()` 在同一个 `if (Tick_10ms_Take())` 里 | `s_max_run_ms` 是否被改大 |
| 编译报 `undefined symbol ADC_*` | ADC 模块是否先做完、`adc.c` 是否加进工程 | IncludePath 是否含 `..\HARDWARE\ADC` |

---

## 9. 与原工程逐项对照

| 原工程 | 原行为 | 本模块 | 差异说明 |
|---|---|---|---|
| `sys_moter_state` / `_old` | 请求 / 已执行，全局 | `s_state` / `s_old`，模块私有 | 语义一致，去掉全局广播 |
| `dcmotor_up/down()` | PD3 方向 + delay 5ms + PD1 主电 | 同 | 时序保留（硬件保护） |
| `dcmotor_stop()` | 只断 PD1、留方向 | 同（`DCMOTOR_DriveStop()`） | 一致 |
| `Dcmotor_Process()` 联锁 | `!ADC_CurrentIsReady()` → 强制 STOP | 同 | 一致，含 `s_old` 同步 |
| ADC 堵转 | ADC 直接调 `dcmotor_request_stop(STALL)` | `ADC_PopStall()` 取标志后断电 | 解耦，见 §2 |
| `dcmotor_keycheck()` 读 `KeyNum` | 全局键码 + `PageSta==0` 门控 | `APP_DispatchKey()` 显式 `Request` + `LCD_PAGES_IsHome()` | 事件分发替代全局；门控用 getter |
| 停止原因 `pending_stop_reason` | 枚举 + 事件上报 | `s_stop_reason` + getter | 不上报，只暴露 |
| `EventPost*` / `PropertyReport` | GPRS 事件 + 属性上报 | 无 | 没有 4G，以后接 |
| 限位停机 t1/t2 | `FLAG_OPENALL/CLOSEALL` 硬件限位 | 无 + 30s 超时兜底 | 新工程无限位硬件；超时是临时安全措施 |
| 开度归一化 / 目标开度 t3/t4 | `armReg.gate_kaidu` / `range_target` | 无 | 没有编码器/4G；开度以后接 |
| `dcmotor_auto()` 校准 | 300ms 步进状态机 | 不搬 | 依赖限位，死代码 `auto_stop()` 一并去掉 |

---

## 10. 约束与安全提醒

- **C90**（`<uC99>0</uC99>`）：变量块首声明；本模块 `.c/.h` 注释全 ASCII。
- **`dcmotor.c` 要 `#include "delay.h"`**：因为用了 `delay_ms(5)`（见 §4 的补充提醒）。host 验证时用空循环替代了它，文档改回真机写法时必须补这个 include。
- **只依赖 ADC，不被 ADC 依赖**：`dcmotor.c` include `adc.h`；`adc.c` **不** include `dcmotor.h`。保持单向，别加反向调用，否则循环依赖。
- **不新增定时器 / 中断**：复用 TIM3 10ms 节拍，本模块没有 `TIMx_IRQHandler`。
- **`Process` 和 `Tick` 职责不同**：`Process()` 主循环每轮调（快速响应），`Tick()` 只在 10ms 节拍里调（累计时长），别合并、别互换位置。
- **`DCMOTOR_Request()` 只收 `MOTOR_UP/DOWN/STOP`**：传别的值会被忽略，不会误动作。
- **⚠️ 安全提醒——30s 超时只是兜底，不是行程控制**：新工程当前没有限位开关和编码器，电机**不知道闸门到没到头**。30s 强制停机是为了防止无限转烧毁，但不同闸门的全行程时间不同——如果你的闸门正常全行程接近或超过 30s，会在没到头时就被强制停下。上板时先用手/万用表确认机构的全行程时间，用 `DCMOTOR_SetMaxRunMs()` 设一个比正常行程略大的值。**正式产品必须接回限位开关**，超时兜底不能替代物理限位保护。

---

## 11. 验证记录

- host 端 `gcc -std=c90 -Wall -Wextra -pedantic`：编译通过，仅 1 条 host 专属警告（ADC 里 64 位指针转 `u32`，真机 32 位无此问题）；
- 行为断言 **30 项，0 失败**（ADC + 电机一起验证），电机相关关键项：
  - `DCMOTOR_Init()` 后主电继电器断；
  - 联锁：ADC 未就绪时即便 `Request(UP)`，状态仍为 STOP、主电保持断；
  - UP：方向继电器置高、主电置高、状态和方向都是 UP；
  - DOWN：方向拉低、主电保持高；
  - STOP：只断主电、方向不变、原因 `MANUAL`；
  - 重复请求同一状态不产生杂散动作、原因复位为 `NONE`；
  - 超时：设 100ms 测试值，90ms 仍在跑、100ms 强制 STOP、主电断、原因 `TIMEOUT`；
  - 堵转：ADC 标志置位后电机强制 STOP、主电立即断、原因 `STALL`；
- 验证方法：host 桩掉 STM32 标准库函数，读继电器引脚电平（`GPIOD` 的 PD1/PD3）和状态 getter 判断行为；
- 代码副本：`C:\Users\JessamynAdmin\AppData\Local\Temp\qibiji_motor_adc_test\`。
- 这是 host 桩验证，**不是 Keil 真机编译**；请在 Keil 点一次 Build 确认链接，并严格按第 8.1 节安全前置上板。

---

> 两份建议文档到此配套完整：先做《ADC重构操作建议.md》，再做本文件。电机和 ADC 都没有接 LCD 显示（本轮不改显示内容）——把电流、开度、电机状态显示在 LCD 上，属于后续"真数据接线"的独立需求，到时再单独生成建议。
