#ifndef __UART_QUEUE_H
#define __UART_QUEUE_H

#include "gprs_config.h"

/* ---------------------------------------------------------------------------
 * USART2 收发队列 —— 两行一发一收，分离业务与中断
 *
 * 职责：TX 队列（8 项，AT/JSON 两种类型 + 一发一收状态机）；
 *       RX 队列（环形缓冲 + 帧队列 + 双槽暂存）；
 *       统一 timeout/retry/debounce 机制。
 *
 * 用法：Init() 一次；每轮调 UartTx_Queue_Process(g_sys_ms)；
 *       TX 侧调 UartTx_Push_AT() / UartTx_Push_JSON()；
 *       RX 侧调 UartRx_Pop() 出帧。
 * --------------------------------------------------------------------------- */

/* Interface declarations only; parameter/return details are documented in uart_queue.c. */

/*==================== TX item types and callbacks ====================*/
typedef enum { TX_ITEM_AT, TX_ITEM_JSON } UartTxItemType;
typedef void (*UartTx_AT_Callback)(char *resp, uint16_t len);
typedef void (*UartTx_JSON_Callback)(char *random, uint16_t len);

/*==================== TX configuration ====================*/
#define UART_TX_QUEUE_SIZE    8u                  /* max pending TX items */
#define UART_TX_AT_CMD_LEN    32u                 /* AT command buffer bytes */
#define UART_TX_AT_RESP_LEN   16u                 /* AT reply keyword buffer bytes */
#define UART_TX_AT_TIMEOUT_MS 1000u               /* AT reply timeout (0 = default) */
#define UART_TX_AT_MAX_RETRY  2u                  /* AT retries after timeout */
#define UART_TX_JSON_LEN      1024u               /* JSON payload buffer bytes */
#define UART_TX_JSON_RANDOM_LEN 20u               /* reply-match token bytes */
#define UART_TX_JSON_FIRE_AND_FORGET_GAP_MS 300u  /* min gap for JSON with no callback */
#define UART_TX_JSON_TIMEOUT_MS 5000u             /* JSON reply timeout (0 = default) */

/*==================== RX configuration ====================*/
#define UART_RX_RING_SIZE     3072u  /* RX ring bytes */
#define UART_RX_QUEUE_SIZE    8u     /* max queued frames */
#define UART_RX_ITEM_MAX      1024u  /* max bytes per frame */

/*==================== TX state machine states ====================*/
#define UART_TX_STATE_IDLE    0u  /* no item in flight */
#define UART_TX_STATE_WAITING 1u  /* head sent, awaiting reply */

/*==================== API: TX queue ====================*/
/* Reset the TX queue (call once at startup). */
void UartTx_Queue_Init(void);
/* Reset the RX ring and frame queue (call once at startup). */
void UartRx_Queue_Init(void);
/* Queue an AT command; returns 1 if queued. */
uint8_t UartTx_Push_AT(const char *cmd, uint8_t cmd_len, const char *resp_key,
                       UartTx_AT_Callback cb, uint16_t timeout_ms, uint8_t max_retry);
/* Queue a JSON payload; returns 0 if not NORMAL or the queue is full. */
uint8_t UartTx_Push_JSON(const char *data, uint16_t len, const char *random,
                         UartTx_JSON_Callback cb, uint16_t timeout_ms, uint8_t max_retry);
/* Drive the TX state machine (call each loop with g_sys_ms). */
void UartTx_Queue_Process(uint32_t tick_ms);
/* Feed a reply to the TX matcher; strtype 1 = AT, 2 = JSON. */
uint8_t UartTx_OnRecv(char *data, uint16_t len, uint8_t strtype);
/* Returns 1 if no TX item is pending. */
uint8_t UartTx_IsEmpty(void);

/*==================== API: RX queue ====================*/
/* Append raw bytes to the RX ring. */
void UartRx_Append(char *data, uint16_t len);
/* Split buffered bytes into queued frames. */
void UartRx_Extract(void);
/* Pop one frame; returns 1 if a frame was available. */
uint8_t UartRx_Pop(char *out, uint16_t max_len, uint16_t *out_len);
/* Returns 1 if no frame is pending. */
uint8_t UartRx_IsEmpty(void);
/* Move ISR-staged frames into the RX queue. */
void UartRx_PumpFromDma(uint8_t verbose);
/* Count of RX bytes dropped when the ring is full (0 = never truncated). */
extern volatile uint32_t g_uartRx_dropped_bytes;

/*==================== JSON helper (stage 2) ====================*/
/* Extract one complete JSON object; returns 0 on success (-1 if none), length via out_len. */
int extract_json_from_string(char *buf, uint16_t buf_len,
                             char **out_start, uint16_t *out_len);

#endif
