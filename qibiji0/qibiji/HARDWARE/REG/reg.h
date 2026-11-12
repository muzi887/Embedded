#ifndef __REG_H
#define __REG_H
#include "sys.h"

#define eepromLength 11

extern u16 EepromAddr;
extern u8 Witeflag; // 写入地址和写入标志

/* 寄存器参数结构体 */
typedef struct
{
	u16 distance_local; // 1远程/本地控制
	u16 motor_on_off;	// 2电机开关控制
	u16 up_flag;		// 3全开标志
	u16 down_flag;		// 4全关标志
	u16 up_err_flag;	// 5上升错误
	u16 down_err_flag;	// 6下降错误
	u16 range_target;	// 7目标开合度
	//u16 range_reality;	// 8实际开合度
	u16 motor_curr;		// 9电机电流
	u16 bat_vol;		// 10电池电压
	u16 bat_curr;		// 11电池电流
	u16 bat_cap;		// 12电池容量
	u16 bat_state;		// 13电池状态
	u16 solar_curr;		// 14太阳能电流
	u16 solar_vol;		// 15太阳能电压
	u16 temperature;	// 16环境温度
	u16 gprs_state;		// 17gprs状态
	u16 gate_range_adc; // 18

	//----------------------------------------------------
	u16 gate_upper;				       // 19闸门上限值
	u16 gate_lower;				       // 20闸门下限值
	u16 motor_warning_curr_upper;     // 21电机电流上限报警值
	u16 gate_maxheight;				  // 22闸门全开高度值mm
	u16 gate_maxturns;				  // 23转速
	u16 gate_kaidu;				      // 闸门开度
	u16 gate_kaidu_count;			  // 计数值
	u16 reportT;				     // 上报时间

	//----------------------------------------------------
	u16 years;	 // 33年
	u16 months;	 // 34月
	u16 days;	 // 35日
	u16 hours;	 // 36时
	u16 minutes; // 37分
	u16 second;	 // 38秒
	u16 warning; // 39报警
	u16 change;

} Register;

extern Register armReg;

// 寄存器函数
void initReg(void);
void setReg(u16 addr, u16 value);
u16 getReg(u16 addr);
void setRegMul(u16 addr, u16 *value, u16 num);
void para_read(void);
void eepromToPara(void);
void paraToEeprom(void);

////////////////////////////////
//阀门参数
#define GATE_MOTOR_MAXCURR_ADDR		2				//电机报警电流上限值参数地址
#define GATE_MAXHEIGHT_ADDR 		3               //闸门高差值(高度值)参数地址
#define GATE_MAXTURNS_ADDR			4               //闸门高差对应的螺纹圈数计数值
#define GATE_KAIDU_ADDR				5               //闸门实际开度值
#define GATE_KAIDU_COUNT_ADDR		6               //闸门实际开度对应的螺纹圈数计数值


//属性上传参数
#define PROP_REPORT_MIN_INTERVAL_MS_ADDR	30		//最小上报间隔（如 30000 ms）参数地址
#define PROP_OPENING_THRESHOLD_ADDR		    31		//开度变化阈值（如 2）参数地址
//OTA参数
#define OTA_ADDR 					200		   // 24C02存储OTA信息的起始地址




#endif
