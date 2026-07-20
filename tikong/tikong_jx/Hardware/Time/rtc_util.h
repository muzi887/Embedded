#ifndef RTC_UTIL_H
#define RTC_UTIL_H

#include "RTC.h"

void ds1302_shift_minutes(RTC_Time *out, const RTC_Time *in, int dm);
char *create_ymdhm(const RTC_Time *currenttim);

#endif

