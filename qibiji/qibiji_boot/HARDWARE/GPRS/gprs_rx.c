// 头文件
#include "main.h"
#include "usart.h"
#include "gprs.h"
#include <stdio.h>
#include <string.h>
#include "crc16.h"
#include "delay.h"
#include "boot.h"
#include "24cxx.h"
#include "ProgressBar.h"
#include "display.h"
u8 rxparagate = 0; // 接收到的闸门开度
u8 atflag = 0;	   // at可查询
extern int pub_version_flag;
OtaPara OtaParas;
CJsonPara CParas;
//char clientid[bClientid];
u16 process = 0; // 进度条分母＝总包数，最大 400 包，必须 u16（u8 会截断，且 count=256 时会除零）
extern OTA_InfoCB OTA_Info; // 存储在24C02内的OTA信息回调的结构体

// OTA下载上下文（跨包保留）
static u32 s_dl_addr = ST32_A2_SADDR;
static u32 s_dl_lastaddr = ST32_A2_SADDR;
static u16 s_dl_last_offset = 0;
static u8 s_dl_crc_retry_cnt = 0;
static u16 s_dl_expected_offset = 0;  // 会话内期望收到的包序号（offset）
static u32 s_dl_written_bytes = 0;    // 会话内已写入 A2 的字节数
static u8 s_dl_seq_retry_cnt = 0;     // 序号不符（重复包/乱序包）的连续重发次数

/* 掉线重连窗口（等 AT / 等网络上线）里到达的 putVersion 回执：先暂存，等状态机
   回到 HANDLE_REPLY 再解析。原实现把这类帧整帧丢弃，导致“请求已发、回包刚到就掉线”
   时平台回包白丢一次（现场表现为重连后一直 request timeout）。 */
#define GPRS_DEFER_BUF_LEN 512
static char s_defer_buf[GPRS_DEFER_BUF_LEN];
static u16 s_defer_len = 0;

/* 是否暂存着一个平台回包（main.c 在 ONLINE 态据此直接进入 HANDLE_REPLY） */
u8 gprs_rx_defer_pending(void)
{
	return (s_defer_len != 0) ? 1u : 0u;
}

void gprs_download_context_reset(void)
{
	s_dl_addr = ST32_A2_SADDR;
	s_dl_lastaddr = ST32_A2_SADDR;
	s_dl_last_offset = 0;
	s_dl_crc_retry_cnt = 0;
	s_dl_expected_offset = 0;
	s_dl_written_bytes = 0;
	s_dl_seq_retry_cnt = 0;
	s_defer_len = 0;   // 下载会话开始/结束：丢弃掉线窗口留下的旧回执
	gprs_rx_frame_reset();
}

// 是否有一轮下载正在进行（收到过包、且会话尚未结束）
u8 gprs_download_pending(void)
{
	return (s_dl_expected_offset != 0) ? 1 : 0;
}

//-------------------------GPRS接收数据处理-----------------------------------//
/*
 * USART2 的 DMA 空闲帧不等于一条完整 OTA 报文。4G 模组可能把 JSON、512B
 * 固件块和 CRC 拆成多次空闲中断，也可能把连续报文一次送入。这里从环形缓冲
 * 中只取一个完整帧，其余字节留给下一次调用。
 */
#define GPRS_RX_FRAME_MAX       1024
#define GPRS_RX_PREFIX_SCAN     32
#define GPRS_RX_TAKE_NONE       0
#define GPRS_RX_TAKE_READY      1
#define GPRS_RX_TAKE_ERROR     -1

static char s_uart_rx2_frame[GPRS_RX_FRAME_MAX];
static char s_uart_rx2_json[GPRS_RX_FRAME_MAX];
static u32 s_rx_last_overflow_count = 0;
static u16 s_rx_last_wait_need = 0;
static u16 s_rx_last_wait_have = 0;
static u16 s_rx_frame_diag_count = 0;
static u16 s_rx_resync_count = 0;

void gprs_rx_frame_reset(void)
{
	Usart2_RxClear();
	s_rx_last_wait_need = 0;
	s_rx_last_wait_have = 0;
}

static u16 gprs_rx_find_json_start(const char *buf, u16 len)
{
	u16 i;
	u16 limit = (len < GPRS_RX_PREFIX_SCAN) ? len : GPRS_RX_PREFIX_SCAN;

	if (buf == NULL)
		return (u16)-1;
	for (i = 0; i < limit; i++)
	{
		if (buf[i] == '{')
			return i;
	}
	return (u16)-1;
}

static int gprs_rx_expected_len(
	const char *buf,
	u16 available,
	u16 json_offset,
	u16 json_len,
	u16 *expected_len,
	u16 *frame_offset,
	u8 *is_firmware)
{
	cJSON *root;
	cJSON *body;
	cJSON *ota_code;
	cJSON *paras;
	cJSON *size_item;
	cJSON *offset_item;
	u16 expected;

	*expected_len = 0;
	*frame_offset = 0;
	*is_firmware = 0;

	if (json_len == 0 || json_len >= sizeof(s_uart_rx2_json))
		return -1;
	if ((u32)json_offset + json_len > available)
		return -1;

	memcpy(s_uart_rx2_json, buf + json_offset, json_len);
	s_uart_rx2_json[json_len] = '\0';
	root = cJSON_Parse(s_uart_rx2_json);
	if (root == NULL)
		return -1;

	expected = (u16)(json_offset + json_len);
	body = cJSON_GetObjectItem(root, "body");
	ota_code = (body != NULL) ? cJSON_GetObjectItem(body, "OTACode") : NULL;
	if (ota_code != NULL && cJSON_IsString(ota_code) && ota_code->valuestring != NULL &&
		strcmp(ota_code->valuestring, "getFirmware") == 0)
	{
		*is_firmware = 1;
		paras = cJSON_GetObjectItem(body, "paras");
		size_item = (paras != NULL) ? cJSON_GetObjectItem(paras, "size") : NULL;
		offset_item = (paras != NULL) ? cJSON_GetObjectItem(paras, "offset") : NULL;
		if (offset_item != NULL && cJSON_IsNumber(offset_item))
			*frame_offset = (u16)cJSON_GetNumberValue(offset_item);

		/*
		 * 正常固件块长度固定为 1..512。非法长度只等 JSON 本身，随后交给
		 * cjson_download_reply 返回明确错误，避免为了不存在的负载无限等待。
		 */
		if (size_item != NULL && cJSON_IsNumber(size_item))
		{
			double size_value = cJSON_GetNumberValue(size_item);
			if (size_value > 0.0 && size_value <= BYTESIZE)
				expected = (u16)(expected + (u16)size_value + 2);
		}
	}

	cJSON_Delete(root);
	if (expected > GPRS_RX_FRAME_MAX)
		return -1;
	*expected_len = expected;
	return 0;
}

static int gprs_rx_take_frame(
	u16 *frame_len,
	u16 *json_offset_out,
	u16 *expected_out,
	u16 *offset_out,
	u8 *is_firmware)
{
	u16 available;
	u16 peek_len;
	u16 json_offset;
	u16 json_len;
	u16 expected;
	u16 frame_offset;
	char *json_end;
	u32 overflow_count;

	*frame_len = 0;
	*json_offset_out = 0;
	*expected_out = 0;
	*offset_out = 0;
	*is_firmware = 0;

	overflow_count = Usart2_RxOverflowCount();
	if (overflow_count != s_rx_last_overflow_count)
	{
		s_rx_last_overflow_count = overflow_count;
		Usart2_RxClear();
		s_rx_last_wait_need = 0;
		s_rx_last_wait_have = 0;
		printf("rx ring overflow: total=%u, resync\r\n", (unsigned)overflow_count);
		return GPRS_RX_TAKE_NONE;
	}

	available = Usart2_RxAvailable();
	if (available == 0)
		return GPRS_RX_TAKE_NONE;

	peek_len = (available > sizeof(s_uart_rx2_frame)) ?
		sizeof(s_uart_rx2_frame) : available;
	peek_len = Usart2_RxCopy(s_uart_rx2_frame, peek_len);
	s_uart_rx2_frame[peek_len] = '\0';

	json_offset = gprs_rx_find_json_start(s_uart_rx2_frame, peek_len);
	if (json_offset == (u16)-1)
	{
		/* 只收到模组前缀时继续等待；明显是 URC/AT 噪声时逐字节重新对齐。 */
		if (peek_len > 8)
		{
			Usart2_RxConsume(1);
			s_rx_resync_count++;
			if (s_rx_resync_count == 1 || (s_rx_resync_count % 64) == 0)
				printf("rx resync: dropped=%u, available=%u\r\n",
					s_rx_resync_count, available);
		}
		return GPRS_RX_TAKE_NONE;
	}

	json_end = find_json_end_by_bracket_matching(
		s_uart_rx2_frame + json_offset,
		(u16)(peek_len - json_offset));
	if (json_end == NULL)
		return GPRS_RX_TAKE_NONE;

	json_len = (u16)(json_end - (s_uart_rx2_frame + json_offset) + 1);
	if (gprs_rx_expected_len(
		s_uart_rx2_frame,
		peek_len,
		json_offset,
		json_len,
		&expected,
		&frame_offset,
		is_firmware) != 0)
	{
		Usart2_RxConsume((u16)(json_offset + json_len));
		printf("rx frame invalid: prefix=%u, json=%u, available=%u\r\n",
			json_offset, json_len, available);
		return GPRS_RX_TAKE_ERROR;
	}

	if (peek_len < expected)
	{
		if (expected != s_rx_last_wait_need || available != s_rx_last_wait_have)
		{
			s_rx_last_wait_need = expected;
			s_rx_last_wait_have = available;
			printf("rx frame wait: prefix=%u, need=%u, have=%u, offset=%u\r\n",
				json_offset, expected, available, frame_offset);
		}
		return GPRS_RX_TAKE_NONE;
	}

	s_uart_rx2_frame[expected] = '\0';
	Usart2_RxConsume(expected);
	s_rx_last_wait_need = 0;
	s_rx_last_wait_have = 0;

	if (s_rx_frame_diag_count < 10 || *is_firmware == 0 || frame_offset == 5)
	{
		printf("rx frame ready: prefix=%u, expected=%u, available=%u, offset=%u\r\n",
			json_offset, expected, available, frame_offset);
	}
	s_rx_frame_diag_count++;

	*frame_len = expected;
	*json_offset_out = json_offset;
	*expected_out = expected;
	*offset_out = frame_offset;
	return GPRS_RX_TAKE_READY;
}

static u8 gprs_rx_consume_at_ok(void)
{
	u16 available = Usart2_RxAvailable();
	u16 len;
	char *ok;

	if (available == 0)
		return 0;
	len = (available > 256) ? 256 : available;
	len = Usart2_RxCopy(s_uart_rx2_frame, len);
	s_uart_rx2_frame[len] = '\0';
	ok = strstr(s_uart_rx2_frame, "OK");
	if (ok == NULL)
		return 0;

	Usart2_RxConsume((u16)((ok - s_uart_rx2_frame) + 2));
	return 1;
}

int gprs_rx_datas(void)
{
	char *pos;
	u16 u2rxcnt = 0;
	u16 json_offset = 0;
	u16 expected = 0;
	u16 frame_offset = 0;
	u8 is_firmware = 0;
	cJSON *root;
	u16 cjson_len = 0;
	u16 message_len = 0;
	int txsta;
	int take_result;
	int rx_ret = GPRS_RX_RET_PARSE_FAILED;
	cJSON *body_item;
	cJSON *ota_code;
	u8 from_defer = 0;
	u16 i;

	/* 掉线重连窗口暂存的 putVersion 回包优先处理。 */
	if (s_defer_len != 0 && (gprs_download_pending() == 0)
		&& SYS_state != OTA_STATE_WAIT_NET && SYS_state != OTA_STATE_AT_OK)
	{
		u2rxcnt = s_defer_len;
		s_defer_len = 0;
		from_defer = 1;
		memcpy(s_uart_rx2_frame, s_defer_buf, u2rxcnt);
		json_offset = gprs_rx_find_json_start(s_uart_rx2_frame, u2rxcnt);
		printf("parse stashed downlink frame (%u bytes)\r\n", u2rxcnt);
	}
	else
	{
		if (SYS_state == OTA_STATE_WAIT_NET || SYS_state == OTA_STATE_AT_OK)
		{
			if (gprs_rx_consume_at_ok())
			{
				if (SYS_state == OTA_STATE_WAIT_NET)
				{
					SYS_state = OTA_STATE_AT_OK;
					printf("Received AT OK\r\n");
					return GPRS_RX_RET_AT_OK;
				}
				return GPRS_RX_RET_NO_DATA;
			}
		}

		take_result = gprs_rx_take_frame(
			&u2rxcnt, &json_offset, &expected, &frame_offset, &is_firmware);
		if (take_result == GPRS_RX_TAKE_NONE)
			return GPRS_RX_RET_NO_DATA;
		if (take_result == GPRS_RX_TAKE_ERROR)
			return GPRS_RX_RET_PARSE_FAILED;
	}

	if (json_offset == (u16)-1 || json_offset >= u2rxcnt)
		return GPRS_RX_RET_PARSE_FAILED;

	/* 握手期间仅暂存 putVersion；完整下载帧在下载会话中正常处理。 */
	if (SYS_state == OTA_STATE_WAIT_NET || SYS_state == OTA_STATE_AT_OK)
	{
		char head[17];
		u16 n = (u2rxcnt > 16) ? 16 : u2rxcnt;

		for (i = 0; i < n; i++)
			head[i] = s_uart_rx2_frame[i];
		head[n] = '\0';
		printf("rx handshake-window frame (%u bytes): %s\r\n", u2rxcnt, head);

		if (!from_defer && str_contains(s_uart_rx2_frame, "putVersion"))
		{
			if (s_defer_len == 0 && u2rxcnt <= sizeof(s_defer_buf))
			{
				memcpy(s_defer_buf, s_uart_rx2_frame, u2rxcnt);
				s_defer_len = u2rxcnt;
				printf("stash putVersion reply (%u bytes), parse after reconnect\r\n", u2rxcnt);
			}
			else
			{
				printf("stash busy, drop this putVersion reply\r\n");
			}
		}
		return GPRS_RX_RET_NO_DATA;
	}

	pos = find_json_end_by_bracket_matching(
		s_uart_rx2_frame + json_offset,
		(u16)(u2rxcnt - json_offset));
	if (pos == NULL)
	{
		printf("s->c back JSON validation failed: offset=%u, bytes=%u\r\n",
			json_offset, u2rxcnt);
		return GPRS_RX_RET_PARSE_FAILED;
	}

	cjson_len = (u16)(pos - (s_uart_rx2_frame + json_offset) + 1);
	if (cjson_len >= sizeof(s_uart_rx2_json))
	{
		printf("json payload too long: %u\r\n", cjson_len);
		return GPRS_RX_RET_PARSE_FAILED;
	}
	memcpy(s_uart_rx2_json, s_uart_rx2_frame + json_offset, cjson_len);
	s_uart_rx2_json[cjson_len] = '\0';
	root = cJSON_Parse(s_uart_rx2_json);
	if (root == NULL)
	{
		printf("cJSON_Parse failed: prefix=%u, json=%u\r\n", json_offset, cjson_len);
		return GPRS_RX_RET_PARSE_FAILED;
	}

	printf("rx json: %s\r\n", s_uart_rx2_json);
	body_item = cJSON_GetObjectItem(root, "body");
	ota_code = (body_item != NULL) ? cJSON_GetObjectItem(body_item, "OTACode") : NULL;
	if (ota_code != NULL && cJSON_IsString(ota_code) && ota_code->valuestring != NULL &&
		strcmp(ota_code->valuestring, "putVersion") == 0)
	{
		txsta = cjson_upgrade(root);
		if (txsta == 1)
		{
			progressbar_Init();
			printf("version: Firmware upgrade started, requesting first data block\r\n");
			rx_ret = GPRS_RX_RET_START_UPDATE;
		}
		else if (txsta == 0)
		{
			printf("version: Version is same or upgrade not needed\r\n");
			rx_ret = GPRS_RX_RET_NO_UPDATE;
		}
		else
		{
			rx_ret = txsta;
		}
	}
	else if (ota_code != NULL && cJSON_IsString(ota_code) && ota_code->valuestring != NULL &&
		strcmp(ota_code->valuestring, "getFirmware") == 0)
	{
		message_len = (u16)(u2rxcnt - (u16)((pos + 1) - s_uart_rx2_frame));
		txsta = cjson_download_reply(root, pos + 1, message_len);
		if (txsta == 1)
		{
			printf("download: Firmware download completed, ready to restart\r\n");
			rx_ret = GPRS_RX_RET_DOWNLOAD_DONE;
		}
		else if (txsta == 0)
		{
			printf("download: Data block written successfully, continue downloading next block\r\n");
			rx_ret = GPRS_RX_RET_DOWNLOAD_CONTINUE;
		}
		else
		{
			rx_ret = txsta;
		}
	}

	(void)expected;
	(void)frame_offset;
	(void)is_firmware;
	cJSON_Delete(root);
	return rx_ret;
}

	//-------------------------CJSON 命令 ------------------------------------
	// 固件升级中，只有客户端主动发送请求，服务器才会回应数据这一种情况，不存在服务器主动发送数据给客户端的情况。

	//------服务器 -> 客户端----------
	// 固件文件信息，及是否允许升级
	int cjson_upgrade(cJSON * root)
	{
		cJSON *para, *size, *ver, *upgrade,*body;
		u8 gradeflag = 0;
		int result;
		size_t ver_len;
		u8 upgrade_flag = 0;   /* 条件1：平台标志（putVersion 回包 upgrade） */
		u8 version_same = 0;   /* 条件2：版本闸（本机 OTA_ver 是否已等于回包 version） */

		// 先验证格式，确保para字段存在且正确  -1~-10
		//	result = validate_para_json_object1(root);
		//	if (result < 0)
		//	{
		//		printf("s->c back JSON validation failed with error code <para>: %d\n", result);
		//		return result;
		//	}

		// {
		// 	char *json_dbg = cJSON_PrintUnformatted(root);
		// 	if (json_dbg != NULL)
		// 	{
		// 		printf("cjson_upgrade json: %s\r\n", json_dbg);
		// 		cJSON_free(json_dbg);
		// 	}
		// }

		body = cJSON_GetObjectItem(root, "body");
		para = (body != NULL) ? cJSON_GetObjectItem(body, "paras") : NULL;
		size = (para != NULL) ? cJSON_GetObjectItem(para, "size") : NULL;
		ver = (para != NULL) ? cJSON_GetObjectItem(para, "version") : NULL;
		upgrade = (para != NULL) ? cJSON_GetObjectItem(para, "upgrade") : NULL;

		// {
		// 	char *s;
		// 	s = cJSON_PrintUnformatted(body);
		// 	printf("body: %s\r\n", s ? s : "NULL");
		// 	if (s) cJSON_free(s);
		// 	s = cJSON_PrintUnformatted(para);
		// 	printf("para: %s\r\n", s ? s : "NULL");
		// 	if (s) cJSON_free(s);
		// 	s = cJSON_PrintUnformatted(size);
		// 	printf("size: %s\r\n", s ? s : "NULL");
		// 	if (s) cJSON_free(s);
		// 	s = cJSON_PrintUnformatted(ver);
		// 	printf("ver: %s\r\n", s ? s : "NULL");
		// 	if (s) cJSON_free(s);
		// 	s = cJSON_PrintUnformatted(upgrade);
		// 	printf("upgrade: %s\r\n", s ? s : "NULL");
		// 	if (s) cJSON_free(s);
		// }

		if (body == NULL || para == NULL || size == NULL || ver == NULL || upgrade == NULL
			|| !cJSON_IsString(ver) || ver->valuestring == NULL
			|| !cJSON_IsNumber(size) || !cJSON_IsNumber(upgrade))
		{
			printf("cjson_upgrade: body/paras/size/version/upgrade invalid\r\n");
			return -30;
		}

		// 判断是否需要升级 = 平台标志 ∧ 版本闸。
		// 设备升级标志 UPDATE_flag 已在 main.c 进入本状态机前判过（为 0 时根本不会发 putVersion），
		// 所以这里不再重复判断。三个条件分别打印，便于现场排查。
		upgrade_flag = (cJSON_GetNumberValue(upgrade) != 0) ? 1 : 0;
		version_same = (strcmp((char *)OTA_Info.OTA_ver, ver->valuestring) == 0) ? 1 : 0;

		printf("cjson_upgrade: [1/3] platform upgrade = %d %s\r\n",
			   (int)upgrade_flag,
			   upgrade_flag ? "(allowed)" : "(not allowed: UpdateHardware=0 or fw not bound/loaded)");
		printf("cjson_upgrade: [2/3] ver->valuestring = %s (target version)\r\n", ver->valuestring);
		printf("cjson_upgrade: [3/3] OTA_Info.OTA_ver = %s (running version, %s target)\r\n",
			   (char *)OTA_Info.OTA_ver, version_same ? "same as" : "differs from");
		printf("cjson_upgrade: size=%d\r\n", (int)cJSON_GetNumberValue(size));

		if (upgrade_flag && !version_same)
		{
			OtaParas.size = cJSON_GetNumberValue(size);
			memset(OtaParas.ver, 0, 32);
			ver_len = strlen(ver->valuestring);
			strncpy(OtaParas.ver, ver->valuestring, (ver_len < 31) ? ver_len : 31);
			OtaParas.ver[31] = '\0'; // 确保字符串结束
			

			// 固件大小合法性：非 0，且不超过 A2 区容量（100 页 × 2KB = 200KB）
			if (OtaParas.size == 0 || OtaParas.size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
			{
				printf("invalid firmware size: %u, abort upgrade\r\n", OtaParas.size);
				return 0; // 视为无需升级 → 主流程跳回 A1（旧程序）
			}

			// offset = 包序号，count = 总包数：按 512 字节/包换算
			if ((OtaParas.size % BYTESIZE) != 0)
			{
				OtaParas.count = (u16)(OtaParas.size / BYTESIZE + 1);
				OtaParas.remaining = (u16)(OtaParas.size % BYTESIZE); // 最后一包字节数
			}
			else
			{
				OtaParas.count = (u16)(OtaParas.size / BYTESIZE);
				OtaParas.remaining = BYTESIZE;
			}
			printf("upgrade is start -- size = %u bytes, count = %u packets, last = %u bytes\r\n",
				   OtaParas.size, OtaParas.count, OtaParas.remaining);

			// 新一轮下载会话：复位写指针与进度
			// （掉线重连不会走到这里：ONLINE 状态会先重发在途请求，见 main.c）
			gprs_download_context_reset();

			// 开始更新
			// ota_step1();
			process = OtaParas.count;
			ProgressBar_Update(0);
			delay_ms(100); // 延迟100ms

			printf("cjson_upgrade: DECISION = START_UPDATE (upgrade=1 and version differs)\r\n");
			cjson_pub_getbin(0);
			// 需要升级
			return 1;
		}
		// 无需升级
		printf("cjson_upgrade: DECISION = NO_UPDATE (%s)\r\n",
			   upgrade_flag ? "version already up to date" : "platform upgrade=0, upgrade not allowed");
		return 0;
	}
	// 应答设备端请求的bin文件数据
	u8 cjson_download_reply(cJSON * root, char *message, u16 message_len)
	{
		cJSON *para, *bOffset, *bsize,*body;
		char *pos;
		u16 Offset = 0, Size = 0;
		u16 crc = 0;
		int result;
		size_t ota_ver_len;

		u16 i, j;

		u16 orig_size;
		// 先验证格式，确保para字段存在且正确 -11~-18
		//	result = validate_para_json_object2(root);
		//	if (result < 0)//error type -1~-6
		//	{
		//		printf("s->c back JSON validation failed with error code <para>: %d\n", result);
		//		return result;
		//	}
		// 获得参数
		// body = cJSON_GetObjectItem(root, "body");
		// para = cJSON_GetObjectItem(body, "para");
		// bOffset = cJSON_GetObjectItem(para, "offset");
		// bsize = cJSON_GetObjectItem(para, "size");

		body = cJSON_GetObjectItem(root, "body");
		para = (body != NULL) ? cJSON_GetObjectItem(body, "paras") : NULL;
		bsize = (para != NULL) ? cJSON_GetObjectItem(para, "size") : NULL;
		bOffset = (para != NULL) ? cJSON_GetObjectItem(para, "offset") : NULL;


		// 转换数据类型
		Offset = cJSON_GetNumberValue(bOffset);
		Size = cJSON_GetNumberValue(bsize);

		// 序号闸门：只接受本会话期望的那一包；重复包/乱序包一律丢弃，
		// 然后“重新申请同一个请求”（不是从 0 重来）
		if (Offset != s_dl_expected_offset)
		{
			printf("packet offset mismatch: got %u, expect %u\r\n", Offset, s_dl_expected_offset);
			if (s_dl_seq_retry_cnt < 5)
			{
				s_dl_seq_retry_cnt++;
				cjson_pub_getbin(s_dl_expected_offset);
				return GPRS_RX_RET_DOWNLOAD_CONTINUE; // 主流程按“仍在下载”处理，不报错
			}
			printf("packet offset mismatch too many times\r\n");
			return GPRS_RX_ERR_SEQ_RETRY_EXCEEDED;    // 交由主流程计数并最终放弃
		}
		s_dl_seq_retry_cnt = 0;

		// 包长闸门：单包不得为 0，且不得超过 512 字节
		if (Size == 0 || Size > BYTESIZE)
		{
			printf("invalid packet size: %u\r\n", Size);
			return GPRS_RX_ERR_PACKET_SIZE;
		}

		// 累计长度闸门：已写字节 + 本包不得越过协议声明的固件大小
		if ((s_dl_written_bytes + Size) > OtaParas.size)
		{
			printf("download overflow: %u + %u > %u\r\n", s_dl_written_bytes, Size, OtaParas.size);
			return GPRS_RX_ERR_LENGTH_OVERFLOW;
		}

		if ((u32)Size + 2 > message_len)
		{
			printf("download packet too short: need %u, have %u\r\n", (u16)(Size + 2), message_len);
			return GPRS_RX_ERR_PACKET_SHORT;
		}

		printf("Offset = %d,size = %d\r\n", Offset, Size);
		//		if(Offset < (OtaParas.count-1))
		//			cjson_pub_getbin(Offset+1);
		//
		//		return 0;
		// 提取校验码
		crc = CRC16_CCITT_FALSE((u8 *)&message[0], Size);
		// printf("crc = --%x , %x , %x , %x , %x , %x\r\n", crc, message[0], message[1], message[511], message[512], message[513]);
		//  crc校验
		if (((crc >> 8) & 0xff) != message[Size + 1] || (crc & 0xff) != message[Size]) // 校验失败，重新请求
		{
			// 每次产生错误请求，先清零错误标志
			if (s_dl_last_offset != Offset)
			{
				s_dl_crc_retry_cnt = 0;
				s_dl_last_offset = Offset;
			}
			else
			{
				s_dl_crc_retry_cnt++;
			}
			if (s_dl_crc_retry_cnt < 5)
				cjson_pub_getbin(Offset);
			else
				s_dl_crc_retry_cnt = 5;
			printf("download data error %d\n", s_dl_crc_retry_cnt);
			return GPRS_RX_ERR_CRC_RETRY_EXCEEDED;
		}

		orig_size = Size; // 保存原始字节数用于打印
		if ((Size % 2) != 0)
			Size = Size / 2 + 1;
		else
			Size = Size / 2;

		// 写flash A2区
		printf("write A2 zone-- %d --\r\n", Offset);
		s_dl_addr += Size * 2;
		// printf("read A2 addr-- %d --%d\r\n", s_dl_addr,s_dl_lastaddr);
		// test_mod
		STMFLASH_Write(s_dl_lastaddr, (u16 *)&message[0], Size);
		s_dl_lastaddr = s_dl_addr;
		s_dl_written_bytes += orig_size; // 累计已写入字节数（orig_size 为字节数）
		s_dl_expected_offset++;          // 推进会话进度：下一包必须是这个序号
		// 打印message数组
		/*	printf("message array: ");
			for (i = 0; i < orig_size + 2; i++) {
				printf("%02x ", (u8)message[i]);
			}
			printf("\r\n");*/

		if (Offset < (OtaParas.count - 1)) // 写满字节数512
		{
			cjson_pub_getbin(Offset + 1);
			ProgressBar_Update(Offset * 100 / process);
			delay_ms(200); // 延迟100ms
			printf("request next data --\r\n");
		}
		else if (Offset == (OtaParas.count - 1)) // 最后一帧
		{
			// 收尾一致性校验：累计写入必须等于协议声明的固件大小，
			// 否则不落盘 OTA_flag，交由主流程计数并放弃（引导旧程序）
			if (s_dl_written_bytes != OtaParas.size)
			{
				printf("download size mismatch: written %u, expect %u\r\n",
					   s_dl_written_bytes, OtaParas.size);
				return GPRS_RX_ERR_LENGTH_OVERFLOW;
			}

			// 存储信息
			// 顺序：先写 OTA_size、再写 OTA_ver，最后才写 OTA_flag。
			// 24C02 是逐字节 I2C 写，中途掉电时只会停在「flag 仍是 A1」的安全状态；
			// 若先写 flag，就可能留下 flag=A2 而 size=0 的坏状态，引导侧会搬运垃圾（护栏2 会拦，但风险不该留给引导侧）。
			gprs_download_context_reset();

			memset(&OTA_Info.OTA_ver, 0, 32);
			ota_ver_len = strlen(OtaParas.ver);
			strncpy((char *)OTA_Info.OTA_ver, OtaParas.ver, (ota_ver_len < 31) ? ota_ver_len : 31);
			OTA_Info.OTA_ver[31] = '\0';	   // 确保字符串结束
			OTA_Info.OTA_size = OtaParas.size; // 总字节 例如47600
			printf("OTA_size = %d \r\n ", OTA_Info.OTA_size);

			if (AT24CXX_Write(OTA_ADDR + OTA_OFF_SIZE, (uint8_t *)&OTA_Info.OTA_size, 4) != AT24CXX_OK)
			{
				printf("write ota size to eeprom error --\r\n");
				return GPRS_RX_ERR_EEPROM_WRITE;
			}
			if (AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (uint8_t *)&OTA_Info.OTA_ver[0], 32) != AT24CXX_OK)
			{
				printf("write ota ver to eeprom error --\r\n");
				return GPRS_RX_ERR_EEPROM_WRITE;
			}

			OTA_Info.OTA_flag = OTA_A2_FLAG;
			if (AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (uint8_t *)&OTA_Info.OTA_flag, 4) != AT24CXX_OK)
			{
				printf("write ota flag to eeprom error --\r\n");
				return GPRS_RX_ERR_EEPROM_WRITE;
			}

			cjson_pub_putError();
			printf("update is ok --\r\n");
			ProgressBar_Update(100);
			delay_ms(200);
			// NVIC_SystemReset(); // 设备重启
			return 1;
		}

		return 0;
	}

	//------ 客户端 -> 服务器----------
	// 版本号上报
	void cjson_pub_version(void)
	{
		//const char *cid = (clientid[0] != '\0') ? clientid : "74000000004";
		//memset(CParas.type, 0, bType);
		//memset(CParas.back, 0, bBack);
		//memset(CParas.CmdName, 0, bCmdName);
		// memset(CParas., 0, bRandom);
		snprintf(CParas.ver, bver, "%s", "v1.0");	/* 协议版本，固定 1.0 */
		snprintf(CParas.productKey, bproductKey, "%s", g_mqtt_productKey);	/* 与 A1 应用同源（24C02） */
		snprintf(CParas.deviceKey, bdeviceKey, "%s", g_mqtt_deviceKey);
		snprintf(CParas.type, btype, "%s", "OTA");
		snprintf(CParas.OTACode, bOTACode, "%s", "putVersion");
		/* id / ts 由 gprs_send_datas() 每次发送时重新生成（含超时重发） */
		// 上报「已安装版本」：只有 OTA_flag==A1（A1 已就绪）时 OTA_ver 才代表本机运行版本；
		// 待安装期间（flag=A2）OTA_ver 是「目标版本」，不能当成运行版本上报。
		if (OTA_Info.OTA_flag == OTA_A1_FLAG && OTA_Info.OTA_ver[0] != '\0')
			snprintf(CParas.version, bversion, "%s", (char *)OTA_Info.OTA_ver);
		else
			snprintf(CParas.version, bversion, "%s", "v0.0.0");
		//generate_random_string(CParas.random);
		// printf("c->s --begin\r\n");
		gprs_send_datas(SVERSION, CParas);
		// printf("c->s --end\r\n");
		pub_version_flag = 0;
	}
	// 请求bin文件块序号
	void cjson_pub_getbin(u16 offset)
	{ // TODO 设备号的设定逻辑不完整。
		// const char *cid = (clientid[0] != '\0') ? clientid : "74000000004";
		// memset(CParas.type, 0, bType);
		// memset(CParas.back, 0, bBack);
		// snprintf(CParas.clientid, bClientid, "%s", cid);
		// memset(CParas.CmdName, 0, bCmdName);
		// memset(CParas.random, 0, bRandom);
		// snprintf(CParas.type, bType, "%s", "cmd");
		// snprintf(CParas.back, bBack, "%s", "true");
		// snprintf(CParas.CmdName, bCmdName, "%s", "download");
		// generate_random_string(CParas.random);
		CParas.offset = offset;
		snprintf(CParas.ver, bver, "%s", "v1.0");	/* 协议版本，固定 1.0 */
		snprintf(CParas.productKey, bproductKey, "%s", g_mqtt_productKey);	/* 与 A1 应用同源（24C02） */
		snprintf(CParas.deviceKey, bdeviceKey, "%s", g_mqtt_deviceKey);
		snprintf(CParas.type, btype, "%s", "OTA");
		snprintf(CParas.OTACode, bOTACode, "%s", "getFirmware");
		/* id / ts 由 gprs_send_datas() 每次发送时重新生成（含超时重发） */
		//snprintf(CParas.offset, boffset, "%s", "0");

		gprs_send_datas(UPGRADE, CParas);
	}
	// 请求bin文件块序号
	void cjson_pub_putError(void)
	{ // TODO 设备号的设定逻辑不完整。
		// const char *cid = (clientid[0] != '\0') ? clientid : "74000000004";
		// memset(CParas.type, 0, bType);
		// memset(CParas.back, 0, bBack);
		// snprintf(CParas.clientid, bClientid, "%s", cid);
		// memset(CParas.CmdName, 0, bCmdName);
		// memset(CParas.random, 0, bRandom);
		// snprintf(CParas.type, bType, "%s", "cmd");
		// snprintf(CParas.back, bBack, "%s", "true");
		// snprintf(CParas.CmdName, bCmdName, "%s", "download");
		// generate_random_string(CParas.random);
		snprintf(CParas.ver, bver, "%s", "v1.0");	/* 协议版本，固定 1.0 */
		snprintf(CParas.productKey, bproductKey, "%s", g_mqtt_productKey);	/* 与 A1 应用同源（24C02） */
		snprintf(CParas.deviceKey, bdeviceKey, "%s", g_mqtt_deviceKey);
		snprintf(CParas.type, btype, "%s", "OTA");
		snprintf(CParas.OTACode, bOTACode, "%s", "otaError");
		/* id / ts 由 gprs_send_datas() 每次发送时重新生成 */
		snprintf(CParas.code, bcode, "%s", "success");
		snprintf(CParas.message, bmessage, "%s", "OTA IS OK");
		// snprintf(CParas.offset, boffset, "%s", "0");

		gprs_send_datas(PUTERROR, CParas);
	}
	// 工具函数，判断json合法性
	//---------------------------------
	/**
	 * @brief 验证JSON是否符合服务器回执格式
	 * @param root cJSON根对象
	 * @return 0-符合条件, 其他值-错误代码
	 *
	 * 错误代码定义：
	 * -21: type字段不存在
	 * -22: type字段不是字符串
	 * -23: type字段值不是"back"
	 * -24: back字段不存在
	 * -25: back字段不是字符串
	 * -26: cliendid字段不存在
	 * -27: CmdName字段不存在
	 * -28: CmdName字段不是字符串
	 * -29: CmdName字段值不是"version"或"download"
	 * -30: para字段不存在
	 * -31: para字段不是对象
	 * -32: para.version字段不存在或不是字符串
	 * -33: para.size字段不存在或不是数字
	 * -34: para.size字段不是整数
	 * -35: para.upgrade字段不存在或不是数字
	 * -36: para.upgrade字段不是整数
	 * -37: para.offset字段不存在或不是数字
	 * -38: para.offset字段不是整数
	 * -39: para.size字段不存在或不是数字（download命令）
	 * -40: para.size字段不是整数（download命令）
	 * -41: random字段不存在
	 */
	int validate_server_reply_json(cJSON * root)
	{
		cJSON *item;
		double size_value;
		double upgrade_value;
		double offset_value;

		// 1. 检查type字段
		item = cJSON_GetObjectItem(root, "type");
		if (item == NULL)
		{
			printf("Error: type field not found\n");
			return -21;
		}
		if (!cJSON_IsString(item) || item->valuestring == NULL)
		{
			printf("Error: type field is not a string\n");
			return -22;
		}
		if (strcmp(item->valuestring, "back") != 0)
		{
			printf("Error: type field value is not 'back', got: %s\n", item->valuestring);
			return -23;
		}

		// 2. 检查back字段
		item = cJSON_GetObjectItem(root, "back");
		if (item == NULL)
		{
			printf("Error: back field not found\n");
			return -24;
		}
		if (!cJSON_IsString(item) || item->valuestring == NULL)
		{
			printf("Error: back field is not a string\n");
			return -25;
		}
		// back字段值可以是"true"或"false"，这里只检查是否存在

		// 3. 检查clientid字段
		item = cJSON_GetObjectItem(root, "clientid");
		if (item == NULL)
		{
			printf("Error: clientid field not found\n");
			return -26;
		}
		// clientid可以是字符串或数字，这里只检查是否存在

		// 4. 检查CmdName字段
		item = cJSON_GetObjectItem(root, "CmdName");
		if (item == NULL)
		{
			printf("Error: CmdName field not found\n");
			return -27;
		}
		if (!cJSON_IsString(item) || item->valuestring == NULL)
		{
			printf("Error: CmdName field is not a string\n");
			return -28;
		}
		if (strcmp(item->valuestring, "version") != 0 &&
			strcmp(item->valuestring, "download") != 0)
		{
			printf("Error: CmdName field value is not 'version' or 'download', got: %s\n",
				   item->valuestring);
			return -29;
		}

		// 5. 检查para字段（必须是对象）
		item = cJSON_GetObjectItem(root, "para");
		if (item == NULL)
		{
			printf("Error: para field not found\n");
			return -30;
		}
		if (!cJSON_IsObject(item))
		{
			printf("Error: para field is not an object\n");
			return -31;
		}

		// 6. 根据CmdName验证para字段的内容
		if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "version") == 0)
		{
			// version命令：para必须包含version, size, upgrade字段
			cJSON *ver = cJSON_GetObjectItem(item, "version");
			cJSON *size = cJSON_GetObjectItem(item, "size");
			cJSON *upgrade = cJSON_GetObjectItem(item, "upgrade");

			if (ver == NULL || !cJSON_IsString(ver) || ver->valuestring == NULL)
			{
				printf("Error: para.version field not found or not a string\n");
				return -32;
			}
			if (size == NULL || !cJSON_IsNumber(size))
			{
				printf("Error: para.size field not found or not a number\n");
				return -33;
			}
			// 检查size字段是否是整数（不是浮点数）
			size_value = cJSON_GetNumberValue(size);
			if (size_value != (int)size_value)
			{
				printf("Error: para.size field is not an integer (got: %f)\n", size_value);
				return -34;
			}
			if (upgrade == NULL || !cJSON_IsNumber(upgrade))
			{
				printf("Error: para.upgrade field not found or not a number\n");
				return -35;
			}
			// 检查upgrade字段是否是整数（不是浮点数）
			upgrade_value = cJSON_GetNumberValue(upgrade);
			if (upgrade_value != (int)upgrade_value)
			{
				printf("Error: para.upgrade field is not an integer (got: %f)\n", upgrade_value);
				return -36;
			}
		}
		else if (strcmp(cJSON_GetObjectItem(root, "CmdName")->valuestring, "download") == 0)
		{
			// download命令：para必须包含offset, size字段
			cJSON *offset = cJSON_GetObjectItem(item, "offset");
			cJSON *size = cJSON_GetObjectItem(item, "size");

			if (offset == NULL || !cJSON_IsNumber(offset))
			{
				printf("Error: para.offset field not found or not a number\n");
				return -37;
			}
			// 检查offset字段是否是整数（不是浮点数）
			offset_value = cJSON_GetNumberValue(offset);
			if (offset_value != (int)offset_value)
			{
				printf("Error: para.offset field is not an integer (got: %f)\n", offset_value);
				return -38;
			}
			if (size == NULL || !cJSON_IsNumber(size))
			{
				printf("Error: para.size field not found or not a number\n");
				return -39;
			}
			// 检查size字段是否是整数（不是浮点数）
			size_value = cJSON_GetNumberValue(size);
			if (size_value != (int)size_value)
			{
				printf("Error: para.size field is not an integer (got: %f)\n", size_value);
				return -40;
			}
		}

		// 7. 检查random字段
		item = cJSON_GetObjectItem(root, "random");
		if (item == NULL)
		{
			printf("Error: random field not found\n");
			return -41;
		}
		// random可以是字符串或数字，这里只检查是否存在

		return 0; // 所有检查通过
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
	 * @brief 检查字符串中是否包含指定的子字符串
	 * @param str 要搜索的字符串
	 * @param substr 要查找的子字符串
	 * @return 1-找到, 0-未找到
	 */
	u8 str_contains(const char *str, const char *substr)
	{
		if (str == NULL || substr == NULL)
		{
			return 0; // 参数无效
		}

		if (strstr(str, substr) != NULL)
		{
			return 1; // 找到子字符串
		}

		return 0; // 未找到
	}

	/**
	 * @brief 检查字符串中是否包含 "WH-LTE-7S1"
	 * @param str 要搜索的字符串
	 * @return 1-包含, 0-不包含
	 */
	u8 str_contains_wh_lte_7s1(const char *str)
	{
		return str_contains(str, "WH-LTE-7S1");
	}
