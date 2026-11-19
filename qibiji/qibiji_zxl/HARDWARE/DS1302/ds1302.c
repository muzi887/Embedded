/*******************************************************************************
** �ļ����� : ds1302.c
** �����汾 : 1.0
** ���뻷�� : RealView MDK-ARM 4.20
** �ļ����� :
** ����˵�� : ds1302��غ�������
**
**                       (c) Copyright 2005-2011, viewtool
**                            http://www.viewtool.com
**                               All Rights Reserved
**
*******************************************************************************/
/* ����ͷ�ļ� *****************************************************************/
#include "ds1302.h"
#include "stdio.h"
#include <string.h>

const uint8_t read[] = {0x81, 0x83, 0x85, 0x87, 0x89, 0x8b, 0x8d};	// ���롢�֡�ʱ���ա��¡��ܡ���ļĴ�����ַ
const uint8_t write[] = {0x80, 0x82, 0x84, 0x86, 0x88, 0x8a, 0x8c}; // д�롢�֡�ʱ���ա��¡��ܡ���ļĴ�����ַ

/*Pc15����Ϊ��©ģʽ����ģʽ���ܹ�ʵ��������˫��IO��*/

// ��ʼ��1302
void ds1302_init()
{
	ds1302_GPIO_Configuration();
	//	ds1302_setch();
}

// IO��ʼ��
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

// ����
void ds1302_setch(void)
{
	uint8_t data = 0;
	data = read_1302(0x81);
	GPRS_Printf("datas = %d\n", data);
	write_1302(0x8e, 0x00); // ȥ��д����
	write_1302(0x80, 0);
	write_1302(0x8e, 0x80); // ��д����

	data = read_1302(0x81);
	GPRS_Printf("datas = %d\n", data);
}

//DS1302
uint8_t ds1302_settime(uint8_t *dat)
{
	uint8_t i = 0, res = 0;
	char ttt[7];
	write_1302(0x8e, 0x00); // ȥ��д����
	for (i = 0; i < 7; i++) // ���ж�ʱ
	{
		write_1302(write[i], dat[i]);
	}
	write_1302(0x8e, 0x80); // ��д����

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

// ds1302ʱ�Ӷ�ȡ
void ds1302_dataread(char *ttt)
{
	uint8_t i = 0;
	char x;
	char y;
	for (i = 0; i < 7; i++)
	{
		ttt[i] = read_1302(read[i]);

		x = ttt[i] % 16; // ת��ʮ���Ʊ���
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

// дһ���ֽڵ�����sck������д����
void write_1302byte(uint8_t dat)
{
	uint8_t i = 0;

	GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;

	// delay_us(2);//��ʱ��Լ2us
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
// ��һ���ֽ�����
uint8_t read_1302(uint8_t add)
{
	uint8_t i = 0, dat1 = 0x00;

	GPIO_ResetBits(ds1302gpio, ds1302rst); // ds1302rst=0;
	GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;

	// delay_us(3);
	GPIO_SetBits(ds1302gpio, ds1302rst); // ds1302rst=1;

	// delay_us(3);
	write_1302byte(add); // ��д�Ĵ����ĵ�ַ
	for (i = 0; i < 8; i++)
	{
		GPIO_SetBits(ds1302gpio, ds1302clk); // ds1302clk=1;

		dat1 >>= 1;
		GPIO_ResetBits(ds1302gpio, ds1302clk); // ds1302clk=0;//����ʱ���ߣ��Ա������ݵĶ���

		if (GPIO_ReadInputDataBit(ds1302gpio, ds1302dat) == 1) // �����ߴ�ʱΪ�ߵ�ƽ
		{
			dat1 = dat1 | 0x80;
		}
	}

	//	delay_us(1);
	GPIO_ResetBits(ds1302gpio, ds1302rst); // ds1302rst=0;

	return dat1;
}

// ��ָ���Ĵ���д��һ���ֽڵ�����
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
