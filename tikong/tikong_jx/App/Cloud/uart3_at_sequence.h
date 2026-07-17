#ifndef UART3_AT_SEQUENCE_H
#define UART3_AT_SEQUENCE_H

#include "sys.h"

#define UART3_AT_SEQ_BUSY         0u
#define UART3_AT_SEQ_DONE_ALL_OK  1u
#define UART3_AT_SEQ_ABORTED      2u

/** 非零：主循环进入 AT 序列模式（占位触发，可改为 EEPROM/命令等） */
extern volatile u8 g_uart3_at_sequence_request;

void uart3_at_sequence_request_set(u8 en);

u8 uart3_at_sequence_should_run(void);

/** @return UART3_AT_SEQ_BUSY | UART3_AT_SEQ_DONE_ALL_OK | UART3_AT_SEQ_ABORTED */
u8 uart3_at_sequence_poll(void);

#endif
