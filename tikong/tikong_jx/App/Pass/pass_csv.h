#ifndef __PASS_CSV_H
#define __PASS_CSV_H

#include <stdio.h>

#define QR_RECV_CSV_MAX 16
/* 含 NUL：单列最多 QR_RECV_CSV_FIELD-1 字符。梯控权限列可达 64+ 字符；MQTT 等最长 64 */
#define QR_RECV_CSV_FIELD 128

extern char s_recv_csv_field[QR_RECV_CSV_MAX][QR_RECV_CSV_FIELD];
extern int s_recv_csv_count;

void qr_split_s_received_by_comma(const char *src);

#endif
