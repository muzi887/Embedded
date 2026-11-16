# LCD 重构操作建议_业务层（第 2 轮：页面状态机 + 汉字显示）

> 本文只给出目标代码和实施步骤，**没有改动任何源码**。照着顺序一个文件一个文件改。
> 目标代码已用 C90（`-std=c90 -Wall -Wextra`）编译通过（0 error / 0 warning），并跑了 26 项行为断言，见第 13 节。
> 前提：驱动层（《LCD重构操作建议_驱动层》那份）已上板验证成功。涉及源码：`qibiji/HARDWARE/Lcd12864/`、`qibiji_zxl/`。

---

## 1. 结论与范围

### 1.1 这轮做什么

- **搬汉字显示**：驱动层加一个 `LCD12864_ShowHanzi()`，一个 DDRAM 地址写 2 个原始字节 —— 汉字（GB2312 双字节）和自造字都用它。
- **建页面状态机**：新模块 `lcd_pages.c/.h`，管理「屏上现在是哪一页」，只响应按键事件：上一页 / 下一页 / 回首页。
- **2 个示范页**：首页（版本 / 电机 / 开度 / 流量 4 行）+ 设置页（上限位下限位 / 堵转 / 校准 3 行）。文字全部用**原工程现成的 GB2312 字节数组**（`Dispaly.c:29-75`），逐字节照抄，不新造字库。

### 1.2 这轮不做什么

- 显示缓存（`DisplayCache`、增量刷新）→ 第 3 轮。
- 参数编辑（`SetPageNum`、光标移动）→ 第 3 轮，届时 `KEY_UP / KEY_DOWN` 才启用。
- GDRAM 图标、进度条、边框、反选（`WRGDRAM*`、`GenerateProgressBar1`）→ 第 4 轮。
- 背光 30 秒自动熄屏（`BLight_Ctrl`）→ 第 4 轮。
- 原工程 5 个页面（首页 / 设置 / 4G / 设备 / 操作）→ 本轮只做 2 页示范；页面框架搭好后加页面就是加一个 case + 一个绘制函数。

### 1.3 原工程的页面切换逻辑（照搬的依据）

`ViewPages(uint8_t KeyNum)`（`Dispaly.c:346-391`）里只有 3 个键翻页：

| 原工程键 | 新工程 `key_code_t` | 动作 |
|---|---|---|
| `KEY_LEFT`（丝印 SET，K4） | `KEY_SET` | 上一页，`ViewNum--`，到 0 再减绕到最后 |
| `KEY_RIGHT`（丝印 CAN，K3） | `KEY_CANCEL` | 下一页，`ViewNum++`，到顶绕回 0 |
| `KEY_ENTER`（丝印 STOP，K5） | `KEY_STOP` | 回首页（`ViewNum = 0`） |

页面号存在 `ViewNum`（`u8`），又复制了一份到 `PageSta`（`:384-389`）—— 两个变量装同一个值，目标版合并成一个枚举。`KEY_UP / KEY_DOWN` 在原工程里做页内项目移动（`HomeNum`），本轮不做。

**注意一个语义点**：CAN 键（丝印"取消"）的功能是**下一页**，STOP 键（丝印"停止"）的功能是**回首页** —— 这是原工程按键接线的历史映射（`key.h:20-22` 注释里写明 STOP 旧名 ENTER、SET 旧名 LEFT、CANCEL 旧名 RIGHT），本轮原样照搬。你觉得别扭想改，改的只是 `LCD_PAGES_Process()` 里的 case 顺序，不动任何其它代码。

---

## 2. 改动文件清单与落地顺序

| 堆 | 文件 | 动作 | 内容 |
|---|---|---|---|
| 驱动层加 1 个函数 | `qibiji_zxl/HARDWARE/LCD12864/lcd12864.h` | 改 1 处 | 加 1 行声明 |
| 驱动层加 1 个函数 | `qibiji_zxl/HARDWARE/LCD12864/lcd12864.c` | 改 1 处 | 加 `LCD12864_ShowHanzi()` |
| 正题 | `qibiji_zxl/HARDWARE/LCD12864/lcd_pages.h` | 新建 | 页面模块对外接口，2 个函数 |
| 正题 | `qibiji_zxl/HARDWARE/LCD12864/lcd_pages.c` | 新建 | 页面状态机 + GB2312 文字行 + 2 个绘制函数 |
| 配套 | `qibiji_zxl/USER/main.c` | 改 3 处 | 加 include、加 `LCD_PAGES_Init()`、`APP_DispatchKey` 里分发事件 |
| 配套 | `qibiji_zxl/USER/SRTUV10.uvprojx` | 改 1 处 | HARDWARE 组加 `lcd_pages.c/.h` 两个节点 |

**IncludePath 不用动**：`lcd_pages.c` 和驱动同目录，`..\HARDWARE\LCD12864` 上轮已在路径里。

**落地顺序**：第 3 节 → 第 4 节 → 第 5 节 → 第 6 节 → 第 7 节，每步一个文件。

---

## 3. 第 1 步：驱动层加 `LCD12864_ShowHanzi()`

**这个函数干什么**：`ShowString()` 的兄弟。`ShowString()` 处理 ASCII（自动补空格、自动配对），`ShowHanzi()` 处理**原始 2 字节格**：调用方给一个字节数组，每 2 字节占一个 DDRAM 地址，原样写入。汉字是 GB2312 双字节，一个地址恰好一个汉字；以后自造字也走这里。

### 3.1 `lcd12864.h` 改 1 处

在 `LCD12864_ShowString` 声明后加一行：

```c
void LCD12864_ShowString(uint8_t row, const char *str);
void LCD12864_ShowHanzi(uint8_t row, const uint8_t *buf, uint8_t count);
void LCD12864_SetBacklight(uint8_t on);
```

### 3.2 `lcd12864.c` 改 1 处

在 `LCD12864_ShowString()` 函数**之后**、`LCD12864_Init()` 之前加：

```c
/* Write raw 2-byte cells (GB2312 hanzi / custom glyph bytes) on one row.
 * One DDRAM address holds two bytes, so 'count' counts address cells
 * (1..8); bytes are written as-is, no padding, no ASCII pairing.
 * The auto-increment mode set by LCD_CMD_ENTRY at init makes the second
 * byte of each cell land on the adjacent half of the same address. */
void LCD12864_ShowHanzi(uint8_t row, const uint8_t *buf, uint8_t count)
{
    uint8_t addr;
    uint8_t i;

    if ((row >= LCD_ROWS) || (buf == (const uint8_t *)0))
        return;
    if (count > LCD_ADDR_PER_ROW)
        count = LCD_ADDR_PER_ROW;

    addr = s_row_addr[row];
    s_WriteCmd(LCD_CMD_BASIC);
    for (i = 0u; i < count; i++)
    {
        s_WriteCmd(addr);
        s_WriteData(buf[i * 2u]);
        s_WriteData(buf[(i * 2u) + 1u]);
        addr++;
    }
}
```

**逐项说明**

| 项 | 说明 |
|---|---|
| `count` 按"格"数 | 1 格 = 2 字节 = 1 个汉字 = 屏上 16×16 点。一行 8 格，超 8 截断。 |
| 开头 `0x30` | 保证任何调用时刻屏都在基本指令集（和 `ShowString` 同样的自保动作）。 |
| **每个格子都重发地址** | 线上序列是 `地址n, 高字节, 低字节, 地址n+1, 高字节, 低字节…`，不是"地址发一次连灌 16 字节"。每格重设地址是防 `0x06` 自增模式在任何边界上出错位，代价只是多几个命令帧。原工程 `ShowQQCharH()`（`Lcd12864.C:61-73`）同样是每格发 `WRCommandH(addr)`。 |
| 2 字节能落进同一地址 | 靠初始化时 `0x06` 设的地址自增：写高字节后 AC 自动 +1，低字节落进同一地址的后半格（上一轮解释过）。 |
| 无补空格 | 字节数是奇数时，**不**像 ShowString 那样补空格 —— 汉字永远是成对的 2 字节，补空格反而会把下一个字切走半个。 |

---

## 4. 第 2 步：新建 `HARDWARE/LCD12864/lcd_pages.h`

**这个文件干什么**：页面模块对外的全部接口。外部只知道"喂给它按键事件"，不知道有几页、怎么画。

```c
#ifndef __LCD_PAGES_H
#define __LCD_PAGES_H

/* LCD pages module (business layer, round 2).
 * Owns: page state machine + the GB2312 text rows drawn on each page.
 * Reacts to key events only; no periodic refresh, no display cache yet.
 * Depends on: key.h (key_code_t) and lcd12864.h (driver layer only). */
#include "key.h"

void LCD_PAGES_Init(void);
void LCD_PAGES_Process(key_code_t key);

#endif
```

`#include "key.h"` 是必须的：接口参数类型是 `key_code_t`。`key.h` 自包含（它自己 include 了 `sys.h`）。

---

## 5. 第 3 步：新建 `HARDWARE/LCD12864/lcd_pages.c`

**这个文件干什么**：三件事 —— 记住当前页、响应翻页键、把当前页画出来。文字数组是从原工程逐字节抄来的，注释里标了每行的内容（用英文/拼音写，遵守 `--no-multibyte-chars`）。

```c
/* ===========================================================================
 * LCD pages module - qibiji_zxl (business layer, round 2)
 *
 * Owns: page state machine (which page is on screen) and the GB2312 text
 *        rows drawn on each page. Reacts to key events only, no periodic
 *        refresh, no display cache yet (round 3).
 * Depends on: key.h (key_code_t) and lcd12864.h (driver layer only).
 *
 * Text bytes are copied verbatim from the original project
 * (qibiji/HARDWARE/Lcd12864/Dispaly.c:29-75) - GB2312 hand-coded arrays,
 * kept byte-for-byte, do not re-type them.
 * =========================================================================== */
#include "lcd_pages.h"
#include "lcd12864.h"

/* ----------------------------- Page ids --------------------------------- */
typedef enum
{
    PAGE_HOME = 0,   /* home: version / motor / opening / flow            */
    PAGE_SET,        /* settings: limits / stall row / calibration row    */
    PAGE_COUNT       /* number of pages, used for wrap-around             */
} page_id_t;

static page_id_t s_page = PAGE_HOME;

/* ---------------- GB2312 text rows (from Dispaly.c:29-75) ----------------
 * Each row = 8 DDRAM address cells x 2 bytes = 16 bytes on screen.       */
static const uint8_t s_home_l1[16] = {         /* "version:" + blanks      */
    0xb0, 0xe6, 0xb1, 0xbe, 0x3a, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 };
static const uint8_t s_home_l2[16] = {         /* "motor: 1.6A 12.6V"      */
    0xb5, 0xe7, 0xbb, 0xfa, 0x3a, 0x20,
    0x31, 0x2e, 0x36, 0x41, 0x20, 0x31, 0x32, 0x2e, 0x36, 0x56 };
static const uint8_t s_home_l3[16] = {         /* "opening: 100% quan kai" */
    0xbf, 0xaa, 0xb6, 0xc8, 0x3a, 0x20,
    0x31, 0x30, 0x30, 0x25, 0x20, 0x20, 0xc8, 0xab, 0xbf, 0xaa };
static const uint8_t s_home_l4[16] = {         /* "flow: 0.0 m3/s"         */
    0xc1, 0xf7, 0xc1, 0xbf, 0x3a, 0x20,
    0x20, 0x30, 0x2e, 0x30, 0x20, 0x6d, 0x33, 0x2f, 0x73, 0x20 };
static const uint8_t s_set_l1[16] = {          /* "up-limit    low-limit"  */
    0xc9, 0xcf, 0xcf, 0xde, 0xce, 0xbb,
    0x20, 0x20, 0x20, 0x20,
    0xcf, 0xc2, 0xcf, 0xde, 0xce, 0xbb };
static const uint8_t s_set_l2[16] = {          /* stall row, bytes verbatim*/
    0xb6, 0xc2, 0xd7, 0xaa, 0x3a, 0x20,
    0x30, 0x30, 0x2e, 0x30, 0x20, 0x41, 0x20, 0x20, 0x20, 0x20 };
static const uint8_t s_set_l3[16] = {          /* "calib: closed"          */
    0xd0, 0xa3, 0xd7, 0xbc, 0xb9, 0xd8, 0xb1, 0xd5,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 };

/* --------------------------- Page painters -------------------------------
 * Clear + redraw all rows. A full redraw is ~100 serial frames, a few ms
 * per page switch - acceptable while there is no dynamic data. The
 * display cache (round 3) will make this incremental.                   */
static void s_DrawHomePage(void)
{
    LCD12864_Clear();
    LCD12864_ShowHanzi(0u, s_home_l1, 8u);
    LCD12864_ShowHanzi(1u, s_home_l2, 8u);
    LCD12864_ShowHanzi(2u, s_home_l3, 8u);
    LCD12864_ShowHanzi(3u, s_home_l4, 8u);
}

static void s_DrawSetPage(void)
{
    LCD12864_Clear();
    LCD12864_ShowHanzi(0u, s_set_l1, 8u);
    LCD12864_ShowHanzi(1u, s_set_l2, 8u);
    LCD12864_ShowHanzi(2u, s_set_l3, 8u);
}

static void s_DrawPage(page_id_t page)
{
    switch (page)
    {
    case PAGE_SET:
        s_DrawSetPage();
        break;
    case PAGE_HOME:
    default:
        s_DrawHomePage();
        break;
    }
}

/* ----------------------------- Public API -------------------------------- */
void LCD_PAGES_Init(void)
{
    s_page = PAGE_HOME;
    s_DrawPage(s_page);
}

/* Feed one key event. Only page-switch keys act; the screen is redrawn
 * only when the page actually changes - same idea as the original
 * displayCache.page_last check, but local to this module.               */
void LCD_PAGES_Process(key_code_t key)
{
    page_id_t old;

    old = s_page;

    switch (key)
    {
    case KEY_SET:      /* was KEY_LEFT in the original: previous page, wrap */
        s_page = (page_id_t)(((int)s_page + (int)PAGE_COUNT - 1) % (int)PAGE_COUNT);
        break;
    case KEY_CANCEL:   /* was KEY_RIGHT: next page, wraps around */
        s_page = (page_id_t)(((int)s_page + 1) % (int)PAGE_COUNT);
        break;
    case KEY_STOP:     /* was KEY_ENTER: back to home page */
        s_page = PAGE_HOME;
        break;
    case KEY_UP:       /* reserved: cursor / item inside a page (round 3) */
    case KEY_DOWN:     /* reserved */
    case KEY_NONE:
    default:
        break;
    }

    if (s_page != old)
    {
        s_DrawPage(s_page);
    }
}
```

### 5.1 逐项说明

| 项 | 说明 |
|---|---|
| `s_page` 一个变量 | 替代原工程 `ViewNum` + `PageSta` 两个重复变量。枚举值 0=首页、1=设置页，加页就加枚举项 + 一个绘制函数 + 一个 case。 |
| 上一页公式 `(s+COUNT-1)%COUNT` | C90 里负数取余结果依赖实现（`-1 % 2` 可能是 -1），所以先 `+COUNT` 再取余，任何编译器都是正确环绕。 |
| `if (s_page != old)` 才重绘 | 只在页面真变了才动屏。按 STOP 时已经在首页 → 什么都不发生。这就是原工程 `displayCache.page_last` 想做的事，本轮用局部变量实现，整套缓存留给第 3 轮。 |
| 切页 = `Clear()` + 整页重画 | 首页 103 帧、设置页 78 帧，约 5~10 ms 一次，按键事件触发，主循环无感。数据量大了以后（第 3 轮）再上缓存做增量刷。 |
| `KEY_UP / KEY_DOWN / KEY_NONE` | 显式 `case` 空操作并注释"预留"。参数编辑（第 3 轮）启用 UP/DOWN 时光改这里。 |

### 5.2 文字数组的三个注意点

1. **逐字节照抄**，不要用输入法重打：Keil 的 `--no-multibyte-chars` 下源码里直接写中文会变乱码字节，原工程全部用 `0xb5,0xe7` 手写字节就是绕这个坑。
2. 每行正好 16 字节 = 8 格 = 8 个汉字位。不足的用 `0x20`（空格）补满——原工程数组已经是补好的。
3. `s_set_l2`（堵转行）字节取自原工程 `duzhuan[]`（`Dispaly.c:41`）：`0xb6, 0xc2` 是「堵」。曾误写成 `0xb6, 0xd2`（GB2312 里是「兑」），屏上显示「兑转」，已订正 —— 汉字字节改动后务必用解码工具核一遍，别靠目测。

---

## 6. 第 4 步：改 `USER/main.c`（3 处）

以你当前磁盘上的版本为底稿，改动加粗标出（实际文件里没有星号）：

```c
#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"      /* 改动 1 */

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

    LCD_PAGES_Process(key);     /* 改动 3：页面状态机消费按键事件 */
}

int main(void)
{
	key_code_t key;

	NVIC_Configuration();
	DEBUG_UART_Init();
	KEY_Init();
	delay_init();
	LCD12864_Init();
	LCD_PAGES_Init();           /* 改动 2：画首页，必须在 LCD12864_Init() 之后 */
	TIM3_10ms_Init();

	DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
	DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

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

三个改动没有别的先后要求，但**改动 2 必须在 `LCD12864_Init()` 之后**（画页面要先有屏）。上一轮建议的 `LCD12864_ShowString(0, "qibiji_zxl LCD OK")` 这轮**不用加了**：上电 `LCD_PAGES_Init()` 会画首页，那行会被盖掉。

---

## 7. 第 5 步：改 `USER/SRTUV10.uvprojx`（1 处）

IncludePath **不用动**。只在 HARDWARE 组 `lcd12864.h` 节点之后、`</Files>` 之前追加：

```xml
            <File>
              <FileName>lcd_pages.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\HARDWARE\LCD12864\lcd_pages.c</FilePath>
            </File>
            <File>
              <FileName>lcd_pages.h</FileName>
              <FileType>5</FileType>
              <FilePath>..\HARDWARE\LCD12864\lcd_pages.h</FilePath>
            </File>
```

**改工程文件前先关掉 Keil 里的工程**（或改完按提示 reload），否则 Keil 保存时会把磁盘上的改动冲掉 —— 驱动层那轮已经踩过一次。

---

## 8. 上板后的按键交互（对照表）

| 按键（丝印） | `key_code_t` | 屏幕反应 | 串口 |
|---|---|---|---|
| SET（K4） | `KEY_SET` | 上一页：首页→设置页；设置页→首页 | `KEY: SET` |
| CAN（K3） | `KEY_CANCEL` | 下一页：首页→设置页；设置页→首页 | `KEY: CANCEL` |
| STOP（K5） | `KEY_STOP` | 回首页（在首页时屏不变） | `KEY: STOP` |
| UP（K1）/ DOWN（K2） | `KEY_UP/DOWN` | 本轮屏不变 | `KEY: UP/DOWN` |

两页循环：任意方向键都能在 首页↔设置页 之间绕圈。

---

## 9. 依赖与冲突核查

### 9.1 包含链

```
lcd_pages.c
 ├─ "lcd_pages.h" ──> "key.h" ──> "sys.h"     (key_code_t)
 └─ "lcd12864.h"  ──> "sys.h"                 (LCD12864_* 驱动接口)
```

页面模块**只**通过 `lcd12864.h` 的 4 个函数碰屏（Clear / ShowHanzi / 以及间接的 SetBacklight），不碰引脚、不碰指令值 —— 这是驱动层/业务层的分界线。

### 9.2 与现有模块

- **KEY**：`lcd_pages` 只消费 `KEY_Pop()` 出来的事件，不读引脚、不做消抖，与 `key.c` 无冲突。
- **TIM3**：页面模块不用节拍（纯事件驱动），TIM3 继续只服务按键采样。
- **DEBUG_UART**：互不干扰，同一按键在串口和屏上各反应一次。

### 9.3 C90 检查

枚举类型、`switch-case`、`static const` 数组都是 C90 合法写法；所有局部变量在块首声明；取余前先转 `int` 并加 `PAGE_COUNT` 避免负数取余。目标代码已按 `-std=c90 -Wall -Wextra` 编译验证。

---

## 10. 相对原工程做了哪些删改

| # | 原工程 | 目标版 | 原因 |
|---|---|---|---|
| 1 | `ViewNum(u8)` + `PageSta(u8)` 两个变量装同一个值 | 一个 `page_id_t` 枚举 | 去冗余；编译器检查枚举值 |
| 2 | `ViewPages(KeyNum)` 收全局 `KeyNum` | `LCD_PAGES_Process(key)` 显式收事件参数 | 模块不偷看别人的全局变量 |
| 3 | `DisplayViewPages()` 里 5 个页面 + GDRAM 反选 | 2 个示范页，无 GDRAM | 图标/反选属第 4 轮 |
| 4 | `displayCache.page_last` 比较决定重绘 | 局部 `old != s_page` 比较 | 整套缓存是第 3 轮的事，先用最小实现 |
| 5 | `homepage()` 里 GDRAM 图标 + 进度条 + 每轮动态刷新 | 静态文字 + 只在切页时重画 | 本轮无数据源；缓存上来再增量刷 |
| 6 | `ShowQQCharH()` 开头 `WRCommandH(0x30)` 隐式兜底 | 收编为驱动层 `LCD12864_ShowHanzi()`，`0x30` 显式保留 | 驱动管"怎么写"，业务管"写什么" |
| 7 | `LCDInit()` 里的 `0x06/0x0C` 由页面函数间接触发 | 初始化已在驱动层完成 | 页面模块不再负责屏配置 |
| 8 | 汉字数组散在 `Dispaly.c` 顶部 | 集中在 `lcd_pages.c`，逐字节照抄 | 内容跟随页面模块走 |

**保持不变**：GB2312 字节数组内容、行地址映射（`80/90/88/98`，驱动层已实现）、每格 2 字节的写入方式、SET/CAN/STOP 三键的翻页方向。

---

## 11. 上板验证

### 11.1 预期现象

1. 上电约 1.5 s 后屏亮，显示首页 4 行汉字：
   - 第 1 行 `版本：`
   - 第 2 行 `电机: 1.6A 12.6V`
   - 第 3 行 `开度: 100%  全开`
   - 第 4 行 `流量:  0.0 m3/s`
   （数字是原工程数组里的静态占位文本，不是真数据 —— 真数据等接了 ADC/EEPROM 才有。）
2. 按 SET（K4）：屏切到设置页 —— `上限位    下限位` / `堵转: 00.0 A` / `校准关闭`，同时串口出 `KEY: SET`。
3. 再按 SET：绕回首页。按 CAN（K3）：首页→设置页→首页循环。按 STOP（K5）：回到首页。
4. 在首页按 STOP：屏不变、不闪（事件驱动，不重绘）。
5. 按 UP / DOWN：屏不变，只有串口输出（本轮预留）。

### 11.2 没有按预期时

| 现象 | 优先检查 |
|---|---|
| 屏全黑/背光不亮 | 驱动层问题，回驱动层文档第 9.3 节排查 |
| 汉字位置对但笔画是乱码 | GB2312 数组抄错字节 —— 逐字节对照 `Dispaly.c:29-75` |
| 字只显示一半/下一行串位 | `LCD12864_ShowHanzi` 没加对（每格必须重发地址命令） |
| 按键串口有输出但屏不翻页 | `APP_DispatchKey` 里是否漏了 `LCD_PAGES_Process(key);` |
| 屏翻页但首页空白 | `LCD_PAGES_Init()` 是否漏加，或位置在 `LCD12864_Init()` 之前 |
| 编译报 `key_code_t undefined` | `lcd_pages.h` 里 `#include "key.h"` 漏了 |
| 编译报 `LCD12864_ShowHanzi undefined` | 链接不到：`lcd12864.c` 的函数体没加，或加了没保存 |

---

## 12. 约束与后续轮次

**约束**

1. `LCD_PAGES_Process()` 只在主循环上下文调用（现在挂在 `APP_DispatchKey` 里），**不要**在中断里调 —— 内部画一页要几毫秒。
2. 加新页面 = 三步：`page_id_t` 加枚举项、写一个 `s_DrawXxxPage()`、`s_DrawPage()` 加一个 case。不要在页面函数里直接调 `s_WriteCmd` —— 那是驱动层的私有层。
3. `LCD12864_ShowHanzi()` 的 `count` 上限 8，超了会截断；字节永远成对给。
4. 页面重绘是毫秒级阻塞，事件触发没有问题；但**不要**把它放进每 10ms 的节拍里刷。

**后续轮次（本轮不做）**

- 第 3 轮：显示缓存 + 参数编辑 —— `DisplayCache` 增量刷新、`SetPageNum`/光标、UP/DOWN 在设置页内移动和改参数（数值格式化 `DisplayInt/DisplayFloat` 一起搬）。
- 第 4 轮：GDRAM 图标与进度条（`WRGDRAM*`、`GenerateProgressBar1`、自造字 `WRCGRAMH`/`ShowCGCharH`，芯片限 4 个自造字位）、背光 30 秒自动熄屏（用 TIM3 的 10ms 节拍计数，不新增定时器）。

---

## 13. 验证记录与证据索引

### 13.1 验证记录

host 端用桩头文件重建引脚波形、解码每一帧（与驱动层验证同一套方法），对页面模块 + 带 `ShowHanzi` 的驱动层做了行为断言：

```
gcc -std=c90 -Wall -Wextra   →  0 errors, 0 warnings
=== LCD pages module / host test ===
=== 26 checks, 0 failures ===
```

26 项覆盖：`LCD_PAGES_Init` 画首页（帧数 103 = Clear 3 帧 + 4 行×25 帧、恰好一次 Clear、4 行地址 `80/90/88/98` 下的字节逐一比对原工程数组）；SET/CAN/STOP 的页面转移（含两页环绕、同页按键零重绘、UP/DOWN/NONE 零输出）；`ShowHanzi` 的行地址映射、每格重发地址（`F8 90, FA 12, FA 34, F8 91, FA 56, FA 78` 序列逐帧比对）、count 超限截断到 8、非法行/空指针保护、每帧一次 `delay_us`、全部帧起始字节合法。

**这是 host + 桩的行为验证，不是 Keil/真机编译**（本机没有 armcc）。请在 Keil 里点一次 Build 确认。

验证代码副本在：`C:/Users/JessamynAdmin/AppData/Local/Temp/qibiji_pages_test/`（`lcd_pages.c/.h`、含 `ShowHanzi` 的 `lcd12864.c/.h`、`test_pages.c`、桩），可直接复制。

### 13.2 证据索引

| 内容 | 位置 |
|---|---|
| 原工程页面切换三键逻辑 | `qibiji/HARDWARE/Lcd12864/Dispaly.c:346-391`（`ViewPages`） |
| 原工程页面分发与 5 个页面 | 同上 `:394-433`（`DisplayViewPages`） |
| `ViewNum`/`PageSta` 冗余 | 同上 `:119-122`、`:384-389` |
| 汉字数组来源（逐字节照抄） | 同上 `:29-75`（`motor/kaidu/liuliang/banben/duzhuan/jiaozhun/shang_xia_xianwei`） |
| `ShowQQCharH` 每格发地址的原型 | `qibiji/HARDWARE/Lcd12864/Lcd12864.C:61-73` |
| 键名新旧映射 | `qibiji_zxl/HARDWARE/KEY/key.h:15-23` |
| 驱动层行地址表 / `0x06` 自增 | `qibiji_zxl/HARDWARE/LCD12864/lcd12864.c`（`s_row_addr`、`LCD_CMD_ENTRY`） |
| main.c 当前状态（改动底稿） | `qibiji_zxl/USER/main.c` |
| uvprojx HARDWARE 组（插入点） | `qibiji_zxl/USER/SRTUV10.uvprojx:499-546` |
