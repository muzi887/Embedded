/*******************************************************************************
** 文件名称 : ds1302.c
** 当前版本 : 1.0
** 编译环境 : RealView MDK-ARM 4.20
** 文件描述 : 	
** 功能说明 : ds1302时钟驱动函数
**
**                       (c) Copyright 2005-2011, viewtool
**                            http://www.viewtool.com
**                               All Rights Reserved
**
*******************************************************************************/
/* 包含头文件 *****************************************************************/
#include "ds1302.h"
#include "stdio.h"
#include <string.h> 

const uint8_t read[]={0x81,0x83,0x85,0x87,0x89,0x8b,0x8d};//秒、分、时、日、月、周、年的寄存器读地址
const uint8_t write[]={0x80,0x82,0x84,0x86,0x88,0x8a,0x8c};//写秒、分、时、日、月、周、年的寄存器写地址
 

/*Pc15配置为开漏模式，输入模式，能够实现真正的双向IO口*/

//初始化1302
void ds1302_init()
{
	u8 year;
	u8 time[7]={0x45,0x59,0x10,0x14,0x11,0x05,0x25};//2025年08月31日10:59:45星期二
	ds1302_GPIO_Configuration();
/*	year = read_1302(read[6]);
	printf("year = %d\n",year);
	if(year < 0x25)              
		ds1302_settime(time);*/
}

//IO初始化
void ds1302_GPIO_Configuration(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin = ds1302clk|ds1302rst;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(ds1302gpio, &GPIO_InitStruct);

	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStruct.GPIO_Pin = ds1302dat;
	GPIO_Init(ds1302gpio, &GPIO_InitStruct);
}

//设置DS1302时间
u8 ds1302_settime(uint8_t *dat)
{
	uint8_t i=0,res = 0;
	char ttt[7];
	write_1302(0x8e,0x00);//去掉写保护
	for(i=0;i<7;i++)//写入所有时间
	{
		write_1302(write[i],dat[i]);
	}
  write_1302(0x8e,0x80);//加上写保护
	
	ds1302_dataread(ttt);
	
	for(i=1;i<7;i++)
	{
		if(ttt[i] == dat[i])
		{
			res = 0;
		}
		else
		{
			res = 0xFF;
			break;
		}
	}
	return res;
}

//ds1302时钟读取
void ds1302_dataread(char *ttt)
{
  u8 i=0;
	char x;
	char y;
  for(i=0;i<7;i++)
	{
	  ttt[i]=read_1302(read[i]);
		
		x = ttt[i] % 16;      //转换十进制表示
		y = ttt[i] /16 * 10;
		ttt[i] = x + y;
	}

// 	printf("sec = %d \n",ttt[0]);
// 	printf("min = %d \n",ttt[1]);
// 	printf("hour = %d \n",ttt[2]);
// 	printf("day = %d \n",ttt[3]);
// 	printf("mon = %d \n",ttt[4]);
// 	printf("week = %d \n",ttt[5]);
// 	printf("year = %d \n",ttt[6]);	
 
// 	printf("sec = %x \n",ttt[0]);
// 	printf("min = %x \n",ttt[1]);
// 	printf("hour = %x \n",ttt[2]);
// 	printf("day = %x \n",ttt[3]);
// 	printf("mon = %x \n",ttt[4]);
// 	printf("week = %x \n",ttt[5]);
// 	printf("year = %x \n",ttt[6]);
}


//写一个字节的数据sck上升沿写数据
void write_1302byte(uint8_t dat)
{
 	uint8_t i=0;
	
	GPIO_ResetBits(ds1302gpio,ds1302clk); 	 //ds1302clk=0;
 	
 // delay_us(2);//延时约2us
 	for(i=0;i<8;i++)
	{
		GPIO_ResetBits(ds1302gpio,ds1302clk); 	//ds1302clk=0;
			
		if(dat&0x01)
			GPIO_SetBits(ds1302gpio,ds1302dat);
		else
			GPIO_ResetBits(ds1302gpio,ds1302dat); //ds1302dat=(dat&0x01);
		
		//delay_us(2);
		GPIO_SetBits(ds1302gpio,ds1302clk);     //ds1302clk=1;
 
		dat>>=1;
	//	delay_us(1);
	}	
}
//读一个字节数据
uint8_t read_1302(uint8_t add)
{
	uint8_t i=0,dat1=0x00;
	
	GPIO_ResetBits(ds1302gpio,ds1302rst); //ds1302rst=0;
	GPIO_ResetBits(ds1302gpio,ds1302clk); //ds1302clk=0;
	
	//delay_us(3); 
	GPIO_SetBits(ds1302gpio,ds1302rst);  //ds1302rst=1;
	
	//delay_us(3); 
	write_1302byte(add);//先写寄存器的地址
	for(i=0;i<8;i++)
	{
		GPIO_SetBits(ds1302gpio,ds1302clk);  //ds1302clk=1;
	
		dat1>>=1;
		GPIO_ResetBits(ds1302gpio,ds1302clk); //ds1302clk=0;//下降时钟线，以便读取数据的读取
	
		if(GPIO_ReadInputDataBit(ds1302gpio,ds1302dat)==1)  //数据线此时为高电平
		{
			dat1=dat1|0x80;
		}
	}
	
//	delay_us(1);
	GPIO_ResetBits(ds1302gpio,ds1302rst); //ds1302rst=0;
	
	return dat1;
}

//向指定的寄存器写入一个字节的数据
void write_1302(uint8_t add,uint8_t dat)
{
	GPIO_ResetBits(ds1302gpio,ds1302rst); //ds1302rst=0;
	GPIO_ResetBits(ds1302gpio,ds1302clk); //ds1302clk=0;
	
//	delay_us(1); 
	GPIO_SetBits(ds1302gpio,ds1302rst);   //ds1302rst=1;
	
//	delay_us(2); 
	write_1302byte(add);
	write_1302byte(dat);
	GPIO_ResetBits(ds1302gpio,ds1302rst); //ds1302clk=0;
	GPIO_ResetBits(ds1302gpio,ds1302clk); //ds1302rst=0;
	
//	delay_us(1);
}
