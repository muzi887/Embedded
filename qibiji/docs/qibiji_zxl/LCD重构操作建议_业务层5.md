# LCD 重构操作建议 —— 业务层第 5 轮：补齐控制/配置之外的三个页面（4G / 设备 / 限位）

> 第 1~4 轮把「控制页 + 配置页」做完了，路线图当时只覆盖 2 页。原工程共 5 页（首页/设置/4G/设备/操作），本轮把剩下 3 页按现有模块风格补齐，页序、顶部大字、进度条段位与原工程一致。

## 1. 结论与范围

### 1.1 这轮做什么

- 新增 3 个页面 id：`PAGE_G4G`（4G）、`PAGE_DEV`（设备/OTA）、`PAGE_OP`（操作/限位）；`PAGE_COUNT` 由 2 变 5，页序固定为 0 控制 / 1 配置 / 2 4G / 3 设备 / 4 限位；
- 顶部图形区按页换字：`网络`、`OTA `、`限位`，进度条分别为 3/5、4/5、5/5（`page+1` 直接算得，与原有 1/5、2/5 同一条公式）；
- 三页都是**只读页**：翻页键沿用现有语义（SET 上一页 / CANCEL 下一页 / STOP 回首页），UP/DOWN 在本轮不绑定动作；
- 4G 页与限位页有**无按键也会变的动态内容**，接入现有的 `LCD_PAGES_Refresh()`，只重发变化的那一行（25 帧），不整页重画。

### 1.2 这轮不做什么

- **不做 OTA 落地**：zxl 没有 OTA 模块，"升级"一行固定显示"未启用"，不伪造状态；
- **不做远程下发**：4G 平台的参数下发链路属于 GPRS/应用层，不在本轮；
- **不做限位页的手动开/关闸**：原工程这一页也只是显示，不响应 UP/DOWN。若要让这一页能手动控制电机，是独立一轮（涉及 GATEIO/DCMOTOR 联锁，不能顺手加）；
- **驱动层只加一个原语**：为做反白新增 `LCD12864_FillBlock()`，其余驱动接口零改动。

### 1.3 数据源现状（先看这张表，决定每页能不能落地）

| 页面 | 显示行 | 数据 | zxl 现状 | 本轮处理 |
|---|---|---|---|---|
| 4G | 信号 | CSQ 的 rssi 数值 | **只有 `g_at_flag_csq` 布尔标志**，rssi 在 `s_csq_signal_ok()` 里解析完即丢弃（gprs_at.c:139-161） | 前置改动：补 `gprs_get_rssi()` 存取器 |
| 4G | CCID | SIM 卡号后 10 位 | **只有 `g_at_flag_iccid` 标志**，回包原文只 `GPRS_Printf` 打印，未存字符串（gprs_at.c:119-125） | 前置改动：补 `gprs_get_iccid()` 存取器 |
| 4G | 状态 | MQTT 是否在线 | `gprs_is_ready()` 现成（进 ST_NORMAL 返回 1） | 直接调用 |
| 设备 | 编号 | 设备 key | `GPRS_DEVICE_KEY`（gprs_config.h:18）现成，编译期常量 `"dev001"` | 直接引用 |
| 设备 | 版本 | 固件版本 | **工程里没有版本串** | 新增 2 行宏（见 §2.3） |
| 设备 | 升级 | OTA 开关 | **没有 OTA 模块** | 固定显示"未启用" |
| 限位 | 上/下限位 | 限位开关状态 | `GATEIO_IsOpenLimit()` / `GATEIO_IsCloseLimit()` 现成（gate_io.h:51-52，已去抖） | 直接调用 |

结论：**限位页现在就能做**；**4G 页需要先补两个 GPRS 存取器**（改动约 25 行，只暴露已有数据，不改状态机）；**设备页**用编译期常量 + 新增版本宏即可，OTA 一行如实显示"未启用"。

### 1.4 与原工程的对应关系

| 项 | 原工程 | 本轮 | 说明 |
|---|---|---|---|
| 4G 页绘制 | `G4gpage()`（Dispaly.c:724-751） | `s_G4gPageDrawAll()` + 3 个行格式化 | 三行内容、顺序一致 |
| 设备页绘制 | `Devicepage()`（:754-785） | `s_DevPageDrawAll()` | 原页是 OTA 参数页，本轮降级为设备信息页 |
| 限位页绘制 | `OperatePage()`（:788-847），`WRGDRAM128X3` 反白 | `s_OpPageDrawAll()` + `LCD12864_FillBlock()` 反白 | 同源，见 §1.5 |
| 顶部大字 | `WRGDRAM(3/4, code[11/12])` 等 | `s_DrawTopZone()` 按页换字模 | 字模逐字节搬自原 `code[]` 表 |
| 动态刷新 | `LcdDisplay()` 按页检测 + 500ms 兜底 | `LCD_PAGES_Refresh()` 按页比对 | 只发变化行 |

### 1.5 限位反白的原理（必须理解，否则会写错）

原工程用 `WRGDRAM128X3(8, 0, 0xff)` 给"上限位/下限位"做反白。它的做法是：**在文字所在的 GDRAM 区域填 0xFF**。

关键在于 ST7920 的显示叠加方式：**DDRAM（字符）与 GDRAM（图形）是按位 XOR 叠加的**，不是 OR。

```
显示像素 = DDRAM像素 XOR GDRAM像素
```

于是：

| DDRAM（字形） | GDRAM | 显示 |
|---|---|---|
| 1（笔画） | 1（填充） | 0 → 黑 |
| 0（背景） | 1（填充） | 1 → 白 |
| 任意 | 0（不填） | 原样 |

所以在字形区域铺一层 0xFF，就会得到"白底黑字"的真反白；铺 0x00 则恢复原样。这也是原工程"选中反白 / 未选中清 0"的写法由来。

**地址换算（zxl 驱动必须处理的一点）**：ST7920 的 GDRAM 每个垂直地址管两行——水平地址 `h=0..7` 对应像素行 `v`，`h=8..15` 对应像素行 `v+32`。原工程写的 `y1=0..15` + `x1=8/13`，换算成像素坐标就是：

- 上限位：像素行 32..47，像素列 0..47（word 0..2）
- 下限位：像素行 32..47，像素列 80..127（word 5..7）

因为文字在第 3 屏行（DDRAM `0x88` → 像素行 32..47），三个汉字 48px，"上限位"占 0..47，两个全角空格占 48..79，"下限位"占 80..127。

zxl 驱动现有的 `LCD12864_DrawRow(y, buf)` 固定在 `h=0`、`y<32`，**够不到下半屏**，所以需要补一个能指定上下半区的填充原语（见 §2.4）。

## 2. 前置改动（先做，再动 lcd_pages.c）

### 2.1 `gprs_at.h` 加两个声明

```c
/* ---- display accessors (LCD 4G page) ---- */
/* Last parsed rssi: 0..31 valid, -1 = unknown / not yet measured.
 * 99 (not detectable) is NOT reported as 99, it keeps the previous value. */
int8_t gprs_get_rssi(void);
/* Last parsed ICCID digits (no prefix/quotes), "" if never received. */
const char *gprs_get_iccid(void);
```

### 2.2 `gprs_at.c` 三处小改

**（1）新增两个静态存储**（放在现有 `s_csq_tick` 一带）：

```c
static int8_t s_rssi = -1;      /* -1 = 还没测到有效值 */
static char   s_iccid[21];      /* 20 位数字 + '\0'    */
```

**（2）把 `s_csq_signal_ok()` 改成带回填的解析**（原函数只返回布尔，数值丢弃）：

```c
/* 解析 +CSQ 回包。*rssi_out 回填原始数值（含 99），返回值语义不变：
 * 1 = 信号可用（1..31），0 = 无信号或解析失败。 */
static uint8_t s_csq_parse(const char *data, uint8_t *rssi_out)
{
    const char *p = strstr(data, "+CSQ:");
    uint16_t v = 0u;
    uint8_t  digits = 0u;

    if (p == NULL)
        return 0u;
    p += 5;
    while (*p == ' ')
        p++;
    while (*p >= '0' && *p <= '9')
    {
        v = (uint16_t)(v * 10u + (uint16_t)(*p - '0'));
        p++;
        digits++;
        if (digits > 3u)
            return 0u;
    }
    if (digits == 0u || v > 99u)
        return 0u;
    if (rssi_out != NULL)
        *rssi_out = (uint8_t)v;
    return (v >= 1u && v <= 31u) ? 1u : 0u;
}

static void s_on_csq(char *data, uint16_t len)
{
    if (strstr(data, "+CSQ") != NULL)          /* 只处理确实是 CSQ 回包的帧 */
    {
        uint8_t v = 0u;
        g_at_flag_csq = s_csq_parse(data, &v) ? 1u : 0u;
        if (v >= 1u && v <= 31u)
            s_rssi = (int8_t)v;                /* 99 不覆盖上次有效值 */
    }
    GPRS_Printf("[GPRS] CSQ: %.*s\r\n", (int)len, data);
}
```

**（3）ICCID 提取 + 两个 getter**：

```c
/* "+ICCID: 8986..." 里取数字，最多 20 位 */
static void s_iccid_capture(const char *data)
{
    const char *p = strstr(data, "+ICCID:");
    uint8_t n = 0u;

    if (p == NULL)
        return;
    p += 7;
    while (*p == ' ' || *p == '"')
        p++;
    while (*p >= '0' && *p <= '9' && n < 20u)
    {
        s_iccid[n++] = *p++;
    }
    s_iccid[n] = '\0';
}

int8_t gprs_get_rssi(void)
{
    return s_rssi;
}

const char *gprs_get_iccid(void)
{
    return s_iccid;
}
```

`s_on_iccid()` 里在置标志前调用一次 `s_iccid_capture(data);`。

> 注意：`s_iccid` 是 `char[21]`，初值全 0，未收到时 `gprs_get_iccid()[0] == '\0'`，显示层据此填空格——不需要额外"是否收到"标志。

### 2.3 固件版本宏

新增 `USER/app_info.h`（只有两行，供 LCD 与后续 MQTT 上报共用）：

```c
#ifndef __APP_INFO_H
#define __APP_INFO_H

#define APP_FW_VERSION   "V1.0.0"

#endif
```

> 如果没有"后续上报也要用"的需求，也可以把这一行宏直接写在 `lcd_pages.c` 顶部；单独成文件是为了不让固件版本分散在显示模块里。

### 2.4 驱动层新增填充原语（反白必需）

`lcd12864.h` 加声明：

```c
/* Fill a rectangular band of whole 16-pixel word columns with a constant byte.
 *   y      : first pixel row, 0..63
 *   rows   : band height in pixel rows
 *   x_word : first 16-pixel word column, 0..7 (within the chosen half)
 *   words  : number of word columns, 1..8
 *   value  : byte written to every word (0xFF = fill, 0x00 = clear)
 * 说明：ST7920 一个垂直地址管两行，h=0..7 → 像素行 v，h=8..15 → 像素行 v+32；
 *       本函数按像素行入参，内部自动加上半区偏移。 */
void LCD12864_FillBlock(uint8_t y, uint8_t rows,
                        uint8_t x_word, uint8_t words, uint8_t value);
```

`lcd12864.c` 实现（沿用文件里已有的 `s_WriteCmd` / `s_WriteData` 和 `LCD_CMD_EXT` / `LCD_CMD_GDRAM` / `LCD_CMD_BASIC` 宏名）：

```c
void LCD12864_FillBlock(uint8_t y, uint8_t rows,
                        uint8_t x_word, uint8_t words, uint8_t value)
{
    uint8_t j;
    uint8_t i;
    uint8_t py;
    uint8_t v;
    uint8_t h;

    if (rows == 0u || words == 0u || words > 8u || x_word > 7u)
        return;
    if ((uint16_t)y + (uint16_t)rows > 64u)
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);

    for (j = 0u; j < rows; j++)
    {
        py = (uint8_t)(y + j);
        v  = (uint8_t)(py & 0x1Fu);
        h  = (uint8_t)(x_word + ((py >= 32u) ? 8u : 0u));

        s_WriteCmd((uint8_t)(0x80u + v));
        s_WriteCmd((uint8_t)(0x80u + h));

        for (i = 0u; i < words; i++)
        {
            s_WriteData(value);   /* 每个 word 16 位 = 16 像素宽 */
            s_WriteData(value);
        }
    }

    s_WriteCmd(LCD_CMD_BASIC);
}
```

> 注意三点：
> 1. 每次换行都要重发垂直 + 水平地址，ST7920 的列地址不会跨行自动回卷；
> 2. `h` 超过 15 会写错区域，所以 `x_word` 限定 0..7、下半区由函数内部 +8；
> 3. 函数结束时只能发 `0x30`（基本指令集），**绝不能发 `0x34`**——0x34 会关闭图形层，顶部大字、进度条、反白块会一起消失（源码 `DrawRow` / `DrawStrip` 结尾处有同样的警告注释）。
>
> 代价：一次 16 行 × 3 word = 48 个 word = 96 字节，约 96×8 = 768 个数据帧。只在进页和限位跳变时触发，可以接受。

## 3. `lcd_pages.h` 改动

只需要在末尾加一个说明性注释即可（对外接口不变，外部仍然只喂按键 + 调 Refresh）。若希望外部能判断"当前是不是 4G 页"（例如以后按页限制按键），再按 `LCD_PAGES_IsHome()` 的样子加 `LCD_PAGES_IsPage()`；本轮不需要。

## 4. `lcd_pages.c` 改动

### 4.1 页面枚举与头注释

```c
typedef enum
{
    PAGE_HOME = 0,   /* 控制: version? no - motor / opening / flow     */
    PAGE_SET,        /* 配置: limits / stall / calibration             */
    PAGE_G4G,        /* 4G:   signal / CCID / link state               */
    PAGE_DEV,        /* 设备: device id / version / upgrade            */
    PAGE_OP,         /* 限位: open limit / close limit highlight       */
    PAGE_COUNT
} page_id_t;
```

### 4.2 新增字模（逐字节搬自原 `Dispaly.c` 的 `code[]`，不要重新取模）

```c
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
```

### 4.3 行模板（GB2312，取自原工程数组）

```c
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
```

### 4.4 顶部图形区按页换字

```c
static void s_DrawTopZone(page_id_t page)
{
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
    LCD12864_DrawRow(15u, s_line_full);
    s_ProgressDraw((uint8_t)((int)page + 1), 5u);   /* 1/5 .. 5/5 */
}
```

> 进度条从"页号判断"改成 `page+1`：原来写的是 `(page == PAGE_SET) ? 2 : 1`，现在 5 页正好对应 1..5，一行公式覆盖全部。

### 4.5 4G 页

```c
static uint8_t s_g4g_rows[3][16];

/* 从 ICCID 数字串里取末尾 10 位（原工程 ccid[6..15]），不足则右侧留空 */
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
```

4G 页刷新（变化行才发）：

```c
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
```

`s_RowEqual`（放 `s_BytesCopy` 旁边，避免为 16 字节比较引入 `<string.h>`）：

```c
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
```

### 4.6 设备页（静态，进页画一次）

```c
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
```

需在文件头加 `#include "gprs_config.h"`（取 `GPRS_DEVICE_KEY`）和 `#include "app_info.h"`（取 `APP_FW_VERSION`）。

> 边界说明：LCD 为了显示设备编号去读 GPRS 的编译期常量，属于"读一个常量"，没有调用 GPRS 运行时接口，不构成模块环依赖。若不喜欢，把这个值也搬到 `app_info.h`（`APP_DEVICE_KEY`），LCD 只依赖 `app_info.h`——这更干净，推荐后者。

### 4.7 限位页（反白）

```c
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
```

要点：

- **`LCD12864_Clear()` 只清 DDRAM，不清 GDRAM**（源码 lcd12864.c:92-98，GDRAM 只有单独的 `LCD12864_ClearGDRAM()` 才清）。所以反白块不会因翻页或 `Clear` 自动消失，必须显式写 0x00；
- **翻页前必须清**：`s_DrawPage()` 入口统一调 `s_OpPageClearBlocks()`（见 §4.8），否则离开限位页后，这块 GDRAM 会继续和其它页第 3 行的 DDRAM 文字 XOR，把整段文字反相——这是本方案最容易漏的一个坑；
- 上限位与下限位同时为真（限位开关故障或双压）时，两块都会反白——如实反映硬件状态，不做"只显示一个"的美化；
- `s_DrawTopZone()` 与填块区域（y=32..47）不重叠，顺序无所谓；先字后块更直观。

### 4.8 分发与刷新

```c
static void s_DrawPage(page_id_t page)
{
    /* 每次切页先清掉限位页的 GDRAM 反白块：Clear() 不清 GDRAM，
     * 不清就会残留到新页面的第 3 行文字上（XOR 反相）。 */
    s_OpPageClearBlocks();

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
```

`LCD_PAGES_Refresh()` 去掉"非配置页直接 return"，改为按页分发：

```c
void LCD_PAGES_Refresh(void)
{
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
```

注意：`s_last_cal_state` / `s_last_limit` 都是 `static` 初值 `0xFF`，保证"进页第一次刷新必定发一次"。翻页回来时若值没变则不重发——此时整页刚被 `s_DrawPage()` 重画，内容已是最新，不会漏。

## 5. 行为规格

### 5.1 翻页

| 按键 | 行为 |
|---|---|
| SET | 上一页，0 再减回到 4（环绕） |
| CANCEL | 下一页，4 再加回到 0（环绕） |
| STOP | 回控制页（页 0） |
| UP / DOWN | 三页新页面上无动作；配置页内行为不变 |

### 5.2 刷新

| 页面 | 触发刷新的事件 | 每次代价 |
|---|---|---|
| 4G | rssi 变化 / ICCID 首次到达 / 在线状态翻转 | 每变 1 行重发 25 帧 |
| 设备 | 无（进页画一次） | 0 |
| 限位 | 上/下限位翻转 | 2 块 × 16 行 × 3 word = 192 字节 ≈ 1536 帧（两块都要写） |
| 配置 | 校准状态变化（原有） | 25 帧 |

进页整页绘制：Clear + 3 行（75）+ 顶部区（4 条竖条 + 分隔线 + 进度条）≈ 324 帧，与原两页同一量级，只在翻页瞬间发生一次。

### 5.3 4G 页显示规则（明确边界，避免现场误读）

- rssi 未测到：显示 `--`，**不显示 0**。0 在 3GPP 里表示 ≤-113dBm，与"还没测"不是一回事；
- rssi = 99（不可测）：不覆盖上一次有效值，页面继续显示上次的值。这与 `g_at_flag_csq` 的语义一致（99 → 标志清零），显示层不额外判断；
- ICCID 未收到：整段留空格；
- 状态行只认 `gprs_is_ready()`（ST_NORMAL），不认 `g_atflag_online`——后者在模块 banner 出现时就为 1，只能说明串口通了。

## 6. 与其他模块的边界

| 模块 | 关系 |
|---|---|
| GPRS | 本轮向 GPRS **只增加两个只读存取器**，不改状态机、不改 AT 流程、不改 `g_at_flag_*` 语义 |
| GATEIO | 只调用已去抖的 `GATEIO_IsOpenLimit/IsCloseLimit`，不在 LCD 里再判电平、不重复去抖 |
| DCMOTOR | 本轮不新增调用；配置页原有的 `DCMOTOR_IsCalibrating/IsCalibrated` 不变 |
| KEY | 不变，仍由 `APP_DispatchKey()` 统一喂 `LCD_PAGES_Process()` |
| 驱动层 | 只新增 `LCD12864_FillBlock()`（§2.4），其余接口与现有调用零改动 |

## 7. 实施步骤

1. 改 `gprs_at.h` / `gprs_at.c`（§2.1、§2.2），编译确认无警告；串口日志里 `[GPRS] CSQ:` 行为不变；
2. 新建 `USER/app_info.h`（§2.3），把 `APP_FW_VERSION`（和推荐的 `APP_DEVICE_KEY`）填好；
3. 驱动层加 `LCD12864_FillBlock()`（§2.4）并**先单独验证**：进限位页前手动调一次 `LCD12864_FillBlock(32,16,0,3,0xFF)`，确认顶部大字还在、反白块出现且字形可辨（这一步验证 XOR 与地址换算，跑不通就先别往下做）；
4. 改 `lcd_pages.c`：枚举 → 字模 → 行模板 → `s_DrawTopZone` → 三个绘制函数 → `s_DrawPage` → `LCD_PAGES_Refresh`（按 §4 顺序，每步都能单独编译）；
5. Rebuild 整个工程，确认没有未定义符号（字模数组名、getter 名要一致）；
6. 上板按 §8 逐项验证。

## 8. 上板验证清单

1. **翻页**：控制 → CANCEL → 配置 → CANCEL → 4G → CANCEL → 设备 → CANCEL → 限位 → CANCEL → 回到控制；反向用 SET 同样能绕回。每页顶部大字分别为 控制/配置/网络/OTA/限位，进度条 1/5…5/5。
2. **4G 页信号**：模块未测到 CSQ 时显示 `信号：--`；打印出 `[GPRS] CSQ: +CSQ: 22,99` 后，页面上应变成 `信号：22`（若当时正在 4G 页，2 秒内自动更新，不需要按键）。
3. **4G 页 CCID**：`[GPRS] ICCID:` 日志出现后，页面显示卡号后 10 位；改卡重启应跟着变。
4. **4G 页状态**：断网（拔天线）→ `状态：离线`；恢复进 NORMAL → `状态：在线`，LINKA 引脚与页面一致。
5. **设备页**：编号 = `GPRS_DEVICE_KEY` 的值；版本 = `APP_FW_VERSION`；升级 = `未启用`（原工程这里是 OTA 开关，本轮明确不实现）。
6. **限位页反白**：压上限位 → 三个字"上限位"白底黑字，"下限位"正常黑底白字；压下限位反之；松开后两者都恢复；
7. **反白无残留**：从上限位直接切到下限位，上限位必须恢复原样（验证 `FillBlock` 写 0x00 生效，而不是只写亮块）；
8. **跨页无残留**：压住上限位时直接 CANCEL 翻到控制页/配置页，那两页第 3 行文字必须正常，不能出现反相白块（验证 `s_DrawPage()` 入口的 `s_OpPageClearBlocks()`）；
9. **文字不被吃掉**：反白区域内的字形仍然清晰可辨（验证 GDRAM 与 DDRAM 确实是 XOR，而不是 OR——若变成整块白条，说明叠加方式判断错了，见 §1.5）；
10. **翻页回归**：连续翻页两圈，顶部大字与进度条不残留旧页内容；
11. **功能回归**：配置页的编辑/确认/取消、背光 30 秒熄屏、首页显示全部不受影响。

## 9. 待确认项

1. **反白叠加方式**：实现依赖 ST7920 "DDRAM XOR GDRAM" 的叠加特性，原工程用同一手法且实物验证过。但若换屏或换初始化序列（例如关掉了绘图显示 `0x36`），反白会失效或变成白条——上板第一步先做 §8 第 9 项确认；
2. **4G 页 CCID 位数**：原工程显示后 10 位。若现场卡号是 20 位且希望显示完整，需把行长改成 2 行或缩到后 6 位；
3. **设备页"升级"行**：当前固定"未启用"。若后续移植 OTA，这一行改为读 OTA 状态即可，页面结构不用动；
4. **限位页是否要能手动开/关闸**：原工程不做，本轮也不做。若要加，涉及 DCMOTOR 联锁与 GATEIO 状态，不能只改显示层。
