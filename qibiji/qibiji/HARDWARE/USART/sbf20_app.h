#ifndef SBF20_APP_H
#define SBF20_APP_H

#include "sys.h"

/* 近场 SBF-20 应用层：读/写 JSON 配置（App 发起，MCU 应答） */
void sbf20_app_init(void);

const u8 *sbf20_app_last_rx_json(void);
u16 sbf20_app_last_rx_json_len(void);

#endif
