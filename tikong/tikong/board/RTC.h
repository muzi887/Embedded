#ifndef __RTC_H
#define __RTC_H
#include "sys.h"
#include <stdint.h>


typedef struct {
    uint8_t year;
    uint8_t month;
    uint8_t date;
    uint8_t dayOfWeek;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
}RTC_Time;

//????????
void RtcChip_SetTime(RTC_Time *time);  //???????
void RtcChip_GetTime(RTC_Time *time);  //??????

uint8_t dec_to_bcd(uint8_t dec); // ??????BCD
uint8_t bcd_to_dec(uint8_t bcd); // BCD?????

uint64_t RtcChip_To_MillisTimestamp_s(RTC_Time *time);  //????
uint64_t RtcChip_To_MillisTimestamp(RTC_Time *time);
uint32_t DS3231_To_Timestamp_s(RTC_Time *time); //S????

/** 秒归零；minute 向下对齐到 modulus_minutes 的倍数。modulus_minutes 仅允许 10/15/20/30/60，否则返回 -1。out 可与 in 同址。 */
int Rtc_Time_ZeroSecondsMinuteMod(RTC_Time *out, const RTC_Time *in, unsigned modulus_minutes);

/** RTC ms Unix timestamp as decimal string (same rule as RtcChip_To_MillisTimestamp); buf >= 22 bytes recommended */
void RtcChip_TimestampMillisToStr(char *buf, uint32_t cap);

#endif
