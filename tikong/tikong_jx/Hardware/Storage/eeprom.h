#ifndef __EEPROM_H
#define __EEPROM_H	 
#include "delay.h"
#include "24cxx.h"


// EEPROM 设备地址（AT24Cxx
#define EEPROM_DEVICE_ADDR 0xA0
/*
 * AT24C32 = 4096 字节(0..4095)。有效地址须 < 4096。
 * 布局：0..2199 黑名单(200*11) | 2200..3753 保留 | 3754..3776 漂移+楼层限定(23B)
 *      | 3777..4095 设备+MQTT(319B，末字节 4095)
 */
#define AT24C32_CAPACITY_BYTES          4096u
#define PUBLIC_DEVICE_ID_ADDR           3777
#define PUBLIC_DEVICE_ID_LEN            16
#define PUBLIC_DEVICE_PASSWORD_ADDR     3793
#define PUBLIC_DEVICE_PASSWORD_LEN      16
#define PUBLIC_DEVICE_TYPE_ADDR         3809
#define PUBLIC_DEVICE_TYPE_LEN          1
#define PUBLIC_DEVICE_CODE_ADDR         3810
#define PUBLIC_DEVICE_CODE_LEN          16
#define PUBLIC_KEY_ADDR                 3826
#define PUBLIC_KEY_LEN                  16
#define PUBLIC_DEVICE_MODE_ADDR         3842
#define PUBLIC_DEVICE_MODE_LEN          30
#define PUBLIC_MQTT_ADDR_ADDR           3872
#define PUBLIC_MQTT_ADDR_LEN            32
#define PUBLIC_MQTT_PRODUCTKEY_ADDR     3904
#define PUBLIC_MQTT_PRODUCTKEY_LEN      64
#define PUBLIC_MQTT_DEVICEKEY_ADDR      3968
#define PUBLIC_MQTT_DEVICEKEY_LEN       64
#define PUBLIC_MQTT_DEVICESECRET_ADDR   4032
#define PUBLIC_MQTT_DEVICESECRET_LEN    64
/* 3754+4+5+14=3777，与 PUBLIC_DEVICE_ID_ADDR 对接 */
#define PUBLIC_DRIFT_TIME_ADDR          3754
#define PUBLIC_DRIFT_TIME_LEN           4
#define PUBLIC_FLOORS_LIMIT_ADDR        3758
#define PUBLIC_FLOORS_LIMIT_LEN         5
#define PUBLIC_FLOORS_LIMIT_TIME_ADDR   3763
#define PUBLIC_FLOORS_LIMIT_TIME_LEN    14
/* 区末：4032+64-1 = 4095 */

void EEPROM_WriteByte(uint16_t eeaddress, uint8_t data);
void EEPROM_WritePage(uint16_t eeaddresspage, uint8_t* data, uint8_t length);
uint8_t EEPROM_ReadByte(uint16_t eeaddress);
/** 读连续 length 字节到 buffer。tag 非 NULL 时打印：与 EEPROM_ReadTextField 类似，首字节 0xFF 输出 (empty)，否则 tag + 十六进制字节 */
void EEPROM_ReadBuffer(uint16_t eeaddress, uint8_t *buffer, uint16_t length, const char *tag);

void WriteKey(const char *key_value,int addr,int len);
/** 写入 len 字节二进制（可含 0x00）；data 为 NULL 时将该地址起的 len 字节清零 */
void WriteRawBytes(const uint8_t *data, int addr, int len);
void ReadKey(void);
/** 将 ReadKey 所读 EEPROM 字段全部清零，并调用 ReadKey 刷新内存与应用解析状态 */
void EEPROM_ClearAllParams(void);

#endif

