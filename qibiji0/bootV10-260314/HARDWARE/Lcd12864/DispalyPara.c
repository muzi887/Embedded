#include "display.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

extern u8 kaival;

//ascii转为十进制数
int asciitoInt(u8 *src,u8 len)
{
	u8 i =0;
	int val = 0;
	for(i=0;i<len;i++)
	{
		if((src[i]>= 0x30) && (src[i]<= 0x39))
		{
			val = val + (src[i]-0x30);
			val = val * 10;
		}
	}
	val = val /10;
//	printf("val = %d\n",val);
	return val;
}

//显示整数 4位
void DisplayInt(u8 *src, u16 val,u8 style)
{
	u16 temp0=0,temp1=0;
	val = val % 10000;
	temp0 = val/1000;
	temp1 = val%1000;
	src[0] = temp0 + 0x30;
	
	temp0 = temp1/100;
	temp1 = temp1%100;
	src[1] = temp0 + 0x30;
	
	temp0 = temp1/10;
	temp1 = temp1%10;
	src[2] = temp0 + 0x30;

	src[3] = temp1 +0x30;
	
	if(style == 0)
	{
		if(val < 10)
		{
			src[0] = 0x20;
			src[1] = 0x20;
			src[2] = 0x20;
		}
		else if(val < 100)
		{
			src[0] = 0x20;
			src[1] = 0x20;
		}
		else if(val < 1000)
		{
			src[0] = 0x20;
		}
	}
}

//显示小数 val 2位整数1位小数 
void DisplayFloat(u8 *src, u16 val,u8 style)
{
	u8 Int=0,dec = 0;
	u16 temp0=0,temp1=0;

	val = val % 1000;
	
	temp0 = val/100;
	temp1 = val%100;
	src[0] = temp0 + 0x30;
	
	temp0 = temp1/10;
	temp1 = temp1%10;
	src[1] = temp0 + 0x30;
	src[2] = 0x2e;
	src[3] = temp1 +0x30;
	
	if(style == 0)
	{
		if(val < 100)
		{
			src[0] = 0x20;
		}
	}
}






