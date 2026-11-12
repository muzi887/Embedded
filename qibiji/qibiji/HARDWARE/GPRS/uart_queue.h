#ifndef __UART_QUEUE_H
#define __UART_QUEUE_H

#include "stm32f10x.h"

/*==================== 发送项类型 ====================*/
typedef enum
{
	TX_ITEM_AT,
	TX_ITEM_JSON,
} UartTxItemType;

/*==================== AT 响应回调 ====================*/
typedef void (*UartTx_AT_Callback)(char *resp, u16 len);
/*==================== JSON 响应回调 ====================*/
typedef void (*UartTx_JSON_Callback)(char *random, u16 len);

/*==================== 配置宏 ====================*/
#define UART_TX_QUEUE_SIZE 8
#define UART_TX_AT_CMD_LEN 32
#define UART_TX_AT_RESP_LEN 16
#define UART_TX_AT_TIMEOUT_MS 1000
#define UART_TX_AT_MAX_RETRY 2
#define UART_TX_JSON_LEN 1024
#define UART_TX_JSON_RANDOM_LEN 20
#define UART_TX_JSON_GAP_MS 1500 /* 1秒统一间隔，满足1.4 */ //目前没用到TODO
#define UART_TX_JSON_FIRE_AND_FORGET_GAP_MS 300 /* 无回调 JSON：发送完成后仅保留安全帧间隔，不等待业务回执 */
#define UART_TX_JSON_TIMEOUT_MS 5000
#define UART_TX_JSON_MAX_RETRY 2

#define UART_RX_RING_SIZE 3072 /* 1536→3072：长阻塞期间可容纳更多待处理下行 */
#define UART_RX_QUEUE_SIZE 8
#define UART_RX_ITEM_MAX 1024

/*==================== 发送状态 ====================*/
#define UART_TX_STATE_IDLE 0
#define UART_TX_STATE_WAITING 1

/*==================== API ====================*/
void UartTx_Queue_Init(void);
void UartRx_Queue_Init(void);

/* AT 入队 */
u8 UartTx_Push_AT(const char *cmd, u8 cmd_len,
				  const char *resp_key, UartTx_AT_Callback callback,
				  u16 timeout_ms, u8 max_retry);

/* JSON 入队 */
u8 UartTx_Push_JSON(const char *data, u16 len,const char *random, UartTx_JSON_Callback callback,
				  u16 timeout_ms, u8 max_retry);

/* 主循环驱动 */
void UartTx_Queue_Process(u32 sys_tick_ms);

/* 接收匹配时调用（AT 响应到达） */
u8 UartTx_OnRecv(char *data, u16 len,u8 strtype);

/* 发送队列是否为空 */
u8 UartTx_IsEmpty(void);

/* 接收侧 */
void UartRx_Append(char *data, u16 len);
void UartRx_Extract(void);

/**
 * 把 USART2(4G) 中断暂存的整帧搬进环形缓冲与帧队列；长阻塞期间也会被调用。
 * @param verbose 1 = 逐帧打印原始内容（`[GPRS] RX[len]: 原文`，用于看模块 banner/AT 回应/下行原文）；
 *                0 = 只搬运不打印。长 EEPROM 写期间传 0，避免在写循环里为打印阻塞约 90ms/帧。
 */
void UartRx_PumpFromDma(u8 verbose);

/* 环形缓冲满导致被丢弃的字节数（0 表示从未截断） */
extern volatile u32 g_uartRx_dropped_bytes;
u8 UartRx_Pop(char *out, u16 max_len, u16 *out_len);
u8 UartRx_IsEmpty(void);

/* 获取线性缓冲剩余数据（用于 OTA 下载的二进制部分） */
void UartRx_GetRemainingBuffer(char **ptr, u16 *len);
void UartRx_Consume(u16 len);

#endif
