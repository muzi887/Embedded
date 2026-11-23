#ifndef __GPRS_CONFIG_PARAM_H
#define __GPRS_CONFIG_PARAM_H

#include "stm32f10x.h"   /* u8/u16/u32 */

/* 维护一张“允许平台远程改的参数表”：平台发 key+值 → 查表 → 校验 → 写 EEPROM → 立即生效；
  上电时反过来从 EEPROM 恢复。EEPROM 参数区 150~199（OTA 组 200~243、Marker 250 不可占用）。 */

/* 参数 key（与平台 serviceCode=setConfig 的 body.params 键名一致） */
#define GPRS_CFG_KEY_STALL_MA      "stallMa"
#define GPRS_CFG_KEY_REPORT_MS     "reportCycleMs"
#define GPRS_CFG_KEY_RAIL_LOW      "railLow"

/* 返回码 */
#define GPRS_CFG_OK            0u
#define GPRS_CFG_ERR_KEY       1u   /* 未知 key */
#define GPRS_CFG_ERR_RANGE     2u   /* 越界 */

void     GprsCfg_Init(void);                  /* 上电恢复，在 ADC/DCMOTOR/property_report 初始化之前调 */
uint8_t  GprsCfg_Set(const char *key, int v); /* 校验 + 写 EEPROM + 即时生效 */
uint16_t GprsCfg_Get(const char *key);        /* 当前生效值 */
uint8_t  GprsCfg_BuildAllJson(char *buf, uint32_t buf_size); /* getConfig 用 */

#endif