#include "RTC.h"
#include <stdbool.h>
#include "ds1302.h"
#include <stdio.h>

// ??????????????????????
static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

//???????
void RtcChip_SetTime(RTC_Time *time) 
{
	u8 buf[7];
	buf[0] = dec_to_bcd(time->second);
  buf[1] = dec_to_bcd(time->minute);
  buf[2] = dec_to_bcd(time->hour);
  buf[3] = dec_to_bcd(time->date);
  buf[4] = dec_to_bcd(time->month);
  buf[5] = dec_to_bcd(time->dayOfWeek);
  buf[6] = dec_to_bcd(time->year);
	ds1302_settime(buf);
}

//??????
void RtcChip_GetTime(RTC_Time *time)  
{
	u8 buf[7];
	ds1302_gettime(buf);	
	time->second 		= buf[0];
  time->minute 		= buf[1];
  time->hour      = buf[2];
  time->date      = buf[3];
  time->month     = buf[4];
  time->dayOfWeek = buf[5];
  time->year      = buf[6];
  printf("RTC get time: 20%02d-%02d-%02d %02d:%02d:%02d\r\n",
           time->year, time->month, time->date,
           time->hour, time->minute, time->second);
}

//??????BCD
uint8_t dec_to_bcd(uint8_t dec)
{
	return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

//BCD??????
uint8_t bcd_to_dec(uint8_t bcd)
{
	return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}


// ???????????
static bool is_leap_year(uint16_t year)
{
   return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}


uint64_t RtcChip_To_MillisTimestamp(RTC_Time *time)
{
    uint8_t m;
    uint16_t y;
    uint16_t full_year = 2000 + time->year;
    uint64_t total_millis;
    // ?????1970???????????????
    uint32_t total_days = 0;

    for (y = 1970; y < full_year; y++)
    {
        total_days += is_leap_year(y) ? 366 : 365;
    }

    for (m = 1; m < time->month; m++)
    {
        uint8_t days = month_days[m - 1];
        if (m == 2 && is_leap_year(full_year))
        {
            days = 29;
        }
        total_days += days;
    }

    total_days += (time->date - 1);

    // ??????????????????????1???
    total_millis = ((uint64_t)total_days * 86400000ULL) +
                   (time->hour * 3600000ULL) +
                   (time->minute * 60000ULL) +
                   (time->second * 1000ULL);

    // ???1?????3600000????
    return total_millis - 8 * 3600000ULL;
}

// ??DS3231???????Unix??????????
uint32_t DS3231_To_Timestamp_s(RTC_Time *time)
{
    // ???????????????????? (2000-2099)
    uint8_t m;
    uint16_t y;
    uint64_t timestamp_seconds;
    uint16_t full_year = 2000 + time->year;

    // ?????1970???????????????
    uint32_t total_days = 0;

    // ????1970????????-1?????????
    for (y = 1970; y < full_year; y++)
    {
        total_days += is_leap_year(y) ? 366 : 365;
    }

    // ????????????????
    for (m = 1; m < time->month; m++)
    {
        total_days += month_days[m - 1];
        // ??????????????????????
        if (m == 2 && is_leap_year(full_year))
        {
            total_days += 1;
        }
    }

    // ?????????????????
    total_days += (time->date - 1);

    // ??????????
    timestamp_seconds = total_days * 86400ULL;
    timestamp_seconds += time->hour * 3600ULL;
    timestamp_seconds += time->minute * 60ULL;
    timestamp_seconds += time->second;

    // ??????????
    return (uint32_t)timestamp_seconds;
}

int Rtc_Time_ZeroSecondsMinuteMod(RTC_Time *out, const RTC_Time *in, unsigned modulus_minutes)
{
    unsigned m;

    if (out == NULL || in == NULL)
        return -1;

    switch (modulus_minutes)
    {
    case 10:
    case 15:
    case 20:
    case 30:
    case 60:
        break;
    default:
        return -1;
    }

    *out = *in;
    out->second = 0;
    m = (unsigned)out->minute;
    out->minute = (uint8_t)((m / modulus_minutes) * modulus_minutes);
    return 0;
}

// ??RTC???????Unix????????????
uint64_t RtcChip_To_MillisTimestamp_s(RTC_Time *time)
{
	// ???????????????????? (2000-2099)
	uint8_t m;
	uint16_t y;
	uint64_t timestamp_millis;
	uint16_t full_year = 2000 + time->year;

	// ?????1970???????????????
	uint32_t total_days = 0;

	// ????1970????????-1?????????
	for (y = 1970; y < full_year; y++)
	{
			total_days += is_leap_year(y) ? 366 : 365;
	}

	// ????????????????
	for (m = 1; m < time->month; m++)
	{
			total_days += month_days[m - 1];
			// ??????????????????????
			if (m == 2 && is_leap_year(full_year))
			{
					total_days += 1;
			}
	}

	// ?????????????????
	total_days += (time->date - 1);

	// ???????????
	timestamp_millis = total_days * 86400ULL * 1000ULL; // ??????????
	timestamp_millis += time->hour * 3600ULL * 1000ULL; // ?????????
	timestamp_millis += time->minute * 60ULL * 1000ULL; // ??????????
	timestamp_millis += time->second * 1000ULL;         // ????????
	// ???????????????????????time->millisecond

	return timestamp_millis;
}

void RtcChip_TimestampMillisToStr(char *buf, uint32_t cap)
{
	RTC_Time t;
	uint64_t ms;

	if (!buf || cap < 2)
	{
		if (buf && cap > 0)
			buf[0] = '\0';
		return;
	}
	RtcChip_GetTime(&t);
	ms = RtcChip_To_MillisTimestamp(&t);
	(void)snprintf(buf, (size_t)cap, "%llu", (unsigned long long)ms);
}
