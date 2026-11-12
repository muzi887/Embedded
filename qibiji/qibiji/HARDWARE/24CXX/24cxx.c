#include "24cxx.h"
#include "delay.h"
#include "wdg.h"
#include "reg.h"
#include "uart_queue.h"
#include <stdio.h>
#include <string.h>
#include "adc.h"

// 初始化IIC接口
void AT24CXX_Init(void)
{
	IIC_Init();
}

// 从AT24CXX指定地址读出一个字节
// ReadAddr:开始读出的地址
// 返回值  :读到的数据
u8 AT24CXX_ReadOneByte(u16 ReadAddr)
{
	u8 temp = 0;
	IIC_Start();
	if (EE_TYPE > AT24C16)
	{
		IIC_Send_Byte(0xA0); // 发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(ReadAddr >> 8); // 发送高地址
		IIC_Wait_Ack();
	}
	else
		IIC_Send_Byte(0xA0 + ((ReadAddr / 256) << 1)); // 发送器件地址0xA0,写命令

	IIC_Wait_Ack();
	IIC_Send_Byte(ReadAddr % 256); // 发送低地址
	IIC_Wait_Ack();
	IIC_Start();
	IIC_Send_Byte(0xA1 + ((ReadAddr / 256) << 1)); // 进入接收模式
	IIC_Wait_Ack();
	temp = IIC_Read_Byte(0);
	IIC_Stop(); // 产生一个停止条件
	return temp;
}
// 在AT24CXX指定地址写入一个字节
// WriteAddr  :写入数据的目的地址
// DataToWrite:要写入的数据
void AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite)
{
	IIC_Start();
	if (EE_TYPE > AT24C16)
	{
		IIC_Send_Byte(0xA0); // 发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(WriteAddr >> 8); // 发送高地址
	}
	else
	{
		IIC_Send_Byte(0xA0 + ((WriteAddr / 256) << 1)); // 发送器件地址0XA0,写命令
	}
	IIC_Wait_Ack();
	IIC_Send_Byte(WriteAddr % 256); // 发送低地址
	IIC_Wait_Ack();
	IIC_Send_Byte(DataToWrite); // 发送字节
	IIC_Wait_Ack();
	IIC_Stop(); // 产生一个停止条件
	delay_ms(10);
	IWDG_Feed(); /* 单字节写要等 10ms，多字节写会累积到秒级：这里喂狗，避免启动/长写越过 3.18s 窗口 */
}
// 在AT24CXX里面的指定地址开始写入长度为Len的数据
// 该函数可以写入16bit或者32bit的数据.
// WriteAddr  :开始写入的地址
// DataToWrite:数据数组的首地址
// Len        :要写入数据的长度2,4
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len)
{
	u8 t;
	for (t = 0; t < Len; t++)
	{
		AT24CXX_WriteOneByte(WriteAddr + t, (DataToWrite >> (8 * t)) & 0xff);
	}
}

// 一次性读取16bit eeprom数据
void AT24CXX_Readdatas(u16 ReadAddr, u16 *Readbuf, u8 Len)
{
	u8 t;
	for (t = 0; t < Len; t++)
	{
		Readbuf[t] = AT24CXX_ReadLenByte(ReadAddr, 2);
		ReadAddr += 2;
	}
}

// 在AT24CXX里面的指定地址开始读出长度为Len的数据
// 该函数用于读出16bit或者32bit的数据.
// ReadAddr   :开始读出的地址
// 返回值     :数据
// Len        :要读出数据的长度2,4
u32 AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len)
{
	u8 t;
	u32 temp = 0;
	for (t = 0; t < Len; t++)
	{
		temp <<= 8;
		temp += AT24CXX_ReadOneByte(ReadAddr + Len - t - 1);
	}
	return temp;
}

// 指定地址写入一个字
void AT24CXX_WriteWord(u16 WriteAddr, u16 Data)
{
	u8 temp_l = (u8)(Data >> 8);
	u8 temp_h = (u8)(Data & 0x00ff);
	AT24CXX_WriteOneByte(WriteAddr, temp_h);
	AT24CXX_WriteOneByte(WriteAddr + 1, temp_l);
}

// 检查AT24CXX是否正常
// 这里用了24XX的最后一个地址(255)来存储标志字.
// 如果用其他24C系列,这个地址要修改
// 返回1:检测失败
// 返回0:检测成功
u8 AT24CXX_Check(void)
{
	u8 temp;

	temp = AT24CXX_ReadOneByte(EE_ADDR); // 读取每次上电都检测AT24CXX
	if (temp == 0x55)
		return 0;
	else // 排除第一次初始化的错误
	{
		AT24CXX_WriteOneByte(EE_ADDR, 0x55);
		temp = AT24CXX_ReadOneByte(EE_ADDR);
		if (temp == 0x55)
		{
			/*
			 * 标记字节丢失有两种可能：
			 *   a) 全新/空白 EEPROM（数据区也是空白）→ 按原逻辑播种默认参数；
			 *   b) 标记字节被写坏，但 MQTT 凭据与闸门标定值仍在 →
			 *      只补回标记，绝不回写默认值，否则会把真实 deviceKey/deviceSecret
			 *      覆盖成 pk001/dev001，造成「设备在平台显示在线、业务上行却被拒」的静默半瘫。
			 */
			u8 pk_first = AT24CXX_ReadOneByte(MQTT_PRODUCTKEY_BYTE_ADDR);
			u16 gate_word = (u16)AT24CXX_ReadLenByte(0x0006, 2); /* gate_maxheight */
			u8 data_blank = ((pk_first == 0x00u || pk_first == 0xFFu) &&
							 (gate_word == 0x0000u || gate_word == 0xFFFFu)) ? 1u : 0u;

			if (data_blank)
			{
				// 写入参数
				paraToEeprom();
			}
			else
			{
				printf("24C02 marker lost but data valid: keep params (pk[0]=%02X, gate=0x%04X)\r\n",
					   (unsigned)pk_first, (unsigned)gate_word);
			}
			return 0;
		}
	}
	return 1;
}

// 在AT24CXX里面的指定地址开始读出指定个数的数据
// ReadAddr :开始读出的地址 对24c02为0~255
// pBuffer  :数据数组首地址
// NumToRead:要读出数据的个数
void AT24CXX_Read(u16 ReadAddr, u8 *pBuffer, u16 NumToRead)
{
	while (NumToRead)
	{
		*pBuffer++ = AT24CXX_ReadOneByte(ReadAddr++);
		NumToRead--;
	}
}

// 在AT24CXX里面的指定地址开始写入指定个数的数据
// WriteAddr :开始写入的地址 对24c02为0~255
// pBuffer   :数据数组首地址
// NumToWrite:要写入数据的个数
void AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite)
{
	u16 written = 0;

	while (NumToWrite--)
	{
		AT24CXX_WriteOneByte(WriteAddr, *pBuffer);
		WriteAddr++;
		pBuffer++;

		/*
		 * 长写（BLE 一次 128 字节 ≈ 1.28s、OTA 标志 41 字节 ≈ 410ms）期间主循环停摆，
		 * 期间到达的下行帧若只留在 DMA 缓冲里会被下一帧覆盖。这里每 8 字节
		 * 把已收到的帧搬进环形缓冲，把停摆切成长度 ≤80ms 的片段。
		 */
		if ((++written & 0x07u) == 0u)
			UartRx_PumpFromDma(0); /* verbose=0：只搬运不打印，避免写循环里被 printf 拖慢 */
	}
}
