#include "uart_queue.h"
#include "gprs_port.h"
#include <string.h>
#include "main.h"

/*==================== TX queue ====================*/

/**
 * UartTxItem - unified TX queue item
 * Supports both AT and JSON types; union aligned to the largest member to save space.
 */
typedef struct
{
	UartTxItemType type; /* item type: TX_ITEM_AT / TX_ITEM_JSON */
	union
	{
		struct /* AT command item */
		{
			char cmd[UART_TX_AT_CMD_LEN];       /* AT command string */
			u8 cmd_len;                         /* command length */
			char resp_key[UART_TX_AT_RESP_LEN]; /* response match key, e.g. "+ICCID" */
			UartTx_AT_Callback callback;        /* callback on matching response */
			u16 timeout_ms;                     /* timeout in milliseconds */
			u8 max_retry;                       /* max retries after timeout */
		} at;
		struct /* JSON data item */
		{
			char data[UART_TX_JSON_LEN];          /* JSON string */
			u16 json_len;                         /* data length */
			char random[UART_TX_JSON_RANDOM_LEN]; /* response match key (random token) */
			UartTx_JSON_Callback callback;        /* callback on matching response */
			u16 timeout_ms;                       /* timeout in milliseconds */
			u8 max_retry;                         /* max retries after timeout */
		} json;
	} u;
} UartTxItem;

/**
 * UartTxQueue - unified TX queue
 * FIFO queue: head is sent then enters WAITING; items with a callback wait for
 * a reply, items without one only keep a safe inter-frame gap.
 */
typedef struct
{
	UartTxItem items[UART_TX_QUEUE_SIZE]; /* queue storage */
	u8 head;                              /* head index (item currently sending / waiting) */
	u8 tail;                              /* tail index (reserved) */
	u8 count;                             /* number of queued items */
	u8 state;                             /* state: IDLE / WAITING */
	u32 send_tick;                        /* timestamp of the last send (sys_ms) */
	u8 at_retry_cnt;                      /* AT timeout retry counter */
	u8 json_retry_cnt;                    /* JSON timeout retry counter (separate from AT to avoid mixing on type switch) */
} UartTxQueue;

static UartTxQueue txQueue;

/*==================== RX queue ====================*/
typedef struct
{
	char data[UART_RX_ITEM_MAX];
	u16 len;
} UartRxItem;

typedef struct
{
	UartRxItem items[UART_RX_QUEUE_SIZE];
	u8 head, tail, count;
} UartRxQueue;

static UartRxQueue rxQueue;

/* linear buffer */
static char rxRingBuf[UART_RX_RING_SIZE + 1];
static u16 rxRingLen;

volatile u32 g_uartRx_dropped_bytes = 0; /* bytes dropped when the ring buffer is full (0 = never truncated) */

/**
 * @brief Find a substring inside a length-bounded buffer (not necessarily NUL-terminated).
 * @param buf     buffer to search
 * @param buf_len number of valid bytes in buf
 * @param needle  NUL-terminated substring to find
 * @return pointer to the first match in buf, or NULL if not found
 */
static char *find_substr(const char *buf, u16 buf_len, const char *needle)
{
	u16 i;
	u16 nlen = strlen(needle);
	if (buf_len < nlen)
		return NULL;
	for (i = 0; i <= buf_len - nlen; i++)
	{
		if (memcmp(buf + i, needle, nlen) == 0)
			return (char *)(buf + i);
	}
	return NULL;
}

/*==================== TX queue implementation ====================*/
/** @brief Reset the TX queue to empty; call once at startup. */
void UartTx_Queue_Init(void)
{
	memset(&txQueue, 0, sizeof(txQueue));
}

/** @brief Pop the head item, advance head and set the TX state back to IDLE. */
static void tx_dequeue(void)
{
	txQueue.head = (txQueue.head + 1) % UART_TX_QUEUE_SIZE;
	txQueue.count--;
	txQueue.state = UART_TX_STATE_IDLE;
}

/**
 * @brief Enqueue an AT command for one-send-one-receive transmission.
 * @param cmd       command bytes (not necessarily NUL-terminated)
 * @param cmd_len   number of command bytes
 * @param resp_key  keyword expected in the reply, e.g. "+ICCID"
 * @param callback  called with the raw reply on a match; may be NULL
 * @param timeout_ms  reply timeout in ms; 0 selects the default
 * @param max_retry   number of retries after a timeout
 * @return 1 if queued, 0 if the queue is full
 */
u8 UartTx_Push_AT(const char *cmd, u8 cmd_len,
				  const char *resp_key, UartTx_AT_Callback callback,
				  u16 timeout_ms, u8 max_retry)
{
	u8 idx;
	u8 rlen;
	UartTxItem *item;
	if (txQueue.count >= UART_TX_QUEUE_SIZE)
		return 0;

	idx = (txQueue.head + txQueue.count) % UART_TX_QUEUE_SIZE;
	item = &txQueue.items[idx];
	item->type = TX_ITEM_AT;
	if (cmd_len > UART_TX_AT_CMD_LEN - 1)
		cmd_len = UART_TX_AT_CMD_LEN - 1;
	memcpy(item->u.at.cmd, cmd, cmd_len);
	item->u.at.cmd[cmd_len] = '\0';
	item->u.at.cmd_len = cmd_len;

	rlen = strlen(resp_key);
	if (rlen > UART_TX_AT_RESP_LEN - 1)
		rlen = UART_TX_AT_RESP_LEN - 1;
	memcpy(item->u.at.resp_key, resp_key, rlen);
	item->u.at.resp_key[rlen] = '\0';
	item->u.at.callback = callback;
	item->u.at.timeout_ms = timeout_ms ? timeout_ms : UART_TX_AT_TIMEOUT_MS;
	item->u.at.max_retry = max_retry;

	txQueue.count++;
	return 1;
}

/**
 * @brief Enqueue a JSON payload; dropped silently unless GPRS is in NORMAL state.
 * @param data      JSON bytes
 * @param len       number of data bytes
 * @param random    random token echoed by the platform, used to match the reply
 * @param callback  called with the reply on a match; NULL = fire-and-forget
 * @param timeout_ms  reply timeout; 0 selects default (gap for fire-and-forget)
 * @param max_retry   number of retries after a timeout
 * @return 1 if queued, 0 if not ready or the queue is full
 */
u8 UartTx_Push_JSON(const char *data, u16 len, const char *random, UartTx_JSON_Callback callback,
					u16 timeout_ms, u8 max_retry)
{
	u8 idx;
	u8 rlen;
	UartTxItem *item;

	/* While not in GPRS NORMAL, business JSON never enters the TX queue. */
	if (!gprs_is_ready())
		return 0;
	if (txQueue.count >= UART_TX_QUEUE_SIZE)
		return 0;
	if (len > UART_TX_JSON_LEN - 1)
		len = UART_TX_JSON_LEN - 1;

	idx = (txQueue.head + txQueue.count) % UART_TX_QUEUE_SIZE;
	item = &txQueue.items[idx];
	item->type = TX_ITEM_JSON;
	memcpy(item->u.json.data, data, len);
	item->u.json.data[len] = '\0';
	item->u.json.json_len = len;

	rlen = strlen(random);
	if (rlen > UART_TX_JSON_RANDOM_LEN - 1)
		rlen = UART_TX_JSON_RANDOM_LEN - 1;
	memcpy(item->u.json.random, random, rlen);
	item->u.json.random[rlen] = '\0';
	item->u.json.callback = callback;
	item->u.json.timeout_ms = timeout_ms
		? timeout_ms
		: (callback == NULL ? UART_TX_JSON_FIRE_AND_FORGET_GAP_MS : UART_TX_JSON_TIMEOUT_MS);
	item->u.json.max_retry = max_retry;

	txQueue.count++;

	return 1;
}

/** @brief Return 1 if the TX queue holds no pending items. */
u8 UartTx_IsEmpty(void)
{
	return txQueue.count == 0;
}

/**
 * @brief Feed a received string to the TX matcher (one send, one receive).
 * Only the queue head is matched; on a match the callback fires and the item is dequeued.
 * @param data    received bytes (not necessarily NUL-terminated)
 * @param len     number of valid bytes in data
 * @param strtype 1 = AT reply (match resp_key then "OK"), 2 = JSON reply (match random token)
 * @return 1 if the head matched and was dequeued, 0 otherwise
 */
u8 UartTx_OnRecv(char *data, u16 len, u8 strtype)
{
	UartTxItem *item;
	char *key;
	char *pos;

	if (txQueue.count == 0 || txQueue.state != UART_TX_STATE_WAITING)
		return 0;
	item = &txQueue.items[txQueue.head];
	if (item->type == TX_ITEM_AT && strtype == 1)
	{
		key = item->u.at.resp_key;
		pos = find_substr(data, len, key);
		if (pos != NULL && find_substr(pos, (u16)(len - (pos - data)), "OK") != NULL)
		{
			if (item->u.at.callback)
				item->u.at.callback(data, len);
			tx_dequeue();
			return 1;
		}
		return 0;
	}
	else if (item->type == TX_ITEM_JSON && strtype == 2)
	{
		key = item->u.json.random;
		pos = find_substr(data, len, key);
		if (pos != NULL)
		{
			if (item->u.json.callback)
				item->u.json.callback(data, len);
			tx_dequeue();
			return 1;
		}
	}
}
/**
 * @brief Find the end of a complete JSON object by bracket matching (simplified)
 * @param start JSON start position (points to '{')
 * @param max_len maximum search length
 * @return pointer to the closing '}', or NULL on failure
 *
 * Note: only the depth of '{' and '}' is counted; braces inside strings are not handled.
 * Use only when the string contains a single JSON object.
 *
 * Example:
 *   char *json_start = strstr(buffer, "{");
 *   char *json_end = find_json_end_by_bracket_matching(json_start, 512);
 *   if (json_end != NULL) {
 *       int json_len = json_end - json_start + 1;  // include the closing '}'
 *   }
 */
char *find_json_end_by_bracket_matching(char *start, u16 max_len)
{
	int depth = 0;
	char *p = start;
	char *end = start + max_len;

	if (start == NULL || *p != '{')
	{
		return NULL; // not the start of a JSON object
	}

	depth = 1;
	p++;

	while (p < end && *p != '\0')
	{
		if (*p == '{')
		{
			depth++;
		}
		else if (*p == '}')
		{
			depth--;
			if (depth == 0)
			{
				return p; // depth back to zero: pointer to the closing '}'
			}
		}
		p++;
	}
	return NULL; // no complete JSON (depth not zero, or out of range)
}
/**
 * Extract the start position and length of a JSON object from a string
 *
 * @param buf        input string buffer
 * @param buf_len    buffer length
 * @param out_start  output: JSON start (points to first '{'), NULL if not found
 * @param out_len    output: JSON length (including braces), 0 if not found
 *
 * @return 0 on success, non-zero on failure / not found
 */
int extract_json_from_string(char *buf, u16 buf_len, char **out_start, u16 *out_len)
{
	int depth;
	char *p;
	char *end;
	char *json_start = NULL;
	char *json_end = NULL;

	if (buf == NULL || out_start == NULL || out_len == NULL)
		return -1;

	*out_start = NULL;
	*out_len = 0;

	if (buf_len == 0)
		return -1;

	end = buf + buf_len;

	/* find the first '{' */
	for (p = buf; p < end && *p != '\0'; p++)
	{
		if (*p == '{')
		{
			json_start = p;
			break;
		}
	}
	if (json_start == NULL)
		return -1;

	/* bracket match to find the closing '}' */
	depth = 1;
	p = json_start + 1;
	while (p < end && *p != '\0')
	{
		if (*p == '{')
			depth++;
		else if (*p == '}')
		{
			depth--;
			if (depth == 0)
			{
				json_end = p;
				break;
			}
		}
		p++;
	}
	if (json_end == NULL)
		return -1;

	*out_start = json_start;
	*out_len = (u16)(json_end - json_start + 1);
	return 0;
}

/**
 * Check whether a string contains exactly one JSON object
 *
 * @return status code:
 *   0 - no '{' found, no JSON
 *   1 - exactly one complete JSON (first '{' to matching '}'; only whitespace outside)
 *   2 - more than one '{'
 *   3 - has '{' but brackets do not match
 *   4 - non-whitespace before the first '{'
 *   5 - non-whitespace after the last '}', JSON is not at the end
 */
int check_json_unique_in_string(char *buf, u16 buf_len)
{
	int depth;
	char *p;
	char *end;
	char *json_start = NULL;
	char *json_end = NULL;

	if (buf == NULL || buf_len == 0)
		return 0;

	end = buf + buf_len;

	/* find the first '{' */
	for (p = buf; p < end && *p != '\0'; p++)
	{
		if (*p == '{')
		{
			json_start = p;
			break;
		}
	}
	if (json_start == NULL)
		return 0;

	/* check for non-whitespace before the first '{' */
	for (p = buf; p < json_start; p++)
	{
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			return 4;
	}

	/* bracket match to find the closing position */
	depth = 1;
	p = json_start + 1;
	while (p < end && *p != '\0')
	{
		if (*p == '{')
			depth++;
		else if (*p == '}')
		{
			depth--;
			if (depth == 0)
			{
				json_end = p;
				break;
			}
		}
		p++;
	}
	if (json_end == NULL)
		return 3;

	/* check for another '{' after the JSON (multiple objects) */
	for (p = json_end + 1; p < end && *p != '\0'; p++)
	{
		if (*p == '{')
			return 2;
	}

	/* check for non-whitespace after the last '}' */
	for (p = json_end + 1; p < end && *p != '\0'; p++)
	{
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			return 5; /* JSON is not at the end of the string */
	}

	return 1;
}
/**
 * @brief Drive the TX state machine; call every main-loop pass with g_sys_ms.
 * Strict one-send-one-receive: send the head only in IDLE; in WAITING handle
 * reply / timeout retry / fire-and-forget gap, then dequeue on a later pass.
 * @param sys_tick_ms current system time in ms
 */
void UartTx_Queue_Process(u32 sys_tick_ms)
{
	UartTxItem *item;
	if (txQueue.count == 0)
		return;

	/* Second guard: drop JSON queued before link loss while not NORMAL; no retry, no send. */
	if (!gprs_is_ready() && txQueue.items[txQueue.head].type == TX_ITEM_JSON)
	{
		tx_dequeue();
		return;
	}

	if (txQueue.state == UART_TX_STATE_WAITING)
	{
		/* While waiting: only handle timeout retry or dequeue; never send a new item */
		UartTxItem *item = &txQueue.items[txQueue.head];
		if (item->type == TX_ITEM_AT)
		{ // no reply within timeout and retries left -> resend; otherwise dequeue
			if ((sys_tick_ms - txQueue.send_tick) >= item->u.at.timeout_ms)
			{
				txQueue.at_retry_cnt++;
				if (txQueue.at_retry_cnt <= item->u.at.max_retry)
				{
					/* retry */
					GPRS_PORT_Send(item->u.at.cmd, item->u.at.cmd_len);
					txQueue.send_tick = sys_tick_ms;
				}
				else
				{
					tx_dequeue();
				}
			}
		}
		else if (item->type == TX_ITEM_JSON)
		{
			u16 wait_ms = item->u.json.callback == NULL
				? UART_TX_JSON_FIRE_AND_FORGET_GAP_MS
				: item->u.json.timeout_ms;
			if ((sys_tick_ms - txQueue.send_tick) >= wait_ms)
			{
				if (item->u.json.callback == NULL)
					tx_dequeue();
				else
				{
					txQueue.json_retry_cnt++;
					if (txQueue.json_retry_cnt <= item->u.json.max_retry)
					{
						/* retry */
						GPRS_PORT_Send(item->u.json.data, item->u.json.json_len);
						txQueue.send_tick = sys_tick_ms;
					}
					else
					{
						tx_dequeue();
					}
				}
			}
		}
		return;
	}

	/* send only when state == IDLE: previous item was received or dequeued on timeout */
	if (txQueue.state != UART_TX_STATE_IDLE)
		return;

	item = &txQueue.items[txQueue.head];
	txQueue.send_tick = sys_tick_ms;
	txQueue.state = UART_TX_STATE_WAITING;

	if (item->type == TX_ITEM_AT)
	{
		txQueue.at_retry_cnt = 0;
		GPRS_PORT_Send(item->u.at.cmd, item->u.at.cmd_len);
		GPRS_Printf("send at-->%s\n", item->u.at.cmd);
	}
	else
	{
		txQueue.json_retry_cnt = 0;
		GPRS_PORT_Send(item->u.json.data, item->u.json.json_len);
		GPRS_Printf("send json-->%s\n", item->u.json.data);  
	}
}

/*==================== RX queue implementation ====================*/
/** @brief Reset the RX frame queue and the linear ring buffer to empty. */
void UartRx_Queue_Init(void)
{
	memset(&rxQueue, 0, sizeof(rxQueue));
	rxRingLen = 0;
}

/**
 * @brief Append raw bytes to the RX ring buffer.
 * Bytes beyond capacity are dropped and counted in g_uartRx_dropped_bytes.
 * @param data bytes to append
 * @param len  number of bytes to append
 */
void UartRx_Append(char *data, u16 len)
{
	u16 space = UART_RX_RING_SIZE - rxRingLen;
	if (len > space)
	{
		g_uartRx_dropped_bytes += (u32)(len - space);
		len = space;
	}
	memcpy(rxRingBuf + rxRingLen, data, len);
	rxRingLen += len;
}

/**
 * @brief Push one frame into the RX frame queue.
 * @param data frame bytes (not necessarily NUL-terminated)
 * @param len  number of frame bytes
 * @return 1 if queued, 0 if the queue is full or the frame is too large
 */
static u8 rx_enqueue(const char *data, u16 len)
{
	u8 idx;
	if (rxQueue.count >= UART_RX_QUEUE_SIZE || len > UART_RX_ITEM_MAX)
		return 0;
	idx = (rxQueue.tail + rxQueue.count) % UART_RX_QUEUE_SIZE;
	if (len > UART_RX_ITEM_MAX - 1)
		len = UART_RX_ITEM_MAX - 1;
	memcpy(rxQueue.items[idx].data, data, len);
	rxQueue.items[idx].data[len] = '\0';
	rxQueue.items[idx].len = len;
	rxQueue.count++;
	return 1;
}

/**
 * @brief Split buffered bytes into frames and move them into the RX frame queue.
 * Stops when the frame queue is full; remaining bytes stay in the ring buffer.
 */
void UartRx_Extract(void)
{
	u16 chunk_len;
	while (rxRingLen > 0)
	{
		chunk_len = rxRingLen;
		if (chunk_len > UART_RX_ITEM_MAX - 1)
			chunk_len = UART_RX_ITEM_MAX - 1;
		if (rx_enqueue(rxRingBuf, chunk_len))
		{
			memmove(rxRingBuf, rxRingBuf + chunk_len, rxRingLen - chunk_len);
			rxRingLen -= chunk_len;
		}
		else
			break; /* queue full, wait for consumer */
	}
}

/**
 * Move whole frames staged by the USART2 (4G) ISR into the ring buffer and frame queue.
 *
 * Two call sites:
 *   1) gprs_rx_datas() - normal main-loop poll, verbose=1, print each raw frame;
 *   2) AT24CXX_Write() - periodic poll during long EEPROM writes, verbose=0, move only.
 * This function does not touch Usart2_Rec_Cnt / UART2_RX_Complete, so the AT sequence logic is unaffected.
 */
void UartRx_PumpFromDma(u8 verbose)
{
	char *buf = NULL;
	u16 len = 0;
	u8 got = 0;
	u8 limit;

	/* at most 4 frames per call, to avoid holding up the main loop */
	for (limit = 0; limit < 4u; limit++)
	{
		u32 primask = __get_PRIMASK();

		/*
		 * Taking and copying a frame must be atomic: GPRS_PORT_RxStageTake marks the slot free;
		 * an ISR writing the next frame into that slot could corrupt data mid-memcpy (silent).
		 * The copy takes only tens of microseconds; a brief IRQ disable (<100us) is safe for the 5ms ADC tick.
		 */
		__disable_irq();
		if (!GPRS_PORT_RxStageTake(&buf, &len))
		{
			__set_PRIMASK(primask);
			break;
		}
		if (len == 0u)
		{
			__set_PRIMASK(primask);
			continue;
		}
		got = 1;
		UartRx_Append(buf, len);
		__set_PRIMASK(primask);

		/* print outside the critical section: data is in the ring buffer; worst case the log line is garbled */
		if (verbose)
			GPRS_Printf("[GPRS] RX[%u]: %.*s\n", (unsigned)len, (int)len, buf);
	}

	if (got)
		UartRx_Extract();
}

/**
 * @brief Pop one frame from the RX frame queue.
 * @param out     output buffer, receives a NUL-terminated string
 * @param max_len capacity of out (writes at most max_len-1 bytes)
 * @param out_len optional; receives the copied byte count
 * @return 1 if a frame was popped, 0 if the queue is empty
 */
u8 UartRx_Pop(char *out, u16 max_len, u16 *out_len)
{
	UartRxItem *item;
	u16 len;
	if (rxQueue.count == 0)
		return 0;
	item = &rxQueue.items[rxQueue.tail];
	len = item->len;
	if (len > max_len - 1)
		len = max_len - 1;
	memcpy(out, item->data, len);
	out[len] = '\0';
	if (out_len)
		*out_len = len;
	rxQueue.tail = (rxQueue.tail + 1) % UART_RX_QUEUE_SIZE;
	rxQueue.count--;
	return 1;
}

/** @brief Return 1 if the RX frame queue holds no pending frames. */
u8 UartRx_IsEmpty(void)
{
	return rxQueue.count == 0;
}

/**
 * @brief Expose the raw bytes left in the RX ring buffer (used by OTA binary reads).
 * @param ptr optional; receives a pointer to the buffer start
 * @param len optional; receives the number of valid bytes
 */
void UartRx_GetRemainingBuffer(char **ptr, u16 *len)
{
	if (ptr)
		*ptr = rxRingBuf;
	if (len)
		*len = rxRingLen;
}

/**
 * @brief Discard the first len bytes from the RX ring buffer.
 * @param len number of bytes to discard; clearing all resets the buffer
 */
void UartRx_Consume(u16 len)
{
	if (len >= rxRingLen)
	{
		rxRingLen = 0;
		return;
	}
	memmove(rxRingBuf, rxRingBuf + len, rxRingLen - len);
	rxRingLen -= len;
}
