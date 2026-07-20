#include "usart3.h"
#include "uart4.h"
#include "data_handler.h"
#include "pass_password.h"
#include "app_comm.h"
#include "app_config.h"
#include "app_run.h"
#include "cmd.h"
#include "main.h"
#include "qr_comm.h"
#include "rtc.h"
#include <stdio.h>
#include <string.h>

static char s_received_uart5[MAX_RECEIVE_LEN];
static char s_received_uart4[MAX_RECEIVE_LEN];
static int s_received_len_uart5;
static int s_received_len_uart4;
static char order_type_uart5[8];
static char order_type_uart4[8];
static char card_Number_uart5[32];
static char card_Number_uart4[32];

#define UART5_RX_ACCUM_MAX 512
static u8 uart5_rx_accum[UART5_RX_ACCUM_MAX];
static u16 uart5_rx_accum_len;

#define UART5_DATA_SLICE_MAX 256
static u8 uart5_data_slice[UART5_DATA_SLICE_MAX];
static u16 uart5_data_slice_len;
static u8 uart5_type_slice[UART5_DATA_SLICE_MAX];
static u16 uart5_type_slice_len;

#define UART4_RX_ACCUM_MAX 512
static u8 uart4_rx_accum[UART4_RX_ACCUM_MAX];
static u16 uart4_rx_accum_len;

#define UART4_DATA_SLICE_MAX 256
static u8 uart4_data_slice[UART4_DATA_SLICE_MAX];
static u16 uart4_data_slice_len;
static u8 uart4_type_slice[UART4_DATA_SLICE_MAX];
static u16 uart4_type_slice_len;

static int uart5_find_sub(const u8 *buf, u16 len, const char *needle)
{
	u16 n = (u16)strlen(needle);
	u16 a, b;

	if (n == 0 || len < n)
		return -1;
	for (a = 0; a + n <= len; a++)
	{
		for (b = 0; b < n; b++)
			if (buf[a + b] != (u8)needle[b])
				break;
		if (b == n)
			return (int)a;
	}
	return -1;
}

static int uart4_find_sub(const u8 *buf, u16 len, const char *needle)
{
	u16 n = (u16)strlen(needle);
	u16 a, b;

	if (n == 0 || len < n)
		return -1;
	for (a = 0; a + n <= len; a++)
	{
		for (b = 0; b < n; b++)
			if (buf[a + b] != (u8)needle[b])
				break;
		if (b == n)
			return (int)a;
	}
	return -1;
}

/**
 * QRProcessUart5 — 读头 2（UART5）通行业务轮询
 *
 * 由 app_poll() 调用。驱动 DMA/IDLE 置 UART5_RX_Complete 后：
 *  1) 将 UART5_RX_BUF 追加到 uart5_rx_accum
 *  2) 对齐到 '{'，凑齐完整 "{...}" 帧后再解析
 *  3) 切片 type / data / uid（搜键名须带引号，避免 data 内容误匹配）
 *  4) type 为 '0'/'1' → CommControl(..., 5)；'2' → 密码处理d
 */
void QRProcessUart5(void)
{
	int i;

	if (UART5_RX_Complete)
	{
		/* 追加本帧 DMA 缓冲到累积区（可跨多次 IDLE 拼包） */
		for (i = 0; i < UART5_RX_CNT && uart5_rx_accum_len < UART5_RX_ACCUM_MAX; i++)
			uart5_rx_accum[uart5_rx_accum_len++] = UART5_RX_BUF[i];
		/* 丢掉 '{' 之前的噪声 */
		for (i = 0; i < (int)uart5_rx_accum_len; i++)
		{
			if (uart5_rx_accum[i] == '{')
				break;
		}
		if (i > 0 && i < (int)uart5_rx_accum_len)
		{
			u16 keep = uart5_rx_accum_len - (u16)i;
			memmove(uart5_rx_accum, uart5_rx_accum + i, keep);
			uart5_rx_accum_len = keep;
		}
		/* 首 '{' 且末 '}' 才视为完整 JSON 对象帧 */
		if (uart5_rx_accum_len >= 2u &&
			uart5_rx_accum[0] == '{' &&
			uart5_rx_accum[uart5_rx_accum_len - 1u] == '}')
		{
			int itype = uart5_find_sub(uart5_rx_accum, uart5_rx_accum_len, "\"type\"");
			int idata = uart5_find_sub(uart5_rx_accum, uart5_rx_accum_len, "\"data\"");
			/* 须搜 "\"uid\""，勿仅用 uid，以免 data 内十六进制子串误匹配 */
			int iuid = uart5_find_sub(uart5_rx_accum, uart5_rx_accum_len, "\"uid\"");

			printf("Received UART5 data: ");
			for (i = 0; i < uart5_rx_accum_len; i++)
			{
				printf("%c", uart5_rx_accum[i]);
			}
			printf("\r\n");

			/* --- 切 type --- */
			uart5_type_slice_len = 0;
			if (itype >= 0 && idata >= 0 && idata >= 2)
			{
				/* itype 为 "\"type\"" 的起始引号：跳过 \"type\"\": 共 7 字到类型值 */
				u16 ts = (u16)itype + 7u;
				/* idata 为 "\"data\"" 的起始引号：类型值在逗号前结束，即 idata 前两字为 ," */
				u16 te = (u16)idata - 1u;

				if (ts < te && te <= uart5_rx_accum_len)
				{
					u16 n = te - ts;
					if (n > UART5_DATA_SLICE_MAX)
						n = UART5_DATA_SLICE_MAX;
					memcpy(uart5_type_slice, uart5_rx_accum + ts, n);
					uart5_type_slice_len = n;
					if (n < UART5_DATA_SLICE_MAX)
						uart5_type_slice[n] = '\0';
				}
			}
			memset(order_type_uart5, 0, sizeof(order_type_uart5));
			for (i = 0; i < uart5_type_slice_len && i < (int)sizeof(order_type_uart5) - 1; i++)
			{
				printf("type-->%c\r\n", uart5_type_slice[i]);
				order_type_uart5[i] = uart5_type_slice[i];
			}
			/* --- 切 data（值位于 "data": 与 ,"uid" 之间） --- */
			uart5_data_slice_len = 0;
			if (idata >= 0 && iuid >= 2)
			{
				/* idata 指向 "\"data\"" 起始引号：+8 跳到值；cut_end=iuid-2 不含结尾引号与逗号 */
				u16 cut_start = (u16)idata + 8u;
				u16 cut_end = (u16)iuid - 2u;

				if (cut_start < cut_end && cut_end <= uart5_rx_accum_len)
				{
					u16 n = cut_end - cut_start;
					if (n > UART5_DATA_SLICE_MAX)
						n = UART5_DATA_SLICE_MAX;
					memcpy(uart5_data_slice, uart5_rx_accum + cut_start, n);
					uart5_data_slice_len = n;
					if (n < UART5_DATA_SLICE_MAX)
						uart5_data_slice[n] = '\0';
				}
			}
			printf("data-->");
			for (i = 0; i < uart5_data_slice_len; i++)
			{
				printf("%c", uart5_data_slice[i]);
			}
			printf("\r\n");
			/* --- 切 uid --- */
			card_Number_uart5[0] = '\0';
			if (iuid >= 0)
			{
				u16 p = (u16)iuid + (u16)strlen("\"uid\"");
				if (p < uart5_rx_accum_len && uart5_rx_accum[p] == ':')
					p++;
				while (p < uart5_rx_accum_len && (uart5_rx_accum[p] == ' ' || uart5_rx_accum[p] == '\t'))
					p++;
				{
					u16 start;
					u16 n = 0;

					if (p < uart5_rx_accum_len && uart5_rx_accum[p] == '"')
					{
						p++;
						start = p;
						while (p < uart5_rx_accum_len && uart5_rx_accum[p] != '"' && n < 31u)
						{
							p++;
							n++;
						}
					}
					else
					{
						start = p;
						while (p < uart5_rx_accum_len && uart5_rx_accum[p] >= '0' && uart5_rx_accum[p] <= '9' && n < 31u)
						{
							p++;
							n++;
						}
					}
					if (n > 0u)
					{
						memcpy(card_Number_uart5, uart5_rx_accum + start, n);
						card_Number_uart5[n] = '\0';
					}
				}
			}

			if (uart5_type_slice_len > 0 && uart5_data_slice_len > 0)
			{
				s_received_len_uart5 = (uart5_data_slice_len < MAX_RECEIVE_LEN - 1) ? uart5_data_slice_len : (MAX_RECEIVE_LEN - 1);
				memcpy(s_received_uart5, uart5_data_slice, s_received_len_uart5);
				s_received_uart5[s_received_len_uart5] = '\0';
				/* type: '1' 二维码 / '0' NFC → 命令分发；'2' 密码 */
				if (uart5_type_slice_len == 1 && (uart5_type_slice[0] == '1' || uart5_type_slice[0] == '0'))
				{
					CommControl(s_received_uart5, order_type_uart5, card_Number_uart5, 5);
				}
				else if (uart5_type_slice_len == 1 && uart5_type_slice[0] == '2')
				{
					qr_handle_password_input(s_received_uart5, 5);
				}
			}
			/* 完整帧已处理：清空累积与切片，准备下一帧 */
			uart5_rx_accum_len = 0;
			uart5_data_slice_len = 0;
			uart5_type_slice_len = 0;
			memset(uart5_rx_accum, 0, sizeof(uart5_rx_accum));
			memset(uart5_type_slice, 0, sizeof(uart5_type_slice));
			memset(uart5_data_slice, 0, sizeof(uart5_data_slice));
		}

		/* 无论是否凑齐完整帧，都清驱动标志，允许收下一截 */
		UART5_RX_Complete = 0;
		UART5_RX_CNT = 0;

		return;
	}
}

/**
 * QRProcessUart4 — 读头 1（UART4）通行业务轮询
 *
 * 逻辑与 QRProcessUart5 对称，独立缓冲 uart4_*；CommControl / 密码端口号为 4。
 */
void QRProcessUart4(void)
{
	int i;

	if (UART4_RX_Complete)
	{
		/* 追加本帧 DMA 缓冲到累积区（可跨多次 IDLE 拼包） */
		for (i = 0; i < UART4_RX_CNT && uart4_rx_accum_len < UART4_RX_ACCUM_MAX; i++)
			uart4_rx_accum[uart4_rx_accum_len++] = UART4_RX_BUF[i];
		/* 丢掉 '{' 之前的噪声 */
		for (i = 0; i < (int)uart4_rx_accum_len; i++)
		{
			if (uart4_rx_accum[i] == '{')
				break;
		}
		if (i > 0 && i < (int)uart4_rx_accum_len)
		{
			u16 keep = uart4_rx_accum_len - (u16)i;
			memmove(uart4_rx_accum, uart4_rx_accum + i, keep);
			uart4_rx_accum_len = keep;
		}
		/* 首 '{' 且末 '}' 才视为完整 JSON 对象帧 */
		if (uart4_rx_accum_len >= 2u &&
			uart4_rx_accum[0] == '{' &&
			uart4_rx_accum[uart4_rx_accum_len - 1u] == '}')
		{
			int itype = uart4_find_sub(uart4_rx_accum, uart4_rx_accum_len, "\"type\"");
			int idata = uart4_find_sub(uart4_rx_accum, uart4_rx_accum_len, "\"data\"");
			/* 须搜 "\"uid\""，勿仅用 uid，以免 data 内十六进制子串误匹配 */
			int iuid = uart4_find_sub(uart4_rx_accum, uart4_rx_accum_len, "\"uid\"");

			printf("Received UART4 data: ");
			for (i = 0; i < uart4_rx_accum_len; i++)
			{
				printf("%c", uart4_rx_accum[i]);
			}
			printf("\r\n");

			/* --- 切 type --- */
			uart4_type_slice_len = 0;
			if (itype >= 0 && idata >= 0 && idata >= 2)
			{
				/* itype 为 "\"type\"" 的起始引号：跳过 \"type\"\": 共 7 字到类型值 */
				u16 ts = (u16)itype + 7u;
				/* idata 为 "\"data\"" 的起始引号：类型值在逗号前结束，即 idata 前两字为 ," */
				u16 te = (u16)idata - 1u;

				if (ts < te && te <= uart4_rx_accum_len)
				{
					u16 n = te - ts;
					if (n > UART4_DATA_SLICE_MAX)
						n = UART4_DATA_SLICE_MAX;
					memcpy(uart4_type_slice, uart4_rx_accum + ts, n);
					uart4_type_slice_len = n;
					if (n < UART4_DATA_SLICE_MAX)
						uart4_type_slice[n] = '\0';
				}
			}
			memset(order_type_uart4, 0, sizeof(order_type_uart4));
			for (i = 0; i < uart4_type_slice_len && i < (int)sizeof(order_type_uart4) - 1; i++)
			{
				printf("type-->%c\r\n", uart4_type_slice[i]);
				order_type_uart4[i] = uart4_type_slice[i];
			}
			/* --- 切 data（值位于 "data": 与 ,"uid" 之间） --- */
			uart4_data_slice_len = 0;
			if (idata >= 0 && iuid >= 2)
			{
				/* idata 指向 "\"data\"" 起始引号：+8 跳到值；cut_end=iuid-2 不含结尾引号与逗号 */
				u16 cut_start = (u16)idata + 8u;
				u16 cut_end = (u16)iuid - 2u;

				if (cut_start < cut_end && cut_end <= uart4_rx_accum_len)
				{
					u16 n = cut_end - cut_start;
					if (n > UART4_DATA_SLICE_MAX)
						n = UART4_DATA_SLICE_MAX;
					memcpy(uart4_data_slice, uart4_rx_accum + cut_start, n);
					uart4_data_slice_len = n;
					if (n < UART4_DATA_SLICE_MAX)
						uart4_data_slice[n] = '\0';
				}
			}
			printf("data-->");
			for (i = 0; i < uart4_data_slice_len; i++)
			{
				printf("%c", uart4_data_slice[i]);
			}
			printf("\r\n");
			/* --- 切 uid --- */
			card_Number_uart4[0] = '\0';
			if (iuid >= 0)
			{
				u16 p = (u16)iuid + (u16)strlen("\"uid\"");
				if (p < uart4_rx_accum_len && uart4_rx_accum[p] == ':')
					p++;
				while (p < uart4_rx_accum_len && (uart4_rx_accum[p] == ' ' || uart4_rx_accum[p] == '\t'))
					p++;
				{
					u16 start;
					u16 n = 0;

					if (p < uart4_rx_accum_len && uart4_rx_accum[p] == '"')
					{
						p++;
						start = p;
						while (p < uart4_rx_accum_len && uart4_rx_accum[p] != '"' && n < 31u)
						{
							p++;
							n++;
						}
					}
					else
					{
						start = p;
						while (p < uart4_rx_accum_len && uart4_rx_accum[p] >= '0' && uart4_rx_accum[p] <= '9' && n < 31u)
						{
							p++;
							n++;
						}
					}
					if (n > 0u)
					{
						memcpy(card_Number_uart4, uart4_rx_accum + start, n);
						card_Number_uart4[n] = '\0';
					}
				}
			}

			if (uart4_type_slice_len > 0 && uart4_data_slice_len > 0)
			{
				s_received_len_uart4 = (uart4_data_slice_len < MAX_RECEIVE_LEN - 1) ? uart4_data_slice_len : (MAX_RECEIVE_LEN - 1);
				memcpy(s_received_uart4, uart4_data_slice, s_received_len_uart4);
				s_received_uart4[s_received_len_uart4] = '\0';
				/* type: '1' 二维码 / '0' NFC → 命令分发；'2' 密码 */
				if (uart4_type_slice_len == 1 && (uart4_type_slice[0] == '1' || uart4_type_slice[0] == '0'))
				{
					CommControl(s_received_uart4, order_type_uart4, card_Number_uart4, 4);
				}
				else if (uart4_type_slice_len == 1 && uart4_type_slice[0] == '2')
				{
					qr_handle_password_input(s_received_uart4, 4);
				}
			}
			/* 完整帧已处理：清空累积与切片，准备下一帧 */
			uart4_rx_accum_len = 0;
			uart4_data_slice_len = 0;
			uart4_type_slice_len = 0;
			memset(uart4_rx_accum, 0, sizeof(uart4_rx_accum));
			memset(uart4_type_slice, 0, sizeof(uart4_type_slice));
			memset(uart4_data_slice, 0, sizeof(uart4_data_slice));
		}

		/* 无论是否凑齐完整帧，都清驱动标志，允许收下一截 */
		UART4_RX_Complete = 0;
		UART4_RX_CNT = 0;

		return;
	}
}
