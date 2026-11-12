#ifndef __LCD_H
#define __LCD_H

#include "sys.h"

/*液晶12864驱动IO电压是5v，stm32 io要选择5v兼容IO口*/

#define CS PEout(3)   // PE.3  片选时为CS 片选信号
#define SID PEout(4)  // PE.4  功能为SID  数据线
#define SCLK PEout(5) // PE.5  功能为SCLK 时钟线
// #define PSB  PBout(6)    //PB.6  功能时PSB=0，串行模式，需要拉低电平，如果没有IO口就直接接地

// 函数声明

void WRCommandH(uint8_t CMD);
void WRDataH(uint8_t Data);
void SPIWH(uint8_t Wdata, uint8_t WRS);
void SendByteLCDH(uint8_t WLCDData);
void ShowQQCharH(uint8_t addr, uint8_t *hanzi, uint8_t count);
void ShowNUMCharH(uint8_t addr, uint8_t i, uint8_t count);
void WRCGRAMH(uint8_t data1, uint8_t data2, uint8_t addr);
void ShowCGCharH(uint8_t addr, uint8_t i);
void WRGDRAM128X8(uint8_t x1, uint8_t y1, uint8_t d1);
void WRGDRAM128X3(uint8_t x1, uint8_t y1, uint8_t d1);
void CLEARGDRAMH(uint8_t c);
void WRGDRAM_line();

//----------------------------
// 画图标
void WRGDRAMp3(void);
// 画图标
void WRGDRAMp4(void);
// 画图标
void WRGDRAMp1(void);
// 画图标
void WRGDRAMp2(void);

void WRGDRAM(u8 x, u8 *buf);
// 颜色反转：1 = 黑色背景白色字，0 = 白色背景黑色字
void lcd_set_invert(uint8_t inv);
uint8_t lcd_get_invert(void);
#endif
