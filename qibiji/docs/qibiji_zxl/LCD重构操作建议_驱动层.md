# LCD 重构操作建议（只做驱动层）

> 本文只给出目标代码和实施步骤，**没有改动任何源码**。你照着顺序一个文件一个文件改即可。
> 目标代码已经用 C90（`-std=c90 -Wall -Wextra`）编译通过，并跑了 30 项行为断言，见第 11 节。
> 章节从 1 开始编号。涉及源码：`qibiji/HARDWARE/Lcd12864/`、`qibiji/HARDWARE/LCD-uart/`、`qibiji_zxl/`。

---

## 1. 结论与范围

### 1.1 原工程使用的是哪一套 LCD

原工程里有两套和 LCD 有关的代码，接口完全不同：

| | `qibiji/HARDWARE/Lcd12864/` | `qibiji/HARDWARE/LCD-uart/` |
|---|---|---|
| 屏的类型 | ST7920 控制的 12864 点阵液晶 | 串口指令屏（0x5A A5 帧） |
| 接口 | GPIO 模拟 3 线串行：PE3=CS、PE4=SID、PE5=SCLK；PA0=背光 | USART5，`Usart5_Send()` |
| 编进 Keil 工程 | 是，`Lcd12864.C`、`Dispaly.c`、`DispalyPara.c` 三个文件在 `HARDWARE` 组（`SRTUV10.uvprojx:503-516`） | 否。工程文件里没有 `lcd.c` / `usart5.c` 的文件节点，IncludePath 里也没有该目录 |
| 是否被主程序调用 | 是。`main.c:42 Lcd_Init()` 注释 `// LCD12864`；`main.c:79 Lcd_Process()` | 否。`order_recv()`、`ToLcd_int/float/string()` 全工程无调用点；`main.c:32-35` 只初始化了 uart1~uart4，没有 uart5 |

**结论：整机显示用的是 `Lcd12864` 这一套（GPIO 模拟 SPI 的 12864 点阵屏）。`LCD-uart` 是未编进工程、未被调用的串口屏残留代码，本轮不搬。**

### 1.2 本轮做什么、不做什么

- **做**：在 `qibiji_zxl` 里新建一个独立的 `HARDWARE/LCD12864/` 驱动层模块，能让屏在上电后完成初始化、清屏、点亮背光、显示一行 ASCII 文字。目的是先把硬件打通，用板子验证接线和时序正确。
- **不做**：页面切换（`ViewPages`）、首页/设置页/4G 页（`homepage/setpage/G4gpage/...`）、参数编辑与数值格式化（`DispalyPara.c`）、显示缓存 `DisplayCache`、按键翻页、背光定时熄灭、GDRAM 图标与进度条（`WRGDRAMp1~p4`、`GenerateProgressBar1`）。这些属于业务层，放到后续轮次，一次只加一个模块。

### 1.3 为什么不能整包搬 `Dispaly.c`

`qibiji/HARDWARE/Lcd12864/Dispaly.c` 开头就包含 12 个头文件（`:1-13`）：`display.h`、`Lcd12864.h`、`timer.h`、`usart.h`、`key.h`、`delay.h`、`adc.h`、`24cxx.h`、`dc_motor.h`、`main.h`、`wdg.h`、`net_liveness.h`、`string.h`；并引用 `autoFlag`、`ViewNum`、`displayCache`、`armReg`、`sys_moter_state`、`gprsonlineflag`、`KeyNum`、`flag300ms`、`IWDG_Feed()` 等一大片全局状态。这些在 `qibiji_zxl` 里都不存在（喂狗能力例外，已由目标工程的 `WDG_Feed()` 提供）。

真正和「把字节送到屏上」有关的只有两块，本轮只抽这两块：

1. `Lcd12864.C:15-57`：`WRCommandH / WRDataH / SPIWH / SendByteLCDH` —— 串行时序。
2. `Dispaly.c:162-185, 220-227`：`Lcd_Gpio_Init()` 的 GPIO 配置、`LCDInit()` 的 4 条基本指令；另加 `Lcd12864.C:164-180` 的 `CLEARGDRAMH()` 清绘图区。

---

## 2. 改动文件清单与落地顺序

按「改动克制」分成两堆。**正题只有 2 个新文件**，其余是让它编译、跑起来所必需的接线。

| 堆 | 文件 | 动作 | 内容 |
|---|---|---|---|
| 正题 | `qibiji_zxl/HARDWARE/LCD12864/lcd12864.h` | 新建 | 驱动层对外接口，5 个函数 |
| 正题 | `qibiji_zxl/HARDWARE/LCD12864/lcd12864.c` | 新建 | GPIO + 串行时序 + 指令集 + 清屏 + 字符显示 + 背光 |
| 配套 | `qibiji_zxl/USER/main.c` | 改 3 处（另有 1 处可选） | 加 2 个 include、加 `delay_init()`、加 `LCD12864_Init()` 与一行屏显；可选：按键时把键名写到屏上 |
| 配套 | `qibiji_zxl/USER/SRTUV10.uvprojx` | 改 2 处 | IncludePath 加目录；`HARDWARE` 组加 `lcd12864.c/.h` |

**不动**：`key.c/.h`、`timer.c/.h`、`debug_uart.c/.h`、原工程 `qibiji/`、其它文档。

**落地顺序**：第 3 步 → 第 4 步 → 第 5 步 → 第 6 步。每步只动一个文件，改完先读一遍再进下一步。

---

## 3. 第 1 步：新建 `HARDWARE/LCD12864/lcd12864.h`

**这个文件干什么**：定义驱动层对外的全部接口。除了这 5 个函数，外部不应该知道任何引脚、指令或时序细节。

```c
#ifndef __LCD12864_H
#define __LCD12864_H

/* LCD12864 (ST7920) driver layer.
 * Scope: hardware access only - GPIO, serial timing, command set,
 *        DDRAM/GDRAM clear, ASCII string display, backlight on/off.
 * Excluded: pages, parameter editing, key dispatch, backlight timeout,
 *           icons / progress bar. Those belong to an upper layer.
 */
#include "sys.h"   /* uint8_t, stm32f10x.h, bit-band macros */

void LCD12864_Init(void);
void LCD12864_Clear(void);
void LCD12864_ClearGDRAM(void);
void LCD12864_ShowString(uint8_t row, const char *str);
void LCD12864_SetBacklight(uint8_t on);

#endif
```

**逐项说明**

| 项 | 说明 |
|---|---|
| `#include "sys.h"` | `sys.h:3` 会包含 `stm32f10x.h`，后者通过 `stm32f10x_conf.h` 引入 `stm32f10x_gpio.h` / `stm32f10x_rcc.h`，所以这个头文件自包含，任何 `.c` 单独包含它都能编译。 |
| `LCD12864_Init()` | 上电调一次。配 GPIO、等屏上电稳定、发初始化指令、清 DDRAM 和 GDRAM、开背光。 |
| `LCD12864_Clear()` | 清字符显示区（DDRAM），发 `0x30 / 0x01 / 0x02`。 |
| `LCD12864_ClearGDRAM()` | 清绘图区（GDRAM），32 行全写 0。上电不清会出现花屏。 |
| `LCD12864_ShowString(row, str)` | 在第 `row` 行（0~3）显示 ASCII 字符串，最多 16 个字符。 |
| `LCD12864_SetBacklight(on)` | `1` = 亮，`0` = 灭。 |

---

## 4. 第 2 步：新建 `HARDWARE/LCD12864/lcd12864.c`

**这个文件干什么**：把「一个字节」变成屏能识别的 3 线串行波形，并在上电时把屏配置到可用状态。它不知道按键、页面、电机、ADC。

分 4 层，从上到下：引脚宏（私有）→ 常量与行地址表 → 底层时序（`static`）→ 5 个对外函数。

```c
/* ===========================================================================
 * LCD12864 (ST7920) driver layer - qibiji_zxl
 *
 * Only hardware access lives here: GPIO, 3-wire serial timing, command set,
 * DDRAM/GDRAM clearing, ASCII string display, backlight on/off.
 * No page state, no key handling, no ADC/motor/EEPROM/GPRS dependencies.
 * =========================================================================== */
#include "lcd12864.h"
#include "delay.h"
#include "sys.h"
#include "stm32f10x.h"
#include "wdg.h"      /* WDG_Feed(): feed after each 500 ms power-on slice */

/* --------------------- Pins (private to this file) ---------------------
 * Keep the original project's assignment: PE3=CS, PE4=SID, PE5=SCLK,
 * PA0=BackLight. These are 5V-tolerant pins (LCD logic is 5V); do not
 * move them to non-FT pins.
 * Same convention as KEY_PIN_* in key.c: private, not in the header. */
#define LCD_CS      PEout(3)
#define LCD_SID     PEout(4)
#define LCD_SCLK    PEout(5)
#define LCD_BL      PAout(0)

/* Backlight polarity. The original project has contradictory comments:
 * Dispaly.c:184 says 1 = off, BLight_Ctrl() says 1 = on. The runtime path
 * (BLight_Ctrl) uses 1 = on, so that convention is used here. If the panel
 * behaves the other way round, only these two macros need swapping. */
#define LCD_BL_ON   1u
#define LCD_BL_OFF  0u

/* --------------------------- ST7920 constants --------------------------- */
#define LCD_FRAME_CMD   0xF8u   /* start byte, RS = 0 */
#define LCD_FRAME_DAT   0xFAu   /* start byte, RS = 1 */

#define LCD_CMD_BASIC   0x30u   /* basic instruction set, RE = 0 */
#define LCD_CMD_EXT     0x34u   /* extended set, RE = 1, graphic OFF */
#define LCD_CMD_GDRAM   0x36u   /* extended set, RE = 1, graphic ON */
#define LCD_CMD_ENTRY   0x06u   /* AC auto-increment, no display shift */
#define LCD_CMD_DISPON  0x0Cu   /* display ON, cursor OFF, blink OFF */
#define LCD_CMD_CLEAR   0x01u   /* clear DDRAM (slow command) */
#define LCD_CMD_HOME    0x02u   /* address return */

#define LCD_ROWS          4u
#define LCD_ADDR_PER_ROW  8u    /* 8 x 16-bit words = 16 ASCII chars */
#define LCD_GDRAM_ROWS   32u
#define LCD_GDRAM_WORDS  16u

/* DDRAM start address of each line (ST7920 is not linear) */
static const uint8_t s_row_addr[LCD_ROWS] = { 0x80u, 0x90u, 0x88u, 0x98u };

/* ---------------------- Low-level serial timing ------------------------- */
static void s_SendByte(uint8_t dat)
{
    uint8_t i;
    for (i = 0; i < 8u; i++)
    {
        LCD_SCLK = 0;
        LCD_SID  = (uint8_t)((dat & 0x80u) != 0u ? 1u : 0u);
        LCD_SCLK = 1;                       /* latched on rising edge */
        dat = (uint8_t)(dat << 1);          /* MSB first */
    }
}

/* One ST7920 serial frame = start byte + high nibble + low nibble.
 * Each nibble occupies the upper 4 bits of one transmitted byte. */
static void s_Write(uint8_t dat, uint8_t is_data)
{
    LCD_CS = 0;
    LCD_CS = 1;
    s_SendByte((uint8_t)(is_data != 0u ? LCD_FRAME_DAT : LCD_FRAME_CMD));
    s_SendByte((uint8_t)(dat & 0xF0u));
    s_SendByte((uint8_t)((uint8_t)(dat << 4) & 0xF0u));
    if (is_data != 0u)
        delay_us(20);
    else
        delay_us(100);
    LCD_CS = 0;
}

static void s_WriteCmd(uint8_t cmd)  { s_Write(cmd, 0u); }
static void s_WriteData(uint8_t dat) { s_Write(dat, 1u); }

/* ----------------------------- Public API -------------------------------- */
void LCD12864_SetBacklight(uint8_t on)
{
    LCD_BL = (uint8_t)(on != 0u ? LCD_BL_ON : LCD_BL_OFF);
}

void LCD12864_Clear(void)
{
    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_CLEAR);
    delay_ms(2);                 /* 0x01 needs extra execution time */
    s_WriteCmd(LCD_CMD_HOME);
}

void LCD12864_ClearGDRAM(void)
{
    uint8_t row, word;

    s_WriteCmd(LCD_CMD_EXT);     /* RE = 1 */
    s_WriteCmd(LCD_CMD_GDRAM);   /* graphic display ON to address GDRAM */
    for (row = 0; row < LCD_GDRAM_ROWS; row++)
    {
        s_WriteCmd((uint8_t)(0x80u + row));  /* Y address */
        s_WriteCmd(0x80u);                   /* X address = 0 */
        for (word = 0; word < LCD_GDRAM_WORDS; word++)
        {
            s_WriteData(0x00u);
            s_WriteData(0x00u);
        }
    }
    /* Return to basic instruction set AND turn graphic display off.
     * The original CLEARGDRAMH() leaves RE=1 / graphic ON; character
     * writes would then be wrong until some page function re-sends 0x30. */
    s_WriteCmd(LCD_CMD_EXT);     /* 0x34: RE = 1, graphic OFF */
    s_WriteCmd(LCD_CMD_BASIC);   /* 0x30: RE = 0, basic set */
}

/* Write an ASCII string on one line. One DDRAM address holds two ASCII
 * characters, so two bytes are written per address; an odd last character
 * is padded with a space. Maximum 8 addresses (16 characters) per line. */
void LCD12864_ShowString(uint8_t row, const char *str)
{
    uint8_t addr;
    uint8_t used;
    const char *p;

    if ((row >= LCD_ROWS) || (str == (const char *)0))
        return;

    addr = s_row_addr[row];
    used = 0;
    p = str;
    s_WriteCmd(LCD_CMD_BASIC);
    while ((*p != '\0') && (used < LCD_ADDR_PER_ROW))
    {
        uint8_t hi, lo;
        hi = (uint8_t)*p;
        p++;
        if (*p != '\0')
        {
            lo = (uint8_t)*p;
            p++;
        }
        else
        {
            lo = (uint8_t)' ';
        }
        s_WriteCmd(addr);
        s_WriteData(hi);
        s_WriteData(lo);
        addr++;
        used++;
    }
}

void LCD12864_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_0;          /* PA0 = backlight */
    GPIO_Init(GPIOA, &gpio);

    LCD_CS   = 0;
    LCD_SID  = 0;
    LCD_SCLK = 0;
    LCD12864_SetBacklight(0u);

    /* ST7920 needs about 1.5 s after power-on before accepting commands.
     * delay_ms() saturates at ~1864 ms @72 MHz, so wait in three 500 ms
     * slices and feed the watchdog after each slice. */
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();

    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_ENTRY);
    s_WriteCmd(LCD_CMD_DISPON);
    LCD12864_Clear();
    LCD12864_ClearGDRAM();
    LCD12864_SetBacklight(1u);
}
```

### 4.1 串行帧格式（这层最容易写错）

一次 `s_Write()` 发 **3 个字节**，和原工程 `SPIWH()`（`Lcd12864.C:36-41`）完全一致：

| 字节 | 内容 | 例：写命令 `0x30` |
|---|---|---|
| 1 | 起始字节 `0xF8`（RS=0）或 `0xFA`（RS=1） | `0xF8` |
| 2 | 数据高 4 位放在字节的高 4 位 | `0x30` |
| 3 | 数据低 4 位左移 4 位 | `0x00` |

`s_SendByte()` 先拉低 SCLK、放好 SID、再拉高 SCLK 锁存，最高位先发，循环 8 次。`CS` 在整帧期间保持高，帧结束拉低。命令帧后等 `delay_us(100)`、数据帧后等 `delay_us(20)`，数值沿用原工程 `WRCommandH / WRDataH`（`Lcd12864.C:15-33`）。

### 4.2 引脚为什么写在 `.c` 而不是 `.h`

和 `key.c` 里 `KEY_PIN_*` 的理由相同：外部只需要「显示一行字」，不需要知道 PE3/PE4/PE5。放 `.h` 会让任何模块都能直接翻引脚，绕过 `LCD12864_ShowString()` 的帧时序。

### 4.3 相对原工程的两个显式修正

1. **清 GDRAM 后回到基本指令集**。原 `CLEARGDRAMH()`（`Lcd12864.C:164-180`）发 `0x34 / 0x36` 后结束，屏停在扩展指令集 + 绘图显示开的状态；原工程靠每个页面函数里 `ShowQQCharH()` 开头补一句 `WRCommandH(0x30)` 兜底（`Lcd12864.C:65`），属于隐式约定。目标版在 `LCD12864_ClearGDRAM()` 末尾显式发 `0x34`（关绘图）+ `0x30`（回基本集），并在 `LCD12864_ShowString()` 开头也发一次 `0x30`，任何调用顺序都成立。
2. **恢复喂狗**。原 `Lcd_Init()`（`Dispaly.c:147-160`）在 3 段 `delay_ms(500)` 之间插 `IWDG_Feed()`；目标工程已启用 IWDG（见《看门狗重构操作建议》§3.3），改为每段后调用 `WDG_Feed()`。分段既满足 `delay_ms()` 单次上限约 1864 ms，也把单段阻塞压到 500 ms。

### 4.4 背光极性（上板后需要确认一处）

原工程两处注释互相矛盾：

- `Dispaly.c:184`：`BackLight = 1; // 关闭背光`（初始化）
- `Dispaly.c:199-213` `BLight_Ctrl()`：有按键时 `BackLight = 1; // 打开背光`，超时 `BackLight = 0; // 关闭背光`

目标版按**运行时逻辑**取 `1 = 亮`，并把它收敛成 `LCD_BL_ON / LCD_BL_OFF` 两个宏。若上板发现上电后屏是黑的、按键时反而灭，只需把这两个宏的值对调，不用改任何函数。

---

## 5. 第 3 步：改 `USER/main.c`（4 处）

**这一步干什么**：把驱动层接进现有的 `KEY + TIM3 + DEBUG_UART` 主循环。改动只有 4 处，其余逻辑一行不动。

### 5.1 改动 1：加 2 个 include

文件顶部现有 4 行 include 之后加：

```c
#include "delay.h"
#include "lcd12864.h"
#include "wdg.h"
```

### 5.2 改动 2：加 `delay_init()`（**最容易漏、后果最严重的一处**）

在 `main()` 里 `NVIC_Configuration();` 之后、`LCD12864_Init();` 之前加一行：

```c
    delay_init();
```

**为什么必须有**：`SYSTEM/delay/delay.c:38-39` 里 `fac_us = 0`、`fac_ms = 0` 是静态变量初值，只有 `delay_init()`（`delay.c:54-74`）才会把它们算成 9 和 9000（72 MHz）。不调用的话，`delay_us()` 执行 `SysTick->LOAD = nus * fac_us`（`delay.c:119`）会得到 0，延时直接失效，LCD 的位时序和 1.5 s 上电等待全部失效，表现是黑屏或花屏，且不报任何编译错误。

原工程 `main.c:27` 第一行就是 `delay_init()`；`qibiji_zxl` 的 `main.c` 目前没有这一句，因为之前的 KEY / TIM3 / DEBUG_UART 都不用 `delay`。**LCD 是第一个用到 `delay` 的模块，这一步必须补。**

### 5.3 改动 3：加初始化与一行显示

在 `KEY_Init();` 之后、`TIM3_10ms_Init();` 之前加 `LCD12864_Init();`；在两条上电串口提示之后加一行屏显：

```c
    LCD12864_Init();
    ...
    LCD12864_ShowString(0, "qibiji_zxl LCD OK");
```

`LCD12864_Init()` 内含 1.5 s 阻塞延时，放在 `TIM3_10ms_Init()` 之前，避免上电阶段中断空转。

### 5.4 改动 4（可选）：按键时把键名也写到屏上

在 `APP_DispatchKey()` 的串口打印之后加一行，就能同时验证按键链路和 LCD：

```c
    LCD12864_ShowString(1, name);
```

不加也不影响驱动层验证，可以等下一轮接页面时再做。

### 5.5 改后的完整 `main.c`

以你当前磁盘上的版本为底稿，只叠加改动 1~3（改动 4 是可选的，不包含在下面这份里）：

```c
#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "wdg.h"

/* 调试观察用：最后一次被分发出去的键码，调试器里可直接查看 */
volatile key_code_t g_last_key = KEY_NONE;

/* 键码 -> 文本，仅供调试串口输出。 */
static const char * const s_key_name[] =
{
    "NONE", "UP", "DOWN", "STOP", "SET", "CANCEL"
};

/* 按键事件的唯一出口。 */
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
}

int main(void)
{
	key_code_t key;

	NVIC_Configuration();   /* NVIC 分组 2，须在所有中断初始化之前 */
	delay_init();           /* 必须先于 LCD12864_Init()：初始化 delay_us/ms 的倍乘数 */
	DEBUG_UART_Init();      /* USART1 115200 8N1，PA9 = TX，只发不收 */
	WDG_Init();             /* 先于 LCD12864_Init()：让看门狗覆盖 1.5 s 上电等待 */
	KEY_Init();
	LCD12864_Init();        /* 含 1.5 s 上电等待（三段间各喂一次），放在 TIM3 启动之前 */
	TIM3_10ms_Init();

	DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
	DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");
	LCD12864_ShowString(0, "qibiji_zxl LCD OK");

	while (1)
	{
		if (Tick_10ms_Take())
			KEY_Tick();

		key = KEY_Pop();
		if (key != KEY_NONE)
			APP_DispatchKey(key);
	}
}
```

> `WDG_Init()` 的源码与窗口配置见《看门狗重构操作建议》§1；`LCD12864_Init()` 内部在三段 500 ms 后各调用一次 `WDG_Feed()`，因此看门狗必须先于 LCD 初始化启动。

---

## 6. 第 4 步：改 `USER/SRTUV10.uvprojx`（2 处）

**这一步干什么**：让 Keil 能找到新头文件、把新 `.c` 编进工程。

### 6.1 改动 1：IncludePath

当前（`:338`）：

```
..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\DEBUG_UART;..\HARDWARE\KEY;..\HARDWARE\TIMER
```

末尾追加一项：

```
..\USER;..\STM32F10x_FWLib\inc;..\SYSTEM\delay;..\SYSTEM\sys;..\CORE;..\HARDWARE\DEBUG_UART;..\HARDWARE\KEY;..\HARDWARE\TIMER;..\HARDWARE\LCD12864
```

`..\SYSTEM\delay` 和 `..\SYSTEM\sys` 已在路径里，`delay.h` / `sys.h` 能找到；`delay.c`、`sys.c` 也已在 `SYSTEM` 组编译（`:408,413`），不用再加源文件。

### 6.2 改动 2：HARDWARE 组加文件

在 `HARDWARE` 组（`:499-533`）的 `</Files>` 之前，追加两个节点：

```xml
            <File>
              <FileName>lcd12864.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\LCD12864\lcd12864.c</FilePath>
            </File>
            <File>
              <FileName>lcd12864.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\LCD12864\lcd12864.h</FilePath>
            </File>
```

`FileType 1` = C 源文件，`5` = 头文件，和现有 `key.c` / `key.h` 节点一致。

### 6.3 不用改的

- `stm32f10x_conf.h` 里 `stm32f10x_gpio.h`（`:39`）、`stm32f10x_rcc.h`（`:43`）已启用，对应的库源文件也在工程里。
- 工程是 C90 模式（`<uC99>0</uC99>`），目标代码所有变量声明都在块首，没有 `for (int i...)` 这类 C99 写法。
- `NVIC` 不需要动：驱动层不开中断、不占 DMA。

---

## 7. 依赖、引脚与冲突核查

### 7.1 包含链（对应你上一轮的 26 个 undefined 错误）

```
lcd12864.c
 ├─ "lcd12864.h" ──> "sys.h" ──> "stm32f10x.h" ──> stm32f10x_conf.h
 │                                                  ├─ stm32f10x_gpio.h  (GPIO_InitTypeDef ...)
 │                                                  └─ stm32f10x_rcc.h   (RCC_APB2PeriphClockCmd ...)
 ├─ "delay.h"    ──> "sys.h"       (delay_us / delay_ms / delay_init)
 ├─ "sys.h"                         (PEout / PAout 位带宏)
 └─ "stm32f10x.h"                  (显式再包含一次；头内有 include guard，重复包含无害)
```

`uint8_t` / `uint32_t` 由 `stm32f10x.h` 引入的 `stdint` 系列提供；`ENABLE`、`GPIO_Mode_Out_PP`、`GPIO_Speed_50MHz`、`GPIO_Pin_0/3/4/5`、`RCC_APB2Periph_GPIOE/GPIOA` 都来自标准库头文件。全部核对过。

### 7.2 引脚占用（无冲突）

| 模块 | 引脚 |
|---|---|
| LCD12864（本轮新增） | PE3、PE4、PE5、PA0 |
| KEY | PD15、PA15、PA11、PA12、PC9 |
| DEBUG_UART | PA9（USART1 TX） |
| TIM3 | 不占 GPIO，只作内部时基 |

没有复用。**注意**：PE3/PE4/PE5 沿用原工程已验证的 5V 容忍引脚，原 `Lcd12864.h:6` 注释写明「液晶12864驱动IO电压是5v，stm32 io要选择5v兼容IO口」，不要换成非 FT 引脚。PA15 是 JTAG 的 JTDI，KEY 沿用原工程只读 IDR 的接法，与 LCD 无关。

### 7.3 `delay` 与 TIM3 的关系（看似矛盾，实际不冲突）

之前反复强调过「不要用 SysTick 当 10 ms 节拍，因为 `delay_us/ms` 会反复重配 `LOAD`/`VAL`」。这里 LCD 用了 `delay_us()`，需要说清楚两者区别：

- TIM3 是**独立的硬件定时器**，有自己的计数器，不读 SysTick；它每 10 ms 进一次中断只置 `s_tick_10ms = 1`。
- `delay_us()` 是**同步阻塞的微秒级延时**，只在 LCD 初始化和写屏时、在主循环上下文里调用。
- 两者不共享寄存器，互不干扰。真正要避免的是**在中断服务函数里调 `delay_us()`**（会重配 SysTick 并长时间占用中断），目标代码没有这样用。

也就是说：TIM3 继续负责按键采样节拍；LCD 的位延时是一次性的、阻塞的、可预期的，两者职责不同。

### 7.4 绝对不要把 `LCD-uart/lcd.c` 编进工程

`qibiji/HARDWARE/LCD-uart/lcd.c:132-145` 定义了一个 `TIM3_IRQHandler()`（100 ms 中断，数 50 次置 `flag1s`）。`qibiji_zxl/HARDWARE/TIMER/timer.c` 里也有 `TIM3_IRQHandler()`。两个同名中断服务函数同时编译会直接报重复定义。

顺带说明：原工程的 `flag1s` 实际由 `qibiji/HARDWARE/TIMER/timer.c:4` 定义、`:246` 置位，`485.c:12` 消费；`LCD-uart/lcd.c` 里那个 `extern u8 flag1s` 是未编入工程的残留，不是链路的一部分。

---

## 8. 相对原工程做了哪些删改

| # | 原工程 | 目标版 | 原因 |
|---|---|---|---|
| 1 | `Lcd12864.C` + `Dispaly.c` + `DispalyPara.c` 三个文件、12 个 include | 一个 `lcd12864.c`，只依赖 `sys.h / delay.h / stm32f10x.h / wdg.h` | 驱动层与页面层分离，不引入 ADC / 电机 / EEPROM / 4G 依赖；`wdg.h` 仅用于上电 1.5s 等待分段喂狗 |
| 2 | `CS/SID/SCLK` 宏在 `Lcd12864.h` 公开，`BackLight` 在 `Display.h` 公开 | 4 个引脚宏收进 `.c`，头文件只有 5 个函数 | 外部不该直接翻引脚，绕过串行帧时序 |
| 3 | `WRCommandH / WRDataH / SPIWH / SendByteLCDH` 4 个全局函数 | `s_SendByte / s_Write / s_WriteCmd / s_WriteData` 4 个 `static` | 收窄可见范围；命令与数据共用一份帧代码 |
| 4 | `Lcd_Init()` 里夹 `IWDG_Feed()`、`DisplayCache_Init()`、`homepage()` | `LCD12864_Init()` 只做 GPIO + 等待 + 指令 + 清屏 + 背光，不显示任何业务内容 | 新工程已启用看门狗（三段等待后各喂一次）、无页面状态；显示内容由调用方决定 |
| 5 | `CLEARGDRAMH()` 结束停在扩展指令集 / 绘图开 | `LCD12864_ClearGDRAM()` 末尾补 `0x34 + 0x30` | 把原来散落在页面函数里的隐式兜底改成显式 |
| 6 | `ShowQQCharH(addr, hanzi, count)` 显示 GB2312 汉字 | `LCD12864_ShowString(row, str)` 显示 ASCII | 驱动层最小集；汉字字库数组属于业务层，后续轮次再接 |
| 7 | `BLight_Ctrl()` 读 `KeyNum`、`flag300ms`，30 秒自动熄屏 | `LCD12864_SetBacklight(on)` 只做电平 | 自动熄屏是策略，不是驱动；以后由上层用 TIM3 节拍实现 |
| 8 | `Lcd_Process()` 每轮主循环调 `BLight_Ctrl() + LcdDisplay()` 刷全屏 | 只在初始化和按键事件时写一次 | 减少无意义刷屏；当前没有页面状态可刷 |
| 9 | 全局变量 `autoFlag / ViewNum / displayCache / HomeNum / PageSta ...` | 全部不搬，模块内只有一张 `static const` 行地址表 | 驱动层无状态 |
| 10 | 全工程中文注释（GBK） | 全部 ASCII 注释 | 工程带 `--no-multibyte-chars`，避免编译日志乱码 |

**保持不变**：引脚分配、`GPIO_Mode_Out_PP` + `GPIO_Speed_50MHz`、3 线帧格式（`0xF8/0xFA` + 两个半字节）、MSB first、命令 100 us / 数据 20 us 延时、1.5 s 上电等待、基本指令 `0x30 / 0x06 / 0x0C / 0x02`、GDRAM 32 行 × 16 字 × 2 字节清 0、行地址 `0x80 / 0x90 / 0x88 / 0x98`。这些是原工程已验证过的硬件时序，不改变。

---

## 9. 上板验证

### 9.1 接线

```
LCD12864  CS    <->  PE3
          SID   <->  PE4
          SCLK  <->  PE5
          背光  <->  PA0
          VCC/GND 按原板接线（注意屏是 5V 逻辑）
USB-TTL   RX    <->  PA9，共地；115200 8N1
```

SSCOM 先「打开串口」，再按板子复位键（启动信息只发一次）。

### 9.2 预期现象

1. 上电约 1.5 s 后，串口出现两行启动信息（说明程序跑起来、时钟与波特率正确）。
2. 屏背光点亮，第 1 行显示 `qibiji_zxl LCD OK`。
3. 按任一键，串口出现 `KEY: xxx`；若做了 5.4 的可选改动，第 2 行同步显示键名，按住不松不重复刷新，松开再按才更新。

### 9.3 没显示时按顺序排查

| 现象 | 优先检查 |
|---|---|
| 串口有输出、屏全黑 | 背光极性：对调 `LCD_BL_ON/LCD_BL_OFF`；再查 PA0 接线 |
| 屏亮但花屏/乱码 | `delay_init()` 是否漏了；`delay_us()` 若倍乘数为 0，时序全部失效 |
| 屏亮、无字符 | `CLEARGDRAMH` 后是否回到基本指令集（目标版已显式补 `0x30`）；CS/SID/SCLK 三根线是否接反 |
| 编译报 `identifier ... is undefined` | `lcd12864.h` 里的 `#include "sys.h"` 是否漏写；IncludePath 是否加了 `..\HARDWARE\LCD12864` |
| 链接报 `TIM3_IRQHandler` 重复定义 | 误把 `qibiji/HARDWARE/LCD-uart/lcd.c` 加进了工程，删掉 |
| 上电后按键要等 1.5 s 才有反应 | 正常，`LCD12864_Init()` 内的上电等待是阻塞的，且放在 TIM3 启动之前 |

---

## 10. 约束与后续轮次

**约束**

1. `delay_init()` 必须在任何 `LCD12864_*()` 之前调用一次，且全工程只调一次。
2. `LCD12864_*()` 一律在主循环里调用，**不得在中断服务函数中调用**（内部有阻塞延时，且会重配 SysTick）。
3. 本轮显示 ASCII 字符，一个 DDRAM 地址放 2 个字符，每行最多 16 个；奇数长度自动补空格，超长截断。汉字需要 GB2312 双字节，属于业务层。
4. `LCD12864_ClearGDRAM()` 会写 1024 个数据帧，耗时几十毫秒，只在上电或切换绘图内容时调用，不要放进循环。
5. 工程是 C90（`<uC99>0</uC99>`，`SRTUV10.uvprojx:325`）：变量一律在块首声明，不要写 `for (int i = 0; ...)`，不要在语句之后再声明变量。目标代码已按此写法给出。

**后续轮次（本轮不做）**

- 第 2 轮：加「页面状态机」模块，把 `ViewPages()` 的翻页逻辑从 `KeyNum` 全局变量改成显式接收 `key_code_t`。
- 第 3 轮：加显示缓存 / 参数编辑，再考虑搬 `DispalyPara.c` 的 `DisplayInt / DisplayFloat / kaiduVal`。
- 第 4 轮：加 GDRAM 图标与进度条（`WRGDRAMp1~p4`、`GenerateProgressBar1`）、背光 30 秒自动熄屏（用 TIM3 的 10 ms 节拍计数，不新增定时器）。

---

## 11. 验证记录与证据索引

### 11.1 验证记录

目标代码在 host 端用桩头文件重建了引脚写入波形，再从 SCLK 上升沿还原出每一位，解码出每一帧，做行为断言：

```
gcc -std=c90 -Wall -Wextra   →  0 errors, 0 warnings
=== LCD12864 driver layer / host test ===
=== 30 checks, 0 failures ===
```

30 项覆盖：RCC 时钟位、两次 `GPIO_Init` 的引脚与模式、3×500 ms 上电等待、背光初始状态、所有帧起始字节合法（`0xF8`/`0xFA`）、初始化指令链 `30 06 0C 30 01 02`、GDRAM 清零帧数（1098 帧 = 6 初始化 + 2 + 32×(2+16×2) + 2）、1024 个数据帧全为 `0x00`、末尾回到基本指令集、`ShowString` 的帧数/行地址 `80 90 88 98`/字符内容/奇数补空格/16 字符截断/非法行与空指针保护、`Clear()` 指令链、背光极性、每帧一次 `delay_us`。

**这是 host 端 + 桩的行为验证，不是 Keil/真机编译**（本机没有 armcc）。请在 Keil 里点一次 Build 确认链接和烧录。

验证用的目标代码副本留在：`C:/Users/JessamynAdmin/AppData/Local/Temp/qibiji_lcd_test/`（`lcd12864.c`、`lcd12864.h`、`test_lcd.c`），可直接复制，不需要重新敲。

### 11.2 证据索引

| 内容 | 位置 |
|---|---|
| 原工程 LCD 初始化与主循环调用 | `qibiji/USER/main.c:42`（`Lcd_Init()`）、`:79`（`Lcd_Process()`）、`:27`（`delay_init()`） |
| 原工程 Keil 编入的 LCD 文件 | `qibiji/USER/SRTUV10.uvprojx:503-516` |
| 驱动层串行时序原型 | `qibiji/HARDWARE/Lcd12864/Lcd12864.C:15-57` |
| 清 GDRAM / 字符显示原型 | 同上 `:61-90`、`:164-180` |
| GPIO 配置、基本指令、页面依赖 | `qibiji/HARDWARE/Lcd12864/Dispaly.c:1-13`、`:147-227`、`:436-467` |
| 背光宏与极性矛盾的注释 | `qibiji/HARDWARE/Lcd12864/Display.h:16`；`Dispaly.c:184`、`:199-213` |
| 5V 兼容引脚提示 | `qibiji/HARDWARE/Lcd12864/Lcd12864.h:6` |
| 串口屏残留与 TIM3 中断冲突 | `qibiji/HARDWARE/LCD-uart/lcd.c:132-145`；`qibiji/HARDWARE/TIMER/timer.c:4,246` |
| 新工程延时实现与初值 | `qibiji_zxl/SYSTEM/delay/delay.c:38-39,54-74,116-149` |
| 新工程位带宏 / 标准库启用 | `qibiji_zxl/SYSTEM/sys/sys.h:3,35,47`；`qibiji_zxl/USER/stm32f10x_conf.h:39,43` |
| 新工程 IncludePath / HARDWARE 组 / SYSTEM 组 | `qibiji_zxl/USER/SRTUV10.uvprojx:338,408,413,499-533` |
| 当前主循环 | `qibiji_zxl/USER/main.c:36-60` |
