#ifndef __DELAY_H
#define __DELAY_H 			   
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32开发板
//使用SysTick的普通计数模式对延迟进行管理
//包括delay_us,delay_ms
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2012/9/2
//版本：V1.5
//版权所有，盗版必究
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//All rights reserved
//********************************************************************************
//V1.2修改说明
//修改了中断服务函数中调用延时循环的代码
//防止时间不准确,采用do while结构!

//V1.3修改说明
//增加了对UCOSII的时间支持.
//如果使用ucosII,delay_init会自动设置SYSTICK的值,使之与ucos的TICKS_PER_SEC对应.
//delay_ms和delay_us也进行了ucos的兼容.
//delay_us不能在ucos中使用,但是精度很高,所以需要占用没有占用ucos的定时器.
//delay_ms在ucos中,可以调用OSTimeDly函数,在未启动ucos时,采用delay_us实现,从而精确延时
//在初始化函数中,如果检测到ucos已经启动,delay_ms延时时的长度,选择OSTimeDly实现或者delay_us实现.

//V1.4修改说明 20110929
//修改了使用ucos,但是ucos未启动时,delay_ms的中断无法响应bug.
//V1.5修改说明 20120902
//在delay_us加入ucos临界区保护,防止在ucos的delay_us在执行,可能导致的时间不准
////////////////////////////////////////////////////////////////////////////////// 	 
void delay_init(void);
void delay_ms(u16 nms);
void delay_us(u32 nus);

#endif





























