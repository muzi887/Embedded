# 按键模块（KEY）现在的逻辑

> 第 1 节列出当前版本相对以前版本的修改；第 2 节起讲当前代码怎么跑：按一次按键，从「引脚被拉低」到「主程序拿到键码」，按执行顺序逐站串起来。
> 每站都贴真实代码（路径见小节标题），按执行顺序排，不按文件顺序排。
> 涉及源码：`qibiji_zxl/HARDWARE/KEY/key.c/.h`、`HARDWARE/TIMER/timer.c/.h`、`USER/main.c`。

---

## 1. 当前版本相对以前版本做了哪些修改

「以前」指原工程 `qibiji/HARDWARE/KEY/key.c/.h`，以及重构前从该工程拷入 `qibiji_zxl/HARDWARE/KEY/` 的那一版（两版逐行一致，重构前仅删掉 4 个业务头文件、注释掉 `flag10ms` 判断和 `KeyLed_Ctl()` 实现）。「当前」指本文件描述的代码，位于 `qibiji_zxl/`。

### 1.1 改动清单

| # | 项目 | 以前 | 当前 |
|---|---|---|---|
| 1 | 对外接口 | `key.h` 暴露 5 个函数 `KEY_Init / KEY_Scan / KEY_Process / Get_KeyVal / KeyLed_Ctl`，另有全局变量 `extern u8 KeyNum` | 只保留 `KEY_Init / KEY_Tick / KEY_Pop` 三个函数和 `key_code_t` 枚举；`KEY_ReadPins`、`KEY_QueuePush` 及全部状态变量改为 `static` |
| 2 | 事件输出 | `KEY_Process()` 把键码写入全局 `KeyNum`；`Dispaly.c`、`dc_motor.c` 等各自读取该变量，没有「读过即清除」 | `KEY_Pop()` 是唯一出口，从私有环形队列 `s_queue[4]` 取出一个键码并把 `s_q_count` 减一；同一事件只返回一次 |
| 3 | 分发位置 | 页面、电机、背光模块各自读 `KeyNum` 自行判断 | `USER/main.c` 的 `App_DispatchKey()` 集中分发，页面 / 电机 / 背光为三个注释占位 |
| 4 | 采样节拍 | `Get_KeyVal()` 靠 `flag10ms` 节流（原工程由 TIM2 的 5ms 中断两次置位）；重构前该判断被注释，采样变为主循环每圈一次，去抖时间随主循环速度变化 | 新增 `HARDWARE/TIMER/timer.c/.h`：TIM3 每 10ms 置 `s_tick_10ms`，主循环用 `Tick_10ms_Take()` 取标志后才调 `KEY_Tick()`；去抖固定 3 × 10ms = 30ms |
| 5 | 去抖实现 | `Get_KeyVal()` 用 5 个 `if / else if` 分支，各带一个计数器和 4 行互相清零，约 90 行；`else if` 使同时按下只处理优先级最高的一颗 | `KEY_ReadPins()` 一次读成位图，`KEY_Tick()` 用循环跑 5 个独立状态机（`KEY_ST_IDLE / KEY_ST_DEBOUNCE / KEY_ST_PRESSED`），键码由 `s_key_code[]` 给出；每颗键独立判定 |
| 6 | 长按行为 | 计数器 `++` 写在 `if (cnt == 3)` 之前，达到 3 后继续自增到 255 溢出回 0，再次数到 3 会重写 `keyvalue[]`，长按约每 2.5 秒重复上报一次 | `cnt` 饱和计数（`if (cnt < KEY_DEBOUNCE_TICKS) cnt++`），进入 `KEY_ST_PRESSED` 后锁住，必须松开回到 `KEY_ST_IDLE` 才能再次触发 |
| 7 | 死逻辑与死变量 | `key_up` 初值 1，按下分支从未置 0，恒为 1；`key_local_remote_change`、`key_setting_change` 全工程无引用 | 全部删除 |
| 8 | 键码命名 | `KEY_ENTER`(PA11)、`KEY_LEFT`(PA12)、`KEY_RIGHT`(PC9) 与面板丝印 STOP / SET / CAN 不符；`KEY_UP` 与 `KEY_UP_VAL` 分别指键码和引脚电平 | 改为 `KEY_STOP`、`KEY_SET`、`KEY_CANCEL`；引脚宏改为 `KEY_PIN_UP` 等 `KEY_PIN_*`；键码数值 1~5 保持不变 |
| 9 | 模块职责 | `KeyLed_Ctl()` 定义在 `key.c` 并操作 `LED_UP/LED_DN`；重构前实现被注释、头文件仍留声明（悬空声明）；`key.c` 依赖 `led.h`、`delay.h`、`dc_motor.h`、`timer.h`、`main.h` | LED 控制从 key 模块移除；`key.c` 只 `#include "key.h"`，除 `sys.h` 外不依赖任何业务头文件 |
| 10 | 类型与初始化 | `u8` 与 `uint8_t` 混用；`KEY_Process()`、`KEY_Scan()` 定义写空参数 `()`、头文件写 `(void)`；输入引脚配置了 `GPIO_Speed_50MHz` | 统一 `uint8_t` / `key_code_t`，函数签名一致；删除对输入无意义的 `GPIO_Speed` 配置；`KEY_Init()` 增加队列头尾与计数的清零 |
| 11 | 工程接线 | 重构前 `USER/main.c` 是空 `while(1)`，未调用 `KEY_Init()` | `main.c` 调用 `KEY_Init()`、`TIM3_10ms_Init()`，主循环里取节拍、取事件、分发；`USER/SRTUV10.uvprojx` 增加 `timer.c/.h` 与 `..\HARDWARE\TIMER` 包含路径 |

### 1.2 由上述修改直接产生的行为差异

1. 同一按键事件不再可能被多个模块或主循环多圈重复读取。
2. 去抖时间固定为 30ms，不再随主循环负载变化。
3. 长按不再自动重复上报。
4. 两颗键同时按下会各报一次（以前只处理优先级最高的一颗）；UP 与 DOWN 的冲突保护需在 `App_DispatchKey()` 中处理。
5. 队列深度 4，满时丢弃新事件，不阻塞主循环。
6. 原工程 `main.c` 中 `uart3_at_sequence_main_loop_tick()` 返回真时 `continue` 会跳过 `KEY_Process()`，使上一轮的 `KeyNum` 被重复消费；当前用「取出即减计数」的队列，跳过某一圈不会造成重复处理。

未改动的内容：5 个引脚的分配、浮空输入模式、低电平表示按下、键码数值 1~5、PA15 复位后默认作为 JTAG JTDI 且当前仅只读 IDR 的约束，以及原工程 `qibiji/` 与 `docs/` 下的其它文档。

---

## 2. 先说这块代码是干什么的

面板上有 5 个按键。这块代码只做三件事：

1. **看一眼**：5 个引脚现在是按下（`0`）还是松开（`1`）。
2. **连续确认**：按下瞬间电平会抖几下，要连着确认 3 次都按着才算真按了（消抖）。
3. **报一次**：把「按了哪一颗」写入模块内的事件队列，一次按下只写入一次。

至于「这个键现在该翻页、该改参数、还是该让闸门停下来」，**不归它管**——同一颗键在首页和设置页含义不同，判断权在主程序手里。

> 对外只输出物理键码 `key_code_t`；主程序根据当前页面状态把键码映射为业务动作。

---

## 3. 整条链路一图看清

```
   [中断侧]                              [主循环侧]

  TIM3 每 10ms 溢出
        │
        └─→ s_tick_10ms = 1  ─────────────┐
                                          ▼
                              main: Tick_10ms_Take()  取走标志
                                          │ 有节拍才往下
                                          ▼
                              KEY_Tick()
                                          │
                    ┌─────────────────────┴─────────────────────┐
                    ▼                                           ▼
          KEY_ReadPins()  读 5 路引脚 → 位图        5 个独立状态机
                                                    IDLE → DEBOUNCE → PRESSED
                                                          │ 连续 3 拍按下
                                                          ▼
                                              KEY_QueuePush(键码)
                                                          │
                                                          ▼
                                               私有环形队列 s_queue[4]
                                                          │
                              main 每圈都调 ──────────────┤
                                                          ▼
                                              KEY_Pop()  取走即清
                                                          │ 非 KEY_NONE
                                                          ▼
                                              App_DispatchKey(key)
```

**两个节奏要分清**：采样是**每 10ms 一次**（跟着 TIM3）；取事件是**主循环每圈一次**（`KEY_Pop()` 在 `if` 外面）。事件一入队，下一圈主循环就会被取走。

---

## 4. 按执行顺序，逐站讲

### 第 1 站 · 上电把硬件准备好 `USER/main.c:25-27`

```c
NVIC_Configuration();   /* NVIC 分组 2，须在所有中断初始化之前 */
KEY_Init();
TIM3_10ms_Init();
```

`KEY_Init()` 做两件事（`key.c:59-83`）：开 GPIOA/C/D 的时钟，把 5 个脚配成**浮空输入**；再把队列的头尾和计数清零。

```c
gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
gpio.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_15; /* STOP/SET/DOWN */
GPIO_Init(GPIOA, &gpio);
gpio.GPIO_Pin   = GPIO_Pin_9;                               /* CANCEL */
GPIO_Init(GPIOC, &gpio);
gpio.GPIO_Pin   = GPIO_Pin_15;                              /* UP */
GPIO_Init(GPIOD, &gpio);

s_q_head = 0;  s_q_tail = 0;  s_q_count = 0;
```

硬件上每个键已有 4.7K 上拉电阻把引脚拉到 `1`，旁边还有 100nF 电容先滤掉毛刺；按下时接地变成 `0`。所以**代码里 `== 0` 就是「按下了」**，硬件滤波之后再由软件的 30ms 去抖确认一次。

引脚与键码的对应关系写在 `key.c:19-35`，是模块私有的：

| 面板丝印 | 引脚 | 位序 `i` | 键码 |
|---|---|---|---|
| K1 UP | PD15 | 0 | `KEY_UP` = 1 |
| K2 DOWN | PA15 | 1 | `KEY_DOWN` = 2 |
| K5 STOP | PA11 | 2 | `KEY_STOP` = 3 |
| K4 SET | PA12 | 3 | `KEY_SET` = 4 |
| K3 CAN | PC9 | 4 | `KEY_CANCEL` = 5 |

> 位序 `i` 必须和 `s_key_code[]` 的顺序一一对应，后面状态机循环就是靠这个下标取键码的。

### 第 2 站 · 每 10ms 产生一次节拍 `HARDWARE/TIMER/timer.c`

TIM3 配置成 `72MHz / 7200 = 10kHz`，数 100 个脉冲就是 10ms（`timer.c:14-19`）。中断里**只置一个标志，不干任何业务**：

```c
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        s_tick_10ms = 1;   /* 中断里只置标志，业务一律留给主循环 */
    }
}
```

标志是 `static volatile uint8_t`，只在 TIMER 模块内部可见。

> 节拍**用 TIM3 而不是 SysTick**：`SYSTEM/delay` 的 `delay_us/ms()` 会反复重配 SysTick 的计数值，两者共用会导致延时不准、节拍丢失；也不能用 `delay_ms()` 阻塞等待，那会把主循环卡住 30ms。

### 第 3 站 · 主循环取节拍标志，有节拍才采样 `USER/main.c:29-38`

```c
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
```

`Tick_10ms_Take()`（`timer.c:35-41`）是「有未处理的节拍就返回 1 并清掉标志，否则返回 0」：

```c
uint8_t Tick_10ms_Take(void)
{
    if (s_tick_10ms == 0)
        return 0;
    s_tick_10ms = 0;
    return 1;
}
```

这样无论主循环跑得多快或多忙，`KEY_Tick()` 都严格每 10ms 执行一次 —— **消抖时间恒为 3 × 10ms = 30ms**，不会随系统负载漂移。

### 第 4 站 · 把 5 路引脚读成一张位图 `key.c:87-98`

```c
static uint8_t KEY_ReadPins(void)
{
    uint8_t bits = 0;

    if (KEY_PIN_UP     == 0) bits |= (uint8_t)(1u << KEY_IDX_UP);
    if (KEY_PIN_DOWN   == 0) bits |= (uint8_t)(1u << KEY_IDX_DOWN);
    if (KEY_PIN_STOP   == 0) bits |= (uint8_t)(1u << KEY_IDX_STOP);
    if (KEY_PIN_SET    == 0) bits |= (uint8_t)(1u << KEY_IDX_SET);
    if (KEY_PIN_CANCEL == 0) bits |= (uint8_t)(1u << KEY_IDX_CANCEL);

    return bits;      /* bit = 1 表示该键此刻被按下 */
}
```

一次读出全部 5 个键，得到 `bits`。后面循环里 `down = (bits >> i) & 1u` 就是取第 `i` 颗键此刻的状态。

### 第 5 站 · 五颗键各自一台状态机 `key.c:133-188`

这是整个模块唯一做判断的地方。每颗键有自己独立的 `s_fsm[i]`，内容是 `{state, cnt}`；三个状态是 `key.c:37-48` 定义的：

| 状态 | 含义 |
|---|---|
| `KEY_ST_IDLE` | 松开，等着被按 |
| `KEY_ST_DEBOUNCE` | 疑似按下，正在连续确认（`cnt` 记连着按了几拍） |
| `KEY_ST_PRESSED` | 已确认并报过一次，**锁住等松开** |

判定主体（`key.c:133-188`，`default` 分支是把异常状态拉回 IDLE 的兜底）：

```c
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
                f->cnt   = 1;             /* 第 1 拍按下 */
            }
            break;

        case KEY_ST_DEBOUNCE:
            if (!down)                    /* 抖动：推倒重来 */
            {
                f->state = KEY_ST_IDLE;
                f->cnt   = 0;
                break;
            }
            if (f->cnt < KEY_DEBOUNCE_TICKS)
                f->cnt++;                 /* 饱和，绝不加到溢出 */
            if (f->cnt >= KEY_DEBOUNCE_TICKS)
            {
                f->state = KEY_ST_PRESSED;
                KEY_QueuePush(s_key_code[i]);   /* 一次按下只报一次 */
            }
            break;

        case KEY_ST_PRESSED:
            if (!down)                    /* 松开才能再触发下一次 */
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

三个关键点：

- **抖动推倒重来**：消抖期间只要有一拍松开，立刻回 `IDLE`、`cnt` 清零，不会误报。
- **`cnt` 饱和**：`if (f->cnt < 3) f->cnt++`，到 3 就停，永远不会一路加到 255 溢出回 0。
- **报完就锁**：一进入 `PRESSED` 就不再产生事件，一直按着也不会连发；**必须完全松开**回到 `IDLE` 才能触发下一次。

`KEY_DEBOUNCE_TICKS = 3`、`KEY_COUNT = 5`、`KEY_QUEUE_SIZE = 4` 都在 `key.c:11-13`，改行为只动这三个宏。

### 第 6 站 · 把键码写入事件队列 `key.c:101-109`

```c
static void KEY_QueuePush(key_code_t code)
{
    if (s_q_count >= KEY_QUEUE_SIZE)
        return;                 /* 队列满：丢弃新事件，绝不阻塞 */

    s_queue[s_q_tail] = code;
    s_q_tail = (uint8_t)((s_q_tail + 1u) % KEY_QUEUE_SIZE);
    s_q_count++;
}
```

`s_queue[4]`、`s_q_head`、`s_q_tail`、`s_q_count` 全是 `static`，**外部看不见也改不了**。队列深度 4，正常手速用不满；真满了就丢新事件，绝不卡住主循环。

### 第 7 站 · 取走即清 `key.c:111-122`

```c
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
```

一个事件**只会被返回一次**；没有事件就返回 `KEY_NONE`（值 0）。队列的 Push 和 Pop 都在主循环里，没有并发访问，不需要加锁或关中断。

> 因此 `KEY_Tick()` 和 `KEY_Pop()` 必须在**同一个执行上下文（主循环）**里调用，不要在中断里调 `KEY_Pop()`。

### 第 8 站 · 主程序决定这个键干什么 `USER/main.c:12-19`

```c
static void App_DispatchKey(key_code_t key)
{
    g_last_key = key;

    /* Page_OnKey(key);      页面模块接入后启用 */
    /* Motor_OnKey(key);     电机模块接入后启用 */
    /* Backlight_OnKey(key); 背光模块接入后启用 */
}
```

目前只把键码写进 `volatile g_last_key`，方便调试器里直接观察。键码是**物理键名**（STOP / SET / CAN），「这一键在设置页是不是确认」由页面自己判断。

以后接页面、电机、背光，**只需在这里加调用**，不用再动 key 模块，也不要让那些模块回头去读某个全局键码。

---

## 5. 跟着一次按键走一遍

以按下 **K5 STOP（PA11）并按住不放** 为例，`t` 是 10ms 节拍：

| 时刻 | PA11 | STOP 的状态 | `cnt` | 队列 | `KEY_Pop()` |
|---|---|---|---|---|---|
| 上电 | 1 松开 | `IDLE` | 0 | 空 | `KEY_NONE` |
| t=10ms 按下 | 0 | `IDLE → DEBOUNCE` | 1 | 空 | `KEY_NONE` |
| t=20ms | 0 | `DEBOUNCE` | 2 | 空 | `KEY_NONE` |
| t=30ms | 0 | `DEBOUNCE → PRESSED` | 3 | `[KEY_STOP]` | — |
| 同圈稍后 | 0 | `PRESSED` | 3 | 空 | **`KEY_STOP` → `App_DispatchKey`** |
| t=40ms ~ 6s 一直按着 | 0 | `PRESSED`（锁住） | 3 | 空 | `KEY_NONE` |
| 松开后的第一个 10ms | 1 | `PRESSED → IDLE` | 0 | 空 | `KEY_NONE` |
| 再按一次 | 0 | `IDLE → DEBOUNCE` | 1 | 空 | `KEY_NONE` |

另外几种情形：

| 情形 | 结果 |
|---|---|
| 只按 20ms 就松开（短按） | 第 3 拍没到就回 `IDLE`，**不报事件** |
| 按一拍 / 松一拍交替抖动 | 每次都在 `DEBOUNCE` 被推倒重来，**不误触发** |
| UP 和 DOWN 同时按下 | 两台状态机各自走到 `PRESSED`，**各报一次**（先 `KEY_UP` 后 `KEY_DOWN`） |
| 连续产生第 5 个事件而队列已满 | 第 5 个被丢弃，前 4 个正常可取，**不阻塞** |

> 双键各报一次是有意的行为：以后接电机时，UP + DOWN 同时按的冲突保护要放在 `App_DispatchKey()` 里，不能靠按键层屏蔽。

---

## 6. 对外只有三个函数 `HARDWARE/KEY/key.h`

```c
typedef enum
{
    KEY_NONE = 0,
    KEY_UP, KEY_DOWN, KEY_STOP, KEY_SET, KEY_CANCEL   /* 1 ~ 5 */
} key_code_t;

void       KEY_Init(void);   /* 上电调一次 */
void       KEY_Tick(void);   /* 只在 10ms 节拍里调用 */
key_code_t KEY_Pop(void);    /* 主循环每圈调，取走即清 */
```

`key.c` 内部的 `KEY_ReadPins`、`KEY_QueuePush` 以及所有状态变量都是 `static`，头文件里看不到。整个模块除 `sys.h` 外不依赖任何业务头文件。

---

## 7. 几个要记住的约定

| # | 约定 | 为什么 |
|---|---|---|
| 1 | 采样固定 10ms 一拍，消抖恒为 30ms | 去抖时间不随主循环快慢变化 |
| 2 | 一次按下只报一次；长按不连发 | `PRESSED` 锁住 + `cnt` 饱和不溢出 |
| 3 | 必须完全松开才能再触发 | 状态机显式写死，不靠计数器巧合 |
| 4 | 事件取走即清，只分发一次 | `KEY_Pop()` 是唯一出口 |
| 5 | `KEY_Tick` / `KEY_Pop` 都在主循环，中断里不调 `KEY_Pop` | 队列无锁，跨上下文会破坏 `s_q_count` |
| 6 | 队列深度 4，满则丢新事件 | 宁可丢一个键，也不卡住主循环 |
| 7 | 键码只表示「哪颗键」，不表示业务动作 | 业务判断归 `App_DispatchKey` / 页面 |
| 8 | PA15 复位后默认是 JTAG 的 JTDI | 现在只读 IDR 电平、没禁用 JTAG；若以后要把 PA15 当普通 IO 输出，需先 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)` 并改用 SWD 调试（见 `key.c:76-78`） |
| 9 | 节拍用 TIM3，不占 SysTick | `delay_us/ms()` 会反复重配 SysTick 计数值 |

---

## 8. 目前还没接的部分

- `App_DispatchKey()` 里页面 / 电机 / 背光三行仍是注释占位，接模块时在这里显式调用。
- 面板指示灯的控制函数不在 key 模块里（它操作 `LED_UP/LED_DN`，归 LED 模块），搬 LED 模块时补上。
- 长按连发目前**不支持**；需要时应做成显式配置，不要靠计数溢出这类副作用。

**这套逻辑验证过**：源码按工程相同的 C90 模式（`-std=c90 -Wall -Wextra`）配合桩头文件编译 `key.c / timer.c / main.c` 零警告；另跑 17 项行为断言全部通过，覆盖空闲、短按 20ms 过滤、稳定 30ms 报一次、取走即清、长按 600 拍不连发、松开再按、抖动不误触发、双键各报一次、队列满丢弃、引脚映射。这是 host 端 + 桩的验证，**不是 Keil / 真机编译**，建议在 Keil 里 Build 一次确认链接。

---

## 9. 名词小词典

| 词 | 大白话 |
|---|---|
| 消抖 | 按下瞬间电平会抖，连着确认几次才算真按了 |
| 节拍（tick） | 一个固定节奏，这里每 10ms 响一下，采样按它走 |
| 状态机 | 把「松开 → 疑似按下 → 确认按下 → 一直按着 → 松开」写成几个明确的状态 |
| 环形队列 | 固定长度数组，头指针读、尾指针写，写到末尾后回绕到开头 |
| `static` | 把变量锁在模块内部，外面看不见也改不了 |
| 取走即清 | 从队列取出后计数减一，同一事件不会被再次返回 |
