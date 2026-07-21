#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "Lcd12864.h"

//#define MoPage      0
#define ViewPage    0
#define SetPage     2
#define SetPara     3   
#define SetVal      4

#define View_Num    4
#define SetP_Num    2
#define SetParaN    2

#define Home_Num    3

#define  BackLight PAout(0)

void Lcd_Process(void); //液晶处理函数
void BLight_Ctrl(void);	//背光控制
void LCDInit(void);
void Lcd_Init(void);
void Lcd_Gpio_Init(void);
void DisplayInit(void);
void LcdDisplay(void);  // 液晶显示

void kaiduVal(u8 *val,u8 *src);   //开度值和状态显示

void fjiaozhun(u8 *src, u8 style);  //校准显示

void DisplayState(u8 *src);   //状态 连接显示

int asciitoInt(u8 *src,u8 len);

void homepage(void); //主页
void setpage(void);  //设置页
void G4gpage(void);  //4g
void Devicepage(void); // 设备页面

uint8_t ViewPages(uint8_t KeyNum); //查询页面
void DisplayViewPages(uint8_t PageNum);


void getval();//获取值并显示

void DisplayInt(u8 *src, u16 val,u8 style);   //显示整数
void DisplayFloat(u8 *src, u16 val,u8 style); //显示小数 val 2位整数1位小数 

// 字符串显示函数
uint8_t Lcd_DisplayString(const char *str, uint8_t row, uint8_t col);  // 通用字符串显示函数
uint8_t Lcd_DisplayString_Default(const char *str);  // 默认位置显示字符串
void loading(void);  // 显示"wait net"（兼容旧版本）


void progressbar_Init(void);
void ota_step1(void);
void ota_step2(void);
void ota_step3(void);
void ota_step4(void);
void ota_step5(void);
void ota_step6(void);

#endif
