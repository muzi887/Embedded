#ifndef __FLOOR_CTRL_H
#define __FLOOR_CTRL_H

#include "stdint.h"
#include "delay.h"

#define FLOOR_IO_START 1  /* 第 1 层对应的 IO */
#define FLOOR_IO_COUNT 40 /* 总楼层数 */

int Floor_DirectGo(uint8_t floor);
int Floor_CallOne(uint8_t floor);
int Floor_AuthCheck(uint8_t floor);
/** 常开授权：拉低楼层 IO，不启动 TIM5 倒计时（与 Floor_AuthCheck 区别） */
int Floor_AuthCheck_open(uint8_t floor);
/** 按限位位图对置位楼层做常开授权（40 层 / 5 字节位序与 elevator_tail5 一致） */
int Floor_AuthCheck_limit(const uint8_t elevator_tail5_limit[5]);
/** 按限位位图关闭曾常开楼层 */
int Floor_AuthCheck_limit_off(const uint8_t elevator_tail5_limit[5]);
int Floor_Off(uint8_t floor);
int Floor_AllOff(void);
int Floor_AllOn(void);

/** 梯控授权 5 字节位图：写入后供 TIM5 超时关楼层使用 */
void FloorCtrl_SetTail5(const uint8_t tail5[5]);
void FloorCtrl_GetTail5(uint8_t out[5]);
/** 梯控授权 5 字节混合位图（elevator_tail5 合并后的副本） */
void FloorCtrl_GetTail5Mixed(uint8_t out[5]);
void FloorCtrl_SetTail5Mixed(const uint8_t mixed5[5]);
/** EEPROM 常开限位 5 字节位图 */
void FloorCtrl_SetLimit(const uint8_t limit[5]);
void FloorCtrl_GetLimit(uint8_t out[5]);

#endif
