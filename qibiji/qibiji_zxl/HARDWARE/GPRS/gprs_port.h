#ifndef __GPRS_PORT_H
#define __GPRS_PORT_H

#include "gprs_config.h"

/* ---------------------------------------------------------------------------
 * USART2 串口驱动（GPRS 物理通道）—— 收发底层
 *
 * 职责：初始化 USART2 + DMA + IDLE 中断；提供 Send() 单字节发送；
 *       维护双槽暂存（stage slots）供上层搬运完整下行帧。
 *
 * 用法：上电 GPRS_PORT_Init(baud)；此后每轮调 GPRS_AT_RxPump() 搬运帧。
 * --------------------------------------------------------------------------- */

#define GPRS_RX_STAGE_SLOTS 2u
#define GPRS_DMA_BUF_LEN    512u

/* banner from module after power-on/reset */
extern volatile uint8_t g_gprs_banner_seen;

void GPRS_PORT_Init(uint32_t baud);
void GPRS_PORT_Send(const char *buf, uint16_t len);

/* take one staged frame; returns 1 and fills *buf/*len if available */
uint8_t GPRS_PORT_RxStageTake(char **buf, uint16_t *len);

/* length-bounded substring search (DMA buffer is not NUL-terminated) */
uint8_t GPRS_PORT_BufContains(const char *buf, uint16_t len, const char *needle);

#endif