#ifndef __BLACKLIST_H
#define __BLACKLIST_H	 
#include "eeprom.h"
#include "usart1.h"

#define MAX_ARRAY_SIZE 200  /* 黑名单 EEPROM 0..2199；设备与 MQTT 区在 3777..4095 见 eeprom.h */
#define DATA_SIZE 11        // 每条数据的大小（字节）

// 黑名单数据结构体
typedef struct {
    uint8_t data[11];   // 数据，可以根据需要调整数据的大小
    uint8_t isEmpty;    // 空标志（1表示空，0表示非空）
} DataEntry;

// 全局结构体数组
extern DataEntry dataArray[MAX_ARRAY_SIZE];

void loadDataFromEEPROM(void);
int addDataToBlacklist(uint8_t *data);
int deleteDataFromBlacklist(uint8_t *data);
void clearPublicKeySpace(void);

#endif

