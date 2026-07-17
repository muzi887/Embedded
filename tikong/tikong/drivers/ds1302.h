/*******************************************************************************
** 文件名称 : ds1302.h
** 软件版本 : 1.0
** 编译环境 : RealView MDK-ARM 4.20
** 文件作者 : 	
** 功能说明 : 在该文件中包含程序需要用到的头文件
**
**                       (c) Copyright 2007-2012, viewtool
**                            http://www.viewtool.com
**                               All Rights Reserved
**
*******************************************************************************/
/* 防止重定义-----------------------------------------------------------------*/
#ifndef _DS1302_H
#define _DS1302_H
/* 包含头文件 *****************************************************************/
#include <stm32f4xx.h>

/* 类型声明 ------------------------------------------------------------------*/
/* 宏定义 --------------------------------------------------------------------*/ 
#define ds1302clk GPIO_Pin_11
#define ds1302dat GPIO_Pin_10
#define ds1302rst GPIO_Pin_9    

#define ds1302gpio GPIOE  

#define SCLK_L  GPIO_ResetBits(ds1302gpio,ds1302clk)
#define SCLK_H  GPIO_SetBits(ds1302gpio,ds1302clk)

#define CE_L		GPIO_ResetBits(ds1302gpio,ds1302rst)
#define CE_H		GPIO_SetBits(ds1302gpio,ds1302rst)

#define DAT_L	  GPIO_ResetBits(ds1302gpio,ds1302dat) 
#define DAT_H	  GPIO_SetBits(ds1302gpio,ds1302dat) 

#define DAT_IN()  GPIO_ReadInputDataBit(ds1302gpio,ds1302dat)

/* 函数定义 ------------------------------------------------------------------*/
void ds1302_init(void);									 //初始化1302
void ds1302_gettime(uint8_t *ttt);       //获取时间
u8 ds1302_settime(uint8_t *dat);				 //设置时间

//驱动
void write_1302byte(uint8_t dat);				 //写一个字节的数据sck上升沿写数据
uint8_t read_1302(uint8_t add);					 //读数据
void write_1302(uint8_t add,uint8_t dat);//向指定寄存器写入一个字节的数据
#endif

