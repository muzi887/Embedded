#ifndef PASS_CRYPTO_H
#define PASS_CRYPTO_H

#include "rtc.h"

int VerifySignature(const char *sha1_hexstr, const char *valid_data);
char toLowerHex(char c);
int hex2val(char c);
int CheckValidPeriod_WithNow(const char *valid_data, const RTC_Time *now);
void adjust_begin_time(char *begin14);
void Get_newTime(char *time, int minute, int act);
void MakeTimestamp14(const RTC_Time *t, char out14[15]);

#endif
