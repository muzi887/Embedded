# LCD 重构操作建议 —— 业务层第 3 轮：顶部图形区（页头大字 / 进度条 / 分隔线）

> 承接《LCD重构操作建议_驱动层.md》《_业务层1.md》《_业务层2.md》。第 3 轮上板已通过（含汉字字节订正）。
> 本轮把原工程每个页面顶部的"大字 + 进度条 + 分隔线"图形区搬进来。
> **`main.c` 和 `SRTUV10.uvprojx` 本轮都不用改**——只动 `lcd12864.h/.c` 和 `lcd_pages.h/.c` 四个文件（头文件加 2 个声明）。

---

## 1. 本轮做什么、不做什么

**做**（对应原工程真实在用的三样东西）：

| 原工程 | 位置 | 本轮对应 |
|---|---|---|
| `WRGDRAM(x, buf)` 页头列块 | `Lcd12864.C:198-212` | `LCD12864_DrawStrip(x, buf)` |
| `WRGDRAM_line()` 分隔线 | `Lcd12864.C:183-196` | `LCD12864_DrawRow(15, s_line_full)` |
| `GenerateProgressBar1(curr, all)` 进度条 | `Lcd12864.C:369-506` | `s_ProgressDraw()` + `LCD12864_DrawRow(14, seg)` |

**不搬**（原工程里**没有任何调用点**的死代码，`Dispaly.c` 全文检索确认）：

- `WRGDRAM128X8 / WRGDRAM128X3`、`WRGDRAMp1~p4`（电池/信号图标）—— 定义了但没人调；
- `WRCGRAMH / ShowCGCharH`（自造字，芯片限 4 个字位）—— 同样零调用；
- `GenerateProgressBar`（无 1 后缀的旧版，写 Y=0 行）—— 被 `GenerateProgressBar1` 取代。

以后真要画电池/信号图标，在本轮的 `DrawStrip/DrawRow` 原语之上加数据表即可，驱动层不用再动。

## 2. 屏幕布局：本轮的可见变化（先说清楚）

原产品每页顶部是一块**图形区**（GDRAM 绘制），文字从**屏幕第 2 行**开始。本轮照搬这个布局：

```
┌──────────────────────────────┐
│   [图] 控  制  ← 页头大字      │ ← GDRAM y=1..12（像素行 2~25）
│   ▂▂▂▂▂▂▂ 1/5 进度条           │ ← GDRAM y=14（像素行 28~29）
│   ──────────────── 分隔线      │ ← GDRAM y=15（像素行 30~31）
│ 电机：1.6A 12.6V              │ ← DDRAM 行 1（地址 0x90）
│ 开度：100%  全开               │ ← DDRAM 行 2（地址 0x88）
│ 流量： 0.0 m3/s               │ ← DDRAM 行 3（地址 0x98）
└──────────────────────────────┘
```

**两个用户可见的行为变化**（有意的，与原工程一致）：

1. **首页第 1 行的「版本：」文字消失**——原工程首页就是"顶部图形 + 3 行文字"，版本号在 OTA 参数页显示，首页顶部被「控制」大字占住。原数组 `s_home_l1` 删除。
2. **设置页 3 个编辑项整体下移一行**（屏幕第 2/3/4 行）——顶部让给「配置」大字 + 进度条 2/5。行缓存内容、光标、编辑逻辑全部不变，只是落屏的 DDRAM 地址平移。

**进页帧数预算**（每页）：Clear(3) + 3 行文字(75) + 4 条竖条(204) + 分隔线(21) + 进度条(21) = **324 帧**，约 20~30 ms，只在进页时发生一次；光标移动仍是 50 帧、编辑改值仍是 25 帧，GDRAM 不参与增量刷新。（注：每个绘图原语收尾只发 `0x30`、比旧版少 1 帧，6 次绘图调用共少 6 帧，故从最初的 330 帧降为 324 帧。）

## 3. ST7920 行地址的坑（改行号前必读）

ST7920 的 DDRAM 4 行地址**不是**顺序的：屏幕第 1~4 行对应 `0x80 / 0x90 / 0x88 / 0x98` —— **第 2 行和第 3 行是交换的**（0x90 在 0x88 前面）。驱动的 `s_row_addr[]` 表已经处理了这件事，所以本轮代码里：

- 屏幕第 2/3/4 行 = `ShowHanzi(1/2/3, ...)` = 地址 `0x90 / 0x88 / 0x98`；
- 上板排查时别按"地址顺序"找行，按屏幕上的物理行找。

GDRAM 那边没有这个交换：`y` 参数就是屏幕像素行的一半（y=14 → 像素行 28~29），两半屏共用 y 0~31，X 字地址 0~7 落上半屏、8~15 落下半屏（驱动层文档 4.3 节讲过）。

## 4. 第 1 步：驱动层加 2 个绘图原语

### 4.1 `lcd12864.h` —— 在 `LCD12864_ShowHanzi` 声明后加 2 行

```c
void LCD12864_DrawRow(uint8_t y, const uint8_t *buf);
void LCD12864_DrawStrip(uint8_t x, const uint8_t *buf);
```

### 4.2 `lcd12864.c` —— 文件末尾（`LCD12864_Init` 之后）追加

```c
/* ------------------------- GDRAM drawing primitives ---------------------- */

/* Write one 128-pixel row on the upper half: 16 bytes = 8 word columns
 * (X word 0..7) at GDRAM row y (0..31). X auto-increments every 2 bytes,
 * so 16 bytes fill only the upper half (X 0..7); the lower half (X 8..15)
 * needs 16 more bytes. The bar and separator are upper-half only. */
void LCD12864_DrawRow(uint8_t y, const uint8_t *buf)
{
    uint8_t i;

    if ((y >= LCD_GDRAM_ROWS) || (buf == (const uint8_t *)0))
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);
    s_WriteCmd((uint8_t)(0x80u + y));
    s_WriteCmd(0x80u);
    for (i = 0u; i < 16u; i++)
    {
        s_WriteData(buf[i]);
    }
    /* Back to basic set ONLY. 0x30 does not touch the graphic-display bit,
     * so the image drawn above stays visible. Do NOT send 0x34 here:
     * 0x34 closes the graphic layer and the banner/bar/line vanish. */
    s_WriteCmd(LCD_CMD_BASIC);
}

/* Write one vertical strip into GDRAM: 12 rows (GDRAM y=1..12, i.e. screen
 * pixel rows 2..25), 2 bytes per row = 16 pixels wide, at word column x
 * (0..15). Byte layout matches the original WRGDRAM(): buf[i*2] then
 * buf[i*2+1] per row, used for the page banner glyphs. */
void LCD12864_DrawStrip(uint8_t x, const uint8_t *buf)
{
    uint8_t i;

    if ((x >= LCD_GDRAM_WORDS) || (buf == (const uint8_t *)0))
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);
    for (i = 0u; i < 12u; i++)
    {
        s_WriteCmd((uint8_t)(0x80u + 1u + i));   /* GDRAM y = 1..12 */
        s_WriteCmd((uint8_t)(0x80u + x));
        s_WriteData(buf[i * 2u]);
        s_WriteData(buf[(i * 2u) + 1u]);
    }
    /* Same closing rule as DrawRow: basic set only, keep graphic display ON. */
    s_WriteCmd(LCD_CMD_BASIC);
}
```

要点：

- **`DrawRow`**：一次发一条 128 像素横线——从 X 字地址 0（`0x80`）起连续写 16 字节，每 2 字节 X 字地址自动 +1，正好覆盖 X 0~7（**上半屏**一整行）。**不跨**到下半屏（X 8~15 需另写 16 字节）；但分隔线和进度条都画在上半屏，所以 16 字节够用。入口判 `y >= LCD_GDRAM_ROWS`（32）和空指针，越界静默返回。
- **`DrawStrip`**：一次发一条 12 行 × 16 像素的竖条，X 参数是**字地址**（0~15，每字 16 像素宽），Y 固定 1~12——和原 `WRGDRAM()` 完全同布局，只是把"画什么"从函数里挪到了参数里。
- 两个函数结尾**只发 `0x30` 回基本指令集，不发 `0x34`**。这是本轮上板后订正的关键点：ST7920 有两层独立显示——字符层由 `0x0C` 的 D 位控制，图形层由 `0x36`（开）/ `0x34`（关）控制。收尾若发 `0x34`，画完就把图形层关了，GDRAM 数据还在却看不见，表现为"页头/进度条只在翻页那一瞬闪过、字符行却一直正常"。原工程 `GenerateProgressBar1()`（`Lcd12864.C:497`）画完停在 `0x36` 从不关绘图，正是为了让图形持续显示。
- 与 `ClearGDRAM` 的收尾不同：清屏函数结尾仍用 `0x34 + 0x30`（`:241-251`），那里关绘图无害（本就没有内容要显示）；绘图原语则必须让图形层保持开。两者规则**有意不统一**，别照抄。
- `0x30` 只切指令集、不动绘图位，所以开头 `0x36` 打开的图形层能一直保持——这是"只发 `0x30`"成立的前提，也已被原工程 `ShowQQCharH()` 开头发 `0x30` 后图形仍在的实屏现象佐证。

## 5. 第 2 步：`lcd_pages.c` 加图形资产与绘制函数

### 5.1 页头字模数组（在 `s_txt_on` 之后插入）

**逐字节照抄，不要手打**——已用脚本与原工程 `code[][24]`（`Dispaly.c:78-111`）做过逐字节 diff，全部一致：

```c
/* ------------------- Top graphic zone (GDRAM) assets ---------------------
 * Banner glyph bitmaps, byte-for-byte from the original code[][24] table
 * (Dispaly.c:78-111). Each glyph is 12 rows x 2 bytes, drawn into GDRAM
 * rows y=1..12 at one X word column via LCD12864_DrawStrip().           */
static const uint8_t s_banner_ctl0[24] = {   /* code[0] */
    0x42, 0x00, 0x41, 0x00, 0x4f, 0xe0, 0xe8, 0x20,
    0x42, 0x80, 0x44, 0x40, 0x68, 0x20, 0xc7, 0xc0,
    0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0xcf, 0xe0 };
static const uint8_t s_banner_ctl1[24] = {   /* code[1] */
    0x10, 0x20, 0x50, 0x20, 0x7d, 0x20, 0x91, 0x20,
    0xff, 0x20, 0x11, 0x20, 0x7d, 0x20, 0x55, 0x20,
    0x55, 0x20, 0x54, 0x20, 0x5c, 0x20, 0x10, 0xe0 };
static const uint8_t s_banner_cfg0[24] = {   /* code[2] */
    0x00, 0x00, 0xfe, 0xe0, 0x28, 0x20, 0xfe, 0x20,
    0xaa, 0x20, 0xaa, 0xe0, 0xc6, 0x80, 0x82, 0x80,
    0xfe, 0x80, 0x82, 0xa0, 0xfe, 0xa0, 0x82, 0x60 };
static const uint8_t s_banner_cfg1[24] = {   /* code[3] */
    0x7f, 0xc0, 0x4a, 0x40, 0x7f, 0xc0, 0x04, 0x00,
    0xff, 0xe0, 0x04, 0x00, 0x3f, 0x80, 0x20, 0x80,
    0x20, 0x80, 0x24, 0x80, 0x24, 0x80, 0xff, 0xe0 };
static const uint8_t s_banner_blank[24] = {  /* code[23] */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* separator line under the top zone: GDRAM y=15, all 128 pixels lit */
static const uint8_t s_line_full[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
```

（原工程 `code[0]=控、code[1]=制、code[2]=配、code[3]=置、code[23]=空列`；注释里不用写中文，靠字节 + 本节说明对照。）

### 5.2 进度条计算 + 顶部区绘制（插在 `s_FlushSetRow` 之前）

```c
/* Light pixels start..end of one 128-pixel GDRAM row, MSB = leftmost.
 * Same math as the original GenerateProgressBar1 (Lcd12864.C:369-506),
 * reduced to two loops; the bar row is fixed at GDRAM y=14. */
static void s_ProgressDraw(uint8_t curr, uint8_t all)
{
    uint8_t seg[16];
    uint8_t start;
    uint8_t end;
    uint8_t p;

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
        seg[p >> 3] |= (uint8_t)(0x80u >> (p & 7u));
    }
    LCD12864_DrawRow(14u, seg);
}

/* Draw the top graphic zone of a page: banner strips at X word 2..5
 * (blank, glyph0, glyph1, blank), separator line at y=15, 5-segment
 * progress bar at y=14 (home = 1/5, set = 2/5, same as the original).
 * Every page draws the same spots, so a page switch overwrites the old
 * banner cleanly - no slow GDRAM clear needed.                     */
static void s_DrawTopZone(page_id_t page)
{
    const uint8_t *g0;
    const uint8_t *g1;

    if (page == PAGE_SET)
    {
        g0 = s_banner_cfg0;
        g1 = s_banner_cfg1;
    }
    else
    {
        g0 = s_banner_ctl0;
        g1 = s_banner_ctl1;
    }

    LCD12864_DrawStrip(2u, s_banner_blank);
    LCD12864_DrawStrip(3u, g0);
    LCD12864_DrawStrip(4u, g1);
    LCD12864_DrawStrip(5u, s_banner_blank);
    LCD12864_DrawRow(15u, s_line_full);
    s_ProgressDraw((page == PAGE_SET) ? 2u : 1u, 5u);
}
```

进度条算法说明：原工程 130 行的分段填充（完整段 / 余数跨字节 / 起止段三种情况）本质就是"把像素 `start..end` 置 1"。每字节 8 个像素、**最高位在最左**，所以像素 p 落在第 `p/8` 字节的第 `7-(p%8)` 位——`seg[p >> 3] |= 0x80 >> (p & 7)` 一行替代全部三种情况，输出与原函数逐位相同（host 测试对 1/5、2/5 两种都验过）。端点公式照抄原工程：`start = (curr-1)*128/all`，`end = curr*128/all - 1`（最后一份补到 127）。**1/5 = 像素 0~24**，不是 0~25——`128/5` 整除得 25 再减 1，上板时进度条第一份比全宽的 1/5 知一个像素是正常的，原工程就这样。

### 5.3 三个 painter 的修改

**`s_FlushSetRow`**（行号 +1）：

```c
/* Send one cached row to the LCD. DDRAM row = item + 1: row 0 pixel area
 * is occupied by the top graphic zone (banner/bar/line), so the three
 * editable items live on DDRAM rows 0x90/0x88/0x98 (screen lines 2..4). */
static void s_FlushSetRow(uint8_t item)
{
    LCD12864_ShowHanzi((uint8_t)(item + 1u), s_set_rows[item], 8u);
}
```

**`s_SetPageDrawAll`**（行循环后加一行）：

```c
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
```

**`s_DrawHomePage`**（去版本行、文字下移、加顶部区；同时**删除 `s_home_l1` 数组定义**，别留成没用的静态变量——`-Wall` 会报 unused）：

```c
static void s_DrawHomePage(void)
{
    /* DDRAM rows 1..3 (0x90/0x88/0x98); row 0's pixel area belongs to the
     * top graphic zone, so the old version row (s_home_l1) is dropped -
     * same as the original homepage, which shows no text above the line. */
    LCD12864_Clear();
    LCD12864_ShowHanzi(1u, s_home_l2, 8u);
    LCD12864_ShowHanzi(2u, s_home_l3, 8u);
    LCD12864_ShowHanzi(3u, s_home_l4, 8u);
    s_DrawTopZone(PAGE_HOME);
}
```

## 6. 第 3 步：确认不用改的文件

- `main.c` —— 无任何变化；
- `SRTUV10.uvprojx` —— 无新文件、无新目录，IncludePath 不动；
- `lcd_pages.h` —— 对外 API 没变，不动。

## 7. 与原工程逐项对照

| 原工程 | 原行为 | 本轮 | 差异说明 |
|---|---|---|---|
| `WRGDRAM(x, buf)` | Y 固定 1..12，画 12×16 竖条 | `DrawStrip(x, buf)` | 同布局；x 加了 0~15 越界保护 |
| `WRGDRAM_line()` | Y=15 一行 0xFF×16 | `DrawRow(15, s_line_full)` | 完全一致 |
| `GenerateProgressBar1` | 130 行分段计算 + 写 Y=14 | `s_ProgressDraw` + `DrawRow(14)` | 输出逐位等价；130 行 → 20 行 |
| 页头字模 `code[][24]` | 全 24 组存一张表 | 只搬在用的 5 组 | 用到哪组搬哪组，其余零调用 |
| 页面切换不清 GDRAM | 靠各页覆盖同位置；实际产品照片上 `dev0015` 后有残留杂点 | 各页覆写**相同**区域（X2~5、y14、y15），无需清 | 我们两个页布局对齐，天然无残留；以后加页若图形区形状不同，进页时补一次 `ClearGDRAM()` |
| 首页文字行 | 3 行（0x90/0x88/0x98） | 同 | 本轮把第 2 轮多加的版本行去掉了 |
| GDRAM 收尾 | `WRGDRAM*` / `GenerateProgressBar1` 结尾停在 `0x36`（绘图开），从不关 | 绘图原语收尾只发 `0x30`，保持绘图开 | 与驱动层 `ClearGDRAM` 的 `0x34+0x30` **有意不同**——清屏关绘图无害，绘图关了图形就消失 |

## 8. 上板排查表

| 症状 | 首查 | 次查 |
|---|---|---|
| 顶部大字位置不对/缺列 | 4 个 `DrawStrip` 的 X 参数是不是 2/3/4/5 | 字模数组是否逐字节照抄（可用解码脚本 diff） |
| 大字显示但叠在文字上 | `ShowHanzi` 行号是否已 +1（第 2~4 行） | `s_home_l1` 是否已从 painter 删除 |
| 进度条长度不对 | `end = curr*128/all - 1`（1/5 = 像素 0~24，比 25.6 知一点是正常的） | `0x80 >> (p & 7)` 的位序（最高位在最左） |
| 页头/进度条/分隔线只在翻页闪一下，字符行正常 | 绘图原语收尾是否误发了 `0x34`（关绘图）——应**只留 `0x30`** | 对照原工程 `GenerateProgressBar1` 结尾停在 `0x36`，图形层须保持开 |
| 分隔线消失或花屏 | `DrawRow` 写地址/数据帧是否完整 | 是否漏了 `delay_init()`（位时序全失效） |
| 光标/编辑正常但行位置不对 | `s_FlushSetRow` 的 `item + 1u` | ST7920 行地址 2/3 交换（0x90 在前） |
| 翻页后旧页大字残留 | 各页顶部区是否都画满 X2~5 + y14 + y15 | 若以后新页图形区形状不同，进页加 `LCD12864_ClearGDRAM()` |

## 9. 约束（与前几轮相同）

- C90（`<uC99>0</uC99>`）：变量全部块首声明；
- `--no-multibyte-chars`：源码/注释全 ASCII；
- 不新增全局状态：本轮 0 个新全局变量（字模都是 `static const` 进 Flash）。

## 10. 验证记录

- host 端 gcc `-std=c90 -Wall -Wextra`：0 error / 0 warning；
- 波形解码行为断言 **41 项，0 失败**：DrawRow/DrawStrip 原语逐帧序列（含 y=0/31、x=16 边界与空指针拒绝）、首页/设置页进页 324 帧预算（收尾只发 `0x30` 后，6 次绘图调用各少 1 帧）、进度条 1/5 与 2/5 的 16 字节逐字节比对、分隔线全亮、4 条竖条内容、文字行地址（0x90/0x88/0x98）、无 DDRAM 行 0 写入、光标移动仍 50 帧 0 帧 GDRAM、翻页回绕恢复；
- 页头字模 5 组 × 24 字节与原工程 `code[]` 表**逐字节 diff 全部 MATCH**（上一轮兑转字的教训：转抄必核验）；
- 代码副本：`C:\Users\JessamynAdmin\AppData\Local\Temp\qibiji_pages_test\`（`lcd12864.c/.h`、`lcd_pages.c`、`test_round4.c`）。

**注意**：`test_pages.c`（第 2 轮快照）与 `test_round3.c`（第 3 轮快照）是当时模块的快照测试，模块演进后部分计数断言不再匹配，属预期——每轮测试只对当轮模块负责，真机回归以上板为准。
