#include "display.h"
#include "main.h"
#include "net_liveness.h"
#include <string.h>
#include <stdio.h>

//extern u8 kaival;
extern u8 OTA_Enable;
// ascii转为十进制数
int asciitoInt(u8 *src, u8 len)
{
	u8 i = 0;
	int val = 0;
	for (i = 0; i < len; i++)
	{
		if ((src[i] >= 0x30) && (src[i] <= 0x39))
		{
			val = val + (src[i] - 0x30);
			val = val * 10;
		}
	}
	val = val / 10;
	//	printf("val = %d\n",val);
	return val;
}

// 显示整数 4位
void DisplayInt(u8 *src, u16 val, u8 style)
{
	u16 temp0 = 0, temp1 = 0;
	val = val % 10000;
	temp0 = val / 1000;
	temp1 = val % 1000;
	src[0] = temp0 + 0x30;

	temp0 = temp1 / 100;
	temp1 = temp1 % 100;
	src[1] = temp0 + 0x30;

	temp0 = temp1 / 10;
	temp1 = temp1 % 10;
	src[2] = temp0 + 0x30;

	src[3] = temp1 + 0x30;

	if (style == 0)
	{
		if (val < 10)
		{
			src[0] = 0x20;
			src[1] = 0x20;
			src[2] = 0x20;
		}
		else if (val < 100)
		{
			src[0] = 0x20;
			src[1] = 0x20;
		}
		else if (val < 1000)
		{
			src[0] = 0x20;
		}
	}
}

// 显示小数 val 2位整数1位小数
void DisplayFloat(u8 *src, u16 val, u8 style)
{
	u8 Int = 0, dec = 0;
	u16 temp0 = 0, temp1 = 0;

	val = val % 1000;

	temp0 = val / 100;
	temp1 = val % 100;
	src[0] = temp0 + 0x30;

	temp0 = temp1 / 10;
	temp1 = temp1 % 10;
	src[1] = temp0 + 0x30;
	src[2] = 0x2e;
	src[3] = temp1 + 0x30;

	if (style == 0)
	{
		if (val < 100)
		{
			src[0] = 0x20;
		}
	}
}

// 开度值显示
void kaiduVal(u8 *val, u8 *src)
{
	u8 len = 0;
	char kval[3] = {'\0'};
	sprintf(kval, "%d", armReg.gate_kaidu);
	len = strlen(kval);
	//	printf("len = %d",len);

	val[0] = 0x20;
	val[1] = 0x20;
	val[2] = 0x20;
	switch (len)
	{
	case 1:
		val[2] = kval[0];
		break;
	case 2:
		val[1] = kval[0];
		val[2] = kval[1];
		break;
	case 3:
		val[0] = kval[0];
		val[1] = kval[1];
		val[2] = kval[2];
		break;
	default:
		break;
	}

	if (armReg.gate_kaidu > 100)
	{
		val[0] = 0x31;
		val[1] = 0x30;
		val[2] = 0x30;
	}

	switch (sys_moter_state)
	{
	case MOTOR_UP: // 上升
		src[0] = 0xc9;
		src[1] = 0xcf;
		src[2] = 0xc9;
		src[3] = 0xfd;
		break;
	case MOTOR_DOWN: // 下降
		src[0] = 0xcf;
		src[1] = 0xc2;
		src[2] = 0xbd;
		src[3] = 0xb5;
		break;

	case MOTOR_STOP: // 停止
		if (FLAG_OPENALL)
		{
			src[0] = 0xc8; // 全开
			src[1] = 0xab;
			src[2] = 0xbf;
			src[3] = 0xaa;
			val[0] = 0x31;
			val[1] = 0x30;
			val[2] = 0x30;
		}
		else if (FLAG_CLOSEALL)
		{
			src[0] = 0xc8; // 全关
			src[1] = 0xab;
			src[2] = 0xb9;
			src[3] = 0xd8;
			val[0] = 0x20;
			val[1] = 0x20;
			val[2] = 0x30;
		}
		else
		{
			src[0] = 0xcd; // 停止
			src[1] = 0xa3;
			src[2] = 0xd6;
			src[3] = 0xb9;
		}
		break;

	case MOTOR_RESET: // 复位

		break;
	default:
		break;
	}
}

// 校准显示
void fjiaozhun(u8 *src, u8 style)
{
	if (style) // 自动执行
	{
		src[0] = 0xc1;
		src[1] = 0xa2;
		src[2] = 0xbc;
		src[3] = 0xb4;

		src[4] = 0xd6;
		src[5] = 0xb4;
		src[6] = 0xd0;
		src[7] = 0xd0;
	}
	else
	{
		src[0] = 0xb9;
		src[1] = 0xd8;
		src[2] = 0xb1;
		src[3] = 0xd5;

		src[4] = 0x20;
		src[5] = 0x20;
		src[6] = 0x20;
		src[7] = 0x20;
	}
}

// 状态 在线显示
void DisplayState(u8 *src)
{
	if (!gprsonlineflag)
	{
		/* 离：模块未注上网 */
		src[0] = 0xc0;
		src[1] = 0xeb;
	}
	else if (NetLiveness_IsServerConfirmed())
	{
		/* 在：模块在线且平台已确认（最近心跳收到回执） */
		src[0] = 0xd4;
		src[1] = 0xda;
	}
	else
	{
		/* 未：模块已注网，但平台还没确认（心跳未收到回执） */
		src[0] = 0xce;
		src[1] = 0xb4;
	}
}

// OTA 使能显示
void DisplayOTAenable(u8 *src)
{
	if (OTA_Enable==0)
	{// bdfb d6b9   no
		src[0] = 0xbd;
		src[1] = 0xfb;
		src[2] = 0xd6;
		src[3] = 0xb9;
	}
	else
	{// d4ca d0ed   yes
		src[0] = 0xd4;
		src[1] = 0xca;
		src[2] = 0xd0;
		src[3] = 0xed;
		
	}
}
