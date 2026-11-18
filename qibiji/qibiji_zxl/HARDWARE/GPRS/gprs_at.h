#ifndef __GPRS_AT_H
#define __GPRS_AT_H

#include "gprs_config.h"

/* ---------------------------------------------------------------------------
 * 链路状态机 + AT 命令初始化 —— 六态闭环
 *
 * 职责：ST_OFFLINE → ST_AT_PUSH → ST_AT_WAIT → ST_NORMAL → 四态主路径，
 *       外加 ST_AT_DELAY / ST_AT_BACKOFF 失败退避。
 *       3 条初始化 AT（ICCID / MQTTCID / CSQ）+ 周期 CSQ + 离线硬复位。
 *
 * 用法：每 10ms 节拍外的主循环每轮调 GPRS_AT_RxPump() 与
 *       GPRS_AT_StateMachine()。
 * --------------------------------------------------------------------------- */

/* State-machine interface; semantics are documented in gprs_at.c. */

/*==================== Link / AT result flags ====================*/
extern volatile uint8_t g_atflag_online;    /* banner seen -> UART ready */
extern volatile uint8_t g_at_flag_iccid;    /* +ICCID reply seen */
extern volatile uint8_t g_at_flag_mqttcid;  /* +MQTTCID reply seen */
extern volatile uint8_t g_at_flag_csq;      /* +CSQ reply seen */

/*==================== API ====================*/
/* Run the link / AT-init state machine (call every loop). */
void GPRS_AT_StateMachine(void);
/* Return 1 when the link is in NORMAL state (global TX gate). */
uint8_t gprs_is_ready(void);
/* Record the module banner seen on UART. */
void GPRS_AT_OnBanner(void);
/* Move staged UART frames into the RX queue. */
void GPRS_AT_RxPump(void);

#endif
