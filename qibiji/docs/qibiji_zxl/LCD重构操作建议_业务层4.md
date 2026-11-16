# LCD 重构操作建议 —— 业务层第 4 轮：背光 30 秒自动熄屏

> 承接《LCD重构操作建议_驱动层.md》《_业务层1.md》《_业务层2.md》《_业务层3.md》。第 3 轮（顶部图形区）上板已通过。
> 本轮是路线图里 LCD 业务层的**最后一项**：把原工程 `BLight_Ctrl()` 的"无按键 30 秒自动熄屏、有按键点亮"搬进来。
> **`main.c` 改 4 处、`uvprojx` 加 2 个文件节点**；驱动层 `lcd12864.c/.h` 和 `lcd_pages.c/.h` 本轮都不改。

---

## 1. 本轮做什么、不做什么

**做**：新建独立模块 `lcd_backlight.c/.h` —— 上电点亮背光；任意按键点亮并重置计时；连续 30 秒无按键则自动熄灭。

**不搬**（明确排除，别顺手加）：

| 项 | 为什么不搬 |
|---|---|
| 自造字 `WRCGRAMH` / `ShowCGCharH` | 原工程**零调用**的死代码（芯片只给 4 个字位也没人用）；真要图标用第 4 轮的 `DrawStrip/DrawRow` 加字模数据表即可，不必引入 CGRAM 这套独立机制 |
| `lcd_set_invert` / `lcd_get_invert`（反显） | 原工程零调用；将来要用就是驱动里一行 `s_WriteCmd(0x0D)`，届时单独加 |
| 新定时器 | 复用现有 TIM3 的 10ms 节拍 `Tick_10ms_Take()`，不新增外设 |

## 2. 原工程机制与本轮对应

原工程 `BLight_Ctrl()`（`Dispaly.c:195-217`）的逻辑：

```c
if (KeyNum) { BackLight = 1; cnt = 0; }          /* 有按键：点亮 + 清零 */
else {
    if (flag300ms & 0x01) { flag300ms &= 0xfe;   /* 每 300ms 一拍 */
        cnt++;
        if (cnt >= 100) { BackLight = 0; cnt = 0; } /* 100×300ms = 30s 熄屏 */
    }
}
```

本轮等价实现，两处差异都是**有意的简化**：

| | 原工程 | 本轮 | 理由 |
|---|---|---|---|
| 时基 | `flag300ms`（300ms 一拍） | `Tick_10ms_Take()`（10ms 一拍），3000 拍 = 30s | 新工程只有 10ms 节拍，不新增定时器（路线图要求） |
| "有按键"来源 | 全局 `KeyNum` | `APP_DispatchKey()` 里显式调 `Kick()` | 新工程按键走事件分发，不读全局键码变量 |
| 计时变量 | `static u16 cnt`（模块内） | `static uint16_t s_idle_ticks` + `s_is_on` 镜像 | 多一个 `s_is_on`，避免熄屏后反复写屏、也让 `Kick` 只在状态变化时写引脚 |

**`s_is_on` 镜像的作用**：原工程每次有按键都无条件写 `BackLight=1`，即使背光本来就亮。本轮加了状态镜像——亮着时 `Kick()` 不碰引脚，只有灭了才写 1；`Tick()` 灭屏后直接短路返回。少写引脚没功能影响，纯省动作、状态更清楚。

## 3. 时基接入：和 KEY_Tick 共用一拍

当前 `main.c` 主循环（`main.c:59-61`）是：

```c
if (Tick_10ms_Take())
    KEY_Tick();
```

`Tick_10ms_Take()` 是**消费式**的（取走即清，`timer.c:33-39`），一拍只能取一次。背光 Tick 必须和 `KEY_Tick()` 挂在**同一个 if** 里，共用这一拍：

```c
if (Tick_10ms_Take())
{
    KEY_Tick();
    LCD_BACKLIGHT_Tick();   /* 本轮新增：每 10ms 累计一次 */
}
```

**别写成两个独立的 `if (Tick_10ms_Take())`** —— 第二个几乎永远取不到（第一个已经消费掉），背光计时会近乎不走。这是本轮最容易踩的坑。

## 4. 第 1 步：新建 `HARDWARE/LCD12864/lcd_backlight.h`

```c
#ifndef __LCD_BACKLIGHT_H
#define __LCD_BACKLIGHT_H

/* LCD backlight auto-off module (business layer, round 4).
 * Scope: light the backlight on power-up / any key, turn it off after
 *        30 s of no key activity. Uses the existing 10 ms system tick;
 *        no new timer is allocated. Hardware on/off goes through the
 *        driver's LCD12864_SetBacklight(). */
#include "sys.h"   /* uint8_t */

/* Timeout in milliseconds. 30 s, matched to the original project's
 * BLight_Ctrl() (100 x 300 ms). */
#define LCD_BACKLIGHT_TIMEOUT_MS  30000u

void LCD_BACKLIGHT_Init(void);  /* power-up: light up, reset idle timer */
void LCD_BACKLIGHT_Tick(void);  /* call once per 10 ms system tick       */
void LCD_BACKLIGHT_Kick(void);  /* call on any key: light up + reset     */

#endif
```

放在 `HARDWARE/LCD12864/`（和 `lcd_pages.*` 同目录）——IncludePath 第 4 轮已追加过该目录，**本轮不用再改 IncludePath**。

## 5. 第 2 步：新建 `HARDWARE/LCD12864/lcd_backlight.c`

```c
#include "lcd_backlight.h"
#include "lcd12864.h"

/* 10 ms per tick -> 30000/10 = 3000 ticks for 30 s.
 * uint16_t holds 3000 fine; saturate at the limit so it never wraps. */
#define BL_TICKS  (LCD_BACKLIGHT_TIMEOUT_MS / 10u)   /* 3000u */

static uint16_t s_idle_ticks;   /* 10 ms ticks since last key activity */
static uint8_t  s_is_on;        /* mirror of the physical backlight state */

void LCD_BACKLIGHT_Init(void)
{
    s_idle_ticks = 0u;
    s_is_on = 1u;
    LCD12864_SetBacklight(1u);
}

void LCD_BACKLIGHT_Kick(void)
{
    s_idle_ticks = 0u;
    if (s_is_on == 0u)          /* only touch the pin when state changes */
    {
        s_is_on = 1u;
        LCD12864_SetBacklight(1u);
    }
}

void LCD_BACKLIGHT_Tick(void)
{
    if (s_is_on == 0u)
        return;                 /* already off: nothing to count */

    if (s_idle_ticks < BL_TICKS)
        s_idle_ticks++;

    if (s_idle_ticks >= BL_TICKS)
    {
        s_is_on = 0u;
        LCD12864_SetBacklight(0u);
    }
}
```

要点：

- **`BL_TICKS = 30000/10 = 3000`**：编译期常量，改超时只动头文件里的 `LCD_BACKLIGHT_TIMEOUT_MS` 一处。想改成 60 秒就写 `60000u`，其余不用动。
- **两个 `static` 变量是模块私有状态**：不进头文件，外部碰不到。`s_idle_ticks` 记距上次按键过了多少拍，`s_is_on` 记当前背光亮灭。
- **`Tick()` 的饱和写法**：`if (s_idle_ticks < BL_TICKS) s_idle_ticks++;` —— 到 3000 后不再加，配合开头 `if (s_is_on == 0u) return;`，灭屏后既不会 `uint16_t` 溢出回绕、也不会反复写引脚。
- **`Kick()` 只在灭着时才写引脚**：亮着时按任意键只重置 `s_idle_ticks`，不产生多余的 GPIO 动作。语义等价于原工程（每次按键都清零计数器），只是省了写引脚。
- **C90 合规**：变量全在块首声明、注释全 ASCII（工程带 `--no-multibyte-chars`）。

## 6. 第 3 步：改 `USER/main.c`（4 处）

### 6.1 加 include（在 `#include "lcd_pages.h"` 之后）

```c
#include "lcd_backlight.h"
```

### 6.2 `APP_DispatchKey()` 里加 Kick（按键出口）

在 `LCD_PAGES_Process(key);` 之后加一行：

```c
    LCD_PAGES_Process(key);     /* 页面状态机消费按键事件 */
    LCD_BACKLIGHT_Kick();       /* 本轮新增：任意按键点亮背光 + 重置 30s 计时 */
```

**为什么放在这里而不是各模块自己调**：`APP_DispatchKey()` 是所有按键事件的唯一出口（`main.c:18-39` 的注释就是这么设计的），在这里加一行，UP/DOWN/STOP/SET/CANCEL 六个键全覆盖，不用在页面状态机里到处插调用。原工程靠全局 `KeyNum` 让 `BLight_Ctrl()` 自己读到，新工程走事件分发，这一行就是等价的"喂按键活动给背光"。

### 6.3 `main()` 里加 Init（在 `LCD12864_Init()` 之后）

```c
    LCD12864_Init();
    LCD_BACKLIGHT_Init();   /* 本轮新增：点亮背光、启动 30s 计时 */
    LCD_PAGES_Init();
```

**顺序要求**：必须在 `LCD12864_Init()` **之后**——驱动 Init 里已把 PA0 配成推挽输出并点亮（`lcd12864.c:215`），背光模块 Init 再点亮一次（幂等无害），同时把自己的 `s_is_on` 镜像置 1。放反了（背光 Init 在前），引脚还没配就写，虽然 C 上不报错但状态镜像和硬件可能对不上。

### 6.4 主循环 Tick 里加一行（和 `KEY_Tick()` 共用一拍）

```c
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();   /* 本轮新增 */
        }
```

### 6.5 改完后的 `main.c` 全貌（核对用）

```c
#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"
#include "lcd_backlight.h"

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
    LCD_BACKLIGHT_Kick();       /* 新增 */
}

int main(void)
{
    key_code_t key;

    NVIC_Configuration();
    DEBUG_UART_Init();
    KEY_Init();
    delay_init();
    LCD12864_Init();
    LCD_BACKLIGHT_Init();       /* 新增 */
    LCD_PAGES_Init();
    TIM3_10ms_Init();

    DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
    DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

    while (1)
    {
        if (Tick_10ms_Take())
        {
            KEY_Tick();
            LCD_BACKLIGHT_Tick();   /* 新增 */
        }

        key = KEY_Pop();
        if (key != KEY_NONE)
            APP_DispatchKey(key);
    }
}
```

## 7. 第 4 步：改 `USER/SRTUV10.uvprojx`（HARDWARE 组加 2 个节点）

IncludePath 不动（第 4 轮已含 `..\HARDWARE\LCD12864`）。只在 HARDWARE 组 `<Files>` 末尾（`lcd_pages.h` 节点之后）追加：

```xml
            <File>
              <FileName>lcd_backlight.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\LCD12864\lcd_backlight.c</FilePath>
            </File>
            <File>
              <FileName>lcd_backlight.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\LCD12864\lcd_backlight.h</FilePath>
            </File>
```

`FileType 1` = C 源码、`FileType 5` = 头文件（和组内现有节点一致）。**两个都要加**：只加 `.c` 也能编译链接，但 `.h` 加进去 Keil 工程树里才看得见、便于维护。

**改完 uvprojx 先关 Keil 工程再重开**（或按 reload 提示重载）——前几轮踩过：Keil 内存里是旧配置，直接保存会把磁盘上的 uvprojx 改动冲掉。

## 8. 依赖与冲突核查

| 检查项 | 结论 |
|---|---|
| 头文件包含链 | `lcd_backlight.h` → `sys.h`（拿 `uint8_t`）；`.c` 再含 `lcd12864.h`（拿 `SetBacklight` 声明）。都在 IncludePath 里 |
| 与驱动层 | 只调 `LCD12864_SetBacklight()`，不改驱动层任何文件；背光极性沿用驱动的 `LCD_BL_ON=1` 约定 |
| 与 TIM3 | 复用 `Tick_10ms_Take()`，不新增中断、不改 `timer.c/.h`；没有新的 `TIMx_IRQHandler`，无冲突 |
| 与按键模块 | 只在 `APP_DispatchKey()` 调 `Kick()`，不改 `key.c/.h`；不读全局键码 |
| 与页面模块 | `lcd_pages.c/.h` 本轮零改动；背光是横切功能，和页面状态无关 |
| C90 / `--no-multibyte-chars` | 变量块首声明、注释全 ASCII，0 warning |

## 9. 上板验证

### 9.1 预期现象

1. 上电/复位：背光亮，屏显示第 4 轮的首页（顶部图形 + 3 行文字）。
2. **静置 30 秒不碰任何键**：背光自动熄灭（屏内容还在，只是背光灭；LCD 是反射式，灭背光后环境光下可能仍隐约可见，属正常）。
3. 背光灭后按任意键（UP/DOWN/STOP/SET/CANCEL 任一）：背光立即点亮，屏恢复到第 2 步状态，并重新开始 30 秒计时。
4. 30 秒内反复按键：背光始终亮，不会中途熄灭（每次按键都重置计时）。

### 9.2 没有按预期时

| 症状 | 首查 | 次查 |
|---|---|---|
| 背光永不熄灭 | `LCD_BACKLIGHT_Tick()` 是否和 `KEY_Tick()` 在**同一个** `if (Tick_10ms_Take())` 里（写成两个 if 就取不到第二拍） | TIM3 是否初始化（`TIM3_10ms_Init()` 在 main 里） |
| 背光灭了按键点不亮 | `APP_DispatchKey()` 里是否加了 `LCD_BACKLIGHT_Kick()` | 按键事件是否到达——串口应打印 `KEY: xxx` |
| 上电背光不亮 | `LCD_BACKLIGHT_Init()` 是否在 `LCD12864_Init()` 之后 | 驱动层 `LCD_BL_ON` 极性（原工程注释自相矛盾，若极性反了，对调 `LCD_BL_ON/LCD_BL_OFF`） |
| 熄灭时间明显不是 30 秒 | `LCD_BACKLIGHT_TIMEOUT_MS` 是否被改过；`Tick_10ms_Take()` 一拍是不是 10ms（`timer.c:14-15` 的 Period/Prescaler） | 是否有别的 `TIM3_IRQHandler` 冲突（本工程只有 `timer.c` 一个） |
| 编译报 `LCD_BACKLIGHT_Kick undefined` | uvprojx 的 HARDWARE 组是否加了 `lcd_backlight.c` 节点 | 关工程重开后再 Build |

## 10. 与原工程逐项对照

| 原工程 | 原行为 | 本轮 | 差异说明 |
|---|---|---|---|
| `BLight_Ctrl()` | 有 `KeyNum` 点亮+清零，否则每 300ms cnt++，到 100 熄屏 | `Kick()` 点亮+清零，`Tick()` 每 10ms 累计，到 3000 熄屏 | 语义等价；时基从 300ms×100 换成 10ms×3000（不新增定时器） |
| `BackLight` 全局位带 | 每次按键无条件写 1 | `s_is_on` 镜像，只在状态变化时写引脚 | 省 GPIO 动作，状态更清楚 |
| `flag300ms` 节拍源 | 别的定时器置位 | `Tick_10ms_Take()` 现有 TIM3 节拍 | 复用，不新增外设 |
| 自造字/反显 | 定义为死代码 | 不搬 | 原工程零调用；将来按需单独加 |

## 11. 约束

- C90（`<uC99>0</uC99>`）：变量全在块首声明；
- `--no-multibyte-chars`：**字符串字面量必须全 ASCII**（汉字一律用 GB2312 字节数组，别写 `"上限位"` 这种中文字面量）。新增的 `lcd_backlight.c/.h` 注释全 ASCII；`main.c` 片段里随工程现有风格保留中文注释（原工程与你当前的 `main.c` 已有中文注释且编译通过，此项只卡字符串不卡注释）；
- 不新增全局状态外泄：`s_idle_ticks`、`s_is_on` 都是 `.c` 内的 `static`，不进头文件；
- 不新增定时器/中断：复用 TIM3 10ms 节拍。

## 12. 验证记录

- host 端 gcc `-std=c90 -Wall -Wextra`：0 error / 0 warning；
- 行为断言 **8 项，0 失败**：Init 点亮、2999 拍仍亮、第 3000 拍熄灭、8000 拍不回绕、超时后 Kick 重新点亮+重置窗口、窗口中途 Kick 重置计数（3500 原始拍仍亮）、熄灭后 re-Init 恢复；
- 验证方法：不链接 `lcd12864.c`，只桩掉 `LCD12864_SetBacklight()` 到 PA0，读 PA0 电平判断亮灭——背光模块无串行帧，比前几轮的波形解码简单；
- 代码副本：`C:\Users\JessamynAdmin\AppData\Local\Temp\qibiji_pages_test\`（`lcd_backlight.c/.h`、`test_backlight.c`）。
- 这是 host 桩验证，**不是 Keil 真机编译**；请在 Keil 点一次 Build 确认链接，并按第 9 节上板验证 30 秒与按键唤醒。

---

**到此 LCD 业务层路线图全部完成**（驱动层 → 页面状态机 → 显示缓存+参数编辑 → 顶部图形区 → 背光自动熄屏）。剩下的真数据接入（ADC 电流电压、EEPROM 参数落盘、开度/流量、电机/GPRS）属于**其它模块**，不是 LCD 这一脉的活，等对应模块迁移时再接。
