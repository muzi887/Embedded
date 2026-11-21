# LCD 重构操作建议_业务层2（第 3 轮：参数编辑 + 行缓存增量刷新）

> 本文只给出目标代码和实施步骤，**没有改动任何源码**。跟着顺序一个文件一个文件改。
> 目标代码已用 C90（`-std=c90 -Wall -Wextra`）编译通过（0 error / 0 warning），并跑了 59 项行为断言，0 失败，见第 11 节。
> 前提：驱动层（《LCD重构操作建议_驱动层》）和业务层第 1 轮（《LCD重构操作建议_业务层1》）已上板验证成功。
> 步及源码：`qibiji_zxl/HARDWARE/LCD12864/`、`qibiji/HARDWARE/Lcd12864/`。

---

## 1. 结论与范围

### 1.1 这轮做什么

- **参数编辑**：设置页 3 个可编辑项（上限位 / 堵转电流阈值 / 校准开关）。`KEY_UP` / `KEY_DOWN` 移动光标，`KEY_STOP` 进入编辑，编辑中 `KEY_UP` / `KEY_DOWN` 改值、`KEY_STOP` 确认、`KEY_CANCEL` 取消恢复原值。第 1 轮预留的 UP/DOWN case 本轮启用。
- **行缓存（增量刷新）**：设置页 3 行在 RAM 里各存一份 16 字节副本。光标移动、改值只把**受影响的行**重新发给屏（每行 25 帧），不再整页 Clear + 重画。全页 Clear 只在进入设置页那一刻出现一次。
- **对外参数 API**：`LCD_PAGES_GetParam()` / `LCD_PAGES_SetParam()`，值存 RAM。以后 EEPROM、4G 下发参数、ADC 校正都从这两个口进出。

### 1.2 这轮不做什么

- **EEPROM 持久化**：编辑结果只存 RAM，断电丢失。原工程编辑完调 `AT24CXX_WriteWord`（`Dispaly.c:531,550`），等 EEPROM 轮再接，接口位置已留好（`LCD_PAGES_SetParam` 内部一行的事）。
- **首页动态数据**：ADC 没接，首页 4 行仍是静态文字。原工程 `LcdDisplay()`（`Dispaly.c:231-343`）那套「按页面轮询数据变化」要到数据源存在才有意义。
- **GDRAM 反选光标**：原工程用 `WRGDRAM128X8` 做行反选（`Dispaly.c:400-402`），属绘图功能，第 4 轮做。本轮光标用**行首 "->" 文本标记**，纯 DDRAM 字节，不碰 GDRAM。

### 1.3 原工程的对应机制（照搬的依据）

- `DisplayCache`（`Dispaly.c:125-144`）：一组 `xxx_last` 影子变量，数据变了才置 `need_refresh`。本轮把这个思想降到**行粒度**：整行 16 字节缓存在 RAM，字节变了才发那一行。
- `setpage()` 参数编辑（`Dispaly.c:499-575`）：原工程是**逐字符**编辑（`Num` 在 `0x30~0x39` 间步进，`KEY_ENTER` 逐位确认，`cnt` 走到 10 位才算完），因为数值是显示数组里的 ASCII 字符。本轮简化为**整值步进**（UP 加一步、DOWN 减一步），理由见第 6 节。
- `ViewPages()` 的 KEY_ENTER 特例（`Dispaly.c:371-377`）：`if (ViewNum != 1)` 才回首页 —— **在设置页按 ENTER 不回首页**，因为 ENTER 在设置页是「确认编辑」。本轮 `KEY_STOP`（旧名 ENTER）在设置页 = 进入编辑，与原工程同源；从设置页回首页改按 `KEY_SET`（上一页环绕）。

---

## 2. 改动文件清单与落地顺序

| 堆 | 文件 | 动作 | 内容 |
|---|---|---|---|
| 改 | `HARDWARE/LCD12864/lcd_pages.h` | **整文件替换** | 增加 `lcd_param_t` 枚举、2 个参数 API 声明，以及 `LCD_PAGES_Refresh()` 声明 |
| 改 | `HARDWARE/LCD12864/lcd_pages.c` | **整文件替换** | 行缓存 + 编辑状态机 + 参数 API |
| 改 | `USER/main.c` | 主循环加 1 行 | 调 `LCD_PAGES_Refresh()`（在校准标志消费之后、`DCMOTOR_Process()` 之前），让校准状态行无按键也能刷新；`LCD_PAGES_Process(key)` 仍在 `APP_DispatchKey` 里调，不动 |
| 不动 | `USER/SRTUV10.uvprojx` | 不改 | 没有新文件，`dcmotor.h` 已在 IncludePath |

落地顺序：替换 `.h` → 替换 `.c` → `main.c` 加 `LCD_PAGES_Refresh();` → Keil 里 Build。共 3 个文件（`main.c` 只加 1 行）。

---

## 3. lcd_pages.h（整文件替换）

```c
#ifndef __LCD_PAGES_H
#define __LCD_PAGES_H

/* LCD pages module (business layer, round 3).
 * Owns: page state machine + the GB2312 text rows drawn on each page,
 *       plus set-page parameter editing (cursor + value edit) and a
 *       per-row RAM cache so only changed rows are re-sent to the LCD.
 * Reacts to key events; plus a per-loop Refresh() for rows that change
 * without a key press (e.g. the calibration status row).
 * Depends on: key.h (key_code_t) and lcd12864.h (driver layer only). */
#include "key.h"

void LCD_PAGES_Init(void);
void LCD_PAGES_Process(key_code_t key);
void LCD_PAGES_Refresh(void);   /* call each main loop: redraws rows whose
                                   content changes without a key press
                                   (e.g. the calibration status row) */

/* Editable parameters, kept in RAM. EEPROM persistence is NOT in this
 * round - the module only guarantees the in-RAM value. */
typedef enum
{
    LCD_PARAM_GATE_MAX = 0,   /* up-limit position, 4-digit decimal     */
    LCD_PARAM_STALL_010A,     /* stall threshold in 0.1 A, 0..999      */
    LCD_PARAM_AUTO_FLAG,      /* calibration auto switch, 0 = off      */
    LCD_PARAM_COUNT
} lcd_param_t;

uint16_t LCD_PAGES_GetParam(lcd_param_t p);
void     LCD_PAGES_SetParam(lcd_param_t p, uint16_t v);

#endif
```

注意：`uint16_t` 来自 `key.h` → `sys.h` 这条包含链（和你现有的 key 模块一致），`.h` 自己不用再包含别的。

---

## 4. lcd_pages.c（整文件替换）

> 2026-11-21 同步：代码块已更新为**表驱动版本**。三项参数的标签、`*` 列、上下限、步进、显示格式合并为 `s_items[]` 一张表，`s_param_max[]` / `s_param_step[]` / `s_star_pos[]` 三组并行数组已删除；钳位收进 `s_StepValue()`，光标移动收进 `s_SetMoveCursor()`。行为与下表规格完全一致。

```c
/* ===========================================================================
 * LCD pages module - qibiji_zxl (business layer)
 *
 * Owns: page state machine (which page is on screen) and the GB2312 text
 *        rows drawn on each page; set-page parameter editing (browse with
 *        a cursor marker + per-item value edit); a per-row RAM cache so
 *        cursor moves and edits re-send only the affected rows instead of
 *        repainting the whole page.
 * Depends on: key.h (key_code_t) and lcd12864.h (driver layer only).
 * =========================================================================== */
#include "lcd_pages.h"
#include "lcd12864.h"
#include "dcmotor.h"

/* ----------------------------- Page ids --------------------------------- */
typedef enum
{
    PAGE_HOME = 0,   /* 控制: motor / opening / flow                    */
    PAGE_SET,        /* 配置: limits / stall / calibration              */
    PAGE_G4G,        /* 4G:   signal / CCID / link state                */
    PAGE_DEV,        /* 设备: device id / version / upgrade             */
    PAGE_OP,         /* 限位: open limit / close limit highlight        */
    PAGE_COUNT       /* number of pages, used for wrap-around           */
} page_id_t;

static page_id_t s_page = PAGE_HOME;

/* ---------------- GB2312 text rows  ----------------
 * Each row = 8 DDRAM address cells x 2 bytes = 16 bytes on screen.         */
static const uint8_t s_home_l2[16] = {         /* "motor: 1.6A 12.6V"       */
    0xb5, 0xe7, 0xbb, 0xfa, 0x3a, 0x20,
    0x31, 0x2e, 0x36, 0x41, 0x20, 0x31, 0x32, 0x2e, 0x36, 0x56 };
static const uint8_t s_home_l3[16] = {         /* "opening: 100% quan kai"  */
    0xbf, 0xaa, 0xb6, 0xc8, 0x3a, 0x20,
    0x31, 0x30, 0x30, 0x25, 0x20, 0x20, 0xc8, 0xab, 0xbf, 0xaa };
static const uint8_t s_home_l4[16] = {         /* "flow: 0.0 m3/s"          */
    0xc1, 0xf7, 0xc1, 0xbf, 0x3a, 0x20,
    0x20, 0x30, 0x2e, 0x30, 0x20, 0x6d, 0x33, 0x2f, 0x73, 0x20 };

/* ------------------------- Editable parameters ---------------------------
 * RAM */
static uint16_t s_param[LCD_PARAM_COUNT] = { 2000u, 300u, 0u };

/* forward declarations - C90 needs them before first use */
static void s_FormatSetRow(uint8_t item);
static void s_FlushSetRow(uint8_t item);

/* ------------------------- Set page row cache ----------------------------
 * The 3 set-page rows live in RAM. Painters format into the cache; only
 * rows whose bytes actually change are flushed to the LCD.   */
#define SET_ITEMS 3u        /* 设置页有 3 个可编辑项（上限位 / 堵转 / 校准） */
typedef enum { SET_BROWSE = 0, SET_EDIT } set_mode_t;  /* 设置页的两态 */
/* 浏览态：光标上下移，参数不动。UP/DOWN 移光标，STOP 回首页。
编辑态​：光标锁定不动，UP/DOWN  改当前项的值，STOP 确认保存，CAN 取消退回。 */

static set_mode_t s_set_mode = SET_BROWSE;
static uint8_t    s_cursor   = 0;      /* selected item, 0..2             */
static uint16_t   s_edit_backup = 0;   /* value before edit, for cancel   */
static uint8_t    s_set_rows[SET_ITEMS][16];    /* 设置页的影子缓存 */

/* GB2312 fragments used to build the rows */
static const uint8_t s_lbl_gate[7] = {       /* up-limit label + colon   */
    0xc9, 0xcf, 0xcf, 0xde, 0xce, 0xbb, 0x3a };
static const uint8_t s_lbl_stall[6] = {      /* stall label + colon+space */
    0xb6, 0xc2, 0xd7, 0xaa, 0x3a, 0x20 };
static const uint8_t s_lbl_auto[6] = {       /* calibration label + colon */
    0xd0, 0xa3, 0xd7, 0xbc, 0x3a, 0x20 };
/* calibration status text reflects the real motor state, not the trigger flag */
static const uint8_t s_txt_cal_none[6] = {   /* not calibrated  */
    0xce, 0xb4, 0xd0, 0xa3, 0xd7, 0xbc };
static const uint8_t s_txt_cal_run[6]  = {   /* calibrating  */
    0xd0, 0xa3, 0xd7, 0xbc, 0xd6, 0xd0 };
static const uint8_t s_txt_cal_ok[6]   = {   /* calibrated   */
    0xd2, 0xd1, 0xd0, 0xa3, 0xd7, 0xbc };

/* --------------------- Set page item table (table-driven) ----------------
 * 一项参数的全部差异集中在一行里：标签、'*' 列、上下限、步进、显示格式。
 * 数组下标与 lcd_param_t 一一对应（0=上限位, 1=堵转, 2=校准）。
 * 新增一项参数时：改这里 + lcd_param_t + s_param 初值，不再散落三处。  */
typedef enum
{
    FMT_DEC4 = 0,   /* 4 位十进制整数，右对齐到第 9 字节 */
    FMT_STALL,      /* "dd.d A"：0.1A 定点 */
    FMT_TOGGLE      /* 开/关，UP=1 DOWN=0 */
} set_item_fmt_t;

typedef struct
{
    const uint8_t *label;       /* GB2312 标签（含冒号） */
    uint8_t        label_len;   /* 标签字节数 */
    uint8_t        star_pos;    /* 编辑态 '*' 所在字节列 */
    uint16_t       max;         /* UP 钳位上限 */
    uint16_t       step;        /* 每次增减量 */
    uint8_t        fmt;         /* set_item_fmt_t */
} set_item_t;

static const set_item_t s_items[SET_ITEMS] =
{
    { s_lbl_gate,  7u, 13u, 9999u, 10u, FMT_DEC4   },
    { s_lbl_stall, 6u, 14u,  999u,  1u, FMT_STALL  },
    { s_lbl_auto,  6u, 14u,    1u,  1u, FMT_TOGGLE },
};

uint16_t LCD_PAGES_GetParam(lcd_param_t p)
{
    if ((int)p < 0 || p >= LCD_PARAM_COUNT)
        return 0u;
    return s_param[p];
}

void LCD_PAGES_SetParam(lcd_param_t p, uint16_t v)
{
    if ((int)p < 0 || p >= LCD_PARAM_COUNT)
        return;
    if (v > s_items[p].max)
        v = s_items[p].max;
    s_param[p] = v;
    if (s_page == PAGE_SET)
    {
        s_FormatSetRow((uint8_t)p);
        s_FlushSetRow((uint8_t)p);
    }
}

/* ---- 4G page ---- */
static const uint8_t s_lbl_sig[6]   = { 0xd0, 0xc5, 0xba, 0xc5, 0x3a, 0x20 }; /* 信号： */
static const uint8_t s_lbl_ccid[6]  = { 'C', 'C', 'I', 'D', ':', ' ' };
static const uint8_t s_lbl_stat[6]  = { 0xd7, 0xb4, 0xcc, 0xac, 0x3a, 0x20 }; /* 状态： */
static const uint8_t s_txt_online[4]  = { 0xd4, 0xda, 0xcf, 0xdf };           /* 在线   */
static const uint8_t s_txt_offline[4] = { 0xc0, 0xeb, 0xcf, 0xdf };           /* 离线   */

/* ---- device page ---- */
static const uint8_t s_lbl_num[6] = { 0xb1, 0xe0, 0xba, 0xc5, 0x3a, 0x20 }; /* 编号： */
static const uint8_t s_lbl_ver[6] = { 0xb0, 0xe6, 0xb1, 0xbe, 0x3a, 0x20 }; /* 版本： */
static const uint8_t s_lbl_upg[6] = { 0xc9, 0xfd, 0xbc, 0xb6, 0x3a, 0x20 }; /* 升级： */
static const uint8_t s_txt_no_ota[6] = { 0xce, 0xb4, 0xc6, 0xf4, 0xd3, 0xc3 }; /* 未启用 */

/* ---- limit page ---- */
static const uint8_t s_op_pair[16] = {      /* "上限位  下限位" */
    0xc9, 0xcf, 0xcf, 0xde, 0xce, 0xbb,
    0x20, 0x20, 0x20, 0x20,
    0xcf, 0xc2, 0xcf, 0xde, 0xce, 0xbb };

/* ------------------- Top graphic zone (GDRAM) assets ---------------------
 * Banner glyph bitmaps: Each glyph is 12 rows x 2 bytes, drawn into GDRAM
 * rows y=1..12 at one X word column via LCD12864_DrawStrip().           */
/* 控制 (code[0], code[1]) */
static const uint8_t s_banner_ctl0[24] = {
    0x42, 0x00, 0x41, 0x00, 0x4f, 0xe0, 0xe8, 0x20,
    0x42, 0x80, 0x44, 0x40, 0x68, 0x20, 0xc7, 0xc0,
    0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0xcf, 0xe0 };
static const uint8_t s_banner_ctl1[24] = {
    0x10, 0x20, 0x50, 0x20, 0x7d, 0x20, 0x91, 0x20,
    0xff, 0x20, 0x11, 0x20, 0x7d, 0x20, 0x55, 0x20,
    0x55, 0x20, 0x54, 0x20, 0x5c, 0x20, 0x10, 0xe0 };

/* 配置 (code[2], code[3]) */
static const uint8_t s_banner_cfg0[24] = {
    0x00, 0x00, 0xfe, 0xe0, 0x28, 0x20, 0xfe, 0x20,
    0xaa, 0x20, 0xaa, 0xe0, 0xc6, 0x80, 0x82, 0x80,
    0xfe, 0x80, 0x82, 0xa0, 0xfe, 0xa0, 0x82, 0x60 };
static const uint8_t s_banner_cfg1[24] = {
    0x7f, 0xc0, 0x4a, 0x40, 0x7f, 0xc0, 0x04, 0x00,
    0xff, 0xe0, 0x04, 0x00, 0x3f, 0x80, 0x20, 0x80,
    0x20, 0x80, 0x24, 0x80, 0x24, 0x80, 0xff, 0xe0 };

/* 空白 (code[23]) */
static const uint8_t s_banner_blank[24] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* 网络 (code[11], code[12]) */
static const uint8_t s_banner_net0[24] = {
    0xff, 0xe0, 0x80, 0x20, 0x80, 0x20, 0xc4, 0x60,
    0xaa, 0xa0, 0x91, 0x20, 0x91, 0x20, 0xaa, 0xa0,
    0xc4, 0x60, 0x80, 0x20, 0x80, 0x20, 0x80, 0xe0 };
static const uint8_t s_banner_net1[24] = {
    0x22, 0x00, 0x23, 0xc0, 0x44, 0x40, 0x9a, 0x80,
    0xe1, 0x00, 0x22, 0x80, 0x4c, 0x60, 0xf7, 0xc0,
    0x04, 0x40, 0x34, 0x40, 0xc7, 0xc0, 0x04, 0x40 };

/* 限位 (code[17], code[18]) */
static const uint8_t s_banner_lim0[24] = {
    0xf7, 0xc0, 0x94, 0x40, 0x94, 0x40, 0xa7, 0xc0,
    0xa4, 0x40, 0x94, 0x40, 0x97, 0xc0, 0x95, 0x20,
    0x95, 0x40, 0xe4, 0x80, 0x86, 0x40, 0x84, 0x20 };
static const uint8_t s_banner_lim1[24] = {
    0x22, 0x00, 0x21, 0x00, 0x40, 0x00, 0x5f, 0xe0,
    0xc0, 0x00, 0x48, 0x40, 0x48, 0x40, 0x44, 0x40,
    0x44, 0x80, 0x44, 0x80, 0x41, 0x00, 0x5f, 0xe0 };

/* OTA  (code[19]="OT", code[20]="A ") */
static const uint8_t s_banner_ota0[24] = {
    0x00, 0x00, 0x00, 0x00, 0x38, 0x7c, 0x44, 0x10,
    0x44, 0x10, 0x44, 0x10, 0x44, 0x10, 0x44, 0x10,
    0x44, 0x10, 0x44, 0x10, 0x38, 0x10, 0x00, 0x00 };
static const uint8_t s_banner_ota1[24] = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00,
    0x28, 0x00, 0x28, 0x00, 0x28, 0x00, 0x44, 0x00,
    0x7c, 0x00, 0x44, 0x00, 0x44, 0x00, 0x00, 0x00 };

/* separator line under the top zone: GDRAM y=15, all 128 pixels lit */
static const uint8_t s_line_full[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void s_RowClear(uint8_t *row)
{
    uint8_t i;
    for (i = 0u; i < 16u; i++)
        row[i] = 0x20u;
}

/* 整数 → 4 个字符 */
static void s_U16ToDec4(uint16_t v, uint8_t *dst)
{
    dst[0] = (uint8_t)('0' + ((v / 1000u) % 10u));
    dst[1] = (uint8_t)('0' + ((v / 100u) % 10u));
    dst[2] = (uint8_t)('0' + ((v / 10u) % 10u));
    dst[3] = (uint8_t)('0' + (v % 10u));
}

static void s_BytesCopy(uint8_t *dst, const uint8_t *src, uint8_t n)
{
    uint8_t i;
    for (i = 0u; i < n; i++)
        dst[i] = src[i];
}

/* Format one set-page row into the RAM cache (no LCD traffic here).
格子:  0  1  | 2...........7 | 8..........14 | 15
内容:  光标 | 标签(汉字/冒号) | 数值或开/关    | 空
标签、'*' 列、数值格式全部取自 s_items[item]。                           */
static void s_FormatSetRow(uint8_t item)
{
    const set_item_t *it = &s_items[item];
    uint8_t *row = s_set_rows[item];
    uint16_t v;

    s_RowClear(row);
    if (s_cursor == item)
    {
        row[0] = '-';
        row[1] = '>';
    }
    s_BytesCopy(&row[2], it->label, it->label_len);

    switch (it->fmt)
    {
    case FMT_DEC4:                            /* 上限位：4 位整数 */
        s_U16ToDec4(s_param[item], &row[9]);
        break;
    case FMT_STALL:                           /* 堵转：dd.d A */
        v = s_param[item];
        row[8]  = (uint8_t)('0' + ((v / 100u) % 10u));
        row[9]  = (uint8_t)('0' + ((v / 10u) % 10u));
        row[10] = '.';
        row[11] = (uint8_t)('0' + (v % 10u));
        row[12] = ' ';
        row[13] = 'A';
        break;
    case FMT_TOGGLE:                          /* 校准：状态文字 */
    default:
        if (DCMOTOR_IsCalibrating())
            s_BytesCopy(&row[8], s_txt_cal_run, 6u);   /* 校准中 */
        else if (DCMOTOR_IsCalibrated())
            s_BytesCopy(&row[8], s_txt_cal_ok, 6u);    /* 已校准 */
        else
            s_BytesCopy(&row[8], s_txt_cal_none, 6u);  /* 未校准 */
        break;
    }

    if ((s_set_mode == SET_EDIT) && (s_cursor == item))
        row[it->star_pos] = '*'; /* 进编辑态时，正在编辑的那行加个 *。 */
}

/* 按 s_items 的 max/step 算下一个值；开/关项直接置 0/1 */
static uint16_t s_StepValue(uint8_t item, uint8_t up)
{
    const set_item_t *it = &s_items[item];
    uint16_t v = s_param[item];

    if (it->fmt == FMT_TOGGLE)
        return up ? 1u : 0u;

    if (up)
        return (v > (uint16_t)(it->max - it->step))
                   ? it->max
                   : (uint16_t)(v + it->step);
    return (v <= it->step) ? 0u : (uint16_t)(v - it->step);
}

/* Light pixels start..end of one 128-pixel GDRAM row, MSB = leftmost. 
5 段进度条  */
static void s_ProgressDraw(uint8_t curr, uint8_t all)
{
    uint8_t seg[16];
    uint8_t start;
    uint8_t end;
    uint8_t p;

    // 防呆
    if (all == 0u)  { all = 1u; }
    if (curr == 0u) { curr = 1u; }
    if (curr > all) { curr = all; }

    start = (uint8_t)(((uint16_t)(curr - 1u) * 128u) / all);
    if (curr == all)
    {
        end = 127u;
    }
    else
    {
        end = (uint8_t)((((uint16_t)curr * 128u) / all) - 1u);
    }

    for (p = 0u; p < 16u; p++)
    {
        seg[p] = 0u;
    }
    for (p = start; p <= end; p++)
    {
        seg[p >> 3] |= (uint8_t)(0x80u >> (p & 7u));  // 像素区间 → 位图
    } /* p >> 3（除以 8）算出像素落在第几个字节，p & 7 算落在字节内第几位，0x80 >> (p&7) 把那一位点亮 */
    LCD12864_DrawRow(14u, seg);
}

/* Draw the top graphic zone of a page: banner strips at X word 2..5
 * (blank, glyph0, glyph1, blank), separator line at y=15, 5-segment
 * progress bar at y=14 (home = 1/5, set = 2/5, same as the original).
 * Every page draws the same spots, so a page switch overwrites the old
 * banner cleanly - no slow GDRAM clear needed. 翻页时新页直接覆写旧页的字，不用清 GDRAM。*/
static void s_DrawTopZone(page_id_t page)
{
    const uint8_t *g0;
    const uint8_t *g1;

    if (page == PAGE_SET)
    {    /* 配置 */
        g0 = s_banner_cfg0;
        g1 = s_banner_cfg1;
    }
    else
    {   /* 控制 */
        g0 = s_banner_ctl0;
        g1 = s_banner_ctl1;
    }

    LCD12864_DrawStrip(2u, s_banner_blank);
    LCD12864_DrawStrip(3u, g0);
    LCD12864_DrawStrip(4u, g1);
    LCD12864_DrawStrip(5u, s_banner_blank);
    LCD12864_DrawRow(15u, s_line_full);  /* 分隔线，128 像素全亮 */
    s_ProgressDraw((page == PAGE_SET) ? 2u : 1u, 5u);   /* 5 段进度条 */
}

/* Send one cached row 缓存行 to the LCD. DDRAM row = item + 1: row 0 pixel area
 * is occupied by the top graphic zone (banner/bar/line), so the three
 * editable items live on DDRAM rows 0x90/0x88/0x98 (screen lines 2..4). */
static void s_FlushSetRow(uint8_t item)
{
    LCD12864_ShowHanzi((uint8_t)(item + 1u), s_set_rows[item], 8u);
}

static void s_SetPageDrawAll(void)
{
    uint8_t i;

    LCD12864_Clear();
    for (i = 0u; i < SET_ITEMS; i++)
    {
        s_FormatSetRow(i);
        s_FlushSetRow(i);
    }
    s_DrawTopZone(PAGE_SET);
}

/* --------------------------- Page painters -------------------------------
 * Clear + redraw all rows. A full redraw is ~100 serial frames, a few ms
 * per page switch - acceptable while there is no dynamic data.        */
static void s_DrawHomePage(void)
{
    LCD12864_Clear();
    LCD12864_ShowHanzi(1u, s_home_l2, 8u);
    LCD12864_ShowHanzi(2u, s_home_l3, 8u);
    LCD12864_ShowHanzi(3u, s_home_l4, 8u);
    s_DrawTopZone(PAGE_HOME);
}

static void s_DrawPage(page_id_t page)
{
    switch (page)
    {
    case PAGE_SET:
        s_set_mode = SET_BROWSE;
        s_cursor   = 0u;
        s_SetPageDrawAll();
        break;
    case PAGE_HOME:
    default:
        s_DrawHomePage();
        break;
    }
}

/* ------------------------- Set page key handling -------------------------- */
/* Edit mode: UP/DOWN step the value, STOP confirms, CANCEL restores the
 * pre-edit value and exits. Page switching is blocked while editing.     */
static void s_SetPageEditKey(key_code_t key)
{
    switch (key)
    {
    case KEY_UP:
    case KEY_DOWN:
        s_param[s_cursor] = s_StepValue(s_cursor, (key == KEY_UP) ? 1u : 0u);
        s_FormatSetRow(s_cursor);
        s_FlushSetRow(s_cursor);              /* 1 row, 25 frames       */
        break;

    case KEY_STOP:                            /* confirm and stay       */
        s_set_mode = SET_BROWSE;
        s_FormatSetRow(s_cursor);
        s_FlushSetRow(s_cursor);
        break;

    case KEY_CANCEL:                          /* cancel: restore value  */
        s_param[s_cursor] = s_edit_backup;
        s_set_mode = SET_BROWSE;
        s_FormatSetRow(s_cursor);
        s_FlushSetRow(s_cursor);
        break;

    case KEY_SET:                             /* ignored while editing  */
    case KEY_NONE:
    default:
        break;
    }
}

/* 光标上下移一项，只重画"旧行 + 新行"两行，不整屏 Clear。 */
static void s_SetMoveCursor(uint8_t up)
{
    uint8_t old = s_cursor;

    s_cursor = up ? (uint8_t)((s_cursor + SET_ITEMS - 1u) % SET_ITEMS)
                  : (uint8_t)((s_cursor + 1u) % SET_ITEMS);
    s_FormatSetRow(old);
    s_FlushSetRow(old);
    s_FormatSetRow(s_cursor);
    s_FlushSetRow(s_cursor);                  /* 2 rows, no Clear()     */
}

/* Browse mode on the set page. Returns 1 if the key was consumed here,
 * 0 to let the caller continue with page switching.                     */
static int s_SetPageBrowseKey(key_code_t key)
{
    switch (key)
    {
    case KEY_UP:
        s_SetMoveCursor(1u);
        return 1;

    case KEY_DOWN:
        s_SetMoveCursor(0u);
        return 1;

    case KEY_STOP:                            /* enter edit mode        */
        s_edit_backup = s_param[s_cursor];
        s_set_mode = SET_EDIT;
        s_FormatSetRow(s_cursor);
        s_FlushSetRow(s_cursor);
        return 1;

    case KEY_SET:
    case KEY_CANCEL:
        return 0;                             /* page switching keys    */

    case KEY_NONE:
    default:
        return 1;                             /* swallow, do nothing    */
    }
}

/* ----------------------------- Public API -------------------------------- */
void LCD_PAGES_Init(void)
{
    s_page = PAGE_HOME;
    s_DrawPage(s_page);
}

/* Feed one key event. Only page-switch keys act; the screen is redrawn
 * only when the page actually changes. */
void LCD_PAGES_Process(key_code_t key)
{
    page_id_t old;

    /* 设置页拦截器 */
    if (s_page == PAGE_SET)
    {
        if (s_set_mode == SET_EDIT)
        {
            s_SetPageEditKey(key);
            return;                           /* page switch blocked    */
        }
        if (s_SetPageBrowseKey(key) != 0)
            return;
    }

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
    case KEY_UP:       /* reserved: cursor / item inside a page  */
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

/* Refresh rows whose content changes without a key press.
 * The calibration row reflects DCMOTOR state (未校准 / 校准中 / 已校准),
 * which transitions while the set page is idle, so it must be polled.
 * Only re-sends the row when the displayed state actually changes, so this
 * is cheap to call every main-loop turn. Skipped while editing a row.      */
void LCD_PAGES_Refresh(void)
{
    /* 0 = 未校准, 1 = 校准中, 2 = 已校准, 0xFF = force first sync */
    static uint8_t s_last_cal_state = 0xFFu;
    uint8_t st;

    if (s_page != PAGE_SET)
        return;
    if (s_set_mode == SET_EDIT)
        return;                       /* key handlers own the row now */

    if (DCMOTOR_IsCalibrating())
        st = 1u;
    else if (DCMOTOR_IsCalibrated())
        st = 2u;
    else
        st = 0u;

    if (st == s_last_cal_state)
        return;
    s_last_cal_state = st;
    s_FormatSetRow((uint8_t)LCD_PARAM_AUTO_FLAG);
    s_FlushSetRow((uint8_t)LCD_PARAM_AUTO_FLAG);
}

uint8_t LCD_PAGES_IsHome(void)
{
    return (s_page == PAGE_HOME) ? 1u : 0u;
}
```

---

## 5. 行为规格

### 5.1 按键行为表（设置页两个模式）

| 按键（丝印） | 设置页·浏览模式 | 设置页·编辑模式 |
|---|---|---|
| UP（K1） | 光标上一项（环绕） | 数值 +1 步；校准项 = 开 |
| DOWN（K2） | 光标下一项（环绕） | 数值 -1 步；校准项 = 关 |
| STOP（K5） | **进入编辑**（不回首页，与原工程 `ViewNum!=1` 特例同源） | 确认，星号消失，留在设置页 |
| SET（K4） | 上一页（设置→首页环绕） | 无效（忽略） |
| CAN（K3） | 下一页 | **取消编辑**，恢复进入编辑前的值 |

首页上行为不变：STOP 回首页、SET/CAN 翻页、UP/DOWN 无动作。编辑模式中翻页被整体封锁，先 STOP 或 CAN 退出编辑才能翻页。

### 5.2 设置页布局（3 行 × 16 字节）

```
行0: "->上限位:2000 "    字节: [0-1]=标记  [2-7]=上限位(GB2312)  [8]=':'  [9-12]=4位数字  [13]=编辑标记
行1: "->堵转: 30.0 A"    字节: [0-1]=标记  [2-7]=堵转:+空格      [8-11]="30.0"    [12]=' ' [13]='A' [14]=编辑标记
行2: "->校准: 未校准"    字节: [0-1]=标记  [2-7]=校准:+空格      [8-13]=未校准/校准中/已校准 [14]=编辑标记
```

- 选中行 `[0-1]` = `'-'','>'`，未选中 = 两个空格。
- **编辑中的行**在标记位之后多一个 `'*'`（位置见 `s_star_pos[]`），确认/取消后消失。
- 所有汉字字节沿用原工程数组：上限位 `0xc9,0xcf,0xcf,0xde,0xce,0xbb`、堵转 `0xb6,0xc2,0xd7,0xaa`、校准 `0xd0,0xa3,0xd7,0xbc`；校准状态行**不再用**关闭/开启，改用三组：未校准 `0xce,0xb4,0xd0,0xa3,0xd7,0xbc`、校准中 `0xd0,0xa3,0xd7,0xbc,0xd6,0xd0`、已校准 `0xd2,0xd1,0xd0,0xa3,0xd7,0xbc`（显示电机真实状态，见 §5.4）。

### 5.3 参数范围与步距

| 项 | 枚举 | 单位 | 默认 | 步距 | 范围 | 显示 |
|---|---|---|---|---|---|---|
| 上限位 | `LCD_PARAM_GATE_MAX` | 无（位置值） | 2000 | 10 | 0~9999 | 4 位十进制 |
| 堵转阈值 | `LCD_PARAM_STALL_010A` | 0.1 A | 300（=30.0A） | 1 | 0~999 | `"dd.d A"` |
| 校准状态 | `LCD_PARAM_AUTO_FLAG` | 0/1 | 0 | UP=触发 / DOWN=0 | 0~1 | 未校准 / 校准中 / 已校准（**显示**取自 DCMOTOR 状态，非参数值） |

越界即钳位（到顶再加不动，到底再减不动）。原工程对应值：`gate_maxheight`、`motor_warning_curr_upper`（原工程存的是 mA，本模块存 0.1A，接 EEPROM 时换算）、`autoFlag`。

> 上表是面向人的规格说明；代码里的唯一真源是 `s_items[]`（下标与 `lcd_param_t` 一一对应）。改参数范围/步进只改 `s_items[]`，不要再去别处找第二份。

### 5.4 其他行为细节

- `LCD_PAGES_SetParam()` 在设置页显示期间被调用（比如以后 4G 下发参数），会立即格式化并重发那一行（25 帧）；在首页调用则只改 RAM，不发任何帧。
- 进设置页时**光标复位到第 0 项、退出编辑模式**（对应原工程进页时 `SetPageNum=0xff; setParaflag=0` 的复位动作）。
- 光标移动 = 重发 2 行（50 帧）；改值/进编辑/确认/取消 = 重发 1 行（25 帧）；整页 Clear 只在进页时出现。这是和第 1 轮「整页重画」的本质区别。
- 校准状态行（第 2 项）显示的是 DCMOTOR 真实状态：`DCMOTOR_IsCalibrating()` → 校准中，否则 `DCMOTOR_IsCalibrated()`（`s_max_turns != 0`）→ 已校准，否则 → 未校准。**不是** `AUTO_FLAG` 的值——`AUTO_FLAG` 只是一次性触发标志（`main.c` 消费后清 0，见 DCMotor 文档 §7.3.1）。这一行会在无按键时变化，由 `main()` 每轮调用 `LCD_PAGES_Refresh()` 刷新：仅当状态真变了才重发该行（25 帧），平时空转；编辑该行时跳过刷新（按键处理器接管）。

---

## 6. 与原工程的差异清单

| # | 原工程做法 | 本轮做法 | 理由 |
|---|---|---|---|
| 1 | `DisplayCache` 影子变量按**数据项**记录（`Dispaly.c:125-144`），轮询数据变化 | 整行 16 字节 RAM 缓存，**事件驱动**（按键/SetParam 才动） | 新工程无数据源可轮询；行级缓存先立骨架，数据轮在行缓存上加"变了才发"即可 |
| 2 | 逐字符编辑：`Num` 在 `0x30~0x39` 步进、ENTER 逐位确认（`Dispaly.c:502-556`） | 整值步进：UP/DOWN 直接加减整个值 | 逐字符编辑依赖「编辑位闪烁 + cnt 计数」一套状态，10 个按键才能输完一个数；整值步进 1 个按键改 1 步，手机改 DPI 这类场景也更快。这是**有意简化**，不是遗漏 |
| 3 | 编辑值直接写进显示数组（`zhaweimax[cnt-1]=Num`），数组和参数耦合 | 参数独立存 `s_param[]`，显示行每次从参数**格式化**生成 | 数值和显示解耦；4G/ADC 改参数不用碰显示数组 |
| 4 | 编辑完 `AT24CXX_WriteWord` 落盘（`:531,550`） | 只存 RAM，EEPROM 轮再接 | EEPROM 模块未搬；接口已集中在 `SetParam` |
| 5 | ENTER 在设置页不回首页（`ViewNum!=1` 特例，`:371-377`） | 同源：STOP 在设置页 = 进编辑；回首页用 SET | 保持一致 |
| 6 | 行反选 `WRGDRAM128X8` 标光标（`:400-402`） | 行首 `"->"` 文本标记 | 反选属 GDRAM 绘图，第 4 轮；先给可见光标 |
| 7 | 设置页数值在数组里预置（如 `00.0 A`） | 数值从 `s_param[]` 实时格式化 | 编辑的前提 |

---

## 7. 实施步骤

1. 用第 3 节代码**整文件替换** `qibiji_zxl/HARDWARE/LCD12864/lcd_pages.h`。
2. 用第 4 节代码**整文件替换** `qibiji_zxl/HARDWARE/LCD12864/lcd_pages.c`。
3. 在 `USER/main.c` 主循环里加一行 `LCD_PAGES_Refresh();`（放在校准标志消费块之后、`DCMOTOR_Process();` 之前，详见 §5.4 与 DCMotor 文档 §7.3.1）。
4. Keil Build（0 error / 0 warning 预期），烧录。
5. 上板走一遍第 9 节的操作序列。

`uvprojx` 本轮不用动（无新文件，`dcmotor.h` 已在 IncludePath）；`main.c` 只加上面那 1 行。

---

## 8. host 验证覆盖点（59 项断言）

与第 1 轮同一套方法：桩环境回放 PE3/PE4/PE5 引脚波形 → 解码出每一帧 3 字节 → 逐帧断言。覆盖：

- 进入设置页整页绘制 3 行（含光标标记与默认值 2000 / 30.0A / 未校准）逐字节比对；
- UP/DOWN 光标移动恰好重发 2 行（50 帧）且 **0 次 Clear**；UP 在第 0 项环绕到第 2 项；
- STOP 进编辑 = 1 行 + 星号；UP/DOWN 改值实时写入 `GetParam`；
- 高位钳位：9995 再加 → 9999 不动；低位钳位：5 再减 → 0 不动；
- 堵转行格式 `29.9 A → 30.0 A`；校准行状态切换（未校准 → 校准中 → 已校准，由电机状态驱动，非按键）；
- CANCEL 取消恢复进入编辑前的值（改了也能退回去）；
- 编辑模式封锁翻页：编辑中 CAN 是取消编辑而不是翻页；
- `SetParam` 在设置页立即重发 1 行、在首页 0 帧；
- 非法枚举索引 `GetParam` 返回 0、`SetParam` 忽略；`SetParam` 越界钳位到 max。

---

## 9. 上板验证与排查

**操作序列（预期现象）**：

1. 上电 → 首页 4 行正常。
2. CAN → 设置页，第 0 行行首有 `->`，显示 `上限位:2000`。
3. DOWN → `->` 跳到堵转行（**只有两行重画，肉眼几乎无闪烁**）。
4. STOP → 该行数字后出现 `*`；UP/DOWN 数值增减，到顶/到底不再动。
5. CAN → `*` 消失、数值弹回；再 STOP → 编辑，改完 STOP → `*` 消失、数值保留。
6. STOP → 进编辑；此时按 CAN → 退出编辑**而不是翻页**；再按 CAN → 回首页。
7. 设置页浏览态按 SET → 回首页（环绕）。

**排查表**：

| 现象 | 优先查 |
|---|---|
| 汉字乱码、数字正常 | `s_lbl_*` / `s_txt_*` 字节被抄错（对照 5.2 节字节表） |
| 光标不动 | `APP_DispatchKey` 是否还在调 `LCD_PAGES_Process(key)`；KEY_UP/DOWN 是否被别处截走 |
| 改值后不刷新 | `s_FlushSetRow` 是否被改掉；`ShowHanzi` 的 count 是否传 8 |
| 编辑中按 CAN 直接翻了页 | `LCD_PAGES_Process` 开头 `SET_EDIT` 分支的 `return` 是否漏了 |
| 数值越界 | `s_items[]` 表里的 `max` / `step` 是否抄错 |
| 编译报 `uint16_t` 未定义 | `key.h` 里 `#include "sys.h"` 是否还在 |

---

## 10. 约束与后续轮次

- 本轮只动了 `lcd_pages.h/.c` 两个文件；驱动层、main.c、Keil 工程一律未动。
- C90：所有声明在块首、无 `//` 注释依赖（代码注释全 ASCII，工程带 `--no-multibyte-chars`）。
- 后续排期（路线不变）：
  - **第 4 轮**：GDRAM 绘图 —— 反选光标（替换 `"->"` 标记）、图标、进度条（原工程 `WRGDRAM128X8` / `GenerateProgressBar` 一族）；
  - **EEPROM 轮**：`LCD_PAGES_SetParam` 里接 `AT24CXX_WriteWord`，上电读回；
  - **数据轮**：ADC/电流接入后，给首页加数据变化检测（原工程 `LcdDisplay` 的影子变量模式，落在第 4 节的行缓存上）。

---

## 11. 验证记录与证据

- 编译：host 端 gcc `-std=c90 -Wall -Wextra`，0 error / 0 warning。
- 行为断言：59 项，0 失败（含上面第 8 节全部覆盖点）。
- 测试代码副本：`C:\Users\JessamynAdmin\AppData\Local\Temp\qibiji_pages_test\`（`lcd_pages.c/.h` 与本文一致，`test_round3.c` 为本轮断言）。可直接复制，不用重敲。
- 这是 host + 桩的行为验证，**不是 Keil 真机编译**，请以 Keil Build 结果为准。
