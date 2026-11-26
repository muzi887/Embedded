#ifndef _DS1302_H
#define _DS1302_H

#include <stm32f10x.h>

/* 引脚：PC8=CLK（推挽）、PC7=DAT（开漏，双向 IO）、PC6=RST（推挽） */
#define DS1302_CLK        GPIO_Pin_8
#define DS1302_DAT        GPIO_Pin_7
#define DS1302_RST        GPIO_Pin_6
#define DS1302_GPIO_PORT  GPIOC

/* ---- 对外 API ---- */
void ds1302_init(void);          /* 初始化：仅配置 GPIO；RTC 靠 VBAT 掉电保持，不重播时间 */
void ds1302_dataread(char *ttt); /* 读 7 字节日历并转十进制：秒/分/时/日/月/周/年 */
u8   ds1302_settime(u8 *dat);    /* 写 7 字节日历+回读校验（跳过秒位）：0=成功；断电补种工具 */

#endif
