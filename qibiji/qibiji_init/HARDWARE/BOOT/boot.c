#include "boot.h"
#include "usart.h"
#include "delay.h"
#include "24cxx.h"
#include <string.h>
#include "stmflash.h"
#include "gprs.h"

load_a load_A;		  // 函数指针load_A
OTA_InfoCB OTA_Info;  // 存储在24C02内的OTA信息相关的结构体
UpDataA_CB UpDataA;	  // A区更新用到的结构体
uint32_t BootStaFlag; // 记录全局状态标志位



