#include "uart_queue.h"
#include "usart.h"
#include <string.h>
#include "main.h"
#include "4G.h"
#include "gprs.h"
/*==================== 发送队列 ====================*/

/**
 * UartTxItem - 统一发送队列项
 * 支持 AT 与 JSON 两种类型，按最大成员对齐，共用 union 节省空间
 */
typedef struct
{
	UartTxItemType type; /* 发送项类型：TX_ITEM_AT / TX_ITEM_JSON */
	union
	{
		struct /* AT 命令项 */
		{
			char cmd[UART_TX_AT_CMD_LEN];		/* AT 命令字符串 */
			u8 cmd_len;							/* 命令长度 */
			char resp_key[UART_TX_AT_RESP_LEN]; /* 响应匹配关键字，如 "+ICCID" */
			UartTx_AT_Callback callback;		/* 收到匹配响应时的回调 */
			u16 timeout_ms;						/* 超时时间（毫秒） */
			u8 max_retry;						/* 超时后最大重试次数 */
		} at;
		struct /* JSON 数据项 */
		{
			char data[UART_TX_JSON_LEN];		  /* JSON 字符串 */
			u16 json_len;						  /* 数据长度 */
			char random[UART_TX_JSON_RANDOM_LEN]; /* 响应匹配关键字，如 "+ICCID" */
			UartTx_JSON_Callback callback;		  /* 收到匹配响应时的回调 */
			u16 timeout_ms;						  /* 超时时间（毫秒） */
			u8 max_retry;						  /* 超时后最大重试次数 */
		} json;
	} u;
} UartTxItem;

/**
 * UartTxQueue - 统一发送队列
 * FIFO 队列，队首发送后进入 WAITING；有回调的等待响应，无回调的仅保留安全帧间隔
 */
typedef struct
{
	UartTxItem items[UART_TX_QUEUE_SIZE]; /* 队列存储 */
	u8 head;							  /* 队首索引（当前发送/等待项） */
	u8 tail;							  /* 队尾索引（预留） */
	u8 count;							  /* 队列中项数量 */
	u8 state;							  /* 状态：IDLE=空闲 / WAITING=等待响应 */
	u32 send_tick;						  /* 最近一次发送时刻（sys_ms） */
	u8 at_retry_cnt;					  /* AT 超时重试计数 */
	u8 json_retry_cnt;					  /* JSON 超时重试计数（与 AT 分开，避免类型切换时串用） */
} UartTxQueue;

static UartTxQueue txQueue;

/*==================== 接收队列 ====================*/
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

/* 线性缓冲 */
static char rxRingBuf[UART_RX_RING_SIZE + 1];
static u16 rxRingLen;

volatile u32 g_uartRx_dropped_bytes = 0; /* 环形缓冲满被丢弃的字节数（0 = 从未截断） */

/* 辅助：在指定长度内查找子串 */
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

/*==================== 发送队列实现 ====================*/
void UartTx_Queue_Init(void)
{
	memset(&txQueue, 0, sizeof(txQueue));
}

/* 出队：收到回执或重试超限后调用，恢复 IDLE 以便发送下一条 */
static void tx_dequeue(void)
{
	txQueue.head = (txQueue.head + 1) % UART_TX_QUEUE_SIZE;
	txQueue.count--;
	txQueue.state = UART_TX_STATE_IDLE;
}

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
	// MY4G_printf("queue.count is %d\n",txQueue.count);
	return 1;
}

u8 UartTx_Push_JSON(const char *data, u16 len, const char *random, UartTx_JSON_Callback callback,
					u16 timeout_ms, u8 max_retry)
{
	u8 idx;
	u8 rlen;
	UartTxItem *item;

	/* 设备未进入 GPRS NORMAL 时，业务 JSON 一律不进入发送队列。 */
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

u8 UartTx_IsEmpty(void)
{
	return txQueue.count == 0;
}

/* 收到回执时调用，仅匹配当前队首，匹配则出队（一发一收） */
u8 UartTx_OnRecv(char *data, u16 len, u8 strtype)
{
	UartTxItem *item;
	char *key;
	char *pos;

	if (txQueue.count == 0 || txQueue.state != UART_TX_STATE_WAITING)
		return 0;
	item = &txQueue.items[txQueue.head];
	// at回执
	if (item->type == TX_ITEM_AT && strtype == 1)
	{
		key = item->u.at.resp_key;
		pos = find_substr(data, len, key);											  // 1.响应关键字
		if (pos != NULL && find_substr(pos, (u16)(len - (pos - data)), "OK") != NULL) // 2.并且后面有"OK"
		{
			/* 匹配到当前等待的 AT 回执，回调并出队 */
			if (item->u.at.callback)
				item->u.at.callback(data, len);
			tx_dequeue();
			return 1;
		}
		return 0;
	}
	// json 回执
	else if (item->type == TX_ITEM_JSON && strtype == 2)
	{
		key = item->u.json.random;
		pos = find_substr(data, len, key);
		if (pos != NULL) // 必须包含随机数，说明是同一条指令回执
		{
			/* 匹配到当前等待的 AT 回执，回调并出队 */
			if (item->u.json.callback)
				item->u.json.callback(data, len);
			tx_dequeue();
			return 1;
		}
	}
}
/**
 * @brief 使用括号匹配算法找到完整JSON的结束位置（简化版）
 * @param start JSON开始位置（指向'{'）
 * @param max_len 最大搜索长度
 * @return JSON结束位置的指针（指向'}'），失败返回NULL
 *
 * 注意：此函数仅根据 '{' 和 '}' 的深度匹配，不处理字符串内的括号
 * 适用于字符串中只包含一个JSON对象的情况
 *
 * 示例：
 *   char *json_start = strstr(buffer, "{");
 *   char *json_end = find_json_end_by_bracket_matching(json_start, 512);
 *   if (json_end != NULL) {
 *       int json_len = json_end - json_start + 1;  // 包含结束的'}'
 *   }
 */
char *find_json_end_by_bracket_matching(char *start, u16 max_len)
{
	int depth = 0; // 括号深度
	char *p = start;
	char *end = start + max_len;

	// 检查起始字符是否为 '{'
	if (start == NULL || *p != '{')
	{
		return NULL; // 不是JSON对象开始
	}

	// 从第一个 '{' 开始计数
	depth = 1;
	p++; // 跳过第一个 '{'

	// 遍历字符串，只根据 '{' 和 '}' 的深度匹配
	while (p < end && *p != '\0')
	{
		if (*p == '{')
		{
			depth++; // 遇到 '{' 深度加1
		}
		else if (*p == '}')
		{
			depth--; // 遇到 '}' 深度减1
			if (depth == 0)
			{
				return p; // 深度归零，返回指向'}'的指针
			}
		}
		p++;
	}
	return NULL; // 未找到完整的JSON（深度未归零或超出范围）
}
/**
 * 从字符串中提取 JSON 的起始位置和长度
 *
 * @param buf        待处理的字符串缓冲区
 * @param buf_len    缓冲区长度
 * @param out_start  输出：JSON 起始位置（指向第一个 '{'），若未找到则为 NULL
 * @param out_len    输出：JSON 长度（含首尾括号），若未找到则为 0
 *
 * @return 0 成功，非 0 失败/未找到
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

	/* 查找第一个 '{' */
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

	/* 括号匹配找结束 '}' */
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
 * 判断字符串中是否唯一包含 JSON 字符串
 *
 * @return 状态码：
 *   0 - 未找到 '{'，不含 JSON
 *   1 - 唯一包含一个完整 JSON（从第一个 '{' 到匹配的 '}'，前后无非空白字符）
 *   2 - 包含多个 '{'
 *   3 - 有 '{' 但括号不匹配
 *   4 - 第一个 '{' 之前有非空白字符
 *   5 - 最后一个 '}' 之后有非空白字符，JSON 不是字符串结尾
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

	/* 查找第一个 '{' */
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

	/* 检查 '{' 之前是否有非空白字符 */
	for (p = buf; p < json_start; p++)
	{
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			return 4;
	}

	/* 括号匹配找结束位置 */
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

	/* 检查 JSON 结束之后是否还有 '{'（多个 JSON） */
	for (p = json_end + 1; p < end && *p != '\0'; p++)
	{
		if (*p == '{')
			return 2;
	}

	/* 新增：检查最后一个 '}' 之后是否有非空白字符 */
	for (p = json_end + 1; p < end && *p != '\0'; p++)
	{
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			return 5; /* JSON 不是字符串结尾 */
	}

	return 1;
}
/**
 * 发送队列处理：严格遵循「一发一收」原则
 * - 仅当 state==IDLE 时发送队首指令
 * - 发送后进入 WAITING：有回调等待回执，无回调只等待 UART_TX_JSON_FIRE_AND_FORGET_GAP_MS
 * - 收到回执、重试超限或安全间隔到达后出队，下一轮才发下一条
 */
void UartTx_Queue_Process(u32 sys_tick_ms)
{
	UartTxItem *item;
	if (txQueue.count == 0)
		return;

	/* 二次保护：如果掉线前已有JSON入队，非NORMAL状态下直接丢弃，不重试也不发送。 */
	if (!gprs_is_ready() && txQueue.items[txQueue.head].type == TX_ITEM_JSON)
	{
		tx_dequeue();
		return;
	}

	if (txQueue.state == UART_TX_STATE_WAITING)
	{
		/* 等待中：只处理超时重试或出队，绝不发送新指令 */
		UartTxItem *item = &txQueue.items[txQueue.head];
		if (item->type == TX_ITEM_AT)
		{ // 在规定时间内没有收到响应，且重试次数未超过最大值，则重试发送；否则出队
			if ((sys_tick_ms - txQueue.send_tick) >= item->u.at.timeout_ms)
			{
				txQueue.at_retry_cnt++;
				if (txQueue.at_retry_cnt <= item->u.at.max_retry)
				{
					/* 重试 */
					Usart2_Send(item->u.at.cmd, item->u.at.cmd_len);
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
						/* 重试 */
						Usart2_Send(item->u.json.data, item->u.json.json_len);
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

	/* 仅 state==IDLE 时发送：确保上一条已收妥或超限出队 */
	if (txQueue.state != UART_TX_STATE_IDLE)
		return;

	item = &txQueue.items[txQueue.head];
	txQueue.send_tick = sys_tick_ms;
	txQueue.state = UART_TX_STATE_WAITING;

	if (item->type == TX_ITEM_AT)
	{
		txQueue.at_retry_cnt = 0;
		Usart2_Send(item->u.at.cmd, item->u.at.cmd_len);
		MY4G_printf("send at-->%s\n", item->u.at.cmd);
	}
	else
	{
		txQueue.json_retry_cnt = 0;
		Usart2_Send(item->u.json.data, item->u.json.json_len);
	}
}

/*==================== 接收队列实现 ====================*/
void UartRx_Queue_Init(void)
{
	memset(&rxQueue, 0, sizeof(rxQueue));
	rxRingLen = 0;
}

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
			break; /* 队列满，等待消费 */
	}
}

/**
 * 把 USART2(4G) 中断暂存的整帧搬进环形缓冲与帧队列。
 *
 * 两处调用：
 *   1) gprs_rx_datas()——主循环常规取帧，verbose=1，逐帧打印模块原文；
 *   2) AT24CXX_Write()——长 EEPROM 写期间定期取帧，verbose=0，只搬不打印。
 * 本函数不碰 Usart2_Rec_Cnt / UART2_RX_Complete，AT 序列自己的收发逻辑不受影响。
 */
void UartRx_PumpFromDma(u8 verbose)
{
	char *buf = NULL;
	u16 len = 0;
	u8 got = 0;
	u8 limit;

	/* 单次最多搬 4 帧，避免极端情况下长时间占用主循环 */
	for (limit = 0; limit < 4u; limit++)
	{
		u32 primask = __get_PRIMASK();

		/*
		 * 取帧与拷贝必须原子：Uart2_RxStageTake 会把槽标记为空，此后中断若往同一槽
		 * 写下一帧，就会在 memcpy 读到一半时把数据改掉（静默损坏）。
		 * 拷贝只有几十微秒，短暂关中断（<100us）不影响 5ms ADC 节拍。
		 */
		__disable_irq();
		if (!Uart2_RxStageTake(&buf, &len))
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

		/* 打印放在关中断之外：数据已拷进环形缓冲，打印期间被中断改写最多是这行日志花掉 */
		if (verbose)
			MY4G_printf("[GPRS] RX[%u]: %.*s\n", (unsigned)len, (int)len, buf);
	}

	if (got)
		UartRx_Extract();
}

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

u8 UartRx_IsEmpty(void)
{
	return rxQueue.count == 0;
}

void UartRx_GetRemainingBuffer(char **ptr, u16 *len)
{
	if (ptr)
		*ptr = rxRingBuf;
	if (len)
		*len = rxRingLen;
}

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

/**
 * @brief 使用括号匹配算法提取完整 JSON 字符串
 * @param buf 待搜索的缓冲区（JSON 不一定从第一个字符开始）
 * @param buf_len 缓冲区长度
 * @param out_buf 输出缓冲区，用于存放提取的 JSON 字符串
 * @param out_buf_size 输出缓冲区大小
 * @return 成功返回 out_buf（已写入 JSON 并以 '\0' 结尾），失败返回 NULL
 *
 * 注意：此函数在 buf 中查找第一个 '{' 作为 JSON 起始，再根据括号匹配找结束
 * 仅根据 '{' 和 '}' 的深度匹配，不处理字符串内的括号
 *
 * 示例：
 *   char json_str[512];
 *   if (find_json_string_by_bracket_matching(buffer, len, json_str, sizeof(json_str)) != NULL) {
 *       // json_str 中为完整 JSON 字符串
 *   }
 */
// char *find_json(char *buf, u16 buf_len, char *out_buf, u16 out_buf_size)
//{
//	int depth;
//	char *p;
//	char *end;
//	char *json_start = NULL;
//	char *json_end = NULL;

//	if (buf == NULL || out_buf == NULL || out_buf_size == 0)
//		return NULL;

//	end = buf + buf_len;

//	/* 1. 在缓冲区中查找第一个 '{' 作为 JSON 起始 */
//	for (p = buf; p < end && *p != '\0'; p++)
//	{
//		if (*p == '{')
//		{
//			json_start = p;
//			break;
//		}
//	}
//	if (json_start == NULL)
//		return NULL;

//	/* 2. 从 '{' 开始括号匹配，找对应的 '}' */
//	depth = 1;
//	p = json_start + 1;
//	while (p < end && *p != '\0')
//	{
//		if (*p == '{')
//			depth++;
//		else if (*p == '}')
//		{
//			depth--;
//			if (depth == 0)
//			{
//				json_end = p;
//				break;
//			}
//		}
//		p++;
//	}
//	if (json_end == NULL)
//		return NULL;

//	/* 3. 拷贝 JSON 到 out_buf */
//	u16 json_len = (u16)(json_end - json_start + 1);
//	if (json_len >= out_buf_size)
//		return NULL;
//	memcpy(out_buf, json_start, json_len);
//	out_buf[json_len] = '\0';
//	return out_buf;
//}
