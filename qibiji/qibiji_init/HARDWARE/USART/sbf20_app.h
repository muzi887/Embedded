#ifndef SBF20_APP_H
#define SBF20_APP_H

#include "sys.h"

/*
 * SBF-20 application layer for the PC configuration tool.
 * The Qt application is the initiator, the MCU only answers.
 *
 * Supported ops (compact UTF-8 JSON opaque):
 *   {"op":"read","configs":["gate","ota"]}
 *   {"op":"write","params":{"motor_warning_curr_upper":..,"OTA_ver":.., ...}}
 *   {"op":"at","cmd":"AT+CSQ?"}     -> USART2, 4G module (WH-LTE-7S1)
 *   {"op":"at3","cmd":"AT+LADDR"}   -> USART3, BLE module (DX-BT24)
 *
 * read/write cover the gate parameters and the OTA info block only. The 4G/
 * MQTT parameters are not stored on this side any more: the PC tool pushes
 * them to the module through the "at" passthrough.
 */

void sbf20_app_init(void);

const u8 *sbf20_app_last_rx_json(void);
u16 sbf20_app_last_rx_json_len(void);

#endif
