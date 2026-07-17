#ifndef DS3231_H
#define DS3231_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "iic.h"
#include "delay.h"

#define DS3231_ADDR 0xD0  // DS3231的I2C地址（左移1位）

typedef struct {
    uint8_t year;
    uint8_t month;
    uint8_t date;
    uint8_t dayOfWeek;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} DS3231_Time;

uint8_t BCD_To_Dec(uint8_t bcd);
uint8_t Dec_To_BCD(uint8_t dec);

//时间初值设定函数
void DS3231_SetTime(DS3231_Time *time);
// 时间获取函数
void DS3231_GetTime(DS3231_Time *time);
uint64_t DS3231_To_MillisTimestamp(DS3231_Time *time);
uint32_t DS3231_To_Timestamp_s(DS3231_Time *time);
uint64_t DS3231_To_MillisTimestamp_s(DS3231_Time *time);
#endif
