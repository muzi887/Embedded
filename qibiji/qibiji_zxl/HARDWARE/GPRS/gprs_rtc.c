#include "gprs_rtc.h"
#include "ds1302.h"
#include "timer.h"          /* g_sys_ms */
#include <stdio.h>

/* DS1302 holds local time; platform serverTime is UTC. */
#define RTC_TZ_OFFSET_SEC (8UL * 3600UL)

static const uint8_t s_days_in_month[12] =
    {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

static uint8_t s_is_leap(int y)
{
    return (uint8_t)((y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0)));
}

/* 1 = Sunday ... 7 = Saturday, matches DS1302 week register. */
static uint8_t s_weekday(int y, int m, int d)
{
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
        y -= 1;
    return (uint8_t)(((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7) + 1);
}

/* unix seconds -> calendar (UTC+8 local) */
static void s_unix_to_cal(uint32_t utc_sec, int *year, int *mon, int *day,
                          int *hour, int *min, int *sec)
{
    uint32_t t = utc_sec + RTC_TZ_OFFSET_SEC;
    uint32_t days = t / 86400UL;
    uint32_t rem  = t % 86400UL;
    int y = 1970;
    int m;

    *hour = (int)(rem / 3600UL);
    rem %= 3600UL;
    *min = (int)(rem / 60UL);
    *sec = (int)(rem % 60UL);

    for (;;)
    {
        int dim = 365 + (int)s_is_leap(y);
        if (days < (uint32_t)dim)
            break;
        days -= (uint32_t)dim;
        y++;
    }
    *year = y;

    for (m = 1; m <= 12; m++)
    {
        int dim = (int)s_days_in_month[m - 1];
        if (m == 2)
            dim += (int)s_is_leap(y);
        if (days < (uint32_t)dim)
            break;
        days -= (uint32_t)dim;
    }
    *mon = m;
    *day = (int)days + 1;
}

/* calendar (UTC+8 local) -> unix seconds */
static uint32_t s_cal_to_unix(int year, int mon, int day, int hour, int min, int sec)
{
    uint32_t days = 0;
    int y, m;

    if (year < 1970 || mon < 1 || mon > 12 || day < 1 || day > 31)
        return 0u;

    for (y = 1970; y < year; y++)
        days += (uint32_t)(365 + (int)s_is_leap(y));
    for (m = 1; m < mon; m++)
    {
        days += s_days_in_month[m - 1];
        if (m == 2)
            days += s_is_leap(year);
    }
    days += (uint32_t)(day - 1);

    {
        uint32_t local_sec = days * 86400UL
                           + (uint32_t)hour * 3600UL
                           + (uint32_t)min * 60UL
                           + (uint32_t)sec;
        return (local_sec >= RTC_TZ_OFFSET_SEC) ? (local_sec - RTC_TZ_OFFSET_SEC) : 0u;
    }
}

/* convert dec to BCD code */
static uint8_t s_dec2bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

void GPRS_RTC_Init(void)
{
    ds1302_init();
}

void GPRS_RTC_Set(uint32_t utc_sec)
{
    uint8_t bcd[7];
    int year, mon, day, hour, min, sec;

    if (utc_sec == 0u)
        return;

    s_unix_to_cal(utc_sec, &year, &mon, &day, &hour, &min, &sec);
    if (year < 2000 || year > 2099)
        return;

    bcd[0] = s_dec2bcd(sec);
    bcd[1] = s_dec2bcd(min);
    bcd[2] = s_dec2bcd(hour);
    bcd[3] = s_dec2bcd(day);
    bcd[4] = s_dec2bcd(mon);
    bcd[5] = s_weekday(year, mon, day);
    bcd[6] = s_dec2bcd(year % 100);

    ds1302_settime(bcd);   /* return value unreliable: see F2-1 写入芯片*/
}

uint32_t GPRS_RTC_GetUnixSec(void)
{
    char t[7];   /* decimal, order: sec/min/hour/day/mon/week/year */

    __disable_irq();          /* F2-3: protect the bit-bang sequence */
    ds1302_dataread(t);
    __enable_irq();

    if ((uint8_t)t[6] > 99u || (uint8_t)t[4] < 1u || (uint8_t)t[4] > 12u ||
        (uint8_t)t[3] < 1u || (uint8_t)t[3] > 31u)
        return 0u;            /* never synced or battery lost */

    return s_cal_to_unix(2000 + (int)(uint8_t)t[6], (int)(uint8_t)t[4],
                         (int)(uint8_t)t[3], (int)(uint8_t)t[2],
                         (int)(uint8_t)t[1], (int)(uint8_t)t[0]);
}

uint16_t GPRS_RTC_GetMsOfSec(void)
{
    return (uint16_t)(g_sys_ms % 1000u);
}

int64_t GPRS_RTC_NowMs(void)
{
    return (int64_t)GPRS_RTC_GetUnixSec() * 1000LL + (int64_t)GPRS_RTC_GetMsOfSec();
}