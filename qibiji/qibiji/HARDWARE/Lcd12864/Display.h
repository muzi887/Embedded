#ifndef __DISPLAY_H
#define __DISPLAY_H
#include "Lcd12864.h"

#define ViewPage 0
#define SetPage 2
#define SetPara 3
#define SetVal 4

#define View_Num 4
#define SetP_Num 2
#define SetParaN 2

#define Home_Num 3

#define BackLight PAout(0)

// 显示缓存结构体 - 用于事件驱动刷新
typedef struct {
	u8 page_last;				  				// 上次显示的页面
	u16 motor_curr_last;		 		  // 上次显示的电机电流
	u16 motor_vol_last;			 			// 上次显示的电机电压
	u8 kaidu_last;				   			// 上次显示的开度值
	u8 motor_state_last;		 		  // 上次显示的电机状态
	u16 gate_max_last;			 			// 上次显示的闸位最大值
	u16 motor_warning_curr_last;  // 上次显示的堵转电流
	u8 autoFlag_last;			   			// 上次显示的校准标志
	u16 PV_last;				   				// 上次显示的光伏电压
	u8 gprsonlineflag_last;		    // 上次显示的在线状态
	u8 serverok_last;			    // 上次显示的"平台已确认"状态（在/未切换时刷新）
	u8 positionState_last;		    // 上次显示的位置状态
	u8 signal_last[2];		        // 上次显示的信号值（2字节）
	u8 ccid_last[10];		        // 上次显示的CCID值（10字节）
} DisplayCache;

extern DisplayCache displayCache;
void DisplayCache_Init(void); // 初始化显示缓存

extern u8 xinhao[16];
extern u8 ccid[16];
extern u8 order[16];
extern u8 kaidu[16];

void Lcd_Process(void); // 液晶显示处理
void BLight_Ctrl(void); // 背光控制
void LCDInit(void);
void Lcd_Init(void);
void Lcd_Gpio_Init(void);
void LcdDisplay(void); // 液晶显示

void kaiduVal(u8 *val, u8 *src); // 开度值和状态显示

void fjiaozhun(u8 *src, u8 style); // 校准显示

void DisplayState(u8 *src); // 状态 在线显示

int asciitoInt(u8 *src, u8 len);

void homepage(void);   // 首页
void setpage(void);    // 设置
void G4gpage(void);    // 4g
void Devicepage(void); // 设备页面
void OperatePage(void);//操作页面

uint8_t ViewPages(uint8_t KeyNum); // 查询页面
void DisplayViewPages(uint8_t PageNum);

void DisplayInt(u8 *src, u16 val, u8 style);   // 显示整数
void DisplayFloat(u8 *src, u16 val, u8 style); // 显示小数 val 2位整数1位小数

#endif
