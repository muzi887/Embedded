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

/* ----------------------------- Page ids --------------------------------- */
typedef enum
{
    PAGE_HOME = 0,   /* home: version / motor / opening / flow            */
    PAGE_SET,        /* settings: limits / stall row / calibration row    */
    PAGE_COUNT       /* number of pages, used for wrap-around             */
} page_id_t;

static page_id_t s_page = PAGE_HOME;

/* ---------------- GB2312 text rows  ----------------
 * Each row = 8 DDRAM address cells x 2 bytes = 16 bytes on screen.       */
static const uint8_t s_home_l2[16] = {         /* "motor: 1.6A 12.6V"      */
    0xb5, 0xe7, 0xbb, 0xfa, 0x3a, 0x20,
    0x31, 0x2e, 0x36, 0x41, 0x20, 0x31, 0x32, 0x2e, 0x36, 0x56 };
static const uint8_t s_home_l3[16] = {         /* "opening: 100% quan kai" */
    0xbf, 0xaa, 0xb6, 0xc8, 0x3a, 0x20,
    0x31, 0x30, 0x30, 0x25, 0x20, 0x20, 0xc8, 0xab, 0xbf, 0xaa };
static const uint8_t s_home_l4[16] = {         /* "flow: 0.0 m3/s"         */
    0xc1, 0xf7, 0xc1, 0xbf, 0x3a, 0x20,
    0x20, 0x30, 0x2e, 0x30, 0x20, 0x6d, 0x33, 0x2f, 0x73, 0x20 };

/* ------------------------- Editable parameters ---------------------------
 * RAM */
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
static const uint8_t s_txt_off[4] = {        /* "closed"                 */
    0xb9, 0xd8, 0xb1, 0xd5 };
static const uint8_t s_txt_on[4] = {         /* "open"                   */
    0xbf, 0xaa, 0xc6, 0xf4 };

/* ------------------- Top graphic zone (GDRAM) assets ---------------------
 * Banner glyph bitmaps: Each glyph is 12 rows x 2 bytes, drawn into GDRAM
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

/* byte offset of the edit marker '*' in each row */
static const uint8_t s_star_pos[SET_ITEMS] = { 13u, 14u, 12u };

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
内容:  光标 | 标签(汉字/冒号) | 数值或开/关    | 空     */
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
    case 0u:                                  /* 上限位 up-limit: 4 digits     */
        s_BytesCopy(&row[2], s_lbl_gate, 7u);
        s_U16ToDec4(s_param[LCD_PARAM_GATE_MAX], &row[9]);
        break;
    case 1u:                                  /* 堵转阈值 stall: "dd.d A"        */
        s_BytesCopy(&row[2], s_lbl_stall, 6u);
        v = s_param[LCD_PARAM_STALL_010A];
        row[8]  = '0' + ((v / 100u) % 10u);   // 百位 → 整数十位，% 10u 是防溢出
        row[9]  = '0' + ((v / 10u)  % 10u);   // 十位 → 整数个位
        row[10] = '.';
        row[11] = (uint8_t)('0' + (v % 10u)); // 个位 → 小数位
        row[12] = ' ';
        row[13] = 'A';
        break;
    case 2u:                                  /* 校准开关 calibration on/off     */
    default:
        s_BytesCopy(&row[2], s_lbl_auto, 6u);
        s_BytesCopy(&row[8],
                    (s_param[LCD_PARAM_AUTO_FLAG] != 0u) ? s_txt_on
                                                         : s_txt_off,
                    4u);
        break;
    }

    if ((s_set_mode == SET_EDIT) && (s_cursor == item))
        row[s_star_pos[item]] = '*'; /* 进编辑态时，正在编辑的那行加个 *。 */
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