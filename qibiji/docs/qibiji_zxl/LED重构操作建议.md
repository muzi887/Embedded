# LED 重构操作建议

> zxl 工程当前已删除全部 LED 代码（`OBJ/led.o` 是早期编译残留，源码不在工程里，`.uvprojx` 也不含 `led.c`，`main.c` 无任何调用）。本建议是把 LED 作为一个**独立硬件模块**恢复进来，并按 zxl 现有模块化规范（参照 `GATEIO`）重写：引脚集中定义、`Init`/`Tick` 两接口、状态查询驱动而不是业务文件散落写引脚。硬件板上已有 4 个 LED（PD11~PD14），不需要改板。

## 原工程现状（qibiji / qibiji_boot）

三个姊妹工程都保留了 `HARDWARE/LED/`：

| 引脚 | 宏名 | 含义 |
|---|---|---|
| PD11 | `LED_UP` | 上行指示 |
| PD12 | `LED_DN` | 下行指示 |
| PD13 | `LED_LINE` | 4G 在线指示 |
| PD14 | `LED_ERR` | 故障指示（原工程从未写入） |

原始用法有三类，全部是**直接写引脚宏**，散落在不同业务文件：

- `key.c` 的 `KeyLed_Ctl(u8 state)`：`state=0x01` → `LED_UP=1`；`0x02` → `LED_DN=1`；`0x00` → 全 0；
- `dc_motor.c` 的 `dcmotor_up/down/stop`：各自调用 `KeyLed_Ctl(0x01/0x02/0x00)`；
- `gprs_AT.c` / `4G.c`：直接写 `LED_LINE = 1`（在线）/ `0`（离线）。

原工程存在的问题：

1. **引脚写操作散落**在 key / dc_motor / gprs_AT / 4G 四个文件，改极性或换引脚要改多处；
2. **`KeyLed_Ctl` 放在 key.c** 名不副实——它控制的是电机方向灯，不是按键灯，且让电机模块反向依赖按键模块；
3. **`LED_ERR`（PD14）从未使用**，硬件资源浪费，故障没有本地可见指示；
4. **极性在两个工程里相反**：qibiji 主工程 `LED_Init()` 末尾 `GPIO_ResetBits`（全灭），qibiji_boot 里是 `GPIO_SetBits`（全亮）。结合 `KeyLed_Ctl` 和 `gprs_AT` 里"写 1 表示亮"的用法，推断板子是**高电平点亮**，boot 那次全亮应是上电自检意图——但这只是推断，最终极性必须以原理图为准（见 §5）。

## zxl 工程现状

- `HARDWARE/` 下没有 `LED/` 目录，全局搜索 `LED_Init / KeyLed / LED_UP` 等零命中；
- `DCMOTOR` 重构时已经把 `KeyLed_Ctl` 剥离（《DCMotor重构操作建议》明确"按键 LED 属于 LED 模块，不归电机"），电机现在只暴露 `DCMOTOR_GetDir()` 等查询接口，不再碰 LED；
- 主循环是 **10ms 节拍轮询**架构（`Tick_10ms_Take()` 里串 `KEY_Tick / ADC_Tick / GATEIO_Tick / DCMOTOR_Tick`），没有本地事件总线。因此 LED 应走同样的 `LED_Tick()` 轮询刷新，而不是引入新的发布-订阅机制。

## 重构目标

1. 新增独立 `HARDWARE/LED/` 模块，对外只暴露 `LED_Init()` 和 `LED_Tick()` 两个函数；
2. 引脚号、端口、电平极性集中在 `led.h` 用宏定义，硬件事实一处修改；
3. LED 的亮灭由 **LED_Tick 查询各模块状态** 决定，业务文件（dcmotor / gprs / key）一律不得直接写 `LED_x`；
4. 四个灯的语义在 §4 定死，`LED_ERR` 正式启用（堵转 / 超时 / 联锁未校准）；
5. 约束同 `GATEIO`：C90、ASCII 注释、不依赖业务头文件的循环包含。

## 新增文件清单

| 文件 | 职责 |
|---|---|
| `HARDWARE/LED/led.h` | 引脚宏、极性宏、`LED_Init` / `LED_Tick` 声明 |
| `HARDWARE/LED/led.c` | GPIO 初始化 + 状态查询刷新 |

> 需要把 `HARDWARE/LED/led.c` 加入 Keil 工程（`USER/SRTUV10.uvprojx`）并 **Rebuild**，避免链接到 `OBJ/` 里残留的旧 `led.o`。旧对象的引脚宏、函数签名都已过期，必须丢弃。

## §1 led.h

```c
#ifndef __LED_H
#define __LED_H

/* ---------------------------------------------------------------------------
 * Board indicator LEDs (hardware layer)
 *
 * Owns four indicator LEDs on the gate-control board:
 *   PD11 = UP    motor driving open
 *   PD12 = DN    motor driving close
 *   PD13 = LINE  4G link online (MQTT NORMAL)
 *   PD14 = ERR   fault (stall / timeout / interlock not calibrated)
 *
 * Responsibilities: clock/GPIO init once at boot; refresh all four outputs
 *   every 10 ms tick by querying DCMOTOR / GPRS state.
 * Not responsible: deciding WHY the motor runs or the link is up. LED only
 *   mirrors state that other modules already own.
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"   /* GPIO / RCC */

/* ---- pin map (all on GPIOD, push-pull 50 MHz) ---- */
#define LED_PORT        GPIOD
#define LED_UP_PIN      GPIO_Pin_11
#define LED_DN_PIN      GPIO_Pin_12
#define LED_LINE_PIN    GPIO_Pin_13
#define LED_ERR_PIN     GPIO_Pin_14
#define LED_ALL_PINS    (LED_UP_PIN | LED_DN_PIN | LED_LINE_PIN | LED_ERR_PIN)

/* Set to 0 if the board LEDs are wired active-low (common anode).
 * The original qibiji code writes "1 = on" everywhere, so the default is
 * active-high; verify against the schematic before first power-up. */
#define LED_ACTIVE_HIGH   1u

void LED_Init(void);   /* call once, after WDG_Init(), with the other GPIO inits */
void LED_Tick(void);   /* call once per 10 ms tick (inside Tick_10ms_Take()) */

#endif
```

## §2 led.c

```c
#include "led.h"
#include "dcmotor.h"    /* DCMOTOR_GetDir / DCMOTOR_GetLastStopReason */
#include "gprs_at.h"    /* gprs_is_ready */

#if (LED_ACTIVE_HIGH != 0)
  #define LED_PIN_ON(p)   GPIO_SetBits(LED_PORT, (p))
  #define LED_PIN_OFF(p)  GPIO_ResetBits(LED_PORT, (p))
#else
  #define LED_PIN_ON(p)   GPIO_ResetBits(LED_PORT, (p))
  #define LED_PIN_OFF(p)  GPIO_SetBits(LED_PORT, (p))
#endif

void LED_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    gpio.GPIO_Pin   = LED_ALL_PINS;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);

    /* all off at boot; if a power-on self-test (all on for ~200 ms) is
     * wanted, do it here before returning -- see §5. */
    LED_PIN_OFF(LED_ALL_PINS);
}

/* Refresh every 10 ms: mirror motor direction + link state + last fault. */
void LED_Tick(void)
{
    uint8_t dir = DCMOTOR_GetDir();

    /* direction lamps: mutually exclusive, off when stopped */
    if (dir == MOTOR_UP)       { LED_PIN_ON(LED_UP_PIN);  LED_PIN_OFF(LED_DN_PIN); }
    else if (dir == MOTOR_DOWN){ LED_PIN_ON(LED_DN_PIN);  LED_PIN_OFF(LED_UP_PIN); }
    else                       { LED_PIN_OFF(LED_UP_PIN); LED_PIN_OFF(LED_DN_PIN); }

    /* link lamp: on only in MQTT NORMAL state */
    if (gprs_is_ready() != 0u) LED_PIN_ON(LED_LINE_PIN);
    else                       LED_PIN_OFF(LED_LINE_PIN);

    /* fault lamp: stall / timeout / interlock-not-calibrated are faults;
     * reaching a limit (LIMIT_OPEN / LIMIT_CLOSE) is normal and must NOT
     * light the fault lamp. */
    switch (DCMOTOR_GetLastStopReason())
    {
        case MOTOR_STOP_STALL:
        case MOTOR_STOP_TIMEOUT:
        case MOTOR_STOP_INTERLOCK:
            LED_PIN_ON(LED_ERR_PIN);
            break;
        default:
            LED_PIN_OFF(LED_ERR_PIN);
            break;
    }
}
```

> 关于 `DCMOTOR_GetDir()` 在停机时的返回值：`dcmotor.c:410` 返回静态变量 `s_dir`，取值为 `dcmotor.h` 里的 `MOTOR_UP / MOTOR_DOWN / MOTOR_STOP`。上面的 `if/else if/else` 已覆盖"非 UP 非 DOWN 即全灭"，所以即使停机时 `s_dir` 保留最后一次方向值，也可以在这里显式判断——若希望停机一定灭灯，把 `else` 分支保留即可（当前实现就是这样）。

## §3 接入 main.c

初始化顺序：放在 `WDG_Init()` 之后、与其它 GPIO 类初始化同级（`KEY_Init` / `LCD12864_Init` 一带）。看门狗必须先启，`LED_Init` 本身是微秒级，不影响窗口。

```c
#include "led.h"          /* 新增 */

int main(void)
{
    NVIC_Configuration();
    DEBUG_UART_Init();
    WDG_Init();
    WDG_Feed();
    /* ... 复位来源打印 ... */

    KEY_Init();
    LED_Init();           /* 新增：4 个指示灯 GPIO，上电全灭 */
    delay_init();
    LCD12864_Init();
    /* ... 其余初始化不变 ... */
```

10ms 节拍里追加 `LED_Tick()`，放在 `DCMOTOR_Tick()` 之后（先让电机刷新状态，LED 再镜像，同一拍内读到的是最新值）：

```c
if (Tick_10ms_Take())
{
    KEY_Tick();
    LCD_BACKLIGHT_Tick();
    ADC_Tick();
    GATEIO_Tick();
    DCMOTOR_Tick();
    LED_Tick();           /* 新增：镜像方向 / 在线 / 故障 */
}
```

## §4 状态映射表（四个灯的语义，定案）

| 灯 | 引脚 | 点亮条件 | 状态来源 |
|---|---|---|---|
| UP | PD11 | `DCMOTOR_GetDir() == MOTOR_UP` | `dcmotor.h` |
| DN | PD12 | `DCMOTOR_GetDir() == MOTOR_DOWN` | `dcmotor.h` |
| LINE | PD13 | `gprs_is_ready() != 0`（MQTT NORMAL） | `gprs_at.h` |
| ERR | PD14 | 最后停机原因 ∈ {堵转, 超时, 联锁未校准} | `dcmotor.h` |

几点说明：

- **LINE 用 `gprs_is_ready()` 而不是 `g_atflag_online`**：`g_atflag_online` 在模块 banner 一出现就置 1，只代表 UART 就绪；`gprs_is_ready()` 要求状态机进到 `ST_NORMAL`（gprs_at.c:396），才是真正的 MQTT 在线。原工程 `gprs_AT.c` 在多处直接写 `LED_LINE`，语义混乱，这里统一为 NORMAL 态。如果现场希望"UART 一就绪就亮灯"，把条件换成 `g_atflag_online` 即可，改动只在 `led.c` 一处。
- **ERR 只在故障时亮**：到上/下限位（`MOTOR_STOP_LIMIT_OPEN / _CLOSE`）是正常到位，不亮故障灯；人工停机（`MOTOR_STOP_MANUAL`）也不亮。只有堵转、跑满 30s 超时、电流零点没标定导致联锁拒绝启动这三类才算故障。
- **ERR 不做闪烁**：本版先做常亮，逻辑最简单。如果现场需要"故障闪烁"（例如 2Hz），在 `LED_Tick` 里加一个 10ms 计数器，每 25 拍翻转一次 `LED_ERR_PIN` 即可，不影响其它三个灯。这是可选扩展，不阻塞本版落地。

## §5 极性与上电自检（必须先确认硬件）

这是本次重构**唯一不能在代码里拍板**的事，原因是原工程两个版本自相矛盾：

- qibiji 主工程 `LED_Init` 末尾 `GPIO_ResetBits`（输出 0），且全工程"写 1 表示亮" → 推断**高电平点亮**，上电全灭；
- qibiji_boot `LED_Init` 末尾 `GPIO_SetBits`（输出 1） → 推断低电平点亮，或意图上电自检全亮。

两者不可能同时对。处理方式：

1. **用宏隔离极性**（§1 的 `LED_ACTIVE_HIGH` + §2 的 `LED_PIN_ON/OFF`），业务逻辑永远写"亮/灭"，不写"0/1"。确认极性后只改 `led.h` 里一个宏，`led.c` 和 `main.c` 都不动；
2. **上电默认全灭**（当前 `LED_Init` 实现），避免 boot 时误报故障。如果需要上电自检（四个灯全亮 200ms 再全灭，用于现场确认灯珠没坏），在 `LED_Init` 的 `LED_PIN_OFF(LED_ALL_PINS)` 之前插入 `LED_PIN_ON(LED_ALL_PINS); delay_ms(200);`，但要注意这 200ms 在看门狗窗口内（2.12s），无需额外喂狗——不过 `delay_init()` 必须在 `LED_Init()` 之前调用才能用 `delay_ms`，所以若要自检，把 `LED_Init()` 挪到 `delay_init()` 之后；
3. **首次上板验证项**：用 `LED_PIN_ON(LED_ALL_PINS)` 临时让四个灯全亮，观察实际亮灭，确认 `LED_ACTIVE_HIGH` 的取值，再恢复正式逻辑。见 §8 验证项 2。

## §6 禁止事项

| 禁止 | 原因 |
|---|---|
| 业务文件直接写 `LED_UP / LED_DN / LED_LINE / LED_ERR` 引脚宏 | 破坏"LED 只镜像状态"的边界，改极性时漏改 |
| 恢复 `KeyLed_Ctl()` 并放回 `key.c` | 已被 DCMOTOR 重构剥离，方向灯归 LED 模块通过 `DCMOTOR_GetDir()` 获取，电机不反向依赖按键 |
| 在中断（TIM3 / EXTI9_5）里写 LED | LED 是 10ms 轮询镜像，中断写会与 Tick 竞争；且中断若跑、主循环已死时灯会乱闪，掩盖主循环挂死 |
| 让 LED 模块持有方向、在线、故障的**判定逻辑** | LED 只查询，不决策；判定权仍在 DCMOTOR / GPRS |

完成后全局搜索 `LED_UP_PIN / LED_LINE_PIN / KeyLed_Ctl`，确认引脚宏只出现在 `led.c`，`KeyLed_Ctl` 在 zxl 工程零命中。

## §7 与其它文档的同步

- 《DCMotor重构操作建议》§1：原表中"`KeyLed_Ctl()` 按键 LED 属于 LED 模块，不归电机"——本建议落地后，LED 模块正式通过 `DCMOTOR_GetDir()` / `DCMOTOR_GetLastStopReason()` 承接方向灯与故障灯，电机侧无需任何改动；
- 《看门狗重构操作建议》§4 初始化序列：`LED_Init()` 插入 `KEY_Init()` 之后、`delay_init()` 之前（若不上电自检）；`LED_Tick()` 加入 10ms 节拍。该文档的预算表不受影响（LED 操作微秒级）；
- 《GPRS与硬件问题总结》：在线指示不再由 GPRS 状态机直接写引脚，改由 `LED_Tick` 统一镜像；GPRS 模块可以删除（若有残留）对 `LED_LINE` 的直接写操作——zxl 工程当前 GPRS 代码已无此类写操作，无需改动。

## §8 验证清单

1. **编译接入**：`HARDWARE/LED/led.c` 加入 Keil 工程并 Rebuild，无未定义符号；删除 `OBJ/led.o` 旧残留或确认 Rebuild 已覆盖。
2. **极性确认**：临时把 `LED_Init()` 末尾改成 `LED_PIN_ON(LED_ALL_PINS)`，上电观察四个灯是否全亮。若全亮 → `LED_ACTIVE_HIGH=1` 正确；若全灭 → 板子是低电平点亮，把 `led.h` 的 `LED_ACTIVE_HIGH` 改为 0 后重试。确认后恢复全灭。
3. **方向灯**：按 UP 键 → PD11 亮、PD12 灭；按 DOWN → PD12 亮、PD11 灭；按 STOP → 两个都灭。
4. **在线灯**：4G 未上线时 PD13 灭；模块注册并进 MQTT NORMAL 后 PD13 亮；断网回到离线后 PD13 灭。可用 `gprs_is_ready()` 在调试器里观察变量与灯的对应。
5. **故障灯**：人为制造堵转（手抓闸门）→ PD14 亮，且电机停转；松开并重新启动后，若停机原因变为限位/人工，PD14 灭。
6. **不闪烁异常**：主循环挂死时（临时插 `while(1)`），LED 停在最后状态不变——这正好可以作为"主循环是否存活"的旁证（灯不再变化 = Tick 停了）。验证后删除故障注入。
7. **边界检查**：全局搜索 `KeyLed_Ctl` 在 zxl 零命中；`LED_UP_PIN` 等只在 `led.c` 出现；中断函数中无 LED 写操作。

## 待确认项（需硬件 / 用户拍板）

1. **实际电平极性**：原理图确认 PD11~14 是共阴（高电平点亮）还是共阳（低电平点亮），据此定 `LED_ACTIVE_HIGH`；
2. **ERR 语义**：当前定为堵转 / 超时 / 联锁三类。是否还要加入"EEPROM 校验失败""RTC 丢失"等故障？这些状态目前没有统一出口，需要先在对应模块暴露查询接口；
3. **是否要上电自检全亮**：现场安装时确认灯珠好坏有价值，但会让 `LED_Init` 依赖 `delay_init` 的顺序。默认不做，需要时按 §5 开启。

---

## 补充（2026-11-21 上板验证与两处修正）

LED 模块已编进 Keil 工程并在真机跑通。本轮上板发现两处问题，均已修正；§1–§8 的设计结论不变，以下记录差异与原因。

### ① `main.c` 节拍里误写 `LED_Init()`（已修，上板确认）

§3 要求 10ms 节拍里加 `LED_Tick()`，实际工程里写成了 `LED_Init()`。后果：**每 10ms 重新开关 RCC、重配 GPIO，并把 PD11~14 全部拉灭**——四个灯永远不亮。现象是"上板后 LINE 灯不亮"（当时网络其实已进 NORMAL）。已改为：

```c
DCMOTOR_Tick();         /* then consume: states/delta + run time */
LED_Tick();             /* mirror direction / online / malfunction */
```

修正后上板确认：LINE 灯在 `State3->4: NORMAL` 后常亮（设计预期），ERR 灯行为正常。引脚复核结论：LED 的 PD11~14 与 DCMOTOR（PD1/3/4/5）、GATEIO（PD7/PB3/PB4）、LCD（PE3/4/5 + PA0 背光）互不冲突。

### ② 方向灯改用 `DCMOTOR_GetState()`（已修，上板确认）

§2 原实现用 `DCMOTOR_GetDir()` 判断方向灯。上板实测：按 STOP 停机后 **UP 灯不灭**。原因：`GetDir()` 返回 `s_dir`（dcmotor.c:432），语义是"最后一次物理行驶方向"，停机不清零——限位逻辑要靠它（见下）。§2 那条说明（157 行）的判断不成立，方向灯不能用 `s_dir`。

`led.c` 的方向灯分支改为（`LED_Tick()` 内）：

```c
uint8_t st = DCMOTOR_GetState();    /* requested state: STOP when idle */

if (st == MOTOR_UP)        { LED_PIN_ON(LED_UP_PIN);  LED_PIN_OFF(LED_DN_PIN); }
else if (st == MOTOR_DOWN) { LED_PIN_ON(LED_DN_PIN);  LED_PIN_OFF(LED_UP_PIN); }
else                       { LED_PIN_OFF(LED_UP_PIN); LED_PIN_OFF(LED_DN_PIN); }
```

**`dcmotor.c` 里的 `s_dir` 本身不动**：`DCMOTOR_UpdateLimits()`（135、146 行）清限位锁存要求"限位电平无效 + 电机正往反方向走"，`s_dir` 提供这个方向。若把它停机清零，下次启动时锁存判断会失去依据。

§4 状态映射表同步订正：

| 灯 | 引脚 | 点亮条件（订正后） | 状态来源 |
|---|---|---|---|
| UP | PD11 | `DCMOTOR_GetState() == MOTOR_UP` | `dcmotor.h` |
| DN | PD12 | `DCMOTOR_GetState() == MOTOR_DOWN` | `dcmotor.h` |
| LINE | PD13 | `gprs_is_ready() != 0`（不变） | `gprs_at.h` |
| ERR | PD14 | 停机原因 ∈ {堵转, 超时, 联锁}（不变）；将来加 `ADC_IsLinkLost()`，见《ADC重构操作建议.md》补充节 | `dcmotor.h` |

### ③ 上板验证记录（2026-11-21 上午）

| 现象 | 判定 |
|---|---|
| 改 `LED_Tick()` 后 LINE 灯在 MQTT NORMAL 后常亮 | 正常（`gprs_is_ready()` 为真即常亮，非心跳闪烁） |
| ERR 灯上电常亮 | 非本模块问题：`stop_reason` 被上电零点窗口写入 INTERLOCK 后无人复位，根因与修法见《DCMotor重构操作建议.md》§17 |
| STOP 后 UP 灯不灭 | §2 原实现缺陷，已按上面改用 `GetState()` |
| 故障后 ERR 锁存到下次启动 | 预期行为（原因只在 UP/DOWN 边沿清成 NONE） |
