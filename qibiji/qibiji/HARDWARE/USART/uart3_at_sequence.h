#ifndef UART3_AT_SEQUENCE_H
#define UART3_AT_SEQUENCE_H

#include "sys.h"

#define UART3_AT_SEQ_BUSY         0u
#define UART3_AT_SEQ_DONE_ALL_OK  1u
#define UART3_AT_SEQ_ABORTED      2u

/** 非零：主循环进入 AT 序列模式（占位触发，可改为 EEPROM/命令等） */
extern volatile u8 g_uart3_at_sequence_request;

/* 可容纳 MQTT_KEY_EEPROM_LEN(32) 字符 + '\0' */
extern char g_mqtt_productKey[33];
extern char g_mqtt_deviceKey[33];
extern char g_mqtt_deviceSecret[33];
extern char g_mqtt_addr[33];

void uart3_at_sequence_request_set(u8 en);

u8 uart3_at_sequence_should_run(void);

/** 将 productKey/deviceKey 写入 EEPROM */
void mqtt_keys_save_to_eeprom(void);

/** @return UART3_AT_SEQ_BUSY | UART3_AT_SEQ_DONE_ALL_OK | UART3_AT_SEQ_ABORTED */
u8 uart3_at_sequence_poll(void);

/** @return 1：本圈跳过常规任务；0：继续执行常规任务 */
u8 uart3_at_sequence_main_loop_tick(void);

#endif
