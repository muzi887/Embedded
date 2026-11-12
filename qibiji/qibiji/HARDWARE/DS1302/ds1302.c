/*******************************************************************************
** 文件名称 : ds1302.c
** 软件版本 : 1.0
** 编译环境 : RealView MDK-ARM 4.20
** 文件作者 :
** 功能说明 : ds1302相关函数定义
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

const uint8_t read[] = {0x81, 0x83, 0x85, 0x87, 0x89, 0x8b, 0x8d};	// 读秒、分、时、日、月、周、年的寄存器地址
const uint8_t write[] = {0x80, 0x82, 0x84, 0x86, 0x88, 0x8a, 0x8c}; // 写秒、分、时、日、月、周、年的寄存器地址

/*Pc15配置为开漏模式，此模式下能够实现真正的双向IO口*/

// 初始化1302
void ds1302_init()
{
	ds1302_GPIO_Configuration();
	//	ds1302_setch();
}

// IO初始化
void ds1302_GPIO_Configuration(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

	GPIO_InitStruct.GPIO_Pin = ds1302clk | ds1302rst;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(ds1302gpio, &GPIO_InitStruct);

	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStruct.GPIO_Pin = ds1302dat;
	GPIO_Init(ds1302gpio, &GPIO_InitStruct);
}

// 起振
void ds1302_setch(void)
{
	u8 data = 0;
	data = read_1302(0x81);
	printf("datas = %d\n", data);
	write_1302(0x8e, 0x00); // 去除写保护
	write_1302(0x80, 0);
	write_1302(0x8e, 0x80); // 加写保护

	data = read_1302(0x81);
	printf("datas = %d\n", data);
}

// 设置DS1302时钟
u8 ds1302_settime(uint8_t *dat)
{
	uint8_t i = 0, res = 0;
	char ttt[7];
	write_1302(0x8e, 0x00); // 去除写保护
	for (i = 0; i < 7; i++) // 进行对时
	{
		write_1302(write[i], dat[i]);
	}
	write_1302(0x8e, 0x80); // 加写保护

	ds1302_dataread(ttt);

	for (i = 1; i < 7; i++)
	{
		if (ttt[i] == dat[i])
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

// ds1302时钟读取
void ds1302_dataread(char *ttt)
{
	u8 i = 0;
	char x;
	char y;
	for (i = 0; i < 7; i++)
	{
		ttt[i] = read_1302(read[i]);

		x = ttt[i] % 16; // 转成十进制编码
		y = ttt[i] / 16 * 10;
		ttt[i] = x + y;
	}

	// 	printf("sec = %d %x\n",ttt[0],ttt[0]);
	// 	printf("min = %d %x\n",ttt[1],ttt[1]);
	// 	printf("hour = %d %x\n",ttt[2],ttt[2]);
	// 	printf("day = %d %x\n",ttt[3],ttt[3]);
	// 	printf("mon = %d %x\n",ttt[4],ttt[4]);
	// 	printf("week = %d %x\n",ttt[5],ttt[5]);
	// 	printf("year = %d %x\n",ttt[6],ttt[6]);
}

// 写一个字节的数据sck上升沿写数据
void write_1302byte(uint8_t dat)
{
	uint8_t i = 0;

	GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;

	// delay_us(2);//延时大约2us
	for (i = 0; i < 8; i++)
	{
		GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;

		if (dat & 0x01)
			GPIO_SetBits(ds1302gpio, ds1302dat);
		else
			GPIO_ResetBits(ds1302gpio, ds1302dat); // ds1302dat=(dat&0x01);

		// delay_us(2);
		GPIO_SetBits(ds1302gpio, ds1302clk); // ds1302clk=1;

		dat >>= 1;
		//	delay_us(1);
	}
}
// 读一个字节数据
uint8_t read_1302(uint8_t add)
{
	uint8_t i = 0, dat1 = 0x00;

	GPIO_ResetBits(ds1302gpio, ds1302rst); // ds1302rst=0;
	GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;

	// delay_us(3);
	GPIO_SetBits(ds1302gpio, ds1302rst); // ds1302rst=1;

	// delay_us(3);
	write_1302byte(add); // 先写寄存器的地址
	for (i = 0; i < 8; i++)
	{
		GPIO_SetBits(ds1302gpio, ds1302clk); // ds1302clk=1;

		dat1 >>= 1;
		GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;//拉低时钟线，以便于数据的读入

		if (GPIO_ReadInputDataBit(ds1302gpio, ds1302dat) == 1) // 数据线此时为高电平
		{
			dat1 = dat1 | 0x80;
		}
	}

	//	delay_us(1);
	GPIO_ResetBits(ds1302gpio, ds1302rst); // ds1302rst=0;

	return dat1;
}

// 向指定寄存器写入一个字节的数据
void write_1302(uint8_t add, uint8_t dat)
{
	GPIO_ResetBits(ds1302gpio, ds1302rst); // ds1302rst=0;
	GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;

	//	delay_us(1);
	GPIO_SetBits(ds1302gpio, ds1302rst); // ds1302rst=1;

	//	delay_us(2);
	write_1302byte(add);
	write_1302byte(dat);
	GPIO_ResetBits(ds1302gpio, ds1302rst); // ds1302clk=0;
	GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302rst=0;

	//	delay_us(1);
}
