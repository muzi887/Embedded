#ifndef __EEPROM_DRV_H
#define __EEPROM_DRV_H

#include "stm32f10x.h"   /* 提供 u8 / u16 / u32 类型 */

/* AT24C02 字节级/字级读写 + Check 播种 */

/* ---- 芯片型号容量（最大可访问字节地址） ---- */
#define AT24C01     127
#define AT24C02     255
#define AT24C04     511
#define AT24C08     1023
#define AT24C16     2047
#define AT24C32     4095
#define AT24C64     8191
#define AT24C128    16383
#define AT24C256    32767

#define EE_TYPE     AT24C02         /* 本板用 AT24C02（256 字节） */
#define EE_ADDR     250u            /* Marker 标志字节地址 */
			  
/* ---- 字节级读写 ---- */
uint8_t  AT24CXX_ReadOneByte(uint16_t ReadAddr);
void     AT24CXX_WriteOneByte(uint16_t WriteAddr, uint8_t DataToWrite);

/* ---- 字级（u16 little endian）读写 ---- */
void     AT24CXX_WriteWord(uint16_t WriteAddr, uint16_t Data);                        //从指定地址开始写入指定长度的数据
uint32_t AT24CXX_ReadLenByte(uint16_t ReadAddr, uint8_t Len);  /* 2 或 4 */           //指定地址开始读取指定长度的数据
void     AT24CXX_WriteLenByte(uint16_t WriteAddr, uint32_t DataToWrite, uint8_t Len); //指定地址开始写入指定长度的数据

/* ---- 定长缓冲 ---- */
void     AT24CXX_Read(uint16_t ReadAddr, uint8_t *pBuffer, uint16_t NumToRead);       //从指定地址开始读取指定长度的数据
void     AT24CXX_Write(uint16_t WriteAddr, uint8_t *pBuffer, uint16_t NumToWrite);    //从指定地址开始写入指定长度的数据

/* ---- 批量读取闸门参数（10 × u16） ---- */
void     AT24CXX_Readdatas(uint16_t ReadAddr, uint16_t *Readbuf, uint8_t Len);        

/* ---- 上电自检 + 播种 ---- */
uint8_t  AT24CXX_Check(void);

/* ---- 初始化（IIC_Init） ---- */
void     AT24CXX_Init(void);

#endif
















