#include "gprs.h"
#include "usart.h"
#include "uart_queue.h"
#include "property_report.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "crc16.h"
#include "delay.h"
#include "wdg.h"
#include "uart3_at_sequence.h"
#include "net_liveness.h"
#include "dc_motor.h"

u8 rxparagate = 0; // 接收到的闸门开度
u8 remoteflag = 0;
static u32 gprs_clock_period_tick = 0;
#define GPRS_CLOCK_PERIOD_MS (24UL * 60 * 60 * 1000) /* 状态4下周期 CLOCK 间隔（24小时） */
#define GPRS_CLOCK_ATTEMPT_TIMEOUT_MS 15000U		 /* 单次对时等待回执上限（毫秒） */
#define GPRS_CLOCK_MAX_ATTEMPTS 3					 /* 一轮对时最多尝试次数 */
#define GPRS_CLOCK_TX_TIMEOUT_MS 300U				 /* 对时报文在发送队列的驻留时长（发完即出队） */
/*
 * 等首帧心跳期间把节奏收紧：探测间隔、探测超时、单次尝试退避、失败后重起一轮都用 5s。
 * 原因：首帧心跳被"必须 timeSync ok"压住时，miss 阶梯不会启动；
 * 若模块会话/订阅坏了，60s 节奏会让设备永久卡住，必须几分钟内自己复位模块。
 * 稳态（没在等首帧心跳时）仍用原来的 60s 探测 / 15-30-60s 退避 / 24h 周期。
 */
#define GPRS_CLOCK_WAIT_FAST_MS 5000U /* 等待首帧心跳期间的快速节奏（毫秒） */
#define RTC_TZ_OFFSET_SEC (8UL * 3600UL) /* DS1302 存东八区本地时；平台 serverTime 为 UTC */

/* ① 对时重试退避：第 1/2/3 次尝试分别等 15s / 30s / 60s（原实现固定 15s 连发） */
static const u16 s_clock_wait_ms[GPRS_CLOCK_MAX_ATTEMPTS] = {GPRS_CLOCK_ATTEMPT_TIMEOUT_MS, 30000U, 60000U};
/* ② 通道确认：掉线恢复后先等"任意一帧合法下行"证明通道双向可用再开始新一轮对时；
      等不到就按下面的节奏自己发单帧探测（只发一帧、不重试），避免请求被成批缓存重放 */
#define GPRS_CLOCK_PROBE_INTERVAL_MS 60000U

CJsonEquipment Cjsonequipment;
OTA_InfoCB OTA_Info; // 存储在24C02内的OTA信息回调的结构体
JsonMsgHeader g_json_msg_hdr; /* 最近一次合法 JSON 的信封字段 */
JsonMsgService g_json_msg_service;

static u32 gprs_periodic_last_tick = 0;
static u8 clock_just_entered = 1; /* 离线时置1，进入 NORMAL 时立即发一次 */

/* 对时（sys.timeSync）一轮尝试状态：重发使用新的时间戳 id，回执按 id 匹配 */
static u8 s_clock_pending = 0;					/* 1=有一轮对时在途 */
static u8 s_clock_attempts_left = 0;			/* 当前尝试之后还可重发的次数 */
static u32 s_clock_attempt_tick = 0;			/* 本次尝试发出时刻（sys_ms） */
static char s_clock_pending_id[JSON_HDR_ID_LEN]; /* 当前在途对时的 id */
static u8 s_clock_attempt_idx = 0;			/* 本轮第几次尝试（0 基）：决定本次等待时长（退避表） */
static u8 s_clock_channel_ok = 0;			/* 1=本次链路恢复后已收到过合法下行帧 */
static u8 s_clock_link_lost = 0;			/* 1=经历过掉线（用于区分"上电首次"与"掉线恢复"） */
static u8 s_clock_wait_channel = 0;			/* 1=正在等通道证据，这段时间只发单帧探测 */
static u32 s_clock_probe_tick = 0;			/* 最近一次进入等待/发出探测的时刻（sys_ms） */
static u8 s_clock_is_probe = 0;				/* 1=在途的是"通道探测帧"（不做重试） */
/*
 * 进 NORMAL 后的首轮对时热身：
 * 三条 AT 只是模块的本地命令，不代表模块的 MQTT 会话/订阅已就绪（TCP+CONNECT+5 条 SUBSCRIBE 通常还要 1~3s），
 * 而 AT+CACHEN=OFF 不缓存上行，发早了这一帧就被模块丢掉、等不到回执。
 * 所以首次对时先等这么久再发；只影响"进 NORMAL 后的第一次"，稳态 24h 周期不受影响。
 */
#define GPRS_CLOCK_WARMUP_AFTER_NORMAL_MS 2000U
static u8 s_clock_warmup_pending = 0;
static u32 s_clock_warmup_tick = 0;
/* 等首帧心跳期间：一轮对时彻底失败后，隔 60s 自动重起一轮（"永不放弃"不能退化成等 24h） */
static u8 s_clock_retry_pending = 0;
static u32 s_clock_retry_tick = 0;

/* down/property/get 回执 + 属性补发共用缓冲：静态分配，避免在 Parse_Process（主循环、非可重入）栈上再占 500+ 字节 */
static char s_property_get_para_buf[PROP_SNAPSHOT_JSON_BUF_SIZE];
static char s_property_get_output_buf[PROP_SNAPSHOT_JSON_BUF_SIZE + 4];

/* ---------------------------------------------------------------------------
 * ReSet 服务（物模型语义：重启）的“先回执、后真复位”状态
 *
 * 回执帧是由发送状态机 UartTx_Queue_Process() 发出的，而它跑在 Gprs_Process()
 * 的最后一步，Parse_Process() 在它之前。所以 ReSet 分支里回执入队后不能阻塞延时，
 * 也不能立刻 NVIC_SystemReset()——那样回执还压在发送队列里没发出去，平台只会记一条
 * “控制命令执行超时”。这里只登记时刻，由 Gprs_Process() 末尾统一轮询，等回执真的
 * 出了发送队列、并留出 4G 模块 publish 的时间之后再复位。
 *
 * 注意：发送队列是“一发一收”，一条无回调的 JSON 要等 timeout 才出队。普通上报沿用
 * 默认 5s（5s 内队列会被占住，后续报文排队），ReSet 回执不需要等任何应答，用短超时，
 * 这样“队列已清空”就等价于“回执已发出”。
 * --------------------------------------------------------------------------- */
#define GPRS_RESET_REPLY_TIMEOUT_MS 300U  /* ReSet 回执的队列驻留时间：发完即出队 */
#define GPRS_RESET_MIN_DELAY_MS 1000U	  /* 回执入队后最早复位时刻：留给 4G publish */
#define GPRS_RESET_MAX_WAIT_MS 6000U	  /* 兜底：队列异常时也必须复位 */

static void ReplyServices_Push(int code, const char *message, const char *output, u16 timeout_ms);

static u8 s_reset_pending = 0;
static u32 s_reset_req_tick = 0;

static void Config_Reply(int code, const char *message, const char *output)
{
	snprintf(g_json_msg_service.serviceCode, sizeof(g_json_msg_service.serviceCode), "%s", "config.push");
	ReplyServices_Push(code, message, output, 0);
}

static u8 Config_WriteWordWithVerify(u16 addr, u16 value)
{
	u16 old_value;
	u16 readback;

	old_value = (u16)AT24CXX_ReadLenByte(addr, 2);
	AT24CXX_WriteWord(addr, value);
	readback = (u16)AT24CXX_ReadLenByte(addr, 2);
	if (readback == value)
		return 1;

	AT24CXX_WriteWord(addr, old_value);
	return 0;
}

static u8 OTAupdateFlagPersist(u8 flag)
{
	OTA_InfoCB old_info = OTA_Info;
	OTA_InfoCB readback;
	u8 normalized = flag ? 1u : 0u;

	OTA_Info.UPDATE_flag = normalized;
	OTA_Enable = normalized;
	AT24CXX_Write(OTA_BYTE_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
	AT24CXX_Read(OTA_BYTE_ADDR, (uint8_t *)&readback, OTA_INFOCB_SIZE);
	if (readback.UPDATE_flag == normalized)
		return 1;

	OTA_Info = old_info;
	OTA_Enable = old_info.UPDATE_flag == 1u ? 1u : 0u;
	AT24CXX_Write(OTA_BYTE_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
	return 0;
}

static u8 Config_ParseU16(cJSON *item, u16 *out)
{
	double value;
	u16 parsed;

	if (!cJSON_IsNumber(item) || out == NULL)
		return 0;

	value = item->valuedouble;
	if (value < 0.0 || value > 65535.0)
		return 0;

	parsed = (u16)value;
	if ((double)parsed != value)
		return 0;

	*out = parsed;
	return 1;
}

static void Gprs_HandleDeviceConfig(cJSON *root)
{
	cJSON *body;
	cJSON *config_code;
	cJSON *config_version;
	cJSON *params;
	cJSON *param;
	char output[128];
	u16 value;
	u8 ok;

	body = cJSON_GetObjectItem(root, "body");
	config_code = (body != NULL) ? cJSON_GetObjectItem(body, "configCode") : NULL;
	config_version = (body != NULL) ? cJSON_GetObjectItem(body, "configVersion") : NULL;
	params = (body != NULL) ? cJSON_GetObjectItem(body, "params") : NULL;

	if (!cJSON_IsString(config_code) || config_code->valuestring == NULL ||
		(config_version != NULL && !cJSON_IsString(config_version)))
	{
		Config_Reply(4002, "invalid config payload", "{\"applied\":false}");
		return;
	}

	if (strcmp(config_code->valuestring, "MotorMaxCurrConfig") != 0 &&
		strcmp(config_code->valuestring, "OTAEnableFlagConfig") != 0 &&
		strcmp(config_code->valuestring, "GateMaxHeightConfig") != 0)
	{
		Config_Reply(4001, "unsupported configCode", "{\"applied\":false}");
		return;
	}

	if (params == NULL || !cJSON_IsObject(params) || params->child == NULL ||
		params->child->next != NULL || params->child->string == NULL)
	{
		Config_Reply(4002, "config params invalid", "{\"applied\":false}");
		return;
	}

	param = params->child;
	if (strcmp(config_code->valuestring, "MotorMaxCurrConfig") == 0)
	{
		if (strcmp(param->string, "MotorMaxCurr") != 0 || !Config_ParseU16(param, &value))
		{
			Config_Reply(4002, "MotorMaxCurr invalid", "{\"applied\":false}");
			return;
		}
		ok = Config_WriteWordWithVerify(0x0004, value);
		if (ok)
			armReg.motor_warning_curr_upper = value;
	}
	else if (strcmp(config_code->valuestring, "GateMaxHeightConfig") == 0)
	{
		if (strcmp(param->string, "GateMaxHeight") != 0 || !Config_ParseU16(param, &value))
		{
			Config_Reply(4002, "GateMaxHeight invalid", "{\"applied\":false}");
			return;
		}
		ok = Config_WriteWordWithVerify(0x0006, value);
		if (ok)
			armReg.gate_maxheight = value;
	}
	else
	{
		if (strcmp(param->string, "OTAEnableFlag") != 0 || !cJSON_IsBool(param))
		{
			Config_Reply(4002, "OTAEnableFlag invalid", "{\"applied\":false}");
			return;
		}
		ok = OTAupdateFlagPersist(cJSON_IsTrue(param) ? 1u : 0u);
	}

	if (!ok)
	{
		Config_Reply(4003, "eeprom write failed", "{\"applied\":false}");
		return;
	}

	snprintf(output, sizeof(output),
			 "{\"applied\":true,\"configCode\":\"%s\"}",
			 config_code->valuestring);
	printf("config applied: %s\r\n", config_code->valuestring);
	Config_Reply(0, "config applied", output);
}

static void Gprs_ReplyDeviceConfig(void)
{
	char output[256];
	const char *ota_enabled = OTA_Info.UPDATE_flag == 1u ? "true" : "false";

	snprintf(output, sizeof(output),
			 "{"
			 "\"MotorMaxCurrConfig\":{\"MotorMaxCurr\":%u},"
			 "\"OTAEnableFlagConfig\":{\"OTAEnableFlag\":%s},"
			 "\"GateMaxHeightConfig\":{\"GateMaxHeight\":%u}"
			 "}",
			 (unsigned)armReg.motor_warning_curr_upper,
			 ota_enabled,
			 (unsigned)armReg.gate_maxheight);

	printf("get device config: %s\r\n", output);
	ReplyServices_Push(0, "success", output, 0);
}

static void Gprs_Reset_Request(void)
{
	s_reset_req_tick = sys_ms;
	s_reset_pending = 1;
}

static void Gprs_Reset_Process(void)
{
	u32 elapsed;

	if (!s_reset_pending)
		return;

	/* sys_ms 由 TIM2 每 5ms 递增；用无符号差值判断，跨回绕安全 */
	elapsed = sys_ms - s_reset_req_tick;

	/* 回执已出队（发送队列为空）且留够静默时间才复位；队列卡住时按上限兜底 */
	if (!(elapsed >= GPRS_RESET_MIN_DELAY_MS &&
		  (UartTx_IsEmpty() || elapsed >= GPRS_RESET_MAX_WAIT_MS)))
		return;

	printf("ReSet: reply sent, system reset now\r\n");
	IWDG_Feed();
	NVIC_SystemReset();
}

static void Gprs_PeriodicPush_Process(void)
{
	if (!gprs_is_ready())
	{
		/* 离线：置位"下次进 NORMAL 要发首轮对时"，并清掉可能在途的热身 */
		clock_just_entered = 1;
		s_clock_warmup_pending = 0;
		return;
	}

	/* 进 NORMAL 后的首轮对时：先热身 GPRS_CLOCK_WARMUP_AFTER_NORMAL_MS，再决定发正规轮还是进"等通道" */
	if (s_clock_warmup_pending)
	{
		if ((u32)(sys_ms - s_clock_warmup_tick) < GPRS_CLOCK_WARMUP_AFTER_NORMAL_MS)
			return;
		s_clock_warmup_pending = 0;
		MY4G_printf("[GPRS] timeSync warmup %us done\n",
					(unsigned)(GPRS_CLOCK_WARMUP_AFTER_NORMAL_MS / 1000U));
		if (!s_clock_pending)
		{
			if (s_clock_link_lost && !s_clock_channel_ok)
			{
				s_clock_wait_channel = 1;
				s_clock_probe_tick = sys_ms;
				MY4G_printf("[GPRS] timeSync deferred: waiting first downlink after link recovery\n");
			}
			else
			{
				Send_cjson_clock();
			}
		}
		return;
	}

	/* 之后每隔 GPRS_CLOCK_PERIOD_MS 发送一次（逻辑同 gprs_AT 中 just_entered） */
	if (clock_just_entered || (sys_ms - gprs_clock_period_tick) >= GPRS_CLOCK_PERIOD_MS)
	{
		u8 first_after_normal = clock_just_entered;

		clock_just_entered = 0;
		gprs_clock_period_tick = sys_ms;

		/* 首次（刚进 NORMAL）：不立刻发，先热身，等模块把 MQTT 会话/订阅建好 */
		if (first_after_normal)
		{
			s_clock_warmup_pending = 1;
			s_clock_warmup_tick = sys_ms;
			MY4G_printf("[GPRS] timeSync warmup %us before first round\n",
						(unsigned)(GPRS_CLOCK_WARMUP_AFTER_NORMAL_MS / 1000U));
			return;
		}

		/* 已有在途对时则视为本轮已发起，只刷新周期起点，避免叠加新请求 */
		if (!s_clock_pending)
		{
			/* ② 掉线恢复后先确认"通道真的通"再起新一轮对时：等到任意一帧下行才算通；
			   等不到就由 Gprs_ClockSync_Process() 按探测节奏单发一帧（不重试）。
			   这样服务器宕机期间不会持续把请求灌进模组缓存，恢复后也就不会被成批重放。 */
			if (s_clock_link_lost && !s_clock_channel_ok)
			{
				s_clock_wait_channel = 1;
				s_clock_probe_tick = sys_ms;
				MY4G_printf("[GPRS] timeSync deferred: waiting first downlink after link recovery\n");
			}
			else
			{
				Send_cjson_clock();
			}
		}
	}
}

//-------------GPRS数据处理------------------
void Gprs_Process(void)
{
	//gprs_check(); /* 检查在线 LED */

	/* 1. 接收：DMA 数据追加、帧提取入队 */
	gprs_rx_datas();

	/* 2. 用户程序：从统一接收队列取帧，按内容分发 */
	Parse_Process();

	/* 3. GPRS 状态机：离线→AT 初始化→正常工作 */
	gprs_state_machine();

	/* 4. 定时 JSON 推送（gateproperty） */
	Gprs_PeriodicPush_Process();

	/* 4.1 探活心跳：每 120s 发一帧极简心跳并校验回执，失联时分级自愈 */
	NetLiveness_Process();

	/* 5. 发送状态机驱动 */
	UartTx_Queue_Process(sys_ms);

	/* 5.1 对时重发：单次尝试超时后用新的 id 重新发起，避免原样重发同一请求 */
	Gprs_ClockSync_Process();

	/* 6. ReSet 待复位处理：回执已由发送状态机送出，延时到点后真复位 */
	Gprs_Reset_Process();

	/* 7. 接收丢包可观测：只在计数变化时打印一次，便于现场判断是否真丢过下行 */
	{
		static u32 last_drop = 0;
		static u16 last_overwrite = 0;

		if (g_uartRx_dropped_bytes != last_drop || g_uart2_rx_overwrite != last_overwrite)
		{
			last_drop = g_uartRx_dropped_bytes;
			last_overwrite = g_uart2_rx_overwrite;
			MY4G_printf("[GPRS] rx drop=%u overwrite=%u\n",
						(unsigned)last_drop, (unsigned)last_overwrite);
		}
	}
}

//------------GPRS接收数据处理-----------------------------------//
void gprs_rx_datas(void)
{
	/*
	 * 帧在中断里已被搬进 USART2 的乒乓暂存槽（见 usart2.c），这里只做消费：
	 * 搬进环形缓冲/帧队列，并把"是否见过模块启动 banner"映射到 atflag_online。
	 * 这样即使主循环被长 EEPROM 写占住，下行帧也不会被 DMA 覆盖丢失。
	 */
	UartRx_PumpFromDma(1); /* verbose=1：逐帧打印模块原文（banner / AT 回应 / 下行） */

	if (g_uart2_banner_seen)
	{
		g_uart2_banner_seen = 0;
		atflag_online = 1;
		MY4G_printf("[GPRS] module banner seen (WH-LTE-7S1) -> atflag_online=1\n");
	}

	/* 正常路径已消费完 DMA 数据，清掉给 AT 序列用的旧标志，避免它读到过期内容 */
	if (Usart2_Rec_Cnt != 0 || UART2_RX_Complete != 0)
	{
		Usart2_Rec_Cnt = 0;
		UART2_RX_Complete = 0;
	}
}

//------------解析分发 Parse_Process-------------------//
void Parse_Process(void)
{
	char frame[UART_RX_ITEM_MAX];
	u16 len;
	char *pos;
	u8 order_type = 0;

	char json_buf[UART_TX_JSON_LEN];
	char *json_start;
	u16 json_len;

	cJSON *root;
	const char *validate_err;
	cJSON *type;
	cJSON *body;
	cJSON *serviceCode;
	cJSON *params;
	cJSON *updateValue;
	cJSON *setVal;

	while (!UartRx_IsEmpty())
	{
		/*
		 * 1. 从接收队列取出一帧数据
		 * 2. 判断内容：如果包含 JSON 则按 JSON 处理，否则按 AT 回执处理
		 *    - JSON 处理：提取 JSON 字符串，判断是那种类型json，命令还是回执
		 *    -命令调用 handleMessage
		 * 	  -回执调用 UartTx_OnRecv
		 *    - AT 回执处理：调用 UartTx_OnRecv 处理当前等待的 AT 指令


		*/
		if (UartRx_Pop(frame, sizeof(frame), &len))
		{
			// 过滤掉混合情况：优先提取 JSON 处理
			extract_json_from_string(frame, len, &json_start, &json_len);
			if (json_len != 0) // json
			{
				memcpy(json_buf, json_start, json_len);
				json_buf[json_len] = '\0';

				// handleMessage(out_buf, json_len);
				root = cJSON_Parse(json_buf);
				/* 紧凑心跳回执：平台对极简心跳只回一帧 `{}`，必须先于严格信封校验识别 */
				if (NetLiveness_IsAck(root))
				{
					NetLiveness_OnAck();
					/* 回执同样是「双向通道可用」的证据。
					   它不带 id，先清掉上一帧残留的 id，避免对时逻辑把旧 id 误判成在途请求的回执。 */
					g_json_msg_hdr.id[0] = '\0';
					Gprs_ClockSync_OnDownlinkOk();
					cJSON_Delete(root);
					continue;
				}
				/* 验证json是否合法，返回 NULL 表示通过，否则为错误原因 */
				validate_err = validate_server_reply_json(root);
				if (validate_err != NULL)
				{
					printf("s->c JSON validation failed: %s\n", validate_err);
					cJSON_Delete(root);
					return;
				}

				/* 保存信封公共字段，供后续回执等逻辑使用 */
				{
					cJSON *item;
					memset(&g_json_msg_hdr, 0, sizeof(g_json_msg_hdr));
					item = cJSON_GetObjectItem(root, "id");
					if (item && cJSON_IsString(item) && item->valuestring)
						strncpy(g_json_msg_hdr.id, item->valuestring, sizeof(g_json_msg_hdr.id) - 1);
					// item = cJSON_GetObjectItem(root, "ver");
					// if (item && cJSON_IsString(item) && item->valuestring)
					// 	strncpy(g_json_msg_hdr.ver, item->valuestring, sizeof(g_json_msg_hdr.ver) - 1);
					// item = cJSON_GetObjectItem(root, "productKey");
					// if (item && cJSON_IsString(item) && item->valuestring)
					// 	strncpy(g_json_msg_hdr.productKey, item->valuestring, sizeof(g_json_msg_hdr.productKey) - 1);
					// item = cJSON_GetObjectItem(root, "deviceKey");
					// if (item && cJSON_IsString(item) && item->valuestring)
					// 	strncpy(g_json_msg_hdr.deviceKey, item->valuestring, sizeof(g_json_msg_hdr.deviceKey) - 1);
					item = cJSON_GetObjectItem(root, "type");
					if (item && cJSON_IsString(item) && item->valuestring)
						strncpy(g_json_msg_hdr.type, item->valuestring, sizeof(g_json_msg_hdr.type) - 1);
				}

				/* 收到一帧合法下行 = 通道双向可用的证据（② 对时窗口起点）；
				   放在信封字段之后，好让对时逻辑判断"这帧是不是当前在途请求的回执" */
				Gprs_ClockSync_OnDownlinkOk();

				// 解析数据
				type = cJSON_GetObjectItem(root, "type");

				if (strcmp(type->valuestring, "service") == 0) // 接收到服务器发来的（命令\服务）
				{
					printf("s->c JSON service\n");
					/* 保存服务 json 的字段 JsonMsgService */
					{
						cJSON *items;
						memset(&g_json_msg_service, 0, sizeof(g_json_msg_service));
						body = cJSON_GetObjectItem(root, "body");

						items = (body != NULL) ? cJSON_GetObjectItem(body, "commandNo") : NULL;
						if (items && cJSON_IsString(items) && items->valuestring)
							strncpy(g_json_msg_service.commandNo, items->valuestring, sizeof(g_json_msg_service.commandNo) - 1);
						items = (body != NULL) ? cJSON_GetObjectItem(body, "serviceCode") : NULL;
						if (items && cJSON_IsString(items) && items->valuestring)
							strncpy(g_json_msg_service.serviceCode, items->valuestring, sizeof(g_json_msg_service.serviceCode) - 1);
					}

					//body = cJSON_GetObjectItem(root, "body");
//--------setValue-------------------------------
					serviceCode = (body != NULL) ? cJSON_GetObjectItem(body, "serviceCode") : NULL;
					if (serviceCode != NULL && cJSON_IsString(serviceCode) &&
						serviceCode->valuestring != NULL &&
						strcmp(serviceCode->valuestring, "setValue") == 0)
					{
						params = cJSON_GetObjectItem(body, "params");
						setVal = (params != NULL) ? cJSON_GetObjectItem(params, "Value") : NULL;
						if (cJSON_IsNumber(setVal))
						{
							int value = (int)cJSON_GetNumberValue(setVal);
							printf("s->c JSON service: setValue %d\n", value);
							SettingValue(value);
						}
					}
//--------stopGate-------------------------------
					serviceCode = (body != NULL) ? cJSON_GetObjectItem(body, "serviceCode") : NULL;
					if (serviceCode != NULL && cJSON_IsString(serviceCode) &&
						serviceCode->valuestring != NULL &&
						strcmp(serviceCode->valuestring, "stopGate") == 0)
					{
						printf("s->c JSON service: stopGate\n");
						dcmotor_request_stop(MOTOR_STOP_REASON_EMERGENCY_STOP);
						ReplyServices_Push(0, "success", "{\"success\": true}", GPRS_RESET_REPLY_TIMEOUT_MS);
					}
//--------getFirmware-------------------------------
					serviceCode = (body != NULL) ? cJSON_GetObjectItem(body, "serviceCode") : NULL;
					if (serviceCode != NULL && cJSON_IsString(serviceCode) &&
						serviceCode->valuestring != NULL &&
						strcmp(serviceCode->valuestring, "getFirmware") == 0)
					{
						printf("s->c JSON service: getFirmware\n");
						FirmwareVer();
					}
//--------reSet-------------------------------
					serviceCode = (body != NULL) ? cJSON_GetObjectItem(body, "serviceCode") : NULL;
					if (serviceCode != NULL && cJSON_IsString(serviceCode) &&
						serviceCode->valuestring != NULL &&
						strcmp(serviceCode->valuestring, "ReSet") == 0)
					{
						printf("s->c JSON service: reSet\n");
						/* 1) 先回执：模型 ReSet 输出 ResetFlag=true；平台按 body.code==0 判成功。
						 *    用短超时入队，发完即出队，便于判断“回执已发出”。 */
						ReplyServices_Push(0, "success", "{\"ResetFlag\": true}", GPRS_RESET_REPLY_TIMEOUT_MS);
						/* 2) 只登记待复位，不在本函数里阻塞（见 Gprs_Reset_Request 注释） */
						Gprs_Reset_Request();
					}
//--------setUpdateValue----------------------------
					if (serviceCode != NULL && cJSON_IsString(serviceCode) &&
						serviceCode->valuestring != NULL &&
						strcmp(serviceCode->valuestring, "setUpdateValue") == 0)
					{
						params = cJSON_GetObjectItem(body, "params");
						updateValue = (params != NULL) ? cJSON_GetObjectItem(params, "updateValue") : NULL;
						if (cJSON_IsBool(updateValue))
						{
							if (cJSON_IsTrue(updateValue))
							{
								printf("s->c JSON service: setUpdateValue true\n");
								OTAupdateFlag(1);
							}
							else
							{
								printf("s->c JSON service: setUpdateValue false\n");
								OTAupdateFlag(0);
							}
						}
					}
//--------getConfig----------------------------
					if (serviceCode != NULL && cJSON_IsString(serviceCode) &&
						serviceCode->valuestring != NULL &&
						strcmp(serviceCode->valuestring, "getConfig") == 0)
					{
						printf("s->c JSON service: getConfig\n");
						Gprs_ReplyDeviceConfig();
					}
				}
				else if (strcmp(type->valuestring, "config") == 0) // 接收到服务器发送的（配置）
				{
					printf("s->c JSON config\n");
					Gprs_HandleDeviceConfig(root);
				}
				else if (strcmp(type->valuestring, "request_reply") == 0) // 对时等 request 回执
				{
					printf("s->c JSON request_reply\n");
					Gprs_ClockSync_OnReply(json_buf, json_len);
				}
				/* 服务器主动读取属性：必须回执，否则平台按“控制命令执行超时”处理 */
				else if (strcmp(type->valuestring, "property_get") == 0)
				{
					body = cJSON_GetObjectItem(root, "body"); /* 形如 {"properties":["Opening"]}；本版固定回全量快照，不按列表过滤 */
					printf("s->c JSON property_get\n");

					if (PropertyReport_BuildSnapshot(s_property_get_para_buf, sizeof(s_property_get_para_buf)))
					{
						/* replyServices 取 g_json_msg_service.serviceCode，这里显式置为协议约定的 property.get */
						snprintf(g_json_msg_service.serviceCode, sizeof(g_json_msg_service.serviceCode), "%s", "property.get");
						snprintf(s_property_get_output_buf, sizeof(s_property_get_output_buf), "{%s}", s_property_get_para_buf);
						/* 1) 回执：up/service/reply，信封 id 原样带回，平台按 id 匹配命令 */
						replyServices(0, "success", s_property_get_output_buf);
						/* 2) 补发：up/property/post；平台不把回执 output 落点位，属性值/时序只能靠这条上报刷新 */
						Send_property(s_property_get_para_buf);
					}
					else
					{
						/* 极端异常也不让平台空等超时 */
						replyServices(500, "property read failed", "{}");
					}
				}
				cJSON_Delete(root); // 释放空间
			}
			else // at
			{
				// 发送队列有等待的指令，从统一接收队列中依次取出每一帧
				UartTx_OnRecv(frame, len, 1);
			}
		}
	}
}
//---------------------------------------------------------------
void OTAupdateFlag(u8 flag)
{
	if (!OTAupdateFlagPersist(flag))
	{
		replyServices(500, "eeprom write failed", "{\"updateFlag\": false}");
		return;
	}

	printf("set OTA_Info.UPDATE_flag=%u \r\n", (unsigned)(flag ? 1u : 0u));
	replyServices(0, "success", flag ? "{\"updateFlag\": true}" : "{\"updateFlag\": false}");
}
//-----------OTA_Info.OTA_ver-------------------------------------------------------------
void FirmwareVer(void)
{
	char out_buf[96];

	printf("get firmwareVer is %s\r\n", OTA_Info.OTA_ver);
	snprintf(out_buf, sizeof(out_buf), "{\"FirmwareVer\":\"%s\"}", (const char *)OTA_Info.OTA_ver);
	replyServices(0, "success", out_buf);
}
//-------------------------------------------------------------------------------------------------
void SettingValue(int value)
{
	rxparagate = (u8)value;

	if (rxparagate != armReg.gate_kaidu)
	{
		armReg.range_target = rxparagate;

		if ((armReg.range_target < armReg.gate_kaidu) && FLAG_CLOSEALL != 1 && autoFlag == 0)
		{
			sys_moter_state = MOTOR_DOWN;
			remoteflag = 1;
		}
		else if ((armReg.range_target > armReg.gate_kaidu) && FLAG_OPENALL != 1 && autoFlag == 0)
		{
			sys_moter_state = MOTOR_UP;
			remoteflag = 1;
		}
	}

	replyServices(0, "success", "{}");
}

/**
 * @brief 根据 g_json_msg_hdr / g_json_msg_service 组装 service_reply 并发送
 * @param code    body.code，0 表示成功
 * @param message body.message，可为 NULL（默认 "success"）
 * @param output  body.output 的 JSON 对象字符串，可为 NULL（默认 "{}"）
 */
/* DS1302 日历 → Unix 整秒（UTC，设备对时后） */
uint32_t rtc_get_unix_sec(void)
{
	static const u8 days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	char time[7];
	int year, month, day, hour, min, sec;
	uint32_t days = 0;
	int y, m;
	u8 leap;

	ds1302_dataread(time);
	year = 2000 + (u8)time[6];
	month = (u8)time[4];
	day = (u8)time[3];
	hour = (u8)time[2];
	min = (u8)time[1];
	sec = (u8)time[0];

	if (year < 1970 || month < 1 || month > 12 || day < 1)
		return 0;

	for (y = 1970; y < year; y++)
	{
		leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
		days += (uint32_t)(365 + leap);
	}
	for (m = 1; m < month; m++)
	{
		days += days_in_month[m - 1];
		if (m == 2)
		{
			leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
			days += leap;
		}
	}
	days += (uint32_t)(day - 1);

	/* DS1302 为本地时，换算为 UTC Unix 秒 */
	{
		uint32_t local_sec = days * 86400UL
			 + (uint32_t)hour * 3600UL
			 + (uint32_t)min * 60UL
			 + (uint32_t)sec;
		if (local_sec >= RTC_TZ_OFFSET_SEC)
			return local_sec - RTC_TZ_OFFSET_SEC;
		return 0;
	}
}

uint16_t rtc_get_ms_of_sec(void)
{
	return (uint16_t)(sys_ms % 1000);
}

/* Unix 整秒 → 日历时间 */
static void unix_sec_to_calendar(uint32_t unix_sec, int *year, int *month, int *day,
								 int *hour, int *min, int *sec)
{
	static const u8 days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t days, rem;
	int y, m, dim;
	u8 leap;

	days = unix_sec / 86400UL;
	rem = unix_sec % 86400UL;
	*hour = (int)(rem / 3600UL);
	rem %= 3600UL;
	*min = (int)(rem / 60UL);
	*sec = (int)(rem % 60UL);

	y = 1970;
	for (;;)
	{
		leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
		dim = 365 + leap;
		if (days < (uint32_t)dim)
			break;
		days -= (uint32_t)dim;
		y++;
	}
	*year = y;

	for (m = 1; m <= 12; m++)
	{
		dim = days_in_month[m - 1];
		if (m == 2)
		{
			leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
			dim += leap;
		}
		if (days < (uint32_t)dim)
			break;
		days -= (uint32_t)dim;
	}
	*month = m;
	*day = (int)days + 1;
}

/* Unix 整秒 + 秒内毫秒 → 13 位毫秒时间戳 */
int64_t unix_ms_now(uint32_t unix_sec, uint16_t ms_of_sec)
{
	return (int64_t)unix_sec * 1000LL + (int64_t)(ms_of_sec % 1000);
}

/* 生成 13 位 Unix 毫秒时间戳字符串，作 JSON 信封 id（buf ≥ 21） */
// void generate_unix_ms_id(char *buf, size_t buflen)
// {
// 	int64_t ms;

// 	if (buf == NULL || buflen == 0)
// 		return;
// 	ms = unix_ms_now(rtc_get_unix_sec(), rtc_get_ms_of_sec());
// 	snprintf(buf, buflen, "%lld", (long long)ms);
// }

/* 组装并发送 service_reply；timeout_ms=0 时由 UartTx_Push_JSON 取默认值 */
static void ReplyServices_Push(int code, const char *message, const char *output, u16 timeout_ms)
{
	char json_str[UART_TX_JSON_LEN];
	const char *msg;
	const char *out;
	const char *reply_id;
	int64_t ts;


	/* 回执 id 原样带回平台下发值，不要重新生成 */
	reply_id = g_json_msg_hdr.id[0] ? g_json_msg_hdr.id : "";

	msg = (message != NULL && message[0] != '\0') ? message : "success";
	out = (output != NULL && output[0] != '\0') ? output : "{}";

	snprintf(json_str, sizeof(json_str),
			 "%d,{"
			 "\"id\":\"%s\","
			 "\"productKey\":\"%s\","
			 "\"deviceKey\":\"%s\","
			 "\"type\":\"service_reply\","
			 "\"body\":{"
			 "\"serviceCode\":\"%s\","
			 "\"code\":%d,"
			 "\"message\":\"%s\","
			 "\"output\":%s"
			 "}"
			 "}",
			 Topic_ServiceReply,
			 reply_id,
			 g_mqtt_productKey,
			 g_mqtt_deviceKey,
			 g_json_msg_service.serviceCode,
			 code,
			 msg,
			 out);

	printf("replyServices-->%s\r\n", json_str);
	UartTx_Push_JSON(json_str, (u16)strlen(json_str), reply_id, NULL, timeout_ms, 0);
}

void replyServices(int code, const char *message, const char *output)
{
	ReplyServices_Push(code, message, output, 0);
}
// void replyFirmwareVer(int code, const char *message, const char *output)
// {
// 	char json_str[UART_TX_JSON_LEN];
// 	const char *msg;
// 	const char *out;
// 	const char *reply_id;
// 	int64_t ts;


// 	/* 回执 id 原样带回平台下发值，不要重新生成 */
// 	reply_id = g_json_msg_hdr.id[0] ? g_json_msg_hdr.id : "";

// 	msg = (message != NULL && message[0] != '\0') ? message : "success";
// 	out = (output != NULL && output[0] != '\0') ? output : "{}";

// 	snprintf(json_str, sizeof(json_str),
// 			 "%d,{"
// 			 "\"id\":\"%s\","
// 			 "\"productKey\":\"%s\","
// 			 "\"deviceKey\":\"%s\","
// 			 "\"type\":\"service_reply\","
// 			 "\"body\":{"
// 			 "\"serviceCode\":\"%s\","
// 			 "\"code\":%d,"
// 			 "\"message\":\"%s\","
// 			 "\"output\":%s"
// 			 "}"
// 			 "}",
// 			 Topic_ServiceReply,
// 			 reply_id,
// 			 g_mqtt_productKey,
// 			 g_mqtt_deviceKey,
// 			 g_json_msg_service.serviceCode,
// 			 code,
// 			 msg,
// 			 out);

// 	printf("replyFirmwareVerVer-->%s\r\n", json_str);
// 	UartTx_Push_JSON(json_str, (u16)strlen(json_str), reply_id, NULL, 0, 0);
// }
// void replyReSet(int code, const char *message, const char *output)
// {
// 	char json_str[UART_TX_JSON_LEN];
// 	const char *msg;
// 	const char *out;
// 	const char *reply_id;


// 	/* 回执 id 原样带回平台下发值，不要重新生成 */
// 	reply_id = g_json_msg_hdr.id[0] ? g_json_msg_hdr.id : "";

// 	msg = (message != NULL && message[0] != '\0') ? message : "success";
// 	out = (output != NULL && output[0] != '\0') ? output : "{}";

// 	snprintf(json_str, sizeof(json_str),
// 			 "%d,{"
// 			 "\"id\":\"%s\","
// 			 "\"productKey\":\"%s\","
// 			 "\"deviceKey\":\"%s\","
// 			 "\"type\":\"service_reply\","
// 			 "\"body\":{"
// 			 "\"serviceCode\":\"%s\","
// 			 "\"code\":%d,"
// 			 "\"message\":\"%s\","
// 			 "\"output\":%s"
// 			 "}"
// 			 "}",
// 			 Topic_ServiceReply,
// 			 reply_id,
// 			 g_mqtt_productKey,
// 			 g_mqtt_deviceKey,
// 			 g_json_msg_service.serviceCode,
// 			 code,
// 			 msg,
// 			 out);

// 	printf("replyFirmwareVerVer-->%s\r\n", json_str);
// 	UartTx_Push_JSON(json_str, (u16)strlen(json_str), reply_id, NULL, 0, 0);
// }
//---------------------------------------------------------------
//------------CJSON 命令 ------------------------------------
// 设备命令处理
u8 cjson_cmd(cJSON *root)
{
	cJSON *back, *CmdName, *random;
	u8 txreply = 0; // 客户端是否需要回执 1需要 2不需要
	u8 txsta = 0;

	back = cJSON_GetObjectItem(root, "back");
	CmdName = cJSON_GetObjectItem(root, "CmdName");
	random = cJSON_GetObjectItem(root, "random");

	// if (random && (random->type == cJSON_String) && (random->valuestring != NULL))
	// {
	// memset(CParas.random, 0, bRandom);
	// strncpy(CParas.random, random->valuestring, strlen(random->valuestring));
	// }
	// else
	// {
	// 	MY4G_printf("Warning: random not found or not a string\n");
	// 	return 1;
	// }

	// if (back && (back->type == cJSON_String) && (back->valuestring != NULL))
	// {
	if (strcmp(back->valuestring, "true") == 0)
		txreply = 1;
	else if (strcmp(back->valuestring, "false") == 0)
		txreply = 0;

	if (strcmp(CmdName->valuestring, "enquiry") == 0) // 刷新状态
	{
		//txsta = reply_cjson_enquiry(root, txreply);
	}
	else if (strcmp(CmdName->valuestring, "setting") == 0) // 设置闸门开度
	{
		txsta = reply_cjson_setting(root, txreply);
	}
	else if (strcmp(CmdName->valuestring, "version") == 0) // 获取版本号
	{
		txsta = reply_cjson_version(root, txreply);
	}
	else if (strcmp(CmdName->valuestring, "update") == 0) // 设置更新使能标志
	{
		txsta = reply_cjson_update(root, txreply);
	}
	else
	{
		MY4G_printf("Warning: CmdName not exist\n");
		return 1;
	}

	return txsta;
}

//------ 客户端 -> 服务器 -------响应回复（主动）------这里一部分，在属性，事件，报警也有---
// 发送对时请求：type=request, requestCode=sys.timeSync
/* 组装并发送一次对时请求（每次尝试使用新的时间戳 id，避免服务器把重发当成同一条请求） */
static void Gprs_ClockSync_IssueAttempt(void)
{
	char json_str[UART_TX_JSON_LEN];
	char id_buf[24];
	int64_t ts;

	ts = unix_ms_now(rtc_get_unix_sec(), rtc_get_ms_of_sec());
	snprintf(id_buf, sizeof(id_buf), "%lld", (long long)ts);

	snprintf(json_str, sizeof(json_str),
			 "%d,{"
			 "\"id\":\"%s\","
			 "\"productKey\":\"%s\","
			 "\"deviceKey\":\"%s\","
			 "\"type\":\"request\","
			 "\"body\":{"
			 "\"requestCode\":\"sys.timeSync\""
			 "}"
			 "}",
			 Topic_RequestInvoke,
			 id_buf,
			 g_mqtt_productKey,
			 g_mqtt_deviceKey);

	/* 记录在途 id：只有与它一致的回执才会被接受 */
	snprintf(s_clock_pending_id, sizeof(s_clock_pending_id), "%s", id_buf);
	s_clock_attempt_tick = sys_ms;
	s_clock_pending = 1;

	printf("--Send_cjson_clock--%s\r\n", json_str);
	/* 回执匹配由 Gprs_ClockSync_OnReply 按 id 完成：此处短驻留、不重发，避免原样重发同一 id */
	UartTx_Push_JSON(json_str, (u16)strlen(json_str), id_buf, NULL, GPRS_CLOCK_TX_TIMEOUT_MS, 0);
}

/* 开启一轮对时：最多尝试 GPRS_CLOCK_MAX_ATTEMPTS 次 */
void Send_cjson_clock(void)
{
	s_clock_is_probe = 0;
	s_clock_attempt_idx = 0;							 /* 每轮从最短等待（15s）开始 */
	s_clock_attempts_left = GPRS_CLOCK_MAX_ATTEMPTS - 1;
	s_clock_wait_channel = 0;							 /* 已开正规轮次：退出"等通道"态 */
	s_clock_retry_pending = 0;							 /* 已重起，清掉待重起标记 */
	Gprs_ClockSync_IssueAttempt();
}

/* 主循环驱动：
   ① 单次尝试超时后用"新的 id + 更长等待"重发（15s→30s→60s），次数用尽则放弃；
   ② 通道未证明（掉线恢复后还没收到过任何下行）时只按固定节奏发单帧探测，不重试、不累积。 */
void Gprs_ClockSync_Process(void)
{
	u16 waited_s;
	u32 wait_ms;
	u32 probe_interval = GPRS_CLOCK_PROBE_INTERVAL_MS;
	u32 retry_interval = GPRS_CLOCK_PROBE_INTERVAL_MS;
	u8 fast = NetLiveness_IsTimeSyncFast();

	/* 等首帧心跳期间用快节奏（5s），尽快拿到 ok 或尽快判定模块坏了；
	   若自愈复位已被 15 分钟节流挡住（fast=0），退回 60s，避免长时间故障下每 10 秒发一帧 */
	if (fast)
	{
		probe_interval = GPRS_CLOCK_WAIT_FAST_MS;
		retry_interval = GPRS_CLOCK_WAIT_FAST_MS;
	}

	/* 等首帧心跳期间：上一轮对时彻底失败后自动重起一轮，直到拿到 ok */
	if (s_clock_retry_pending && !s_clock_pending && !s_clock_wait_channel &&
		(u32)(sys_ms - s_clock_retry_tick) >= retry_interval)
	{
		s_clock_retry_pending = 0;
		MY4G_printf("[GPRS] timeSync restart round (waiting first heartbeat)\n");
		Send_cjson_clock();
		return;
	}

	if (s_clock_wait_channel && !s_clock_pending)
	{
		if ((u32)(sys_ms - s_clock_probe_tick) >= probe_interval)
		{
			s_clock_probe_tick = sys_ms;
			MY4G_printf("[GPRS] timeSync probe (channel not confirmed, single frame, no retry)\n");
			s_clock_is_probe = 1;
			s_clock_attempt_idx = 0;
			s_clock_attempts_left = 0;					 /* 探测帧不做 15/30/60 重试 */
			Gprs_ClockSync_IssueAttempt();
		}
		return;
	}

	if (!s_clock_pending)
		return;

	/* 单次等待时长：等首帧心跳期间压到 5s，否则用 15/30/60s 退避表 */
	wait_ms = s_clock_wait_ms[s_clock_attempt_idx];
	if (fast && wait_ms > GPRS_CLOCK_WAIT_FAST_MS)
		wait_ms = GPRS_CLOCK_WAIT_FAST_MS;
	if ((u32)(sys_ms - s_clock_attempt_tick) < wait_ms)
		return;

	/* 本次尝试超时仍未收到匹配回执 */
	waited_s = (u16)(wait_ms / 1000U);
	s_clock_pending = 0;
	s_clock_pending_id[0] = '\0';

	if (s_clock_is_probe)
	{
		s_clock_is_probe = 0;
		MY4G_printf("[GPRS] timeSync probe got no reply in %us (channel still unconfirmed)\n",
					(unsigned)waited_s);
		s_clock_probe_tick = sys_ms;
		NetLiveness_OnTimeSyncFail();
	}
	else if (s_clock_attempts_left > 0 && (u8)(s_clock_attempt_idx + 1u) < (u8)GPRS_CLOCK_MAX_ATTEMPTS)
	{
		s_clock_attempt_idx++;
		s_clock_attempts_left--;
		MY4G_printf("[GPRS] timeSync no reply in %us, retry with new id (attempt %u/%u, backoff %us)\n",
					(unsigned)waited_s,
					(unsigned)(s_clock_attempt_idx + 1u),
					(unsigned)GPRS_CLOCK_MAX_ATTEMPTS,
					(unsigned)(fast
								   ? (GPRS_CLOCK_WAIT_FAST_MS / 1000U)
								   : (s_clock_wait_ms[s_clock_attempt_idx] / 1000U)));
		NetLiveness_OnTimeSyncFail();
		Gprs_ClockSync_IssueAttempt();
	}
	else
	{
		MY4G_printf("[GPRS] timeSync give up after %u attempts\n", (unsigned)GPRS_CLOCK_MAX_ATTEMPTS);
		if (NetLiveness_IsWaitingTimeSync())
		{
			s_clock_retry_pending = 1;
			s_clock_retry_tick = sys_ms;
			NetLiveness_OnTimeSyncFail();
			MY4G_printf("[GPRS] timeSync give up -> retry round in %us (waiting first heartbeat)\n",
						(unsigned)(retry_interval / 1000U));
		}
	}
}

/* 掉线确认或硬复位：取消在途尝试，避免旧 id 的回执被接受或继续重发；
   同时清掉"通道已通"的证据——下次进入正常态要先等一帧下行再开始新一轮对时 */
void Gprs_ClockSync_Abort(void)
{
	s_clock_pending = 0;
	s_clock_attempts_left = 0;
	s_clock_pending_id[0] = '\0';
	s_clock_is_probe = 0;
	s_clock_channel_ok = 0;
	s_clock_link_lost = 1;
	s_clock_wait_channel = 0;
	s_clock_probe_tick = sys_ms;
}

/* 收到任意一帧合法下行帧时调用（Parse_Process 校验通过处）：
   ② 通道被证明双向可用 → 若正等通道，立刻开始新一轮对时（"成功收发一帧"作为对时窗口起点） */
void Gprs_ClockSync_OnDownlinkOk(void)
{
	s_clock_channel_ok = 1;

	if (s_clock_wait_channel)
	{
		s_clock_wait_channel = 0;

		/* 这帧就是当前在途请求（含探测帧）的回执 → 交给 OnReply 正常处理，不必再补发一帧 */
		if (s_clock_pending && strcmp(g_json_msg_hdr.id, s_clock_pending_id) == 0)
			return;

		if (!s_clock_pending || s_clock_is_probe)
		{
			/* 在途的只是探测帧：取消它，直接开正规轮次（它的回执会按 id 判为过期丢弃） */
			s_clock_pending = 0;
			s_clock_pending_id[0] = '\0';
			s_clock_is_probe = 0;
			MY4G_printf("[GPRS] timeSync channel confirmed by downlink, start round\n");
			Send_cjson_clock();
		}
	}
}

/* request_reply 分发入口：只接受当前在途 id 的回执，迟到/重复回执直接丢弃 */
void Gprs_ClockSync_OnReply(char *json, u16 len)
{
	if (json == NULL || len == 0)
		return;

	if (!s_clock_pending || strcmp(g_json_msg_hdr.id, s_clock_pending_id) != 0)
	{
		printf("[GPRS] timeSync stale reply ignored (id=%s)\n",
			   g_json_msg_hdr.id[0] ? g_json_msg_hdr.id : "?");
		return;
	}

	s_clock_pending = 0;
	s_clock_attempts_left = 0;
	s_clock_pending_id[0] = '\0';
	s_clock_is_probe = 0;
	s_clock_wait_channel = 0;	/* 对时成功 = 通道确实可用，退出"等通道"态 */
	reply_cjson_clock(json, len);
}
//------ 客户端 -> 服务器 -------响应回复（非主动）---------
// 装配状态回执的json字符串
#define PARA_ENQUIRY_JSON_BUF_SIZE 256
void  enquiry_para_to_json(const EnquiryPara *p, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size,
			 "\"MotorCurrent\":%0.2f,"
			 "\"MotorVoltage\":%0.2f,"
			 "\"BatteryCurrent\":%0.2f,"
			 "\"BatteryVoltage\":%0.2f,"
			 "\"Opening\":%u,"
			 "\"MeasuredValue1\":%0.2f,"
			 "\"MeasuredValue2\":%0.2f,"
			 "\"MeasuredValue3\":%0.2f,"
			 "\"MeasuredValue4\":%0.2f",
			 p->MotorCurrent, p->MotorVoltage, p->BatteryCurrent, p->BatteryVoltage,
			 p->Opening,
			 p->MeasuredValue1, p->MeasuredValue2, p->MeasuredValue3, p->MeasuredValue4);
}
// 装配state的json字符串
#define PARA_STATE_JSON_BUF_SIZE 128
void state_para_to_json(const StatePara *p, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size,
			 "{"
			 "\"state\":\"%s\""
			 "}",
			 p->state);
}
// 装配version的json字符串
#define PARA_VERSION_JSON_BUF_SIZE 128
void version_para_to_json(const VersionPara *p, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size,
			 "{"
			 "\"version\":\"%s\""
			 "}",
			 p->version);
}
// 总装json函数
void generateJsonString(char *buf, size_t buf_size, const char *type, const char *back, const char *clientid, const char *CmdName, const char *random, const char *para)
{
	snprintf(buf, buf_size,
			 "%d,{"
			 "\"type\":\"%s\","
			 "\"back\":\"%s\","
			 "\"clientid\":\"%s\","
			 "\"CmdName\":\"%s\","
			 "\"para\":%s,"
			 "\"random\":\"%s\""
			 "}",
			 Topic_Pub, type, back, clientid, CmdName, para, random);
}

u8 reply_cjson_update(cJSON *root, u8 reply)
{
	cJSON *random_obj, *para_obj, *enable_obj;
	// const char *type = "back";
	// const char *back = "false";
	// const char *CmdName = "setting";
	char json_str[512] = {'\0'};
	const char *random;

	StatePara para_state;
	char para_buf[PARA_STATE_JSON_BUF_SIZE] = {'\0'};

	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	/* enable 在 para 内：{"para":{"enable":true}} */
	para_obj = cJSON_GetObjectItem(root, "para");
	enable_obj = (para_obj != NULL) ? cJSON_GetObjectItem(para_obj, "enable") : NULL;
	if (cJSON_IsBool(enable_obj))
	{
		if (cJSON_IsTrue(enable_obj))
		{
			OTA_Info.UPDATE_flag = 1; // 0 无需ota ，1 需要联网ota
			OTA_Enable=1;
			printf("set OTA_Info.UPDATE_flag=1 \r\n");
			AT24CXX_Write(OTA_BYTE_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
		}
		else
		{
			OTA_Info.UPDATE_flag = 0; // 0 无需ota ，1 需要联网ota
			OTA_Enable=0;
			printf("set OTA_Info.UPDATE_flag=0 \r\n");
			AT24CXX_Write(OTA_BYTE_ADDR, (uint8_t *)&OTA_Info, OTA_INFOCB_SIZE);
		}
	}

	if (reply)
	{
		sprintf(para_state.state, "ok");
		state_para_to_json(&para_state, para_buf, sizeof(para_buf));
		generateJsonString(json_str, sizeof(json_str), "back", "false", Cjsonequipment.clientid, "update", random, para_buf);

		printf("reply_cjson_update-->%s\r\n", json_str);
		UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);
	}
	return 0;
}

// 回执 - 对时：解析 body.output.serverTime（毫秒）写入 DS1302，精度到秒
// 格式：{"id":"...","productKey":"...","deviceKey":"...","type":"request_reply",
//        "body":{"requestCode":"sys.timeSync","code":0,"message":"ok","output":{"serverTime":1785243312510}}}
void reply_cjson_clock(char *data, u16 len)
{
	cJSON *root, *body, *output, *serverTime_obj, *code_obj;
	double server_time_ms;
	uint32_t unix_sec;
	int year, month, day, hour, min, sec;
	u8 time_bcd[7];

	if (data == NULL || len == 0)
		return;

	root = cJSON_Parse(data);
	if (root == NULL)
		return;

	body = cJSON_GetObjectItem(root, "body");
	if (body == NULL)
	{
		cJSON_Delete(root);
		return;
	}

	code_obj = cJSON_GetObjectItem(body, "code");
	if (code_obj != NULL && cJSON_IsNumber(code_obj) && (int)code_obj->valuedouble != 0)
	{
		printf("timeSync failed, code=%d\r\n", (int)code_obj->valuedouble);
		cJSON_Delete(root);
		return;
	}

	output = cJSON_GetObjectItem(body, "output");
	serverTime_obj = (output != NULL) ? cJSON_GetObjectItem(output, "serverTime") : NULL;
	if (serverTime_obj == NULL || !cJSON_IsNumber(serverTime_obj))
	{
		cJSON_Delete(root);
		return;
	}

	/* 毫秒时间戳，只保留到秒；平台为 UTC，写入 DS1302 前转为东八区本地时 */
	server_time_ms = cJSON_GetNumberValue(serverTime_obj);
	unix_sec = (uint32_t)(server_time_ms / 1000.0);
	unix_sec += RTC_TZ_OFFSET_SEC;
	cJSON_Delete(root);

	unix_sec_to_calendar(unix_sec, &year, &month, &day, &hour, &min, &sec);

	if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
		hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
		return;

	/* 十进制转 BCD：DS1302 顺序为 秒/分/时/日/月/星期/年 */
	time_bcd[0] = (u8)(((sec / 10) << 4) | (sec % 10));
	time_bcd[1] = (u8)(((min / 10) << 4) | (min % 10));
	time_bcd[2] = (u8)(((hour / 10) << 4) | (hour % 10));
	time_bcd[3] = (u8)(((day / 10) << 4) | (day % 10));
	time_bcd[4] = (u8)(((month / 10) << 4) | (month % 10));
	time_bcd[5] = (u8)(simple_weekday(year, month, day));
	time_bcd[6] = (u8)((((year % 100) / 10) << 4) | ((year % 100) % 10));

	ds1302_settime(time_bcd);
	printf("timeSync ok: %04d-%02d-%02d %02d:%02d:%02d\r\n",
		   year, month, day, hour, min, sec);
	/* 校时成功：解锁首帧心跳（"3 条 AT → 校时 → 心跳"顺序的最后一环） */
	NetLiveness_OnTimeSyncOk();
}
// 回执 - 查询设备版本号
u8 reply_cjson_version(cJSON *root, u8 reply)
{
	cJSON *random_obj;
	// const char *type = "back";
	// const char *back = "false";
	// const char *CmdName = "setting";
	char json_str[512] = {'\0'};
	const char *random;

	VersionPara para_version;
	char para_buf[PARA_VERSION_JSON_BUF_SIZE] = {'\0'};

	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	if (reply)
	{
		snprintf(para_version.version, sizeof(para_version.version), "%s", (const char *)OTA_Info.OTA_ver);
		version_para_to_json(&para_version, para_buf, sizeof(para_buf));
		generateJsonString(json_str, sizeof(json_str), "back", "false", Cjsonequipment.clientid, "version", random, para_buf);

		UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);
	}
	return 0;
}
// 回执 - 查询状态
// u8 reply_cjson_enquiry(cJSON *root, u8 reply)
// {
// 	EnquiryPara para;
// 	char para_buf[PARA_ENQUIRY_JSON_BUF_SIZE] = {'\0'};
// 	char json_str[512] = {'\0'};

// 	cJSON *random_obj;
// 	const char *random;
// 	random_obj = cJSON_GetObjectItem(root, "random");
// 	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

// 	/* 按预定结构体赋值 */
// 	para.MotorCurrent = g_adc_val[0] / 100.0f;
// 	para.MotorVoltage = g_adc_val[2] / 10.0f + 0.3f;
// 	para.BatteryCurrent = 15.52f;
// 	para.BatteryVoltage = g_adc_val[2] / 10.0f + 0.3f;
// 	para.Traffic = 15.52f;
// 	get_lasttime(para.LastTime);
// 	para.Opening = (u8)armReg.gate_kaidu;

// 	enquiry_para_to_json(&para, para_buf, sizeof(para_buf));
// 	MY4G_printf("[GPRS] 3\r\n");
// 	generateJsonString(json_str, sizeof(json_str), "back", "false", "xxx", "enquiry", random, para_buf);
// 	MY4G_printf("[GPRS] 2\r\n");
// 	UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);

// 	return 0;
// }
// 回执 - 设置闸门开度
u8 reply_cjson_setting(cJSON *root, u8 reply)
{
	cJSON *para, *opening, *random_obj;
	// const char *type = "back";
	// const char *back = "false";
	// const char *CmdName = "setting";
	char json_str[512] = {'\0'};
	const char *random;

	StatePara para_state;
	char para_buf[PARA_STATE_JSON_BUF_SIZE] = {'\0'};

	para = cJSON_GetObjectItem(root, "para");
	opening = cJSON_GetObjectItem(para, "valve_opening");
	rxparagate = cJSON_GetNumberValue(opening);

	random_obj = cJSON_GetObjectItem(root, "random");
	random = (random_obj && random_obj->valuestring) ? random_obj->valuestring : "";

	if (rxparagate != armReg.gate_kaidu)
	{
		armReg.range_target = rxparagate;

		// printf("armReg =%d, %d\n",armReg.range_target,armReg.range_reality);

		if ((armReg.range_target < armReg.gate_kaidu) && FLAG_CLOSEALL != 1 && autoFlag == 0)
		{
			sys_moter_state = MOTOR_DOWN;
			remoteflag = 1;
			// if(armReg.range_target >=2)
			// 	armReg.range_target += 2;
		}
		else if ((armReg.range_target > armReg.gate_kaidu) && FLAG_OPENALL != 1 && autoFlag == 0)
		{
			sys_moter_state = MOTOR_UP;
			remoteflag = 1;
			// if(armReg.range_target >=2)
			// 	armReg.range_target -= 2;
		}
	}

	if (reply)
	{
		sprintf(para_state.state, "ok");
		state_para_to_json(&para_state, para_buf, sizeof(para_buf));
		generateJsonString(json_str, sizeof(json_str), "back", "false", Cjsonequipment.clientid, "setting", random, para_buf);

		UartTx_Push_JSON(json_str, strlen(json_str), random, NULL, 0, 0);
	}
	return 0;
}

// 工具函数------------------------------

/**
 * @brief 验证下行 JSON 是否符合统一信封格式
 * 格式：
 * {
 *   "id": "message-id",
 *   "ver": "1.0",
 *   "ts": 1779771714791,
 *   "productKey": "pk001",
 *   "deviceKey": "dev001",
 *   "type": "property",
 *   "body": {}
 * }
 * @param root cJSON根对象
 * @return NULL-合法；非 NULL-错误原因字符串（静态常量/静态缓冲，勿 free）
 */
const char *validate_server_reply_json(cJSON *root)
{
	cJSON *item;
	const char *type_str;
	static char type_err_buf[96];

	if (root == NULL)
		return "JSON root is NULL";

	/* id */
	item = cJSON_GetObjectItem(root, "id");
	if (item == NULL)
		return "id field not found";
	if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0')
		return "id field is not a valid string";


	/* productKey */
	item = cJSON_GetObjectItem(root, "productKey");
	if (item == NULL)
		return "productKey field not found";
	if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0')
		return "productKey field is not a valid string";

	/* deviceKey */
	item = cJSON_GetObjectItem(root, "deviceKey");
	if (item == NULL)
		return "deviceKey field not found";
	if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0')
		return "deviceKey field is not a valid string";

	/* type */
	item = cJSON_GetObjectItem(root, "type");
	if (item == NULL)
		return "type field not found";
	if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0')
		return "type field is not a valid string";
	type_str = item->valuestring;
	if (strcmp(type_str, "property") != 0 &&
		strcmp(type_str, "service") != 0 &&
		strcmp(type_str, "config") != 0 &&
		strcmp(type_str, "property_get") != 0 &&
		strcmp(type_str, "request_reply") != 0)
	{
		snprintf(type_err_buf, sizeof(type_err_buf),
				 "type field value invalid, got: %s", type_str);
		return type_err_buf;
	}

	/* body */
	item = cJSON_GetObjectItem(root, "body");
	if (item == NULL)
		return "body field not found";
	if (!cJSON_IsObject(item))
		return "body field is not an object";

	return NULL;
}
// 时间格式化
void get_lasttime(char *output)
{
	char time[7];
	ds1302_dataread(time);
	sprintf(output, "20%02d-%02d-%02d %02d:%02d:%02d", //%04d
			time[6], time[4], time[3], time[2], time[1], time[0]);
}

// 生成随机字符串
void generate_random_string(char *output)
{
	char time[7];
	ds1302_dataread(time);
	sprintf(output, "20%02d%02d%02d%02d%02d%02d1000", //%04d
			time[6], time[4], time[3], time[2], time[1], time[0]);
	// rand() % 10000);
}
