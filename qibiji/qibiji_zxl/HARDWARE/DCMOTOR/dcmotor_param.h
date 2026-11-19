#ifndef __DCMOTOR_PARAM_H
#define __DCMOTOR_PARAM_H

#include "stm32f10x.h"

#define EEPROM_ADDR_PARAM_VERSION     0x000Au   /* 参数格式版本号，1 字节（空闲区） */
#define DCMOTOR_PARAM_VERSION         0x02u     /* 当前格式版本；旧固件此处为 0xFF */

/* ---- EEPROM 地址（字节地址，对应原工程 reg.h 地址 map） ---- */
#define EEPROM_ADDR_MOTOR_WARN_CURR   0x0004u   /* u16, mA */
#define EEPROM_ADDR_GATE_MAXHEIGHT    0x0006u   /* u16, mm */
#define EEPROM_ADDR_GATE_MAXTURNS     0x0008u   /* u16, 圈数 */

/* ---- 默认值 ---- */
#define DCMOTOR_DEFAULT_WARN_CURR     3500u     /* 3.5A */
#define DCMOTOR_DEFAULT_MAXHEIGHT     1000u     /* 1000mm */
#define DCMOTOR_DEFAULT_MAXTURNS      80u

/* ---- RAM 变量（由 DCMOTOR 模块直接 extern 引用或通过 Get 接口读） ---- */
extern uint16_t g_gate_maxheight;
extern uint16_t g_motor_warning_curr_upper;
extern uint16_t g_gate_maxturns;

/* 空白 EEPROM 播种闸门参数默认值（被 eeprom_drv 的 AT24CXX_Check 在真空白时调用） */
void paraToEeprom(void);

/* 上电：从 EEPROM 读入 RAM，无效则用默认值；对旧编码自动迁移 */
void dcmotor_param_load(void);

/* 写回单个字段（远程 config 或本地设置触发） */
void dcmotor_param_save_warn_curr(uint16_t mA);
void dcmotor_param_save_maxheight(uint16_t mm);
void dcmotor_param_save_maxturns(uint16_t turns);

#endif