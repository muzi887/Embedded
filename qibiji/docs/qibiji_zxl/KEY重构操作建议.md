# KEY 模块重构操作建议（自行实施版）

> **起点**：代码已撤回。`qibiji_zxl/HARDWARE/KEY/key.c/.h` 是从原工程 `qibiji/` 拷入的版本，`USER/main.c` 是空的 `while(1)`，`USER/SRTUV10.uvprojx` 是原始配置，没有 `HARDWARE/TIMER/` 和 `HARDWARE/DEBUG_UART/` 目录。
>
> **目标**：按键模块只负责把 5 路引脚电平翻译成「一次按键事件」，由主程序显式分发；用 TIM3 提供固定 10ms 采样节拍；用 USART1 把事件打印到 PC 串口以便上板验证。
>
> **怎么用这份文件**：第 2~7 节各对应一个文件，按编号顺序做，每节都给出该文件的完整目标代码。第 8 节是验证方法，第 9 节是必须遵守的约束，第 10 节是符号对照速查。
>
> 现有的 `docs/qibiji_zxl/KEY模块说明.md` 描述的就是目标状态的运行逻辑，可作为理解参考；本文件负责给出「怎么改」。

---

## 1. 目标状态与文件清单

改完后系统具备的能力：

| 能力 | 说明 |
|---|---|
| 固定采样节拍 | TIM3 每 10ms 产生一次节拍，去抖时间与主循环运行速度无关 |
| 30ms 软件消抖 | 连续 3 拍都检测到按下才确认，中途松开立即重新计数 |
| 一次按下只报一次 | 确认后状态锁在 PRESSED，必须完全松开才能再次触发，长按不连发 |
| 单一事件出口 | `KEY_Pop()` 取走一个事件，取走即清；无事件返回 `KEY_NONE` |
| 模块状态私有 | 引脚映射、计数器、状态机、事件队列全部 `static`，对外不可见 |
| 显式分发 | 主循环拿到键码后调用 `App_DispatchKey()`，由它决定交给页面 / 电机 / 背光 |
| 上板可验证 | USART1 打印启动信息和每次按键事件，用 PC 串口助手观察 |

文件清单。**正题只有 2 个**，其余 5 个是让它能编译、能跑起来、能观察的配套文件，不含业务逻辑：

| # | 文件 | 操作 | 作用 |
|---|---|---|---|
| 1 | `HARDWARE/KEY/key.h` | 重写 | 定义对外接口 `key_code_t` + 3 个函数 |
| 2 | `HARDWARE/KEY/key.c` | 重写 | 引脚映射表、5 个独立状态机、私有事件队列 |
| 3 | `HARDWARE/TIMER/timer.h` | 新建 | 10ms 节拍接口声明 |
| 4 | `HARDWARE/TIMER/timer.c` | 新建 | TIM3 初始化、中断置标志、主循环取标志 |
| 5 | `HARDWARE/DEBUG_UART/debug_uart.h` | 新建 | 调试串口接口声明 |
| 6 | `HARDWARE/DEBUG_UART/debug_uart.c` | 新建 | USART1 初始化与轮询发送字符串 |
| 7 | `USER/main.c` | 重写 | 初始化各模块、主循环接线、显式分发 |
| 8 | `USER/SRTUV10.uvprojx` | 修改 | 增加 IncludePath，把 4 个新文件加入编译组 |

**落地顺序**：1 → 2 → 3/4 → 5/6 → 7 → 8。前两步定契约，中间两步提供时基和观察手段，最后接线并让 Keil 编译到新文件。按这个顺序做，后面加页面、电机模块时不用返工。

---

## 2. 第 1 步：重写 `HARDWARE/KEY/key.h`

### 2.1 相对起点改了什么

| 起点 | 目标 | 原因 |
|---|---|---|
| 5 个引脚宏 `KEY_UP_VAL` 等 | 移到 `key.c` 内部并改名 `KEY_PIN_*` | 引脚属于实现细节，不应对外暴露 |
| 5 个键码宏 `KEY_UP`~`KEY_RIGHT` | `key_code_t` 枚举 | 类型安全；`KEY_UP` 与 `KEY_UP_VAL` 只差后缀，容易混淆 |
| `KEY_ENTER/LEFT/RIGHT` | `KEY_STOP/KEY_SET/KEY_CANCEL` | 与面板丝印 K5 STOP、K4 SET、K3 CAN 对齐；旧名是从方向键模板抄来的 |
| `extern u8 KeyNum` | 删除 | 全局广播变量，无「消费即清除」，同一事件会被多个模块重复读取 |
| `KEY_Scan / Get_KeyVal / KEY_Process` | 删除 | 三个函数绕圈，实际只完成「采样 + 去抖 + 写一个全局变量」 |
| `KeyLed_Ctl` 声明 | 删除 | 实现已在起点被注释，是悬空声明；LED 控制本就不属于按键模块 |
| 5 个对外函数 + 1 个全局变量 | 3 个函数 | 契约收窄 |

键码数值 1~5 与起点完全一致，便于以后对接既有页面代码。

### 2.2 完整目标代码

```c
#ifndef __KEY_H
#define __KEY_H

#include "sys.h"

/* ---------------------------------------------------------------------------
 * 面板按键（KEY）模块 —— 对外唯一接口
 *
 * 职责：把 5 路按键电平翻译成「一次按键事件」。
 *       「这个键在当前页面里该做什么」不归它管，由主程序显式分发。
 *
 * 用法：上电调一次 KEY_Init()；
 *       此后每 10ms 调一次 KEY_Tick()（采样 + 去抖）；
 *       主循环用 KEY_Pop() 取事件，取走即清，无事件返回 KEY_NONE。
 *
 * 约束：KEY_Tick() 与 KEY_Pop() 必须在同一执行上下文（主循环）里调用。
 *       不要在中断里调 KEY_Pop()。
 * --------------------------------------------------------------------------- */

/* 键码按面板丝印命名，只表示「哪颗键」，不表示业务动作。
 * 数值沿用旧版 1~5，方便以后与既有页面代码对接。 */
typedef enum
{
    KEY_NONE = 0,
    KEY_UP,             /* K1 UP   = 1 */
    KEY_DOWN,           /* K2 DOWN = 2 */
    KEY_STOP,           /* K5 STOP = 3，旧名 KEY_ENTER */
    KEY_SET,            /* K4 SET  = 4，旧名 KEY_LEFT  */
    KEY_CANCEL          /* K3 CAN  = 5，旧名 KEY_RIGHT */
} key_code_t;

void       KEY_Init(void);
void       KEY_Tick(void);
key_code_t KEY_Pop(void);

#endif
```

---

## 3. 第 2 步：重写 `HARDWARE/KEY/key.c`

### 3.1 相对起点改了什么

1. 5 份重复的 `if / else if` 分支（起点 `Get_KeyVal()` 约 90 行）改为一张引脚映射表 + 5 个独立状态机，每键只有 `{state, cnt}` 两个状态量。
2. 状态机显式写出三态：`KEY_ST_IDLE`（松开）→ `KEY_ST_DEBOUNCE`（连续确认）→ `KEY_ST_PRESSED`（已报事件、等松开）→ 回到 IDLE。
3. 计数器改为饱和计数（`cnt < 3` 才自增）。起点的 `cnt++` 在 `if (cnt == 3)` 之前，锁到 3 后仍继续加到 255 溢出回 0，导致长按每隔约 2.56 秒自动重复上报一次。
4. 起点 `key_up` 初值为 1，按下分支中从未置 0，恒为 1，是死逻辑；删除。
5. 起点 `key_local_remote_change`、`key_setting_change` 全工程零引用；删除。
6. 起点夹带的 `KeyLed_Ctl()`（操作 `LED_UP/LED_DN`）删除，归 LED 模块。
7. 起点注释掉的 `flag10ms` 节流不再依赖全局变量，改由外部调用者按节拍调 `KEY_Tick()`。
8. 事件输出由写全局 `KeyNum` 改为压入模块私有环形队列，深度 4，队列满时丢弃新事件且不阻塞。
9. 行为变化：起点同时按下两键时 `else if` 只认优先级最高的一个；目标版本每个键独立，会各报一次。冲突保护放到 `App_DispatchKey()` 里处理。

### 3.2 完整目标代码

```c
#include "key.h"

/* ===========================================================================
 * 面板按键模块实现
 *
 * 分三层：引脚映射表 -> 每个键独立的状态机 -> 模块私有的事件队列。
 * 对外只在 key.h 暴露 KEY_Init / KEY_Tick / KEY_Pop。
 * =========================================================================== */

/* ---------------------------- 可调参数 ---------------------------- */
#define KEY_COUNT           5
#define KEY_DEBOUNCE_TICKS 3      /* 3 x 10ms = 30ms，由 TIM3 提供节拍 */
#define KEY_QUEUE_SIZE     4      /* 环形事件队列深度 */

/* ------------------------- 引脚映射（本文件私有） -------------------------
 * 低电平 = 按下。硬件上每键已有 4.7K 上拉 + 100nF 滤波电容，
 * 所以 GPIO 配浮空输入即可，软件 30ms 去抖是第二道保险。
 * 位序 i = 0..4 必须与 s_key_code[] 一一对应。 */
#define KEY_PIN_UP      PDin(15)  /* K1 UP   */
#define KEY_PIN_DOWN    PAin(15)  /* K2 DOWN */
#define KEY_PIN_STOP    PAin(11)  /* K5 STOP */
#define KEY_PIN_SET     PAin(12)  /* K4 SET  */
#define KEY_PIN_CANCEL  PCin(9)   /* K3 CAN  */

#define KEY_IDX_UP     0
#define KEY_IDX_DOWN   1
#define KEY_IDX_STOP   2
#define KEY_IDX_SET    3
#define KEY_IDX_CANCEL 4

/* ------------------------- 内部状态（全部 static） ------------------------- */
static const key_code_t s_key_code[KEY_COUNT] =
{
    KEY_UP, KEY_DOWN, KEY_STOP, KEY_SET, KEY_CANCEL
};

typedef enum
{
    KEY_ST_IDLE = 0,      /* 松开 */
    KEY_ST_DEBOUNCE,      /* 正在连续确认 */
    KEY_ST_PRESSED        /* 已确认按下并报过一次，等松开 */
} key_state_t;

typedef struct
{
    uint8_t state;
    uint8_t cnt;          /* 饱和计数，不会溢出 */
} key_fsm_t;

static key_fsm_t s_fsm[KEY_COUNT];

/* 环形队列：Push / Pop 都在主循环里，无并发访问 */
static key_code_t s_queue[KEY_QUEUE_SIZE];
static uint8_t    s_q_head;
static uint8_t    s_q_tail;
static uint8_t    s_q_count;

/* ============================ 1. 初始化 ============================ */
void KEY_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD, ENABLE);

    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;  /* 输入模式下该字段无效，仅填满结构体 */
    gpio.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_15; /* STOP/SET/DOWN */
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_9;                               /* CANCEL */
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_15;                              /* UP */
    GPIO_Init(GPIOD, &gpio);

    /* 备注：PA15 复位后默认复用为 JTAG 的 JTDI。这里沿用原工程硬件接法
     * （只读 IDR 电平），没有禁用 JTAG。若以后要把 PA15 当普通 IO 输出，
     * 需先 GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE) 并改用 SWD 调试。 */

    s_q_head  = 0;
    s_q_tail  = 0;
    s_q_count = 0;
}

/* =================== 2. 硬件读取：5 路引脚 -> 位图 ===================
 * bit = 1 表示「该键此刻被按下」 */
static uint8_t KEY_ReadPins(void)
{
    uint8_t bits = 0;

    if (KEY_PIN_UP     == 0) bits |= (uint8_t)(1u << KEY_IDX_UP);
    if (KEY_PIN_DOWN   == 0) bits |= (uint8_t)(1u << KEY_IDX_DOWN);
    if (KEY_PIN_STOP   == 0) bits |= (uint8_t)(1u << KEY_IDX_STOP);
    if (KEY_PIN_SET    == 0) bits |= (uint8_t)(1u << KEY_IDX_SET);
    if (KEY_PIN_CANCEL == 0) bits |= (uint8_t)(1u << KEY_IDX_CANCEL);

    return bits;
}

/* ============================ 3. 事件队列 ============================ */
static void KEY_QueuePush(key_code_t code)
{
    if (s_q_count >= KEY_QUEUE_SIZE)
        return;                 /* 队列满：丢弃新事件，绝不阻塞 */

    s_queue[s_q_tail] = code;
    s_q_tail = (uint8_t)((s_q_tail + 1u) % KEY_QUEUE_SIZE);
    s_q_count++;
}

key_code_t KEY_Pop(void)
{
    key_code_t code;

    if (s_q_count == 0)
        return KEY_NONE;

    code = s_queue[s_q_head];
    s_q_head = (uint8_t)((s_q_head + 1u) % KEY_QUEUE_SIZE);
    s_q_count--;
    return code;                /* 取走即清 */
}

/* ============================ 4. 状态机 ============================
 * 每 10ms 由 KEY_Tick() 调用一次。
 *
 *   松开(IDLE) --连续 3 拍按下--> 报一次事件 + 锁住(PRESSED) --松开--> IDLE
 *   消抖期间只要有一拍松开，立刻退回 IDLE 重新数。
 *
 * 关键点：进入 PRESSED 后不再产生事件，计数也饱和不溢出，
 *         所以长按不会自动重复；必须完全松开才能触发下一次。
 * 每个键各自独立；同时按下两键会各报一次，由上层决定如何处理。 */
void KEY_Tick(void)
{
    uint8_t bits;
    uint8_t i;

    bits = KEY_ReadPins();

    for (i = 0; i < KEY_COUNT; i++)
    {
        uint8_t    down;
        key_fsm_t *f;

        down = (uint8_t)((bits >> i) & 1u);
        f    = &s_fsm[i];

        switch (f->state)
        {
        case KEY_ST_IDLE:
            if (down)
            {
                f->state = KEY_ST_DEBOUNCE;
                f->cnt   = 1;
            }
            break;

        case KEY_ST_DEBOUNCE:
            if (!down)                       /* 抖动：推倒重来 */
            {
                f->state = KEY_ST_IDLE;
                f->cnt   = 0;
                break;
            }
            if (f->cnt < KEY_DEBOUNCE_TICKS)
                f->cnt++;                    /* 饱和，绝不加到溢出 */
            if (f->cnt >= KEY_DEBOUNCE_TICKS)
            {
                f->state = KEY_ST_PRESSED;
                KEY_QueuePush(s_key_code[i]);   /* 一次按下只报一次 */
            }
            break;

        case KEY_ST_PRESSED:
            if (!down)
            {
                f->state = KEY_ST_IDLE;
                f->cnt   = 0;
            }
            break;

        default:
            f->state = KEY_ST_IDLE;
            f->cnt   = 0;
            break;
        }
    }
}
```

---

## 4. 第 3 步：新建 `HARDWARE/TIMER/`

### 4.1 为什么需要这个模块

`KEY_DEBOUNCE_TICKS = 3` 只有在采样间隔固定时才等于 30ms。起点版本依赖原工程 TIM2 每 5ms 中断两次置位的 `flag10ms`，拷入 `qibiji_zxl` 时这行被注释，采样退化为「主循环跑 3 圈」；主循环速度取决于当时系统负载，去抖时间不可复现。

不使用 `SYSTEM/delay/delay.c` 的 SysTick：`delay_us()` / `delay_ms()` 每次调用都会重配 SysTick 的 `LOAD` / `VAL`，同时用作固定节拍会导致延时不准、节拍丢失。也不在按键代码里用 `delay_ms()` 阻塞等待，那会让主循环停 30ms。

TIM3 中断里只写一个标志，采样和业务一律留给主循环，代价最小。时钟前提：`SYSCLK = 72MHz`（`system_stm32f10x.c` 中 `SYSCLK_FREQ_72MHz`），APB1 预分频不为 1 时定时器时钟 ×2 = 72MHz，`72MHz / 7200 = 10kHz`，计 100 个脉冲 = 10ms。

### 4.2 `HARDWARE/TIMER/timer.h` 完整代码

```c
#ifndef __TIMER_H
#define __TIMER_H

#include "sys.h"

/* ---------------------------------------------------------------------------
 * 10ms 系统节拍
 *
 * TIM3 中断里只置一个标志，主循环取走后自行调度各模块。
 * 不使用 SysTick：SYSTEM/delay 的 delay_us/ms 会反复重配 SysTick 计数值。
 * 时钟前提：SYSCLK = 72MHz（system_stm32f10x.c 中 SYSCLK_FREQ_72MHz）。
 * --------------------------------------------------------------------------- */

void    TIM3_10ms_Init(void);
uint8_t Tick_10ms_Take(void);   /* 有未处理的节拍返回 1 并消费，否则返回 0 */

#endif
```

### 4.3 `HARDWARE/TIMER/timer.c` 完整代码

```c
#include "timer.h"

static volatile uint8_t s_tick_10ms;

void TIM3_10ms_Init(void)
{
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef        nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* TIM3 挂 APB1；APB1 预分频不为 1 时定时器时钟 x2 = 72MHz。
     * 72MHz / (7199+1) = 10kHz，(99+1) 个脉冲 = 10ms。 */
    tim.TIM_Period            = 99;
    tim.TIM_Prescaler         = 7199;
    tim.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim.TIM_CounterMode       = TIM_CounterMode_Up;
    tim.TIM_RepetitionCounter = 0;      /* 仅 TIM1/TIM8 使用，TIM3 忽略 */
    TIM_TimeBaseInit(TIM3, &tim);

    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    /* NVIC 分组由 main() 里的 NVIC_Configuration() 统一设为 Group_2 */
    nvic.NVIC_IRQChannel                   = TIM3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 1;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    s_tick_10ms = 0;
    TIM_Cmd(TIM3, ENABLE);
}

uint8_t Tick_10ms_Take(void)
{
    if (s_tick_10ms == 0)
        return 0;
    s_tick_10ms = 0;   /* 若此刻恰好来一次中断，最多少采样一拍，不影响正确性 */
    return 1;
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        s_tick_10ms = 1;   /* 中断里只置标志，业务一律留给主循环 */
    }
}
```

---

## 5. 第 4 步：新建 `HARDWARE/DEBUG_UART/`

### 5.1 为什么单独一个模块

上板后需要不依赖调试器就能确认三件事：固件已运行、时钟与波特率正确、KEY → TIM3 → 状态机 → 队列 → 分发这条链路通。USART1（PA9 = TX，115200 8N1）只发不收，上电打印两行启动信息，之后每分发一个事件打印一行 `KEY: xxx`。

不使用工程里已有的 `SYSTEM/usart/usart.c`：该文件是模板残留，`fputc` 指向 USART3，`uart_init()` 与 `USART1_IRQHandler` 被未定义的宏 `EN_USART3_RX` 整段裁掉，且未编进 Keil 工程。同时避开 `printf` 重定向，因为工程已启用 microLIB（`<useUlib>1</useUlib>`），模板那套半主机写法不配套。

命名用 `debug_uart` 而不是 `usart1.c`：这一路不承担业务协议，只是调试输出通道；以后搬入 `usart2.c`（4G）、`usart4.c`（RS485）时可以并行存在，也不会与 `SYSTEM/usart/usart.h` 撞名。

### 5.2 `HARDWARE/DEBUG_UART/debug_uart.h` 完整代码

```c
#ifndef __DEBUG_UART_H
#define __DEBUG_UART_H

/* ---------------------------------------------------------------------------
 * 调试串口模块（USART1，PA9 = TX，115200 8N1，只发不收）
 *
 * 职责：把字符串发到 PC 调试口。上板时用它确认固件在跑、
 *       并观察按键事件是否被主循环分发出来。
 *
 * 边界：不接收、不开中断、不重定向 printf，因此不依赖 microLIB / 半主机，
 *       也不占用 NVIC 或 DMA。
 *
 * 用法：上电调一次 DEBUG_UART_Init()，之后随时调 DEBUG_UART_SendString()。
 * 约束：发送是轮询等待 TXE，只能在主循环里调用，不要在中断里调用。
 * --------------------------------------------------------------------------- */

void DEBUG_UART_Init(void);
void DEBUG_UART_SendString(const char *str);

#endif
```

### 5.3 `HARDWARE/DEBUG_UART/debug_uart.c` 完整代码

```c
#include "debug_uart.h"
#include "stm32f10x.h"

/* ===========================================================================
 * 调试串口实现：USART1 只发不收，轮询发送。
 * 与原工程调试口一致：PA9 = TX，115200 8N1（见 qibiji/HARDWARE/USART/usart1.c）。
 * 不使用 printf/fputc：本工程 uvprojx 已启用 microLIB（<useUlib>1</useUlib>），
 * 模板里那套 __use_no_semihosting + struct __FILE 与 microLIB 不配套。
 * =========================================================================== */

#define DEBUG_UART_BAUDRATE  115200u

void DEBUG_UART_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    /* 时钟：USART1 与 GPIOA 都挂在 APB2。不做引脚重映射，不需 AFIO 时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    USART_DeInit(USART1);

    /* 只发不收：只配 PA9。PA10 留给以后要加接收时再配置。 */
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = DEBUG_UART_BAUDRATE;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx;

    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

void DEBUG_UART_SendString(const char *str)
{
    const char *p;

    if (str == (const char *)0)
        return;

    p = str;
    while (*p != '\0')
    {
        /* TXE = 1 表示发送数据寄存器已空，可以写下一字节。
         * 读 SR + 写 DR 这个顺序同时会清掉 TC，所以末尾等 TC 才有意义。 */
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        {
        }
        USART_SendData(USART1, (uint16_t)(uint8_t)*p);
        p++;
    }

    /* 等最后一字节真正移出，避免连续两行粘在一起。 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
    {
    }
}
```

---

## 6. 第 5 步：重写 `USER/main.c`

### 6.1 接线说明

初始化顺序固定：`NVIC_Configuration()` 必须在所有中断初始化之前（NVIC 分组 2）→ 串口 → 按键 → TIM3。主循环分三步：有节拍才采样、每圈都取一次事件、有事件才分发。

`App_DispatchKey()` 是业务动作的唯一入口。目标版本只把键码写进 `volatile g_last_key`（调试器可观察）并打印到串口；`Page_OnKey / Motor_OnKey / Backlight_OnKey` 保留为注释占位，以后接模块时只改这一处。

### 6.2 完整目标代码

```c
#include "main.h"
#include "sys.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"

/* 调试观察用：最后一次被分发出去的键码，调试器里可直接查看 */
volatile key_code_t g_last_key = KEY_NONE;

/* 键码 -> 文本，仅供调试串口输出；顺序必须与 key.h 的枚举一致。
 * 这只是「给这一颗键起个名字」，不是业务动作，也不参与判断。 */
static const char * const s_key_name[] =
{
    "NONE", "UP", "DOWN", "STOP", "SET", "CANCEL"
};

/* 按键事件的唯一出口。
 * 以后接入页面 / 电机 / 背光时，在这里显式调用对应模块，
 * 不要让各模块自己去读一个全局键码变量。 */
static void App_DispatchKey(key_code_t key)
{
    const char *name;

    g_last_key = key;

    if ((key >= KEY_NONE) && (key <= KEY_CANCEL))
        name = s_key_name[key];
    else
        name = "UNKNOWN";

    /* 上板验证用：每分发一个按键事件，就在 PC 串口助手上看到一行 KEY: xxx。
     * 发送是轮询的，几个字节约 1ms，只在主循环里调用。 */
    DEBUG_UART_SendString("KEY: ");
    DEBUG_UART_SendString(name);
    DEBUG_UART_SendString("\r\n");

    /* Page_OnKey(key);      页面模块接入后启用 */
    /* Motor_OnKey(key);     电机模块接入后启用 */
    /* Backlight_OnKey(key); 背光模块接入后启用 */
}

int main(void)
{
    key_code_t key;

    NVIC_Configuration();   /* NVIC 分组 2，须在所有中断初始化之前 */
    DEBUG_UART_Init();      /* USART1 115200 8N1，PA9 = TX，只发不收 */
    KEY_Init();
    TIM3_10ms_Init();

    /* 上电提示：看到这两行说明程序已经跑起来、时钟和串口都对。 */
    DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
    DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

    while (1)
    {
        /* 1) 固定节拍：每 10ms 采样一次，去抖时间与主循环速度无关 */
        if (Tick_10ms_Take())
            KEY_Tick();

        /* 2) 取走一个事件，取走即清；没有事件就什么都不做 */
        key = KEY_Pop();
        if (key != KEY_NONE)
            App_DispatchKey(key);

        /* 3) 后续模块的 _Process() 依次加在这里 */
    }
}
```

---

## 7. 第 6 步：修改 `USER/SRTUV10.uvprojx`

只改两处，均为文本编辑（也可在 Keil 的 Options for Target → C/C++ → Include Paths 和工程树里操作）。

### 7.1 IncludePath（约第 338 行）

改前：

```xml
<IncludePath>..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\KEY</IncludePath>
```

改后：

```xml
<IncludePath>..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\KEY;..\HARDWARE\TIMER;..\HARDWARE\DEBUG_UART</IncludePath>
```

### 7.2 HARDWARE 组的文件清单（约第 509 行，`key.h` 那个 `<File>` 节点之后、`</Files>` 之前）

在原有 `key.h` 节点后面追加 4 个 `<File>` 节点：

```xml
            <File>
              <FileName>timer.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\TIMER\timer.c</FilePath>
            </File>
            <File>
              <FileName>timer.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\TIMER\timer.h</FilePath>
            </File>
            <File>
              <FileName>debug_uart.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\DEBUG_UART\debug_uart.c</FilePath>
            </File>
            <File>
              <FileName>debug_uart.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\DEBUG_UART\debug_uart.h</FilePath>
            </File>
```

`FileType 1` = C 源文件，`5` = 头文件。工程为标准库工程，`stm32f10x_tim.c`、`stm32f10x_gpio.c`、`stm32f10x_rcc.c`、`stm32f10x_usart.c`、`misc.c` 均已编入，无需再添加。工程为 C90 模式（`<uC99>0</uC99>`），上面所有代码都按 C90 写：变量声明在块首、无 `//` 之外的新语法、无单行 `for` 内声明。

---

## 8. 验证方法

### 8.1 Keil 编译

点一次 Build，确认 0 Error、0 Warning，链接通过（`TIM3_IRQHandler` 会覆盖启动文件中的弱符号）。本机没有 armcc，之前的验证是 host 端 gcc + 桩头文件做的，不能替代 Keil 真机编译。

若报 `undefined symbol`，优先检查第 7.2 节的 4 个文件是否真的加进了编译组；若报找不到头文件，检查第 7.1 节的 IncludePath。

### 8.2 上板观察

接线（交叉、必须共地）：

```
USB-TTL  RX   ←→  PA9   (板子 TX)
USB-TTL  GND  ←→  GND
USB-TTL  TX   可不接    (固件只发不收)
```

PC 串口助手（SSCOM 等）设置：端口选 `USB-SERIAL CH340` 对应的 COM，115200、8 数据位、1 停止位、无校验、无流控，不勾 HEX 显示。

**操作顺序**：先点「打开串口」，再给板子复位或重新上电。启动信息只在 `main()` 开头打印一次，板子先上电再开串口会收不到。

预期输出：

```
qibiji_zxl KEY test ready
USART1 115200 8N1, PA9=TX
```

按 K5 STOP 出现 `KEY: STOP`。

### 8.3 行为检查清单

| 操作 | 预期 | 对应代码 |
|---|---|---|
| 短按 20ms | 不输出 | 消抖期未满 3 拍即松开，退回 IDLE |
| 稳定按下 30ms | 恰好输出一行 | `KEY_ST_DEBOUNCE` 计数到 3 |
| 按住不松 6 秒 | 仍只有一行 | `KEY_ST_PRESSED` 不产生事件，`cnt` 饱和 |
| 松开后再按 | 再输出一行 | 松开回到 IDLE |
| 抖动（按下过程中反复松开） | 不误触发 | 消抖期任一拍松开立即清零 |
| 同时按 UP + DOWN | 输出两行 | 5 个键各自独立 |
| 快速连按超过 4 次 | 最多输出 4 行 | 队列深度 4，满则丢弃 |

无输出时的排查顺序：上电顺序 → 共地与波特率 → 有启动信息但按键无反应则查按键侧（低电平有效）与 PA9 是否被占用。

---

## 9. 实施时要遵守的约束

1. **PA15 的 JTAG 复用**：PA15 复位后默认是 JTAG 的 JTDI。目标代码沿用原工程接法，只读 IDR 电平，不禁用 JTAG。若以后要把 PA15 当普通 IO 输出，需先 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)` 并改用 SWD 调试。
2. **`KEY_Tick()` 与 `KEY_Pop()` 必须在主循环同一上下文调用**，不要在中断里调 `KEY_Pop()`；队列的 Push/Pop 没有加锁。
3. **`DEBUG_UART_SendString()` 是轮询等待，只能在主循环调用**，不要在中断里调用。
4. **双键冲突保护放在 `App_DispatchKey()`**：目标版本 UP 与 DOWN 同时按下会各报一次，接电机时「同时上升下降」的互斥判断要写在分发处，不要改 key 模块。
5. **采样是每 10ms 一次，`KEY_Pop()` 是主循环每圈调用**，两者节奏不同，不要合并。
6. **键码数值 1~5 保持不变**，旧版页面代码里的 `KEY_UP=1` 等数值仍可对应；但宏名已改为枚举类型，引用处要包含 `key.h` 并用 `key_code_t`。
7. **不要删除 `SYSTEM/usart/usart.c`**，也不要把它加进编译：它是失效的模板残留，保持原状即可。
8. **`NVIC_Configuration()` 必须先于 `TIM3_10ms_Init()`**，否则 NVIC 分组未设置，中断优先级配置不生效。
9. 目标版本**没有**长按连发、组合键、按键 LED 控制、接收中断。需要时作为独立需求另行添加，不要塞回 key 模块。

---

## 10. 变更对照速查

| 起点 | 目标 | 说明 |
|---|---|---|
| `u8 KeyNum`（全局） | `KEY_Pop()` 返回值 | 事件出口唯一，取走即清 |
| `KEY_Scan / Get_KeyVal / KEY_Process` | `KEY_Init / KEY_Tick / KEY_Pop` | 5 个函数收为 3 个 |
| `flag10ms`（TIM2 5ms×2） | `Tick_10ms_Take()`（TIM3 10ms） | 固定时基，不依赖主循环速度 |
| 5 个 `if/else if` + 各 5 个计数器 | 引脚映射表 + 5 个 `{state, cnt}` | 消除重复；每键独立 |
| `cnt++` 无上限 | 饱和计数 `cnt < 3` 才自增 | 消除长按溢出后自动重复上报 |
| `key_up` 恒为 1 | 删除 | 死逻辑 |
| `key_local_remote_change` / `key_setting_change` | 删除 | 全工程零引用 |
| `KeyLed_Ctl()` | 删除 | 悬空声明；LED 职责归 LED 模块 |
| `KEY_ENTER/LEFT/RIGHT` | `KEY_STOP/KEY_SET/KEY_CANCEL` | 与面板丝印对齐，值 3/4/5 不变 |
| `u8` 与 `uint8_t` 混用 | `uint8_t` / `key_code_t` | 类型统一 |
| `KEY_Process()` 定义无参、声明 `void` | 统一 | 签名一致 |
| 空 `while(1)` | 初始化 + 三步主循环 | 模块接线 |
| 无调试输出 | USART1 打印 | 上板可验证 |

**未改动的部分**：引脚分配（PD15 / PA15 / PA11 / PA12 / PC9）、浮空输入模式、低电平有效、键码数值 1~5、PA15 JTAG 约束、72MHz 时钟前提、标准库工程配置。

---

> 撤回后的代码状态可通过 git 提交 `29dab9f empty_qibiji_zxl` 核对。目标版本的完整文件另在临时目录 `C:/Users/JessamynAdmin/AppData/Local/Temp/qibiji_zxl_target_backup/` 留有一份备份。
