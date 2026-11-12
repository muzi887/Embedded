#ifndef __24CXX_H
#define __24CXX_H
#include "myiic.h"   
								  
#define AT24C01		127
#define AT24C02		255
#define AT24C04		511
#define AT24C08		1023
#define AT24C16		2047
#define AT24C32		4095
#define AT24C64	  8191
#define AT24C128	16383
#define AT24C256	32767  

// 返回值定义
#define AT24CXX_OK          0   // 写入成功
#define AT24CXX_ERROR       1   // 写入失败（ACK失败）
#define AT24CXX_TIMEOUT     2   // 超时
#define AT24CXX_ADDR_ERROR  3   // 地址错误

//根据EE_TYPE为AT24C04
#define EE_TYPE   AT24C02

#define EE_ADDR      250//标志寄存器
		  
u8   AT24CXX_ReadOneByte(u16 ReadAddr);							//指定地址读出一个字节
void AT24CXX_WriteOneByte_old(u16 WriteAddr,u8 DataToWrite);		//指定地址写入一个字节
u8 AT24CXX_WriteOneByte(u16 WriteAddr,u8 DataToWrite);
void AT24CXX_WriteLenByte(u16 WriteAddr,u32 DataToWrite,u8 Len);//指定地址开始写入指定长度的数据
u32  AT24CXX_ReadLenByte(u16 ReadAddr,u8 Len);					//指定地址开始读出指定长度的数据
void AT24CXX_Write_old(u16 WriteAddr,u8 *pBuffer,u16 NumToWrite);	//在指定地址开始写入指定长度的数据
u8 AT24CXX_Write(u16 WriteAddr,u8 *pBuffer,u16 NumToWrite);
void AT24CXX_Read(u16 ReadAddr,u8 *pBuffer,u16 NumToRead);   	//在指定地址开始读出指定长度的数据

void AT24CXX_Readdatas(u16 ReadAddr,u16 *Readbuf,u8 Len);  //读取多个eeprom数据

u8 AT24CXX_Check(void);  //检测函数
void AT24CXX_Init(void); //初始化IIC

void AT24CXX_WriteWord(u16 WriteAddr,u16 Data);

void Eeprom_Wpara(void);

#endif
















