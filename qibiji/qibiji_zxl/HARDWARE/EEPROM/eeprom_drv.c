#include "eeprom_drv.h"
#include "delay.h"
#include "uart_queue.h"
#include <stdio.h>
#include "gprs_config_store.h"
#include "debug_uart.h"
#include "wdg.h"

/* 初始化IIC接口 */
void AT24CXX_Init(void)
{
	IIC_Init();
}

/*
 * 从AT24CXX指定地址读出一个字节
 * ReadAddr:开始读出的地址
 * 返回值  :读到的数据
 */
uint8_t AT24CXX_ReadOneByte(uint16_t ReadAddr)
{
	uint8_t temp = 0;
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
/*
 * 在AT24CXX指定地址写入一个字节
 * WriteAddr  :写入数据的目的地址
 * DataToWrite:要写入的数据
 */
void AT24CXX_WriteOneByte(uint16_t WriteAddr, uint8_t DataToWrite)
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
	IIC_Send_Byte(DataToWrite); 		// 发送字节
	IIC_Wait_Ack();
	IIC_Stop();                  /* 产生一个停止条件 */
	delay_ms(10);                /* AT24C02 最大写周期 5ms，10ms 已是 2 倍余量 */
	WDG_Feed();                  /* 单字节 10ms；32B 槽 320ms、四槽连续 1.28s，必须在此喂狗 */
}

/*
 * 在AT24CXX里面的指定地址开始写入长度为Len的数据
 * 该函数可以写入16bit或者32bit的数据.
 * WriteAddr  :开始写入的地址
 * DataToWrite:数据数组的首地址
 * Len        :要写入数据的长度 2,4
 */
void AT24CXX_WriteLenByte(uint16_t WriteAddr, uint32_t DataToWrite, uint8_t Len)
{
	uint8_t t;
	for (t = 0; t < Len; t++)
	{
		AT24CXX_WriteOneByte(WriteAddr + t, (DataToWrite >> (8 * t)) & 0xff);
	}
}

/* 一次性读取16bit eeprom数据 */
void AT24CXX_Readdatas(uint16_t ReadAddr, uint16_t *Readbuf, uint8_t Len)
{
	uint8_t t;
	for (t = 0; t < Len; t++)
	{
		Readbuf[t] = AT24CXX_ReadLenByte(ReadAddr, 2);
		ReadAddr += 2;
	}
}

/*
 * 在AT24CXX里面的指定地址开始读出长度为Len的数据
 * 该函数用于读出16bit或者32bit的数据.
 * ReadAddr   :开始读出的地址
 * 返回值     :数据
 * Len        :要读出数据的长度2,4
 */
uint32_t AT24CXX_ReadLenByte(uint16_t ReadAddr, uint8_t Len)
{
	uint8_t t;
	uint32_t temp = 0;
	for (t = 0; t < Len; t++)
	{
		temp <<= 8;
		temp += AT24CXX_ReadOneByte(ReadAddr + Len - t - 1);
	}
	return temp;
}

/* 指定地址写入一个字 */
void AT24CXX_WriteWord(uint16_t WriteAddr, uint16_t Data)
{
	uint8_t temp_l = (uint8_t)(Data >> 8);
	uint8_t temp_h = (uint8_t)(Data & 0x00ff);
	AT24CXX_WriteOneByte(WriteAddr, temp_h);
	AT24CXX_WriteOneByte(WriteAddr + 1, temp_l);
}

/*
 * 检查AT24CXX是否正常
 * 这里用了24XX的最后一个地址(255)来存储标志字.
 * 如果用其他24C系列,这个地址要修改
 * 返回1:检测失败
 * 返回0:检测成功
 */
uint8_t AT24CXX_Check(void)
{
	uint8_t temp;

	temp = AT24CXX_ReadOneByte(EE_ADDR); // 读取每次上电都检测AT24CXX
	if (temp == 0x55)
	{
		/* 标记字节有效，但参数仍可能是旧固件遗留的越界值 → 做一次范围校验 */
		uint16_t mc = (uint16_t)AT24CXX_ReadLenByte(0x0004, 2);  /* warn_curr mA */
		uint16_t mh = (uint16_t)AT24CXX_ReadLenByte(0x0006, 2);  /* maxheight mm */
		uint16_t mt = (uint16_t)AT24CXX_ReadLenByte(0x0008, 2);  /* maxturns */

		if (!((mc == 0u) || (mc >= 500u  && mc <= 10000u)) ||
		    !((mh == 0u) || (mh >= 200u  && mh <= 5000u))  ||
		    !((mt == 0u) || (mt >= 10u   && mt <= 5000u)))
		{
			DEBUG_UART_SendString("24C02 param out of range: reseed\r\n");
			paraToEeprom();
		}
		return 0;
	}
	else 																// 排除第一次初始化的错误
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
			uint8_t pk_first = AT24CXX_ReadOneByte(MQTT_PRODUCTKEY_BYTE_ADDR);
			uint16_t gate_word = (uint16_t)AT24CXX_ReadLenByte(0x0006, 2); /* gate_maxheight */
			uint8_t data_blank = ((pk_first == 0x00u || pk_first == 0xFFu) &&
							 (gate_word == 0x0000u || gate_word == 0xFFFFu)) ? 1u : 0u;

			if (data_blank)
			{
				// 写入参数
				DEBUG_UART_SendString("24C02 blank: seed defaults via paraToEeprom\r\n");
				paraToEeprom();
			}
			else
			{
					char line[80];
					(void)snprintf(line, sizeof(line),
													"24C02 marker lost but data valid: keep params (pk[0]=%02X, gate=0x%04X)\r\n",
													(unsigned)pk_first, (unsigned)gate_word);
					DEBUG_UART_SendString(line);
			}
			return 0;
		}
	}
	return 1;
}

/*
 * 在AT24CXX里面的指定地址开始读出指定个数的数据
 * ReadAddr :开始读出的地址 对24c02为0~255
 * pBuffer  :数据数组首地址
 * NumToRead:要读出数据的个数
 */
void AT24CXX_Read(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead)
{
	while (NumToRead)
	{
		*pBuffer++ = AT24CXX_ReadOneByte(ReadAddr++);
		NumToRead--;
	}
}

/*
 * 在AT24CXX里面的指定地址开始写入指定个数的数据
 * WriteAddr :开始写入的地址 对24c02为0~255
 * pBuffer   :数据数组首地址
 * NumToWrite:要写入数据的个数
 */
void AT24CXX_Write(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite)
{
	uint16_t written = 0;

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
