#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f10x.h"

/* ---- OTA 状态机状态码 ---- */
#define OTA_STATE_WAIT_NET      0   // 等 4G 模块 AT 握手应答
#define OTA_STATE_AT_OK         1   // AT 握手完成，等模块联网上线（LINK 变 1）
#define OTA_STATE_ONLINE        2   // 已联网，可发送版本询问
#define OTA_STATE_SEND_VERSION  3   // 已发 putVersion，等平台回包
#define OTA_STATE_HANDLE_REPLY  4   // 消费平台回包（版本应答 / 下载帧）

extern u8 g_ota_state;   // 状态机当前状态（OTA_STATE_* 之一）

#endif