#ifndef	_CRC16_H_
#define	_CRC16_H_

#include "stm32f10x.h"

u16 crc16(u8 *puchMsg,u16 usDataLen);               //crc校验
u16 CharToInt(u8 *pChar);                           //字符转整形

//CRC16_CCITT_FALSE 算法
u16 CRC16_CCITT_FALSE(unsigned char* puchMsg, u16 usDataLen);

#endif	//_MODBUS_H_
