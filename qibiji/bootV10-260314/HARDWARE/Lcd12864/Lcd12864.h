#ifndef __LCD_H
#define __LCD_H

#include "sys.h"

/*液晶12864驱动IO口电压为5v，stm32 io要选择5v兼容的IO口*/

#define CS   PEout(3)   //PE.3  片选时为CS 芯片选择端
#define SID  PEout(4)   //PE.4  定义为SID  数据输入
#define SCLK PEout(5)   //PE.5  定义为SCLK 时钟信号
//#define PSB  PBout(6)    //PB.6  片选时使PSB=0，并行模式需要低电平，串行模式IO口直接接地

// 函数声明
                                                                           
void WRCommandH(uint8_t CMD);
void WRDataH(uint8_t Data);
void SPIWH(uint8_t Wdata,uint8_t WRS);
void SendByteLCDH(uint8_t WLCDData);
void ShowQQCharH(uint8_t addr,uint8_t *hanzi,uint8_t count);
void ShowNUMCharH(uint8_t addr,uint8_t i,uint8_t count);
void WRCGRAMH(uint8_t data1,uint8_t data2,uint8_t addr);
void ShowCGCharH(uint8_t addr,uint8_t i);
void WRGDRAM128X8(uint8_t x1,uint8_t y1,uint8_t d1);
void CLEARGDRAMH(uint8_t c);
void WRGDRAM1(uint8_t x,uint8_t l,uint8_t r);

//----------------------------
//画图1
void WRGDRAMp3(void);
//画图2
void WRGDRAMp4(void);
//画图3
void WRGDRAMp1(void);
//画图4
void WRGDRAMp2(void);

void WRGDRAM(u8 x, u8 *buf);
#endif
