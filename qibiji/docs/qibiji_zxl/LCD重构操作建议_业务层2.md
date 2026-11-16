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
| 改 | `HARDWARE/LCD12864/lcd_pages.h` | **整文件替换** | 增加 `lcd_param_t` 枚举和 2 个参数 API 声明 |
| 改 | `HARDWARE/LCD12864/lcd_pages.c` | **整文件替换** | 行缓存 + 编辑状态机 + 参数 API |
| 不动 | `USER/main.c` | 不改 | 第 1 轮已把 `LCD_PAGES_Process(key)` 接进 `APP_DispatchKey`，本轮行为自动升级 |
| 不动 | `USER/SRTUV10.uvprojx` | 不改 | 没有新文件，IncludePath 不动 |

落地顺序：替换 `.h` → 替换 `.c` → Keil 里 Build。共 2 个文件。

---

## 3. lcd_pages.h（整文件替换）

```c
#ifndef __LCD_PAGES_H
#define __LCD_PAGES_H

/* LCD pages module (business layer, round 3).
 * Owns: page state machine + the GB2312 text rows drawn on each page,
 *       plus set-page parameter editing (cursor + value edit) and a
 *       per-row RAM cache so only changed rows are re-sent to the LCD.
 * Reacts to key events only; no periodic refresh in this round.
 * Depends on: key.h (key_code_t) and lcd12864.h (driver layer only). */
#include "key.h"

void LCD_PAGES_Init(void);
void LCD_PAGES_Process(key_code_t key);

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

```c
/* ===========================================================================
 * LCD pages module - qibiji_zxl (business layer, round 3)
 *
 * Owns: page state machine (which page is on screen) and the GB2312 text
 *        rows drawn on each page; set-page parameter editing (browse with
 *        a cursor marker + per-item value edit); a per-row RAM cache so
 *        cursor moves and edits re-send only the affected rows instead of
 *        repainting the whole page.
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
    PAGE_SET,        /* settings: 3 editable items                        */
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
static const uint8_t s_home_l3[16] = {         /* "opening: 100% full-open"*/
    0xbf, 0xaa, 0xb6, 0xc8, 0x3a, 0x20,
    0x31, 0x30, 0x30, 0x25, 0x20, 0x20, 0xc8, 0xab, 0xbf, 0xaa };
static const uint8_t s_home_l4[16] = {         /* "flow: 0.0 m3/s"         */
    0xc1, 0xf7, 0xc1, 0xbf, 0x3a, 0x20,
    0x20, 0x30, 0x2e, 0x30, 0x20, 0x6d, 0x33, 0x2f, 0x73, 0x20 };

/* ------------------------- Editable parameters ---------------------------
 * RAM only in this round; EEPROM persistence comes with the EEPROM round. */
static uint16_t s_param[LCD_PARAM_COUNT] = { 2000u, 300u, 0u };
static const uint16_t s_param_max[LCD_PARAM_COUNT]  = { 9999u, 999u, 1u };
static const uint16_t s_param_step[LCD_PARAM_COUNT] = {   10u,   1u, 1u };

/* forward declarations - C90 needs them before first use */
static void s_FormatSetRow(uint8_t item);
static void s_FlushSetRow(uint8_t item);

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
    if (v > s_param_max[p])
        v = s_param_max[p];
    s_param[p] = v;
    if (s_page == PAGE_SET)
    {
        s_FormatSetRow((uint8_t)p);
        s_FlushSetRow((uint8_t)p);
    }
}

/* ------------------------- Set page row cache ----------------------------
 * The 3 set-page rows live in RAM. Painters format into the cache; only
 * rows whose bytes actually change are flushed to the LCD. This is the
 * row-level seed of the original project's DisplayCache mechanism
 * (Dispaly.c:125-144): "re-send only what changed".                     */
#define SET_ITEMS 3u
typedef enum { SET_BROWSE = 0, SET_EDIT } set_mode_t;

static set_mode_t s_set_mode = SET_BROWSE;
static uint8_t    s_cursor   = 0;      /* selected item, 0..2             */
static uint16_t   s_edit_backup = 0;   /* value before edit, for cancel   */
static uint8_t    s_set_rows[SET_ITEMS][16];

/* GB2312 fragments used to build the rows */
static const uint8_t s_lbl_gate[7] = {       /* up-limit label + colon   */
    0xc9, 0xcf, 0xcf, 0xde, 0xce, 0xbb, 0x3a };
static const uint8_t s_lbl_stall[6] = {      /* stall label + colon+space */
    0xb6, 0xc2, 0xd7, 0xaa, 0x3a, 0x20 };
static const uint8_t s_lbl_auto[6] = {       /* calibration label + colon */
    0xd0, 0xa3, 0xd7, 0xbc, 0x3a, 0x20 };
static const uint8_t s_txt_off[4] = {        /* "closed"                 */
    0xb9, 0xd8, 0xb1, 0xd5 };
static const uint8_t s_txt_on[4] = {         /* "open"                   */
    0xbf, 0xaa, 0xc6, 0xf4 };

/* byte offset of the edit marker '*' in each row */
static const uint8_t s_star_pos[SET_ITEMS] = { 13u, 14u, 12u };

static void s_RowClear(uint8_t *row)
{
    uint8_t i;
    for (i = 0u; i < 16u; i++)
        row[i] = 0x20u;
}

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

/* Format one set-page row into the RAM cache (no LCD traffic here). */
static void s_FormatSetRow(uint8_t item)
{
    uint8_t *row = s_set_rows[item];
    uint16_t v;

    s_RowClear(row);
    if (s_cursor == item)
    {
        row[0] = '-';
        row[1] = '>';
    }

    switch (item)
    {
    case 0u:                                  /* up-limit: 4 digits     */
        s_BytesCopy(&row[2], s_lbl_gate, 7u);
        s_U16ToDec4(s_param[LCD_PARAM_GATE_MAX], &row[9]);
        break;
    case 1u:                                  /* stall: "dd.d A"        */
        s_BytesCopy(&row[2], s_lbl_stall, 6u);
        v = s_param[LCD_PARAM_STALL_010A];
        row[8]  = (uint8_t)('0' + ((v / 100u) % 10u));
        row[9]  = (uint8_t)('0' + ((v / 10u) % 10u));
        row[10] = '.';
        row[11] = (uint8_t)('0' + (v % 10u));
        row[12] = ' ';
        row[13] = 'A';
        break;
    case 2u:                                  /* calibration on/off     */
    default:
        s_BytesCopy(&row[2], s_lbl_auto, 6u);
        s_BytesCopy(&row[8],
                    (s_param[LCD_PARAM_AUTO_FLAG] != 0u) ? s_txt_on
                                                         : s_txt_off,
                    4u);
        break;
    }

    if ((s_set_mode == SET_EDIT) && (s_cursor == item))
        row[s_star_pos[item]] = '*';
}

/* Send one cached row to the LCD - 25 frames, no Clear(). */
static void s_FlushSetRow(uint8_t item)
{
    LCD12864_ShowHanzi(item, s_set_rows[item], 8u);
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
}

/* --------------------------- Page painters -------------------------------
 * Home page is still static text (real data sources come later), drawn
 * with a full Clear + 4 rows. Set page uses the row cache.              */
static void s_DrawHomePage(void)
{
    LCD12864_Clear();
    LCD12864_ShowHanzi(0u, s_home_l1, 8u);
    LCD12864_ShowHanzi(1u, s_home_l2, 8u);
    LCD12864_ShowHanzi(2u, s_home_l3, 8u);
    LCD12864_ShowHanzi(3u, s_home_l4, 8u);
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
    uint16_t v;
    uint16_t max;
    uint16_t step;

    switch (key)
    {
    case KEY_UP:
    case KEY_DOWN:
        v    = s_param[s_cursor];
        max  = s_param_max[s_cursor];
        step = s_param_step[s_cursor];
        if (s_cursor == LCD_PARAM_AUTO_FLAG)
        {
            v = (key == KEY_UP) ? 1u : 0u;    /* toggle like row3flag   */
        }
        else if (key == KEY_UP)
        {
            v = (uint16_t)((v > (uint16_t)(max - step)) ? max : v + step);
        }
        else
        {
            if (v <= step)
                v = 0u;
            else
                v = (uint16_t)(v - step);
        }
        s_param[s_cursor] = v;
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

/* Browse mode on the set page. Returns 1 if the key was consumed here,
 * 0 to let the caller continue with page switching.                     */
static int s_SetPageBrowseKey(key_code_t key)
{
    uint8_t old;

    switch (key)
    {
    case KEY_UP:
        old = s_cursor;
        s_cursor = (uint8_t)((s_cursor + SET_ITEMS - 1u) % SET_ITEMS);
        s_FormatSetRow(old);
        s_FlushSetRow(old);
        s_FormatSetRow(s_cursor);
        s_FlushSetRow(s_cursor);              /* 2 rows, no Clear()     */
        return 1;

    case KEY_DOWN:
        old = s_cursor;
        s_cursor = (uint8_t)((s_cursor + 1u) % SET_ITEMS);
        s_FormatSetRow(old);
        s_FlushSetRow(old);
        s_FormatSetRow(s_cursor);
        s_FlushSetRow(s_cursor);
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

void LCD_PAGES_Process(key_code_t key)
{
    page_id_t old;

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
    case KEY_SET:      /* previous page, wraps */
        s_page = (page_id_t)(((int)s_page + (int)PAGE_COUNT - 1) % (int)PAGE_COUNT);
        break;
    case KEY_CANCEL:   /* next page, wraps around */
        s_page = (page_id_t)(((int)s_page + 1) % (int)PAGE_COUNT);
        break;
    case KEY_STOP:     /* back to home page */
        s_page = PAGE_HOME;
        break;
    case KEY_UP:
    case KEY_DOWN:
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
行2: "->校准: 关闭"      字节: [0-1]=标记  [2-7]=校准:+空格      [8-11]=关闭/开启 [12]=编辑标记
```

- 选中行 `[0-1]` = `'-'','>'`，未选中 = 两个空格。
- **编辑中的行**在标记位之后多一个 `'*'`（位置见 `s_star_pos[]`），确认/取消后消失。
- 所有汉字字节沿用原工程数组：上限位 `0xc9,0xcf,0xcf,0xde,0xce,0xbb`、堵转 `0xb6,0xc2,0xd7,0xaa`、校准 `0xd0,0xa3,0xd7,0xbc`、关闭 `0xb9,0xd8,0xb1,0xd5`、开启 `0xbf,0xaa,0xc6,0xf4`。

### 5.3 参数范围与步距

| 项 | 枚举 | 单位 | 默认 | 步距 | 范围 | 显示 |
|---|---|---|---|---|---|---|
| 上限位 | `LCD_PARAM_GATE_MAX` | 无（位置值） | 2000 | 10 | 0~9999 | 4 位十进制 |
| 堵转阈值 | `LCD_PARAM_STALL_010A` | 0.1 A | 300（=30.0A） | 1 | 0~999 | `"dd.d A"` |
| 校准开关 | `LCD_PARAM_AUTO_FLAG` | 0/1 | 0（关） | UP=开 / DOWN=关 | 0~1 | 关闭 / 开启 |

越界即钳位（到顶再加不动，到底再减不动）。原工程对应值：`gate_maxheight`、`motor_warning_curr_upper`（原工程存的是 mA，本模块存 0.1A，接 EEPROM 时换算）、`autoFlag`。

### 5.4 其他行为细节

- `LCD_PAGES_SetParam()` 在设置页显示期间被调用（比如以后 4G 下发参数），会立即格式化并重发那一行（25 帧）；在首页调用则只改 RAM，不发任何帧。
- 进设置页时**光标复位到第 0 项、退出编辑模式**（对应原工程进页时 `SetPageNum=0xff; setParaflag=0` 的复位动作）。
- 光标移动 = 重发 2 行（50 帧）；改值/进编辑/确认/取消 = 重发 1 行（25 帧）；整页 Clear 只在进页时出现。这是和第 1 轮「整页重画」的本质区别。

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
3. Keil Build（0 error / 0 warning 预期），烧录。
4. 上板走一遍第 9 节的操作序列。

`main.c` 和 `uvprojx` **本轮都不用动**。

---

## 8. host 验证覆盖点（59 项断言）

与第 1 轮同一套方法：桩环境回放 PE3/PE4/PE5 引脚波形 → 解码出每一帧 3 字节 → 逐帧断言。覆盖：

- 进入设置页整页绘制 3 行（含光标标记与默认值 2000 / 30.0A / 关闭）逐字节比对；
- UP/DOWN 光标移动恰好重发 2 行（50 帧）且 **0 次 Clear**；UP 在第 0 项环绕到第 2 项；
- STOP 进编辑 = 1 行 + 星号；UP/DOWN 改值实时写入 `GetParam`；
- 高位钳位：9995 再加 → 9999 不动；低位钳位：5 再减 → 0 不动；
- 堵转行格式 `29.9 A → 30.0 A`；校准行 开启/关闭 切换；
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
| 数值越界 | `s_param_max/s_param_step` 表是否抄错 |
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
