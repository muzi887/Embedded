#ifndef __EEPROM_H
#define __EEPROM_H	 
#include "delay.h"
#include "iic.h"


// EEPROM 设备地址 (AT24C02: 0xA0)
#define EEPROM_DEVICE_ADDR 0x57 
//预留黑名单存放空间
#define PUBLICKEY_ADR     4000

void EEPROM_WriteByte(uint16_t eeaddress, uint8_t data);
void EEPROM_WritePage(uint16_t eeaddresspage, uint8_t* data, uint8_t length);
uint8_t EEPROM_ReadByte(uint16_t eeaddress);
void EEPROM_ReadBuffer(uint16_t eeaddress, uint8_t* buffer, uint16_t length);

void WriteKey(const char *public_key);
void ReadKey(void);


#endif

