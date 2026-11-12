/**
 * @file event_pub.c
 * @brief 事件发布模块：电机状态变化、限位、报警、远程到达等事件发生时，构建 JSON 入队发送
 */
#include "event_pub.h"
#include "uart_queue.h"
#include "gprs.h"
#include "reg.h"
#include <stdio.h>
#include <string.h>
#include "uart3_at_sequence.h"

#define EVENT_JSON_BUF_SIZE 384

extern CJsonEquipment Cjsonequipment;

/* 构建 status 事件 JSON 并入队，格式与 WARNING 一致 */
//TODO 可增加参数，如流量、电池电压等
static u8 event_push_json(const char *para_json)
{
	char json_str[EVENT_JSON_BUF_SIZE];
	char random_buf[32];

	if (para_json == NULL)
		return 0;

	generate_random_string(random_buf);

	/* 格式：Topic_Pub,{"type":"status","clientid":"xxx","status":"event","para":{...},"random":"xxx"} */
	snprintf(json_str, sizeof(json_str),
			 "%d,{\"type\":\"event\",\"back\":\"false\",\"clientid\":\"%s\",\"CmdName\":\"gateevent\",\"para\":%s,\"random\":\"%s\"}",
			 Topic_Pub, Cjsonequipment.clientid, para_json, random_buf);
    printf("even_push_json-->%s\r\n",json_str);
	//printf("even_push_json-->\r\n");
	return UartTx_Push_JSON(json_str, (u16)strlen(json_str), random_buf, NULL, 0, 0);
}
static u8  replyEven(const char *eventCode,const char *message, const char *others)
{
	char json_str[UART_TX_JSON_LEN];
	const char *msg;
	const char *oth;
	char reply_id[24];
	int64_t ts;

	/* 按文档：unix_ms_now → 13 位毫秒时间戳字符串作 id */
	ts = unix_ms_now(rtc_get_unix_sec(), rtc_get_ms_of_sec());
	snprintf(reply_id, sizeof(reply_id), "%lld", (long long)ts);

	msg = (message != NULL && message[0] != '\0') ? message : "";
	oth = (others != NULL && others[0] != '\0') ? others : "";

	snprintf(json_str, sizeof(json_str),
			 "%d,{"
			 "\"id\":\"%s\","
			 "\"productKey\":\"%s\","
			 "\"deviceKey\":\"%s\","
			 "\"type\":\"event\","
			 "\"body\":{"
			 "\"identifier\":\"%s\","
			 "\"params\":{%s}"
			 "}"
			 "}",
			 Topic_evenPub,
			 reply_id,
			 g_mqtt_productKey,
			 g_mqtt_deviceKey,
			 eventCode,
			 msg);

	printf("evenPub-->%s\r\n", json_str);
	return UartTx_Push_JSON(json_str, (u16)strlen(json_str), reply_id, NULL, 0, 0);
}

u8 EventPostMotorStall(u16 motor_current_ma)
{
	char para_buf[32];

	if (!gprs_is_ready() || autoFlag == 1)
		return 0;

	/* replyEven 会补齐 params 外层花括号，这里只生成内部字段。 */
	snprintf(para_buf, sizeof(para_buf), "\"motor_current\":%u",
			 (unsigned)motor_current_ma);
	return replyEven("motor_stall", para_buf, "");
}

static const char *MotorStopReasonText(MotorStopReason reason)
{
	switch (reason)
	{
	case MOTOR_STOP_REASON_TARGET_REACH:
		return "target_reach";
	case MOTOR_STOP_REASON_LIMIT_OPEN:
		return "limit_open";
	case MOTOR_STOP_REASON_LIMIT_CLOSE:
		return "limit_close";
	case MOTOR_STOP_REASON_MANUAL:
		return "manual";
	case MOTOR_STOP_REASON_STALL:
		return "stall";
	case MOTOR_STOP_REASON_EMERGENCY_STOP:
		return "emergency_stop";
	default:
		return "unknown";
	}
}

u8 EventPostMotorStop(MotorStopReason reason, u16 opening)
{
	char para_buf[64];

	if (!gprs_is_ready() || autoFlag == 1)
		return 0;

	snprintf(para_buf, sizeof(para_buf),
			 "\"reason\":\"%s\",\"opening\":%u",
			 MotorStopReasonText(reason),
			 (unsigned)opening);
	return replyEven("motor_stop", para_buf, "");
}

u8 EventPost(EvtType evt)
{
	char para_buf[24];
	if (!gprs_is_ready()||autoFlag==1)//autoFlag=1时不自动上报
		return;
	switch (evt)
	{
	case EVT_MOTOR_START_UP:
		snprintf(para_buf, sizeof(para_buf), "motor_start_up");
		break;
	case EVT_MOTOR_START_DOWN:
		snprintf(para_buf, sizeof(para_buf), "motor_start_down");
		break;
	case EVT_MOTOR_STOP:
		return EventPostMotorStop(MOTOR_STOP_REASON_UNKNOWN, armReg.gate_kaidu);
	case EVT_REMOTE_REACH:
		snprintf(para_buf, sizeof(para_buf), "remote_reach");
		break;
	case EVT_ALARM:
		snprintf(para_buf, sizeof(para_buf), "motor_stall");
		break;
	default:
		return 0;
	}
	return replyEven(para_buf,"","");
	//return event_push_json(para_buf);
}

u8 EventPostSimple(EvtType evt)
{
	const char *para_json = NULL;

	switch (evt)
	{
	case EVT_LIMIT_OPEN:
		para_json = "{\"event\":\"limit_open\"}";
		break;
	case EVT_LIMIT_CLOSE:
		para_json = "{\"event\":\"limit_close\"}";
		break;
	default:
		return 0;
	}
	return event_push_json(para_json);
}
