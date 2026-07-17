#ifndef __QR_COMM_H
#define __QR_COMM_H

#include "sys.h"
#include "RTC.h"

void QRProcessUart5(void);
void QRProcessUart4(void);
void ds3231_shift_minutes(RTC_Time *out, const RTC_Time *in, int dm);

#endif
