#include "gprs_config_param.h"
#include "eeprom_drv.h"
#include "adc.h"
#include "property_report.h"
#include <string.h>
#include <stdio.h>

/* ---- EEPROM 槽位 ---- */
#define EE_STALL_MA_VAL     150u
#define EE_REPORT_MS_VAL    152u
#define EE_RAIL_LOW_VAL     154u

/* 哨兵值：EEPROM 空白（0xFFFF）或坏值（0x0000）→ 用默认值。
   三个参数合法范围均不含这两个值，判断可靠。 */
#define EE_SENTINEL_LO      0x0000u
#define EE_SENTINEL_HI      0xFFFFu

typedef struct
{
    const char *key;         /* 平台下发的键名 */
    uint16_t    ee_val;      /* u16 值 EEPROM 槽地址 */
    uint16_t    def_val;     /* 未初始化/EEPROM 异常时的默认值 */
    uint16_t    min;
    uint16_t    max;         /* 合法范围，越界拒收 */
} GprsCfgItem;

static GprsCfgItem s_items[3] =
{
    /* key                    val 地址          默认                           min     max */
    { GPRS_CFG_KEY_STALL_MA,  EE_STALL_MA_VAL,  ADC_STALL_MA_DEFAULT,        1000u,  0xFFFFu },  
    { GPRS_CFG_KEY_REPORT_MS, EE_REPORT_MS_VAL, PROP_REPORT_MIN_INTERVAL_MS, 1000u, 60000u },
    { GPRS_CFG_KEY_RAIL_LOW,  EE_RAIL_LOW_VAL,  ADC_RAIL_LOW,                2u,    100u  },
};

static uint16_t s_cur[3];    /* 当前生效值（内存副本），下标与 s_items 对应 */

/* ---- 即时生效：调各模块的运行时接口 ---- */
static void s_apply(uint8_t idx)
{
    switch (idx)
    {
    case 0u:  ADC_SetStallThresholdMa(s_cur[0]);            break;
    case 1u:  PropertyReport_SetMinIntervalMs(s_cur[1]);    break;
    case 2u:  ADC_SetRailLow(s_cur[2]);                     break;
    default:                                                break;
    }
}

/* ---- 上电恢复 ---- */
void GprsCfg_Init(void)
{
    uint8_t i;

    /* EEPROM 自检失败：全部用默认值，设备照常跑，不要卡启动 */
    if (AT24CXX_Check() == 0u)
    {
        for (i = 0u; i < 3u; i++) { s_cur[i] = s_items[i].def_val; s_apply(i); }
        return;
    }

    for (i = 0u; i < 3u; i++)
    {
        uint16_t stored = (uint16_t)AT24CXX_ReadLenByte(s_items[i].ee_val, 2u);

        /* 哨兵判断：空白/坏值 → 默认值 */
        if ((stored == EE_SENTINEL_LO) || (stored == EE_SENTINEL_HI))
            s_cur[i] = s_items[i].def_val;
        else
            s_cur[i] = stored;

        /* 防御：EEPROM 里存了越界旧值时，按默认值跑 */
        if ((s_cur[i] < s_items[i].min) || (s_cur[i] > s_items[i].max))
            s_cur[i] = s_items[i].def_val;

        s_apply(i);
    }
}

/* --- 查表 --- */
static int s_find(const char *key)
{
    uint8_t i;
    for (i = 0u; i < 3u; i++)
    {
        if (strcmp(s_items[i].key, key) == 0) return (int)i;
    }
    return -1;
}

/* ---- 设置：校验 + 写 EEPROM + 生效 ---- */
uint8_t GprsCfg_Set(const char *key, int v)
{
    /* 查表 */
    int idx = s_find(key);
    GprsCfgItem *it;
    uint16_t val;

    /* 范围校验 */
    if (idx < 0)                 return GPRS_CFG_ERR_KEY;
    if ((v < 0) ||
        ((uint32_t)v > (uint32_t)s_items[idx].max) ||
        ((uint32_t)v < (uint32_t)s_items[idx].min))
        return GPRS_CFG_ERR_RANGE;

    /* 写EEPROM */
    it  = &s_items[idx];
    val = (uint16_t)v;
    /* 先值后标志：即使中途断电，标志未写则下次上电仍走默认值，不会读到半截数据 */
    AT24CXX_WriteLenByte(it->ee_val, (uint32_t)val, 2u);

    /* 生效 */
    s_cur[idx] = val;
    s_apply(idx);
    return GPRS_CFG_OK;
}

/* --- 查单值 --- */
uint16_t GprsCfg_Get(const char *key)
{
    int idx = s_find(key);
    if (idx < 0) return 0u;
    return s_cur[idx];
}

/* ---- getConfig：拼 {"stallMa":..,"reportCycleMs":..,"railLow":..} ---- */
uint8_t GprsCfg_BuildAllJson(char *buf, uint32_t buf_size)
{
    int n;
    n = snprintf(buf, buf_size,
                 "{\"%s\":%u,\"%s\":%u,\"%s\":%u}",
                 s_items[0].key, (unsigned)s_cur[0],
                 s_items[1].key, (unsigned)s_cur[1],
                 s_items[2].key, (unsigned)s_cur[2]);
    if ((n <= 0) || ((uint32_t)n >= buf_size)) return 0u;
    return 1u;
}