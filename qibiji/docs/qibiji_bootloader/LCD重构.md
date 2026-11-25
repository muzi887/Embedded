# LCD 重构

> **来源（双层）**：驱动层取自 `qibiji_zxl/HARDWARE/LCD12864/lcd12864.c/.h`——**应用工程的重写版驱动**，
> 与 boot 版同硬件接线（PE3/4/5 + PA0）、同 ST7920 时序，但修掉了 boot 51 遗留版的若干实病（§1.1）；
> OTA 提示层取自 `qibiji_boot/HARDWARE/Lcd12864/Dispaly.c/Display.h`（升级画面只有 boot 有，zxl 没有）；
> 进度条按 zxl 驱动原语（`LCD12864_FillBlock`/`LCD12864_ShowHanzi`）**重写**，屏上画面与原实机渲染等价（§9）。
> 迁移计划阶段 8。boot 的 8 个文件收敛为 5 个：`Lcd12864.h/.c`（驱动层）+ `Display.h/.c`（OTA 提示层）+
> `ProgressBar.h/.c`（进度条）。
> 本阶段两轮修订：① 命名审计（51 尾缀名/纯序号名按功能重定义，见 §1.1）；② **换源参考 zxl**——
> 用户确认重构逻辑应参考重写后的应用工程：驱动层整体换 zxl 版、进度条用 zxl 原语重写。

## 1. 产出清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/LCD/Lcd12864.h` | 换源重写 | 引脚宏 + 驱动层声明（源自 zxl lcd12864.h，u8 化，见 §2） |
| `HARDWARE/LCD/Lcd12864.c` | 换源重写 | 串行时序、ST7920 常量、DDRAM/GDRAM 绘制、含上电稳定期的初始化（源自 zxl lcd12864.c，见 §3） |
| `HARDWARE/LCD/Display.h` | 重写 | OTA_ERR_* 8 个错误码（**定义归属维持原工程，就在此文件**）+ OTA 提示层声明（见 §4） |
| `HARDWARE/LCD/Display.c` | 重写 | GB2312 文案码表（static const）+ Ota_Show* 状态/错误页 + wait net 提示（见 §5） |
| `HARDWARE/LCD/ProgressBar.h/.c` | 重写 | 进度条：配置结构 + 画条/边框/百分比，按 zxl 原语实现（见 §6） |
| `USER/main.c` | 合并 | 4G 联测 + 本模块四幕验证合并为一个循环（实机定稿形态，见 §7） |
| Keil 工程 | 手动加 | HARDWARE 组加 `Lcd12864.c`、`Display.c`、`ProgressBar.c`；Include Paths 加 `..\HARDWARE\LCD`（见 §8） |

### 1.1 与原工程的差异（换源 + 裁剪 + 命名按功能重定义）

| 差异 | 原工程位置 | 现位置 | 理由 |
|---|---|---|---|
| **驱动层换源**：boot `Lcd12864.C/.h` → zxl `lcd12864.c/.h` | `qibiji_boot/HARDWARE/Lcd12864` | `Lcd12864.c/.h` | 两个工程是**同一套硬件接线**（PE3/4/5 + PA0）、同一颗 ST7920，zxl 版是应用工程里重写过的驱动，严格更优：① `LCD12864_Init` 内置 ST7920 上电 1.5s 稳定期（500ms×3 分片 + 每片喂狗），boot 版完全不等（原怪异 5）；② GDRAM 行绘制后只回基本指令集——boot 版每次发 0x34 会**关掉绘图层**（zxl 注释明确记载这个坑）；③ `FillBlock` 按像素行入参、自动处理 ST7920 上下半屏偏移，boot 版没有矩形原语；④ 全参数防御（row/x/y 越界、空指针） |
| 驱动函数名采用 zxl 原名（不改）：`LCD12864_Init/Clear/ClearGDRAM/ShowString/ShowHanzi/FillBlock/SetBacklight` | — | `Lcd12864.c/.h` | 与 zxl 应用工程、docs 里既有 LCD 解读稿跨工程一致；boot 旧名对照：`WRCommandH/WRDataH`→驱动私有 `s_WriteCmd/s_WriteData`（不再公开）、`SendByteLCDH/SPIWH`→`s_SendByte/s_Write`、`ShowQQCharH`→`LCD12864_ShowHanzi`（addr 入参改为 row 行号）、`CLEARGDRAMH`→`LCD12864_ClearGDRAM`（无参化，固定清 0x00） |
| `LCD12864_DrawRow/DrawStrip` | zxl `lcd12864.c` | **删除** | bootloader 零调用（12 行竖条是业务页横幅用途）；bootloader 用面由 `FillBlock`（进度条矩形）+ `ShowHanzi`（文本格）+ `ClearGDRAM` 覆盖；zxl 应用工程保留全量不受影响 |
| `Lcd_Gpio_Init` + `Lcd_Init` + `Lcd_ClearGDRAM` 三步 → `LCD12864_Init()` 一步 | boot `Dispaly.c`/`Lcd12864.C` + boot main | `Lcd12864.c` | GPIO 配置、指令序列、清屏本来就是一件事（zxl 的做法）；原 boot main 的三步序列废止，正式 main 上电只调一个 `LCD12864_Init()`（§10.3） |
| 目录 `HARDWARE\Lcd12864` → `HARDWARE\LCD`；`Lcd12864.C`（大写扩展名）→ `.c` | — | — | 与工程内其它模块（4G/GPRS/BOOT）风格一致；扩展名统一小写 |
| `Dispaly.c`（文件名拼写错误）→ `Display.c` | — | `Display.c` | 修正拼写，与 `Display.h` 配套 |
| `ota_step1~7` → `Ota_ShowConnecting/Ota_ShowRequestVer/Ota_ShowUpdating/Ota_ShowNoUpdate/Ota_ShowErrorHint/Ota_ShowDone/Ota_ClearText` | `Dispaly.c` | `Display.c` | 纯序号名不可读（step4 画什么必须看代码）；bootloader 里只有尚未编写的正式 main 调用，改名零成本 |
| `progressbar_Init→ProgressBar_Setup`、`ota_show_error→Ota_ShowError`、`loading→Lcd_ShowWaitNet` | `Dispaly.c` | `Display.c` | 前缀归家族（Ota_/ProgressBar_/Lcd_）；Setup = OTA 场景装配，与库级 `ProgressBar_Init` 区分 |
| 文案数组改 `static const u8 s_txt_*`：`title→s_txt_title`、`step1~7→s_txt_connecting/connected/request_ver/updating/no_update/error/done`、`empty→s_txt_blank`、`err_*→s_txt_err_*` | `Dispaly.c` | `Display.c` | 只读数据进 RO 段；文件级数据加 s_ 前缀（工程惯例） |
| **`Lcd_DisplayString` 删除**（原 `Dispaly.c`） | `Dispaly.c` | **删除** | 唯一消费方是 `loading()`；「wait net」改由 `LCD12864_ShowString(0, "    wait net")` 前导空格定位实现（zxl 风格，画面位置不变）；整行预清空职责归调用方（OTA 页本来就先铺 blank 行） |
| **进度条按 zxl 原语重写**：`DrawBar/DrawBorder/Clear` 改用 `LCD12864_FillBlock`，`ShowPercent` 改用 `LCD12864_ShowHanzi` 整行重画；配置字段改像素直述（`y_start`/`height`=像素行、`x_word`=字列） | `ProgressBar.c` | `ProgressBar.c` | 消除原版三处怪异：X 基址把 DDRAM 行地址表当 GDRAM 基址混用、`ProgressBar_Clear` 的 Y 地址不随循环递增、循环外未初始化的 `i`（§9 逐条对照）；**屏上画面与原实机渲染一致**（几何推导见 §9） |
| `ProgressBar_Update_Incremental` 删除 | `ProgressBar.c` | **删除** | 全工程零调用；`ProgressBar_Update` 本身已只在百分比变化时刷文字 |
| `clearall` 删除 | `ProgressBar.c` | **删除** | 零引用；功能与 GDRAM 清屏重复，名字还不符（是填充不是清除） |
| `Lcd_DisplayString_Default`；`ProgressBar.h` 里的 `loading` 声明 | `Dispaly.c`、`ProgressBar.h` | **删除**（loading 归位 Display.h，更名 `Lcd_ShowWaitNet`） | 兼容旧版的垫片函数，bootloader 无调用方 |
| p1/p2 图标数组、`WRGDRAM1/WRGDRAM128X8/WRGDRAM/WRGDRAMp1~4`、`ShowNUMCharH/WRCGRAMH/ShowCGCharH`、`code[][24]` 字模 | `Lcd12864.C`、`Dispaly.c` | **删除** | 全工程零调用（业务图标/画图） |
| `DispalyPara.c` 整文件、`ProgressBar_Example.c` | — | **删除** | 应用业务层 / 演示文件，bootloader 不用 |
| `Display.h` 业务声明（homepage/setpage/ViewPages/DisplayState/getval/kaiduVal/fjiaozhun/DisplayViewPages/Lcd_Process/BLight_Ctrl/DisplayInit/LcdDisplay/ViewPage 等页面宏） | `Display.h` | **删除** | 对应实现要么在 DispalyPara.c（不迁）要么为空壳/零调用 |
| `autoFlag/setParaflag/HomeNum/ViewNum/SetPageNum/SetParaNum/PageSta` | `Dispaly.c` | **删除** | 业务页面状态变量，零引用 |
| `#include "usart.h"`、`#include "24cxx.h"` | `Dispaly.c` | **删除** | 文件内无串口/EEPROM 调用 |
| 类型名 `uint8_t` → `u8`（zxl 源码用 `uint8_t`） | zxl/lcd12864.c | 统一 | 工程类型惯例（`u8` 由 `sys.h` typedef，4G/LED/验证 main 同风格） |
| 引脚宏沿用 zxl 的 `LCD_CS/LCD_SID/LCD_SCLK/LCD_BL`（boot 版裸名 `CS/SID/SCLK/BackLight` 弃用） | `Lcd12864.h` | `Lcd12864.h` | 裸名污染全局宏命名空间；zxl 同款先例 |
| `WDG_Feed` 依赖（zxl `LCD12864_Init` 内 3 处） | zxl `lcd12864.c` | `Lcd12864.c` | 上电稳定期分片喂狗；`wdg.h` 属 WDG 阶段产物，落盘策略见 §10.5 |
| **保留**：`ProgressBar_Init/Update/DrawBorder/ShowPercent/Clear`、OTA_ERR_* 归属 `Display.h` | — | 原名/原归属 | 名实相符；REG 文档已按 Display.h 归属预告 |
| 注释 | 原/源注释 | 全部规范中文注释 | 现行注释规范 |
| 时序 / 指令序列 / GB2312 码表 / 屏上画面 | — | 逐行等价 | 驱动层取 zxl 源码原文（仅 u8 化）；进度条为重写但画面等价（§9） |

依赖说明：`Lcd12864.c` 引 `delay.h`（每帧命令/数据后的稳定等待）与 `wdg.h`（Init 分片喂狗，§10.5）；`Display.c` 引 `ProgressBar.h`（ProgressBar_Setup 装配进度条）；`ProgressBar.c` 引 `stdio.h/string.h`（sprintf/strlen）。`usart.h/24cxx.h` 不再被 LCD 层引用。

## 2. Lcd12864.h（全文）

```c
#ifndef __LCD12864_H
#define __LCD12864_H

/* LCD12864 (ST7920) 驱动层。
 * 职责边界：只管硬件访问——GPIO、串行时序、指令集、DDRAM/GDRAM 绘制、背光开关。
 * 不含：页面、参数编辑、按键分发、背光超时、进度条——那些属于上层
 * （Display.c 提示层 / ProgressBar.c 进度条）。 */
#include "sys.h"    /* u8、stm32f10x.h、位带宏 */

/* 液晶 12864 串行驱动 IO：屏逻辑为 5V，STM32 的 IO 必须选 5V 兼容（FT）引脚，
 * 不可挪到非 FT 引脚。 */
#define LCD_CS      PEout(3)   /* PE3 片选 */
#define LCD_SID     PEout(4)   /* PE4 数据线 */
#define LCD_SCLK    PEout(5)   /* PE5 时钟线 */
#define LCD_BL      PAout(0)   /* PA0 背光控制 */

/* 背光极性。 */
#define LCD_BL_ON   1u
#define LCD_BL_OFF  0u

/* 初始化：GPIO + 上电稳定期 1.5s（分片喂狗）+ 指令集/显示开 + 清两层 */
void LCD12864_Init(void);

/* 清文字层（DDRAM），图形层保留 */
void LCD12864_Clear(void);

/* 清图形层（GDRAM），文字层保留 */
void LCD12864_ClearGDRAM(void);

/* 文本行 row(0..3) 显示 ASCII 串：每地址 2 字符、落单补空格、超 16 截断、不预清行 */
void LCD12864_ShowString(u8 row, const char *str);

/* 文本行 row(0..3) 写原始 2 字节格：1 格 = 1 汉字 = 16×16 点，count 按格计、超 8 截断 */
void LCD12864_ShowHanzi(u8 row, const u8 *buf, u8 count);

/* GDRAM 像素矩形填充：y=首像素行 0..63，rows=块高，x_word=首字列 0..7，words=字列数 1..8，value=字节图案（0xFF 填满） */
void LCD12864_FillBlock(u8 y, u8 rows,
                        u8 x_word, u8 words, u8 value);

/* 背光开关，极性由 LCD_BL_ON/LCD_BL_OFF 宏定 */
void LCD12864_SetBacklight(u8 on);

#endif
```

## 3. Lcd12864.c（全文）

```c
/* ===========================================================================
 * LCD12864 (ST7920) 驱动层 - qibiji_bootloader
 * 换源说明：取自 qibiji_zxl 应用工程的重写版驱动（同硬件接线、同 ST7920 时序），
 * 仅做 u8 化；原 boot 版（51 时代遗留）不等上电稳定期、GDRAM 绘制会误关绘图层，
 * 本版均已修复。对照与裁剪见《LCD重构.md》§1.1。
 * 串行三线时序（LCD_CS/LCD_SID/LCD_SCLK）+ 两套指令集：
 * 0x30 基本指令集（文本层）、0x34/0x36 扩展指令集（绘图层）。
 * =========================================================================== */
#include "Lcd12864.h"
#include "delay.h"
#include "wdg.h"        /* WDG_Feed：上电稳定期分片喂狗。依赖《WDG重构.md》的 wdg.h/wdg.c
                           先落盘；该阶段尚未入库时，临时注释本行与 Init 内三处 WDG_Feed()，
                           并同步去掉验证 main 的 WDG_Init/s_WaitFeed（见 §7），见《LCD重构.md》§10.5 */

/* --------------------------- ST7920 常量 --------------------------- */
#define LCD_FRAME_CMD   0xF8u   /* 起始字节，RS = 0（命令） */
#define LCD_FRAME_DAT   0xFAu   /* 起始字节，RS = 1（数据） */

#define LCD_CMD_BASIC   0x30u   /* 基本指令集，RE = 0 */
#define LCD_CMD_EXT     0x34u   /* 扩展指令集，RE = 1，绘图关 */
#define LCD_CMD_GDRAM   0x36u   /* 扩展指令集，RE = 1，绘图开 */
#define LCD_CMD_ENTRY   0x06u   /* AC 自动加一；整屏不滚动 */
#define LCD_CMD_DISPON  0x0Cu   /* 显示开，光标关，闪烁关 */
#define LCD_CMD_CLEAR   0x01u   /* 清 DDRAM（慢指令） */
#define LCD_CMD_HOME    0x02u   /* 地址归位 */

#define LCD_ROWS          4u
#define LCD_ADDR_PER_ROW  8u    /* 每行 8 个 16 位字地址 = 16 个 ASCII 字符 */
#define LCD_GDRAM_ROWS   32u
#define LCD_GDRAM_WORDS  16u

/* 每行文本的 DDRAM 起始地址（ST7920 的 DDRAM 不线性） */
static const u8 s_row_addr[LCD_ROWS] = { 0x80u, 0x90u, 0x88u, 0x98u };

/* ---------------------- 底层串行时序 ------------------------- */
/* 把 1 个字节拆成 8 个 bit，最高位先出。
 * SCLK 拉低 → 把这一位放到 SID 上 → SCLK 拉高（屏在这一瞬锁存） */
static void s_SendByte(u8 dat)
{
    u8 i;
    for (i = 0; i < 8u; i++)
    {
        LCD_SCLK = 0;
        LCD_SID  = (u8)((dat & 0x80u) != 0u ? 1u : 0u);
        LCD_SCLK = 1;                       /* rising edge latches */
        dat = (u8)(dat << 1);               /* MSB first */
    }
}

/* 一个 ST7920 串行帧 = 起始字节 + 高半字节 + 低半字节。
 * 每个半字节占一个传输字节的高 4 位。GPIO 位绑发送，不是硬件 SPI。 */
static void s_Write(u8 dat, u8 is_data)
{
    LCD_CS = 0;
    LCD_CS = 1;         /* 边沿，表示一帧开始 */
    s_SendByte((u8)(is_data != 0u ? LCD_FRAME_DAT : LCD_FRAME_CMD));
    s_SendByte((u8)(dat & 0xF0u));            /* 高 4 位照搬，低 4 位填 0 */
    s_SendByte((u8)((u8)(dat << 4) & 0xF0u)); /* 低 4 位挪到高 4 位 */
    if (is_data != 0u)  /* 命令慢（尤其 0x01 清屏），数据快 */
        delay_us(20);
    else
        delay_us(100);
    LCD_CS = 0;         /* 一帧结束，取消片选 */
}

static void s_WriteCmd(u8 cmd)  { s_Write(cmd, 0u); }
static void s_WriteData(u8 dat) { s_Write(dat, 1u); }

/* ----------------------------- 对外接口 -------------------------------- */
void LCD12864_SetBacklight(u8 on)
{
    LCD_BL = (u8)(on != 0u ? LCD_BL_ON : LCD_BL_OFF);
}

void LCD12864_Clear(void)
{
    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_CLEAR);
    delay_ms(2);                 /* 0x01 需要额外执行时间 */
    s_WriteCmd(LCD_CMD_HOME);
}

/* GDRAM 全屏清空：32 行 × 16 字 × 2 字节 = 1024 字 = 8192 点，
 * 正好覆盖 128×64 整屏；只清图形层，文字层（DDRAM）不受影响 */
void LCD12864_ClearGDRAM(void)
{
    u8 row, word;

    s_WriteCmd(LCD_CMD_EXT);     /* 切到扩展指令集：RE = 1 */
    s_WriteCmd(LCD_CMD_GDRAM);   /* 打开绘图显示，进入 GDRAM 寻址 */
    for (row = 0; row < LCD_GDRAM_ROWS; row++)
    {
        s_WriteCmd((u8)(0x80u + row));  /* Y 行地址 */
        s_WriteCmd(0x80u);              /* X 地址 = 0 */
        for (word = 0; word < LCD_GDRAM_WORDS; word++)
        {
            s_WriteData(0x00u);
            s_WriteData(0x00u);         /* 每个字地址写 2 个全 0 字节 */
        }
    }
    /* 收尾：回基本指令集并且关闭绘图显示 */
    s_WriteCmd(LCD_CMD_EXT);     /* 0x34: RE = 1, graphic OFF */
    s_WriteCmd(LCD_CMD_BASIC);   /* 0x30: RE = 0, basic set */
}

/* 在一行上显示 ASCII 字符串。一个 DDRAM 地址放两个 ASCII 字符，
 * 所以每个地址写 2 字节；末尾落单的字符补空格。每行最多 8 个地址（16 字符）。 */
void LCD12864_ShowString(u8 row, const char *str)
{
    u8 addr;
    u8 used;
    const char *p;

    if ((row >= LCD_ROWS) || (str == (const char *)0))
        return;

    addr = s_row_addr[row];     /* 行号换算成 DDRAM 地址 */
    used = 0;
    p = str;
    s_WriteCmd(LCD_CMD_BASIC);
    while ((*p != '\0') && (used < LCD_ADDR_PER_ROW))
    {
        u8 hi, lo;
        hi = (u8)*p;
        p++;                    /* 取第 1 个字符 */
        if (*p != '\0')         /* 还有第 2 个？ */
        {
            lo = (u8)*p;
            p++;
        }
        else                    /* 没有 → 补一个空格 */
        {
            lo = (u8)' ';
        }
        s_WriteCmd(addr);
        s_WriteData(hi);
        s_WriteData(lo);
        addr++;
        used++;
    }
}

/* 在一行上写原始 2 字节格（GB2312 汉字码 / 自造字符字节）。
 * 一个 DDRAM 地址放两字节，count 按地址格计数（1..8）；字节原样写出、
 * 不做 ASCII 配对。Init 设的 AC 自动加一模式使每格第 2 字节落到同一地址的另一半。 */
void LCD12864_ShowHanzi(u8 row, const u8 *buf, u8 count)
{
    u8 addr;
    u8 i;

    /* 1 格 = 2 字节 = 1 个汉字 = 屏上 16×16 点。一行 8 格，超 8 截断。 */
    if ((row >= LCD_ROWS) || (buf == (const u8 *)0))
        return;
    if (count > LCD_ADDR_PER_ROW)
        count = LCD_ADDR_PER_ROW;

    addr = s_row_addr[row];
    s_WriteCmd(LCD_CMD_BASIC);
    for (i = 0u; i < count; i++)
    {
        s_WriteCmd(addr);       /* 每格重设地址 */
        s_WriteData(buf[i * 2u]);
        s_WriteData(buf[(i * 2u) + 1u]);
        addr++;
    }
}

/* 用常量字节填充一整块 16 像素宽的字列矩形。
 *   y      : 首个像素行，0..63
 *   rows   : 块高（像素行数）
 *   x_word : 半屏内的首个 16 像素字列，0..7
 *   words  : 字列数，1..8
 *   value  : 写入每个字的字节（0xFF=填满，0x00=清空）
 * 说明：ST7920 一个垂直地址管两行，h=0..7 → 像素行 v，h=8..15 → 像素行 v+32；
 *       本函数按像素行入参，内部自动加上半区偏移。 */
void LCD12864_FillBlock(u8 y, u8 rows,
                        u8 x_word, u8 words, u8 value)
{
    u8 j;
    u8 i;
    u8 py;
    u8 v;
    u8 h;

    if (rows == 0u || words == 0u || words > 8u || x_word > 7u)
        return;
    if ((u16)y + (u16)rows > 64u)
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);

    for (j = 0u; j < rows; j++)
    {
        py = (u8)(y + j);
        v  = (u8)(py & 0x1Fu);
        h  = (u8)(x_word + ((py >= 32u) ? 8u : 0u));

        s_WriteCmd((u8)(0x80u + v));
        s_WriteCmd((u8)(0x80u + h));

        for (i = 0u; i < words; i++)
        {
            s_WriteData(value);     /* 每字 16 像素宽 */
            s_WriteData(value);
        }
    }

    s_WriteCmd(LCD_CMD_BASIC);
}

/* 上电调一次。配 GPIO、等屏上电稳定、发初始化指令、清 DDRAM 和 GDRAM、开背光。
 * 原 boot 版把 GPIO 初始化 / 指令序列 / 清屏拆成三步由 main 串、且不等稳定期；
 * 本版一次到位（zxl 做法），正式 main 只调这一个函数。 */
void LCD12864_Init(void)
{
    /* GPIO 初始化 */
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;     /* PA0 = 背光 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    LCD_CS   = 0;
    LCD_SID  = 0;
    LCD_SCLK = 0;
    LCD12864_SetBacklight(0u);

    /* ST7920 上电后约需 1.5 s 才受理指令。
     * delay_ms() 在 72MHz 下约 1864ms 饱和，所以分三个 500ms 分片，
     * 每片之后喂一次看门狗（正式 main 开 IWDG 时必需；未开狗时无害）。 */
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();

    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_ENTRY);
    s_WriteCmd(LCD_CMD_DISPON);
    LCD12864_Clear();           /* 清 DDRAM（0x01 + 地址归位） */
    LCD12864_ClearGDRAM();      /* 清图形层 */
    LCD12864_SetBacklight(1u);  /* 背光点亮 */
}
```

## 4. Display.h（全文）

```c
#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "Lcd12864.h"

/* ---------------- OTA 状态页（文案为屏上汉字，码表在 Display.c） ----------------
 * 原工程序号名 ota_step1~7 已按画面内容更名，对照：
 *   step1→Ota_ShowConnecting  step2→Ota_ShowRequestVer  step3→Ota_ShowUpdating
 *   step4→Ota_ShowNoUpdate    step5→Ota_ShowErrorHint   step6→Ota_ShowDone
 *   step7→Ota_ClearText */
void Ota_ShowConnecting(void);  /* 四行清空 + 第1行「在线升级」+ 第2行「连接服务器中」 */
void Ota_ShowRequestVer(void);  /* 第2行「连接成功」+ 第3行「申请最新固件」 */
void Ota_ShowUpdating(void);    /* 第3行「开始更新」 */
void Ota_ShowNoUpdate(void);    /* 第3行「无新版本」 */
void Ota_ShowErrorHint(void);   /* 第3行「更新出错」（要显示错误类型请用 Ota_ShowError） */
void Ota_ShowDone(void);        /* 第3行「更新完毕」 */
void Ota_ClearText(void);       /* 四行全清（复位/跳转前调用） */

/* 统一错误画面：第1行标题 + 第2行清空 + 第3行「更新出错」 + 第4行错误类型 */
void Ota_ShowError(u8 kind);

/* ---------------- 进度条入口 / 联网提示 ---------------- */
void ProgressBar_Setup(void);   /* 清图形层 + 装配并画出进度条底框 + 显示 0%（原 progressbar_Init） */
void Lcd_ShowWaitNet(void);     /* 第1行显示 "wait net"（原 loading） */

/* ---------------- OTA 出错类型（GPRS / BOOT / 正式 main 共用） ----------------
 * 液晶第 4 行显示对应中文文案（实现见 Display.c 的 Ota_ShowError）；
 * 错误码定义归属维持原工程：就在 Display.h（REG 文档 §跨阶段已按此预告） */
#define OTA_ERR_NET         0   /* 网络连接超时：AT 无应答 / 等待上线超时 */
#define OTA_ERR_TIMEOUT     1   /* 请求超时：等平台回包超时重试超限 */
#define OTA_ERR_LINK        2   /* 连接中断：升级过程中掉线超限 */
#define OTA_ERR_DATA        3   /* 数据校验失败：CRC / 长度 / 序号 */
#define OTA_ERR_IMAGE       4   /* 固件包无效：A2 镜像头非法 / A1、A2 均无可用镜像 */
#define OTA_ERR_VERIFY      5   /* 写入校验失败：搬运 memcmp 不一致 / 自愈校验失败 */
#define OTA_ERR_SIZE        6   /* 固件大小错误：OTA_size 非法 */
#define OTA_ERR_EEPROM      7   /* 存储异常：24C02 自检失败（OTA 被关闭） */

#endif
```

## 5. Display.c（全文）

```c
/* ===========================================================================
 * LCD OTA 提示层 - qibiji_bootloader
 * 来源：qibiji_boot/HARDWARE/Lcd12864/Dispaly.c（升级画面只有 boot 有）。
 * 内容：OTA 状态页/错误页文案（GB2312 码表，屏自带汉字库不需字模）+ 状态页/
 * 错误页绘制 + 联网等待提示。
 * 不含：业务页面（主页/设置页/参数编辑）、背光超时、按键分发——应用层不迁移。
 * 底层全部经 zxl 换源驱动（LCD12864_ShowHanzi/ShowString/ClearGDRAM）落地，
 * 其中 ShowHanzi/ShowString 内部自带切回文本模式（0x30），不需手工发指令。
 * =========================================================================== */
#include "Display.h"
#include "ProgressBar.h"

/* ---------------- 文案码表（GB2312，每行 8 格 = 16 字节；只读，进 RO 段） ---------------- */

/* 在线升级 */
static const u8 s_txt_title[16] = {0x20,0x20,0x20,0x20,0xd4,0xda,0xcf,0xdf,0xc9,0xfd,0xbc,0xb6,0x20,0x20,0x20,0x20};
/* 连接服务器中（原 step1） */
static const u8 s_txt_connecting[16] = {0x20,0x20,0xc1,0xac,0xbd,0xd3,0xb7,0xfe,0xce,0xf1,0xc6,0xf7,0xd6,0xd0,0x20,0x20};
/* 连接成功（原 step2） */
static const u8 s_txt_connected[16] = {0x20,0x20,0x20,0x20,0xc1,0xac,0xbd,0xd3,0xb3,0xc9,0xb9,0xa6,0x20,0x20,0x20,0x20};
/* 申请最新固件（原 step3） */
static const u8 s_txt_request_ver[16] = {0x20,0x20,0xc9,0xea,0xc7,0xeb,0xd7,0xee,0xd0,0xc2,0xb9,0xcc,0xbc,0xfe,0x20,0x20};
/* 开始更新（原 step4） */
static const u8 s_txt_updating[16] = {0x20,0x20,0x20,0x20,0xbf,0xaa,0xca,0xbc,0xb8,0xfc,0xd0,0xc2,0x20,0x20,0x20,0x20};
/* 无新版本（原 step5） */
static const u8 s_txt_no_update[16] = {0x20,0x20,0x20,0x20,0xce,0xde,0xd0,0xc2,0xb0,0xe6,0xb1,0xbe,0x20,0x20,0x20,0x20};
/* 更新出错（原 step6） */
static const u8 s_txt_error[16] = {0x20,0x20,0x20,0x20,0xb8,0xfc,0xd0,0xc2,0xb3,0xf6,0xb4,0xed,0x20,0x20,0x20,0x20};
/* 更新完毕（原 step7） */
static const u8 s_txt_done[16] = {0x20,0x20,0x20,0x20,0xb8,0xfc,0xd0,0xc2,0xcd,0xea,0xb1,0xcf,0x20,0x20,0x20,0x20};
/* 空行：8 格全空格（原 empty） */
static const u8 s_txt_blank[16] = {0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20};

/* OTA 出错类型文案（液晶自带汉字库，直接发 GB2312 码不需点阵字模） */
static const u8 s_txt_err_net[16]     = {0xCD,0xF8,0xC2,0xE7,0xC1,0xAC,0xBD,0xD3,0xB3,0xAC,0xCA,0xB1,0x20,0x20,0x20,0x20}; /* 网络连接超时 */
static const u8 s_txt_err_timeout[16] = {0xC7,0xEB,0xC7,0xF3,0xB3,0xAC,0xCA,0xB1,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}; /* 请求超时 */
static const u8 s_txt_err_link[16]    = {0xC1,0xAC,0xBD,0xD3,0xD6,0xD0,0xB6,0xCF,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}; /* 连接中断 */
static const u8 s_txt_err_data[16]    = {0xCA,0xFD,0xBE,0xDD,0xD0,0xA3,0xD1,0xE9,0xCA,0xA7,0xB0,0xDC,0x20,0x20,0x20,0x20}; /* 数据校验失败 */
static const u8 s_txt_err_image[16]   = {0xB9,0xCC,0xBC,0xFE,0xB0,0xFC,0xCE,0xDE,0xD0,0xA7,0x20,0x20,0x20,0x20,0x20,0x20}; /* 固件包无效 */
static const u8 s_txt_err_verify[16]  = {0xD0,0xB4,0xC8,0xEB,0xD0,0xA3,0xD1,0xE9,0xCA,0xA7,0xB0,0xDC,0x20,0x20,0x20,0x20}; /* 写入校验失败 */
static const u8 s_txt_err_size[16]    = {0xB9,0xCC,0xBC,0xFE,0xB4,0xF3,0xD0,0xA1,0xB4,0xED,0xCE,0xF3,0x20,0x20,0x20,0x20}; /* 固件大小错误 */
static const u8 s_txt_err_eeprom[16]  = {0xB4,0xE6,0xB4,0xA2,0xD2,0xEC,0xB3,0xA3,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}; /* 存储异常 */

/* ---------------- 进度条场景装配（GPRS 开始下载时调用） ---------------- */
void ProgressBar_Setup(void)
{
    ProgressBarConfig_t config;

    LCD12864_ClearGDRAM();   /* 清空图形层 */

    /* 进度条装配：底行（与百分比文字同在文本第 4 行区域）、带边框、实心。
     * 几何取值 = 原工程实机渲染出来的画面，像素直述（推导见《LCD重构.md》§9） */
    config.row           = 3;       /* 百分比文字：文本第 4 行 */
    config.y_start       = 50;      /* 条起始像素行（底行区域） */
    config.x_word        = 1;       /* 起始字列（像素列 16 起） */
    config.width         = 6;       /* 宽 6 字 = 96 像素 */
    config.height        = 11;      /* 总高 11 像素行（含上下边框） */
    config.border_enable = 1;
    config.style         = PROGRESS_STYLE_SOLID;

    ProgressBar_Init(&config);
    ProgressBar_Update(0);
}

/* ---------------- OTA 状态页（原 ota_step1~7，按画面内容更名） ---------------- */

/* 四行清空 + 第1行「在线升级」+ 第2行「连接服务器中」 */
void Ota_ShowConnecting(void)
{
    LCD12864_ShowHanzi(0, s_txt_blank, 8);
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(0, s_txt_title, 8);        /* 第1行：在线升级 */
    LCD12864_ShowHanzi(1, s_txt_connecting, 8);   /* 第2行：连接服务器中 */
}

/* 第2行「连接成功」+ 第3行「申请最新固件」 */
void Ota_ShowRequestVer(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(1, s_txt_connected, 8);    /* 第2行：连接成功 */
    LCD12864_ShowHanzi(2, s_txt_request_ver, 8);  /* 第3行：申请最新固件 */
}

/* 第3行「开始更新」 */
void Ota_ShowUpdating(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_updating, 8);     /* 第3行：开始更新 */
}

/* 第3行「无新版本」 */
void Ota_ShowNoUpdate(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_no_update, 8);    /* 第3行：无新版本 */
}

/* 第3行「更新出错」（错误类型用 Ota_ShowError） */
void Ota_ShowErrorHint(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_error, 8);        /* 第3行：更新出错 */
}

/* 第3行「更新完毕」 */
void Ota_ShowDone(void)
{
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);

    LCD12864_ShowHanzi(2, s_txt_done, 8);         /* 第3行：更新完毕 */
}

/* 四行全清（复位/跳转前调用） */
void Ota_ClearText(void)
{
    LCD12864_ShowHanzi(0, s_txt_blank, 8);
    LCD12864_ShowHanzi(1, s_txt_blank, 8);
    LCD12864_ShowHanzi(2, s_txt_blank, 8);
    LCD12864_ShowHanzi(3, s_txt_blank, 8);
}

/*
 * OTA 错误提示：第1行「在线升级」、第2行清空、第3行「更新出错」（复用更新出错文案）、
 * 第4行错误类型。图形模式残留（清屏/进度条）由 ShowHanzi 内部的 0x30 自动切回，
 * 不需手工发指令。
 */
void Ota_ShowError(u8 kind)
{
    const u8 *line;

    switch (kind)
    {
        case OTA_ERR_NET:     line = s_txt_err_net;     break;
        case OTA_ERR_TIMEOUT: line = s_txt_err_timeout; break;
        case OTA_ERR_LINK:    line = s_txt_err_link;    break;
        case OTA_ERR_DATA:    line = s_txt_err_data;    break;
        case OTA_ERR_IMAGE:   line = s_txt_err_image;   break;
        case OTA_ERR_VERIFY:  line = s_txt_err_verify;  break;
        case OTA_ERR_SIZE:    line = s_txt_err_size;    break;
        case OTA_ERR_EEPROM:  line = s_txt_err_eeprom;  break;
        default:              line = s_txt_blank;       break;
    }

    LCD12864_ShowHanzi(0, s_txt_title, 8);          /* 第1行：在线升级 */
    LCD12864_ShowHanzi(1, s_txt_blank, 8);          /* 第2行：清空 */
    LCD12864_ShowHanzi(2, s_txt_error, 8);          /* 第3行：更新出错 */
    LCD12864_ShowHanzi(3, line, 8);                 /* 第4行：错误类型 */
}

/* ---------------- 联网等待提示 ---------------- */

/* 第 1 行显示 "wait net"（原 loading，按画面内容更名）。
 * 原画面：第 3 个字符位（第 2 格）起；用 4 个前导空格定位，画面不变。
 * 注意本函数不预清整行，需要干净画面时由调用方先清（OTA 页先铺 blank 行）。 */
void Lcd_ShowWaitNet(void)
{
    LCD12864_ShowString(0, "    wait net");
}
```

## 6. ProgressBar.h / ProgressBar.c（重写，全文）

```c
/**
 * @file ProgressBar.h
 * @brief LCD12864 进度条显示头文件
 *
 * GDRAM 画条（矩形块）+ DDRAM 百分比文字（第 4 字符位起）；实心样式 + 可选边框。
 * 全部经驱动层 zxl 原语（LCD12864_FillBlock / LCD12864_ShowHanzi）落地，
 * 几何参数按像素/字列直述。GPRS 下载进度与 OTA 状态画面共用。
 */

#ifndef __PROGRESSBAR_H
#define __PROGRESSBAR_H

#include "Lcd12864.h"   /* u8 类型经 sys.h 提供 */

/* 进度条配置结构（几何像素直述，替换原版「DDRAM 行地址表当 GDRAM 基址」的写法） */
typedef struct {
    u8 row;                /* 百分比文字所在文本行（0-3）                 */
    u8 y_start;            /* 条起始像素行（0-63，含上边框）              */
    u8 x_word;             /* 条起始字列（0-7，半屏内；1 字 = 16 像素宽） */
    u8 width;              /* 条宽（字数，建议 6~8）                      */
    u8 height;             /* 条总高（像素行，含上下边框）                */
    u8 border_enable;      /* 边框使能（0=禁用，1=启用）                  */
    u8 style;              /* 样式（见下方定义；本版仅 SOLID 生效）       */
} ProgressBarConfig_t;

/* 进度条样式定义 */
#define PROGRESS_STYLE_SOLID     0  /* 实心样式 */
#define PROGRESS_STYLE_GRADIENT  1  /* 渐变样式（预留，原工程亦未实现） */
#define PROGRESS_STYLE_SEGMENTED 2  /* 分段样式（预留，原工程亦未实现） */

/* 函数声明 */
void ProgressBar_Init(ProgressBarConfig_t *config);
void ProgressBar_Update(u8 percent);
void ProgressBar_DrawBorder(ProgressBarConfig_t *config);
void ProgressBar_ShowPercent(u8 row, u8 percent);
void ProgressBar_Clear(ProgressBarConfig_t *config);

/* 全局配置（可在外部修改；extern 全局保留 g_ 前缀，同 g_ota_lcd_* 先例） */
extern ProgressBarConfig_t g_progressbar_config;

#endif /* __PROGRESSBAR_H */
```

```c
/* ===========================================================================
 * LCD12864 进度条 - qibiji_bootloader（按 zxl 驱动原语重写）
 * 原 boot 版自己摸 GDRAM：X 基址把 DDRAM 行地址表当 GDRAM 基址混用、Clear 的
 * Y 地址不随循环递增、循环外用了未初始化的 i（三处怪异见《LCD重构.md》§9）。
 * 本版改用驱动层 LCD12864_FillBlock / LCD12864_ShowHanzi 原语，几何像素直述，
 * 屏上画面与原实机渲染一致（几何推导见《LCD重构.md》§9）。
 * =========================================================================== */
#include "ProgressBar.h"
#include <stdio.h>
#include <string.h>

/* 全局配置（ProgressBar_Init 装配） */
ProgressBarConfig_t g_progressbar_config;

/* 进度条状态 */
static u8 s_last_percent = 255;         /* 255 = 尚未画过 */
static u8 s_config_initialized = 0;

/**
 * @brief 默认配置（= OTA 装配几何）。
 *        原版默认值 y_start=0/height=15 从未被实际用到（progressbar_Init
 *        每次都传 config），本版默认值与实机画面一致。
 */
static void ProgressBar_InitDefaultConfig(void)
{
    g_progressbar_config.row           = 3;     /* 百分比：文本第 4 行      */
    g_progressbar_config.y_start       = 50;    /* 底行区域（像素行 50 起） */
    g_progressbar_config.x_word        = 1;     /* 像素列 16 起             */
    g_progressbar_config.width         = 6;     /* 宽 6 字 = 96 像素        */
    g_progressbar_config.height        = 11;    /* 含上下边框共 11 像素行   */
    g_progressbar_config.border_enable = 1;
    g_progressbar_config.style         = PROGRESS_STYLE_SOLID;
    s_config_initialized = 1;
}

/**
 * @brief 初始化进度条（清条区 + 画边框；NULL 则用默认配置）
 * @param config 进度条配置结构指针，NULL 则使用默认配置
 */
void ProgressBar_Init(ProgressBarConfig_t *config)
{
    if (!s_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }

    if (config == NULL) {
        config = &g_progressbar_config;
    } else {
        g_progressbar_config = *config;
    }

    ProgressBar_Clear(config);                   /* 条区 + 百分比行清空 */

    if (config->border_enable) {
        ProgressBar_DrawBorder(config);
    }

    s_last_percent = 255;                        /* 重置状态 */
}

/**
 * @brief 更新进度条显示
 * @param percent 百分比值（0-100）
 */
void ProgressBar_Update(u8 percent)
{
    ProgressBarConfig_t *config;
    u8 fill_words;

    if (!s_config_initialized) {
        ProgressBar_InitDefaultConfig();
    }
    config = &g_progressbar_config;

    if (percent > 100) {
        percent = 100;
    }

    /* 实心填充：行覆盖 y_start..y_start+height-2（底行像素留给下边框），
     * 字覆盖 x_word 起 fill_words 个字列；0% 时无事可做。
     * 填充值 0xFF 会盖住已填充字列的上边框与左右边框像素——与原实机行为一致。 */
    fill_words = (u8)((config->width * percent) / 100);
    if (fill_words > 0) {
        LCD12864_FillBlock(config->y_start, config->height - 1,
                           config->x_word, fill_words, 0xFF);
    }

    if (percent != s_last_percent) {             /* 百分比文字只在变化时重画 */
        ProgressBar_ShowPercent(config->row, percent);
        s_last_percent = percent;
    }
}

/**
 * @brief 绘制进度条边框（上下实线 + 左右单像素）
 * @param config 配置结构指针
 */
void ProgressBar_DrawBorder(ProgressBarConfig_t *config)
{
    /* 上/下边框：整宽 0xFF 实线；左/右边框：端字列内的单像素
     * （0x80 = 字内最左点，0x01 = 字内最右点）。
     * FillBlock 按像素行入参，自动处理 ST7920 上下半屏地址偏移。 */
    LCD12864_FillBlock(config->y_start, 1,
                       config->x_word, config->width, 0xFF);            /* 上边框 */
    LCD12864_FillBlock(config->y_start + config->height - 1, 1,
                       config->x_word, config->width, 0xFF);            /* 下边框 */
    LCD12864_FillBlock(config->y_start + 1, config->height - 2,
                       config->x_word, 1, 0x80);                        /* 左边框 */
    LCD12864_FillBlock(config->y_start + 1, config->height - 2,
                       config->x_word + config->width - 1, 1, 0x01);    /* 右边框 */
}

/**
 * @brief 在文本行 row 上显示百分比文字（第 4 字符位起，居中位置与原版一致）
 * @param row 文本行号（0-3）
 * @param percent 百分比值（0-100）
 */
void ProgressBar_ShowPercent(u8 row, u8 percent)
{
    char percent_str[5];    /* "100%" + '\0' */
    u8 line[16];
    u8 len;
    u8 i;

    if (percent > 100) {
        percent = 100;
    }
    if (row > 3) {
        row = 3;
    }

    /* 格式化百分比字符串（"0%"、"50%"、"100%"，2-4 字符） */
    sprintf(percent_str, "%d%%", percent);
    len = strlen(percent_str);
    if (len > 4) {
        len = 4;
    }

    /* 整行 16 字节重画：前 3 格空格 + 百分比（不足 4 字符右补空格）+ 尾 1 格空格。
     * 替代原版「清 4 字符位后只写 2 格」的绕行写法，画面相同。 */
    for (i = 0; i < 16; i++) {
        line[i] = 0x20;
    }
    for (i = 0; i < len; i++) {
        line[6 + i] = (u8)percent_str[i];
    }

    LCD12864_ShowHanzi(row, line, 8);    /* 8 格整行；函数内部自带切回文本模式 */
}

/**
 * @brief 清除进度条区域（条区 GDRAM + 百分比行）
 * @param config 配置结构指针
 */
void ProgressBar_Clear(ProgressBarConfig_t *config)
{
    u8 line[16];
    u8 i;

    /* 条区 GDRAM 清空（原版此函数 Y 地址不随循环递增、只反复清同一行，
     * 且 X 用的 0x80+x_start 与画条用的基址还不一致——重写版按几何全清） */
    LCD12864_FillBlock(config->y_start, config->height,
                       config->x_word, config->width, 0x00);

    for (i = 0; i < 16; i++) {
        line[i] = 0x20;
    }
    LCD12864_ShowHanzi(config->row, line, 8);    /* 百分比行清空 */
}
```

## 7. 验证 main（4G 联测 + LCD 四幕合并版，实机定稿）

**本 main 是 4G 联测与 LCD 四幕的合并版（2026-11-25 实机验证通过后的定稿形态）**：
开狗 + 4G 心跳打印 + 四幕循环演示，所有长延时分片喂狗，灯光心跳随喂狗走
（每喂一次翻一次灯 → 灯 ~1Hz 常闪，等待期间也能看出死活）。
覆盖迁移计划对本阶段的验证要求（字符串 / 状态页 / 进度条 / 错误页）；
真实验收场景就是阶段 10~12 的三个用面：`Lcd_ShowWaitNet()` 对应联网等待、
`ProgressBar_Update` 对应固件下载、`Ota_ShowError` 对应引导失败画面。

```c
/* USER/main.c —— 4G 联测 + LCD 四幕合并验证（实机定稿）
   开场 wdg test start（窗口 + 复位来源）→ 每轮 lcd round + 4G alive 心跳 →
   第一幕 字符串（Ota_ClearText 清行 + Lcd_ShowWaitNet + LCD12864_ShowString）
   第二幕 OTA 状态页（Ota_ShowConnecting → Ota_ShowRequestVer）
   第三幕 进度条（ProgressBar_Setup + 0→100 步进）
   第四幕 错误页（Ota_ShowError 固定展示两种错误码）
   喂狗策略：所有延时走 s_WaitFeed 分片，每片喂狗 + 翻灯（IWDG 最坏窗口 2.12s，
   见《WDG重构.md》）；循环顶部另留一处 0.5s + 喂狗用于停喂复位验证 */
#include "stm32f10x.h"
#include "sys.h"        /* NVIC_Configuration、位带宏 */
#include "delay.h"
#include "led.h"
#include "usart.h"
#include "stmflash.h"
#include "flash_layout.h"
#include "24cxx.h"
#include "reg.h"
#include "4G.h"         /* Gprs4G_Init、GPRS_LINK_STATUS、g_gprs_online */
#include "wdg.h"        /* WDG_Init/WDG_Feed、WDG_WINDOW_MIN_MS、g_wdg_reset_cause */
#include "Display.h"    /* Ota_* 状态页/错误页、Lcd_ShowWaitNet、OTA_ERR_* */
#include "ProgressBar.h"    /* ProgressBar_Setup/Update（不靠 Display.h 间接带入） */

static u32 blink_cnt = 0;     /* 心跳计数：每次喂狗翻灯 + 递增（4G alive 打印同步输出） */
static u32 s_loop_cnt = 0;    /* 演示轮次：主循环存活证据 + 花屏时的进度坐标 */

/* 分片等待：把长延时切成 ≤1s 的片，每片结束喂狗 + 翻灯。
 * 单段 delay_ms(3000) 必超 IWDG 最坏窗口（2.12s）被复位，长延时一律走本函数；
 * "等待即喂"是一条不变规则——改时长、加步骤都不用重新心算喂狗间隔。 */
static void s_WaitFeed(u16 ms)
{
    while (ms >= 1000u) { delay_ms(1000); WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; ms -= 1000u; }
    if (ms > 0u)        { delay_ms(ms);   WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; }
}

int main(void)
{
    u8 percent;            /* 进度条步进值 */

    delay_init();          /* SysTick 时基 */
    NVIC_Configuration();  /* 中断分组 2：2 位抢占 + 2 位响应 */
    uart1_init(115200);    /* 调试口 */
    uart2_init(115200);    /* 4G 口（回环 echo 用） */
    LED_Init();            /* 四路 LED 引脚初始化 */
    LED_CLOSE();           /* 全部熄灭，建立干净基准 */

    AT24CXX_Init();        /* I²C 引脚就绪（REG 阶段产物，随合并 main 保留） */
    WDG_Init();            /* 开 IWDG；内部记录并清除上一次复位来源 */
    printf("wdg test start, window=%ums, reset cause: %u\r\n",
           (unsigned int)WDG_WINDOW_MIN_MS, (unsigned int)g_wdg_reset_cause);

    Gprs4G_Init();         /* 电平/EXTI 配置 + PA1 复位脉冲：模块从这里点火 */
    LCD12864_Init();       /* 一步到位：GPIO + 1.5s 上电稳定期（内含 3 处 WDG_Feed）
                              + 指令序列 + 清 DDRAM/GDRAM + 背光点亮 */

    while (1)
    {
        s_loop_cnt++;
        printf("lcd round %u\r\n", (unsigned int)s_loop_cnt);

        delay_ms(500);         /* 半秒节拍：喂狗间隔 0.5s << 最坏窗口 2.12s */
        WDG_Feed();            /* 【验证复位时注释本行】停喂后约 3.2s（40kHz 标称）复位 */
        /* 灯光/计数心跳已移交 s_WaitFeed（每喂一次翻一次灯），此处不再翻转 */
        printf("4G alive, cnt=%u, LINK=%d, online=%u\r\n",
            (unsigned int)blink_cnt, (int)GPRS_LINK_STATUS, (unsigned int)g_gprs_online);

        /* 第一幕：字符串 */
        Ota_ClearText();                        /* 先清四行，去掉上一轮残留 */
        LCD12864_ClearGDRAM();
        Lcd_ShowWaitNet();                      /* 第1行：wait net（第3字符位起） */
        LCD12864_ShowString(1, "    LCD OK");   /* 第2行：LCD OK */
        printf("act1 string ok\r\n");
        s_WaitFeed(3000);

        /* 第二幕：OTA 状态页 */
        Ota_ShowConnecting();                   /* 在线升级 / 连接服务器中 */
        s_WaitFeed(3000);
        Ota_ShowRequestVer();                   /* 连接成功 / 申请最新固件 */
        s_WaitFeed(3000);

        /* 第三幕：进度条 0 -> 100，步进 5%（21 步 × 500ms，每步一喂） */
        ProgressBar_Setup();                    /* 清图形层 + 画底框 + 0% */
        for (percent = 0; percent <= 100; percent += 5)
        {
            ProgressBar_Update(percent);
            s_WaitFeed(500);
        }
        printf("act3 progress ok\r\n");
        s_WaitFeed(2000);

        /* 第四幕：错误页固定展示两种（SIZE=6 排在 VERIFY=5 之后，升序循环永远进不去，
           故显式调用；正式流程由 BOOT/GPRS 登记、main 显示具体码） */
        Ota_ShowError(OTA_ERR_SIZE);            /* 固件大小错误 */
        s_WaitFeed(3000);
        Ota_ShowError(OTA_ERR_VERIFY);          /* 写入校验失败 */
        s_WaitFeed(3000);
    }
}
```

**测试方法**：上电串口先出 `wdg test start, window=2123ms, reset cause: 0`
（约 2.3s 后——PA1 脉冲 0.5s + LCD 初始化 1.8s），随后每轮依次输出
`lcd round N` → `4G alive, cnt=…, LINK=…, online=…` → `act1 string ok` →
（约 20s 静默，act2 + act3 在走）→ `act3 progress ok` →（8s 静默，act4 两张错误页）→ 下一轮。
**每轮 cnt 恰好 +38**（3+6+21+2+6 次喂狗）——这不是巧合，是喂狗计数的核对值：
实际打出的增量对不上 38，说明某处延时没走 `s_WaitFeed`，先查裸 delay 再查别的。
屏上四幕依次轮播（第 3 幕画面在底行，见 §9.1）；任一幕花屏/错位，把串口
`lcd round N` 与屏上现象一起记录，用来区分「屏没画对」和「流程没跑到」。
灯（LED_ERR）应全程 ~1Hz 稳定闪烁——等待期间灯停住 = 程序卡死，一眼可辨。

**WDG 联测**（前提：wdg.h/wdg.c 已按《WDG重构.md》落盘入库）：

1. **正常跑**：连续数轮四幕无复位，串口 `reset cause: 0`（本次上电不是看门狗复位）；
2. **看狗咬人**：把循环顶部 `WDG_Feed()` 注释掉不管用（s_WaitFeed 还在喂）——
   把 `s_WaitFeed()` 里的 `WDG_Feed()` 也注释掉重新编译，约 2~3s 后板子复位、
   串口重新打印 `wdg test start` 且 `reset cause: 1`——两步合起来同时验证了
   `LCD12864_Init` 1.5s 稳定期的分片喂狗与复位来源记录。

## 8. Keil 配置要点（LCD）

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `Lcd12864.c`、`Display.c`、`ProgressBar.c` | 三个头文件同目录自动可寻；**注意别把旧名 `Dispaly.c` 再加进去**（文件已改名 `Display.c`） |
| Include Paths | 加 `..\HARDWARE\LCD` | main.c include "Display.h"/"ProgressBar.h" 需要 |
| WDG 依赖 | `Lcd12864.c` include `wdg.h` | 已闭环（《WDG重构.md》定稿且已落盘验证）；`main.c` 本身也 include `wdg.h/4G.h`，路径已在各自阶段配置 |
| FWLib 勾选 | 无新增 | 只用 GPIO（`stm32f10x_gpio.c` 已勾）；LCD 是纯 GPIO 位带操作，无 EXTI/DMA |
| stm32f10x_conf.h | 无新增 | 不引新固件库头 |
| 拷贝注意 | zxl 源文件名 `lcd12864.c/.h`（全小写）→ 落盘为 `Lcd12864.c/.h` | 与工程内其它模块命名风格一致（`Lcd12864` 驼峰） |

## 9. 进度条几何推导与怪异消除情况

### 9.1 原实机渲染几何的推导（重要背景）

原 boot 版进度条画条时 X 地址发的是 `s_ddram_row_addr[3] + x_start` = 0x88+1 = **0x89**。
ST7920 的 GDRAM 地址结构：X 地址 h=0..7 管上半屏（像素行 0~31），h=8..15 管下半屏（像素行 32~63）。
h=9 → **下半屏字列 1**（像素列 16 起）；Y 地址发 0x80+18..28 → **像素行 50~60**。
所以原实机进度条画面在**屏幕底行**（像素行 50~60、像素列 16~111）——恰与百分比文字所在的
文本第 4 行（像素行 48~63）重合，这也是「实机显示正常」的原因。原代码注释写的「第三行区域 /
GDRAM Y 坐标 32」与实际渲染不符。

重写版**以实机渲染几何为准**、像素直述：`y_start=50、height=11、x_word=1、width=6`——
`FillBlock` 按像素行入参，py≥32 时自动加 h+8 偏移，发出的地址恰好还原成原版的
(Y=18..28, X=9..14)，屏上画面不变。

### 9.2 原版怪异 → 消除情况（全部随换源/重写消除）

| 原怪异 | 原行为 | 现状态 |
|---|---|---|
| 进度条 X 基址混用 | 画条/边框用 `s_ddram_row_addr[row]+x_start`（DDRAM 地址表当 GDRAM 基址），Clear 却用 `0x80+x_start`，两处不一致 | 配置字段改像素直述（y_start/x_word），画与清走同一个几何 |
| `ProgressBar_Clear` 的 Y 地址不随循环递增 | 只反复清同一行 GDRAM | `FillBlock` 整块清除 |
| `ProgressBar_Clear` 循环外用未初始化的 `i` | UB，多发一条随机地址命令 | 整行重画写法，无此问题 |
| 百分比文字清 4 字符位只写 2 格 | 逻辑绕但等效 | `ShowHanzi` 整行 16 字节重画 |
| 背光极性注释矛盾（`LCD_BL=1` 注释写「关闭背光」） | 赋值与注释打架 | zxl 版 `LCD_BL_ON/OFF` 极性常量，注释一致 |
| 原 boot main 不等屏上电 1.5s 稳定期 | 靠运气（实测能跑） | `LCD12864_Init` 内置 1.5s 分片等待 + 喂狗 |

### 9.3 残留事项（登记备查）

1. `style` 渐变/分段两个字段为预留，本版未实现（原工程同样只有 SOLID 生效）；
2. 填充值 0xFF 会盖住已填充字列的上边框/左右边框像素——与原实机行为一致（100% 时边框融入实心条），不是缺陷；
3. 背光实测：上电后若背光不亮，说明该板极性与 zxl 定义相反——届时先量 PA0 电平与背光亮度对应关系再定，**不要直接翻赋值**；
4. `LCD12864_ShowString/ShowHanzi` 不预清整行，残留字符由调用方负责——OTA 页均先铺 blank 行，验证 main 第一幕先调 `Ota_ClearText()`，行为可控。

## 10. 跨阶段边界（LCD）

1. **GPRS（阶段 10）**：`gprs_rx.c` 在开始下载时调 `ProgressBar_Setup()`、每包落位后调
   `ProgressBar_Update()` 刷新进度（原工程 gprs_rx.c:416/573 调的是 `progressbar_Init()`/
   `ProgressBar_Update()`——前者本阶段更名 `ProgressBar_Setup`）——include `ProgressBar.h` 即得；
   GPRS 的错误返回码 `OTA_ERR_DATA` 等经 `Display.h` 获得（REG 文档 §跨阶段已预告此归属）；
2. **BOOT（阶段 11）**：`boot.c` include `Display.h` 拿 OTA_ERR_*；`g_ota_lcd_err/hold` 两个
   登记变量的定义**仍在 boot.c**（属主不变，REG 文档已登记）；`Boot_Clear()` 跳 A1 前的
   LCD 清屏用 `LCD12864_Clear()`（原工程写作 `WRCommandH(0x01)`，换源对照见 §1.1，
   BOOT 文档两处已同步）；
3. **正式 main（阶段 12）**：上电 LCD 部分一步 = `LCD12864_Init()`（原工程
   `Lcd_Gpio_Init` → 不等稳定期 → `Lcd_ClearGDRAM` 的三步序列由此等价覆盖，且补上了稳定期）；
   之后补显示 `g_ota_lcd_err`（BOOT 登记的引导期错误）→ OTA 状态机内的状态页与 `Ota_ShowError`
   挂钩——原工程调用点对照：`ota_step1→Ota_ShowConnecting`、`ota_step2→Ota_ShowRequestVer`、
   `ota_step3→Ota_ShowUpdating`、`ota_step4→Ota_ShowNoUpdate`、`ota_step5→Ota_ShowErrorHint`、
   `ota_step6→Ota_ShowDone`、`ota_step7→Ota_ClearText`、`ota_show_error→Ota_ShowError`、
   `progressbar_Init→ProgressBar_Setup`；与原工程 main.c 逐处对应；
4. **跳转 A1 无需反初始化**：LCD 无中断、无 DMA，跳 A1 前不用关任何东西；背光亮着交给 A1
   应用处置（原工程同此行为）；
5. **WDG 依赖（已闭环）**：`Lcd12864.c` include `wdg.h`，`LCD12864_Init` 内 3 处
   `WDG_Feed()`（1.5s 稳定期分三片、每片一喂）——WDG 阶段文档《WDG重构.md》已定稿
   （zxl 换源版，含复位来源记录），wdg.h 落盘后本条依赖自然生效，此前的「临时注释」策略作废；
   **正式 main 开狗后这 1.5s 期间的喂狗是必需的，不可省**；LCD 验证 main 也已开狗联测
   （`WDG_Init` + `s_WaitFeed` 分片喂狗，含复位来源打印，见 §7），不再是「不开狗跑 LCD」的形态；
6. **依赖闭环**：本模块只依赖已入库的 `sys.h/delay.h`（阶段 1/2 产物）+ `wdg.h`（后续阶段，
   见第 5 条），不依赖 REG/USART/4G，迁移顺序表「Lcd12864 无依赖」成立。

---

本文档只生成/修订 md，不改任何 `.c/.h` 工程文件；`Lcd12864.h/.c、Display.h/.c、ProgressBar.h/.c、main.c`
均由你按本文档落盘，落盘前提：4G 联测已验证通过（main.c 按 §7 的合并版落盘——4G 联测代码
与四幕验证共存于同一循环，实机定稿形态）。
