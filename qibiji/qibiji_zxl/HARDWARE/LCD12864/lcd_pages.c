/* ===========================================================================
 * LCD pages module - qibiji_zxl (business layer)
 *
 * Owns: page state machine (which page is on screen) and the GB2312 text
 *        rows drawn on each page; set-page parameter editing (browse with
 *        a cursor marker + per-item value edit); a per-row RAM cache so
 *        cursor moves and edits re-send only the affected rows instead of
 *        repainting the whole page.
 * Depends on: key.h (key_code_t), lcd12864.h, dcmotor.h, gprs_config.h,
 *             app_info.h, <string.h>.
 * =========================================================================== */
#include "lcd_pages.h"
#include "lcd12864.h"
#include "dcmotor.h"
#include <string.h>
#include "gprs_config.h"
#include "app_info.h"
#include "gprs_at.h"
#include "gate_io.h"

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

/* ------------------------ 控制页 (PAGE_HOME) ----------------------------
 * 三条整行数据，每条 16 字节 = 8 个 DDRAM 地址单元 x 2 字节。
 * 第 1 屏行留给顶部横幅图形区，故文字从第 2 屏行开始。                    */
/* "电机：1.6A 12.6V" */
static const uint8_t s_home_l2[16] = {
    0xb5, 0xe7, 0xbb, 0xfa, 0x3a, 0x20,
    0x31, 0x2e, 0x36, 0x41, 0x20, 0x31, 0x32, 0x2e, 0x36, 0x56 };
/* "开度：100%  全开" */
static const uint8_t s_home_l3[16] = {
    0xbf, 0xaa, 0xb6, 0xc8, 0x3a, 0x20,
    0x31, 0x30, 0x30, 0x25, 0x20, 0x20, 0xc8, 0xab, 0xbf, 0xaa };
/* "流量： 0.0 m3/s " */
static const uint8_t s_home_l4[16] = {
    0xc1, 0xf7, 0xc1, 0xbf, 0x3a, 0x20,
    0x20, 0x30, 0x2e, 0x30, 0x20, 0x6d, 0x33, 0x2f, 0x73, 0x20 };

/* --------------------- 可编辑参数 (RAM，配置页) -------------------------
 * 下标与 lcd_param_t 一一对应：0=上限位, 1=堵转值(0.1A), 2=校准开关。     */
static uint16_t s_param[LCD_PARAM_COUNT] = { 2000u, 300u, 0u };

/* ---------------- 配置页 (PAGE_SET)：前瞻声明 ---------------------------
 * C90 要求先声明后使用：这两个函数定义在绘制段，按键与公共 API 会调用。  */
static void s_FormatSetRow(uint8_t item);
static void s_FlushSetRow(uint8_t item);

/* ---------------- 配置页 (PAGE_SET)：状态与行缓存 ----------------------- */
#define SET_ITEMS 3u        /* 可编辑项数：上限位 / 堵转 / 校准 */
typedef enum { SET_BROWSE = 0, SET_EDIT } set_mode_t;

/* 浏览态：UP/DOWN 移动光标，参数不动，STOP 进入编辑。
 * 编辑态：光标锁定，UP/DOWN 改当前项的值，STOP 确认，CANCEL 撤销。      */
static set_mode_t s_set_mode = SET_BROWSE;
static uint8_t    s_cursor   = 0;               /* 光标所在项，0..SET_ITEMS-1 */
static uint16_t   s_edit_backup = 0;            /* 进入编辑前的值，供撤销用   */
static uint8_t    s_set_rows[SET_ITEMS][16];    /* 影子缓存：屏上当前三行     */

/* ---------------- 配置页 (PAGE_SET)：文字资源 --------------------------- */
/* 标签与状态文字均为 GB2312 双字节码，由 s_FormatSetRow() 拼进行缓存。    */
static const uint8_t s_lbl_gate[7]  = { 0xc9, 0xcf, 0xcf, 0xde, 0xce, 0xbb, 0x3a }; /* 上限位： */
static const uint8_t s_lbl_stall[6] = { 0xb6, 0xc2, 0xd7, 0xaa, 0x3a, 0x20 };       /* 堵转：   */
static const uint8_t s_lbl_auto[6]  = { 0xd0, 0xa3, 0xd7, 0xbc, 0x3a, 0x20 };       /* 校准：   */
/* 校准状态取自电机真实状态，而非触发标志 */
static const uint8_t s_txt_cal_none[6] = { 0xce, 0xb4, 0xd0, 0xa3, 0xd7, 0xbc };    /* 未校准   */
static const uint8_t s_txt_cal_run[6]  = { 0xd0, 0xa3, 0xd7, 0xbc, 0xd6, 0xd0 };    /* 校准中   */
static const uint8_t s_txt_cal_ok[6]   = { 0xd2, 0xd1, 0xd0, 0xa3, 0xd7, 0xbc };    /* 已校准   */

/* ---- 4G 页 ---- */
static const uint8_t s_lbl_sig[6]   = { 0xd0, 0xc5, 0xba, 0xc5, 0x3a, 0x20 }; /* 信号： */
static const uint8_t s_lbl_ccid[6]  = { 'C', 'C', 'I', 'D', ':', ' ' };
static const uint8_t s_lbl_stat[6]  = { 0xd7, 0xb4, 0xcc, 0xac, 0x3a, 0x20 }; /* 状态： */
static const uint8_t s_txt_online[4]  = { 0xd4, 0xda, 0xcf, 0xdf };           /* 在线   */
static const uint8_t s_txt_offline[4] = { 0xc0, 0xeb, 0xcf, 0xdf };           /* 离线   */
static uint8_t s_g4g_rows[3][16];             /* 4G 页影子缓存：屏上当前三行 */

/* ---- 设备页 ---- */
static const uint8_t s_lbl_num[6] = { 0xb1, 0xe0, 0xba, 0xc5, 0x3a, 0x20 }; /* 编号： */
static const uint8_t s_lbl_ver[6] = { 0xb0, 0xe6, 0xb1, 0xbe, 0x3a, 0x20 }; /* 版本： */
static const uint8_t s_lbl_upg[6] = { 0xc9, 0xfd, 0xbc, 0xb6, 0x3a, 0x20 }; /* 升级： */
static const uint8_t s_txt_no_ota[6] = { 0xce, 0xb4, 0xc6, 0xf4, 0xd3, 0xc3 }; /* 未启用 */

/* ---- 限位页 ---- */
static const uint8_t s_op_pair[16] = {      /* "上限位  下限位" */
    0xc9, 0xcf, 0xcf, 0xde, 0xce, 0xbb,
    0x20, 0x20, 0x20, 0x20,
    0xcf, 0xc2, 0xcf, 0xde, 0xce, 0xbb };

/* ------------------- 配置页 (PAGE_SET)：参数表（表驱动） -----------------
 * 一项参数的全部差异集中在一行：标签、'*' 列、上限、步进、显示格式。
 * 下标与 lcd_param_t 一一对应（0=上限位, 1=堵转, 2=校准）。
 * 新增一项：改这里 + lcd_param_t + s_param 初值，不再散落多处。         */
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

/* ------------------ 顶部图形区（GDRAM）资源 -----------------------------
 * 横幅字模：每个字 12 行 x 2 字节，经 LCD12864_DrawStrip() 画入 GDRAM
 * y=1..12，横向占一个 word 列。                                          */
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

static uint8_t s_RowEqual(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;
    for (i = 0u; i < 16u; i++)
    {
        if (a[i] != b[i])
            return 0u;
    }
    return 1u;
}

/**  Format one set-page row into the RAM cache (no LCD traffic here).
 * 第 item 项参数拼成一行 16 字节，写进 RAM 缓存 s_set_rows[item]
* 格子:  0  1  | 2...........7 | 8..........14 | 15
* 内容:  光标 | 标签(汉字/冒号) | 数值或开/关    | 空
* 标签、'*' 列、数值格式全部取自 s_items[item]。        */
static void s_FormatSetRow(uint8_t item)
{
    const set_item_t *it = &s_items[item];
    uint8_t *row = s_set_rows[item];
    uint16_t v;

    s_RowClear(row);          /* 16 字节全填空格，保证每次重画不残留旧字符 */
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
    /* 两个指针，指向当前页要显示的两个汉字点阵（各 24 字节） */
    const uint8_t *g0;
    const uint8_t *g1;

    switch (page)
    {
    case PAGE_SET: g0 = s_banner_cfg0; g1 = s_banner_cfg1; break;
    case PAGE_G4G: g0 = s_banner_net0; g1 = s_banner_net1; break;
    case PAGE_DEV: g0 = s_banner_ota0; g1 = s_banner_ota1; break;
    case PAGE_OP:  g0 = s_banner_lim0; g1 = s_banner_lim1; break;
    case PAGE_HOME:
    default:       g0 = s_banner_ctl0; g1 = s_banner_ctl1; break;
    }

    LCD12864_DrawStrip(2u, s_banner_blank);
    LCD12864_DrawStrip(3u, g0);
    LCD12864_DrawStrip(4u, g1);
    LCD12864_DrawStrip(5u, s_banner_blank);
    LCD12864_DrawRow(15u, s_line_full);                 /* 分隔线，128 像素全亮 */
    s_ProgressDraw((uint8_t)((int)page + 1), 5u);   /* 1/5 .. 5/5 */
}

/* Send one cached row 缓存行 to the LCD. DDRAM row = item + 1: row 0 pixel area
 * is occupied by the top graphic zone (banner/bar/line), so the three
 * editable items live on DDRAM rows 0x90/0x88/0x98 (screen lines 2..4). */
static void s_FlushSetRow(uint8_t item)
{
    LCD12864_ShowHanzi((uint8_t)(item + 1u), s_set_rows[item], 8u);
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

/* ---- 配置页 ---- */
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

/* ---- 4G 页 ---- */
/* 取 ICCID 后 10 位 */
static void s_CcidTail10(uint8_t *dst)
{
    const char *id = gprs_get_iccid();
    uint8_t len = 0u;
    uint8_t i;

    while (id[len] != '\0' && len < 20u)
        len++;
    for (i = 0u; i < 10u; i++)
    {
        uint8_t from = (uint8_t)((len > 10u) ? (len - 10u + i) : i);
        dst[i] = (from < len) ? (uint8_t)id[from] : (uint8_t)' ';
    }
}

/* 拼一行，不碰 LCD */
static void s_FormatG4gRow(uint8_t item, uint8_t *row)
{
    int8_t rssi;

    s_RowClear(row);
    switch (item)
    {
    case 0u:                                  /* 信号：22 / -- */
        s_BytesCopy(row, s_lbl_sig, 6u);
        rssi = gprs_get_rssi();
        if (rssi < 0)
        {
            row[6] = '-';
            row[7] = '-';
        }
        else
        {
            row[6] = (rssi < 10) ? (uint8_t)' '
                                 : (uint8_t)('0' + ((rssi / 10) % 10));
            row[7] = (uint8_t)('0' + (rssi % 10));   /* 个位数右对齐 */
        }
        break;

    case 1u:                                  /* CCID：后 10 位 */
        s_BytesCopy(row, s_lbl_ccid, 6u);
        s_CcidTail10(&row[6]);
        break;

    default:                                  /* 状态：在线 / 离线 */
        s_BytesCopy(row, s_lbl_stat, 6u);
        if (gprs_is_ready())
            s_BytesCopy(&row[6], s_txt_online, 4u);
        else
            s_BytesCopy(&row[6], s_txt_offline, 4u);
        break;
    }
}

static void s_FlushG4gRow(uint8_t item)
{
    LCD12864_ShowHanzi((uint8_t)(item + 1u), s_g4g_rows[item], 8u);
}

/* 整页重绘 */
static void s_G4gPageDrawAll(void)
{
    uint8_t i;

    LCD12864_Clear();
    for (i = 0u; i < 3u; i++)
    {
        s_FormatG4gRow(i, s_g4g_rows[i]);
        s_FlushG4gRow(i);
    }
    s_DrawTopZone(PAGE_G4G);
}

/* 只重发变化的行 */
static void s_G4gPageRefresh(void)
{
    uint8_t tmp[16];
    uint8_t i;

    for (i = 0u; i < 3u; i++)
    {
        s_FormatG4gRow(i, tmp);
        if (!s_RowEqual(tmp, s_g4g_rows[i]))
        {
            s_BytesCopy(s_g4g_rows[i], tmp, 16u);
            s_FlushG4gRow(i);
        }
    }
}

/* ---- 设备页 ---- */
static void s_DrawLeftString(uint8_t *row, const char *s)
{
    uint8_t i = 0u;
    while (s[i] != '\0' && i < 10u)
    {
        row[6u + i] = (uint8_t)s[i];
        i++;
    }
}

static void s_DevPageDrawAll(void)
{
    uint8_t row[16];

    LCD12864_Clear();

    s_RowClear(row);                       /* 编号：dev001 */
    s_BytesCopy(row, s_lbl_num, 6u);
    s_DrawLeftString(row, GPRS_DEVICE_KEY);
    LCD12864_ShowHanzi(1u, row, 8u);

    s_RowClear(row);                       /* 版本：V1.0.0 */
    s_BytesCopy(row, s_lbl_ver, 6u);
    s_DrawLeftString(row, APP_FW_VERSION);
    LCD12864_ShowHanzi(2u, row, 8u);

    s_RowClear(row);                       /* 升级：未启用 */
    s_BytesCopy(row, s_lbl_upg, 6u);
    s_BytesCopy(&row[6], s_txt_no_ota, 6u);
    LCD12864_ShowHanzi(3u, row, 8u);

    s_DrawTopZone(PAGE_DEV);
}

/* ---- 限位页 ---- */
/* 反白区域：文字在第 3 屏行（像素行 32..47）
 *   上限位 = word 0..2（像素列 0..47）
 *   下限位 = word 5..7（像素列 80..127）
 * GDRAM 与 DDRAM 是 XOR 叠加，铺 0xFF 即"白底黑字"反白，铺 0x00 恢复正常。 */
#define OP_ROW_Y      32u
#define OP_ROW_H      16u
#define OP_OPEN_WORD  0u
#define OP_CLOSE_WORD 5u
#define OP_WORDS      3u

static void s_OpPageHighlight(void)
{
    uint8_t open_on  = GATEIO_IsOpenLimit();
    uint8_t close_on = GATEIO_IsCloseLimit();

    /* 两个区域都必须写：点亮的铺 0xFF，未点亮的铺 0x00（清掉上一次的残留） */
    LCD12864_FillBlock(OP_ROW_Y, OP_ROW_H, OP_OPEN_WORD, OP_WORDS,
                       open_on ? 0xFFu : 0x00u);
    LCD12864_FillBlock(OP_ROW_Y, OP_ROW_H, OP_CLOSE_WORD, OP_WORDS,
                       close_on ? 0xFFu : 0x00u);
}

/* 清掉两块反白区域（0x00 = 不做 XOR 反相）。翻页离开限位页时必须调用，
 * 否则这块 GDRAM 会一直和后续页面的 DDRAM 文字 XOR，把别的页文字反相。 */
static void s_OpPageClearBlocks(void)
{
    LCD12864_FillBlock(OP_ROW_Y, OP_ROW_H, OP_OPEN_WORD, OP_WORDS, 0x00u);
    LCD12864_FillBlock(OP_ROW_Y, OP_ROW_H, OP_CLOSE_WORD, OP_WORDS, 0x00u);
}

static void s_OpPageDrawAll(void)
{
    LCD12864_Clear();          /* 只清 DDRAM，GDRAM 靠下面两块 0x00 兜底 */
    LCD12864_ShowHanzi(2u, s_op_pair, 8u);
    s_DrawTopZone(PAGE_OP);
    s_OpPageHighlight();
}

/* ---- 分发 ---- */
static void s_DrawPage(page_id_t page)
{
    switch (page)
    {
    case PAGE_SET:
        s_set_mode = SET_BROWSE;
        s_cursor   = 0u;
        s_SetPageDrawAll();
        break;
    case PAGE_G4G:
        s_G4gPageDrawAll();
        break;
    case PAGE_DEV:
        s_DevPageDrawAll();
        break;
    case PAGE_OP:
        s_OpPageDrawAll();
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

    case KEY_STOP:                            /* confirm and stay: 退出编辑态，重画本行（去掉 *），值保留  */
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

/* 光标上下移一项 */
static void s_SetMoveCursor(uint8_t up)
{
    uint8_t old = s_cursor;

    s_cursor = up ? (s_cursor + SET_ITEMS - 1u) % SET_ITEMS   /* 上移：等效 -1，绕回末项 */
              : (s_cursor + 1u) % SET_ITEMS;                  /* 下移：+1，绕回首项 */
    s_FormatSetRow(old);
    s_FlushSetRow(old);
    s_FormatSetRow(s_cursor);
    s_FlushSetRow(s_cursor);                  /* 2 rows  */
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
    static uint8_t s_last_limit = 0xFFu;
    uint8_t st;

    switch (s_page)
    {
    case PAGE_SET:
        if (s_set_mode == SET_EDIT)
            return;                       /* 编辑时由按键处理负责这一行 */
        if (DCMOTOR_IsCalibrating())
            st = 1u;
        else if (DCMOTOR_IsCalibrated())
            st = 2u;
        else
            st = 0u;
        if (st == s_last_cal_state)
            return;
        s_last_cal_state = st;
        s_FormatSetRow(2u);
        s_FlushSetRow(2u);
        break;

    case PAGE_G4G:
        s_G4gPageRefresh();
        break;

    case PAGE_OP:
        st = (uint8_t)(GATEIO_IsOpenLimit() ? 1u : (GATEIO_IsCloseLimit() ? 2u : 0u));
        if (st == s_last_limit)
            return;
        s_last_limit = st;
        s_OpPageHighlight();
        break;

    case PAGE_DEV:
    case PAGE_HOME:
    default:
        break;                            /* 无动态内容 */
    }
}

uint8_t LCD_PAGES_IsHome(void)
{
    return (s_page == PAGE_HOME) ? 1u : 0u;
}

