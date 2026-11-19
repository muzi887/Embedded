#ifndef __GPRS_RTC_H
#define __GPRS_RTC_H

#include "gprs_config.h"

/* Set RTC from unix seconds; 0 keeps current time. */
void     GPRS_RTC_Set(uint32_t unix_sec);
/* Get current unix seconds. */
uint32_t GPRS_RTC_GetUnixSec(void);
/* Get milliseconds elapsed within current second, range 0-999. */
uint16_t GPRS_RTC_GetMsOfSec(void);
/* Initialize RTC hardware */
void GPRS_RTC_Init(void);
/* Build a 13-digit unix-millisecond value, used as JSON envelope id. */
int64_t  GPRS_RTC_NowMs(void);
#endif