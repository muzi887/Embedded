#ifndef __MAIN_H
#define __MAIN_H
#include "stm32f10x.h"
#include "reg.h"

// 业务逻辑定义
//------------------------------------------------------------------------------
#define MOTOR_UP    0x01
#define MOTOR_DOWN  0x02
#define MOTOR_STOP  0x03
#define MOTOR_RESET 0x04

extern u8 sys_moter_state;     // 电机当前状态:停止 0x01:上 0x02:下
extern u8 sys_moter_state_old; // 电机旧状态
extern u8 sys_local_remote;
extern u8 sys_moter_dir;
extern u8 rx_lcd_complete;

extern u8 FLAG_OPENALL, FLAG_CLOSEALL;
extern u8 FLAG_FORWORD, FLAG_BACKWORD;
extern u8 FLAG_A,FLAG_B;
extern u16 Num_count;

//extern Register armReg;
#define OTA_STATE_WAIT_NET        0
#define OTA_STATE_ONLINE          1
#define OTA_STATE_SEND_VERSION    2
#define OTA_STATE_HANDLE_REPLY    3
#define OTA_STATE_REBOOT          4
#define OTA_STATE_AT_OK             5
#define OTA_STATE_MQTTCID_OK       6
extern u8 gprsonlineflag;

extern u8 autoFlag;   // 校准相关标志
#endif
