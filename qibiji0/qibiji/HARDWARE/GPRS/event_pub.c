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
    //printf("even_push_json-->%s\r\n",json_str);
	printf("even_push_json-->\r\n");
	return UartTx_Push_JSON(json_str, (u16)strlen(json_str), random_buf, NULL, 0, 0);
}

u8 EventPost(EvtType evt)
{
	char para_buf[128];
	if (!gprs_is_ready()||autoFlag==1)//autoFlag=1时不自动上报
		return;
	switch (evt)
	{
	case EVT_MOTOR_START_UP:
		snprintf(para_buf, sizeof(para_buf), "{\"event\":\"motor_start_up\"}");
		break;
	case EVT_MOTOR_START_DOWN:
		snprintf(para_buf, sizeof(para_buf), "{\"event\":\"motor_start_down\"}");
		break;
	case EVT_MOTOR_STOP:
		snprintf(para_buf, sizeof(para_buf), "{\"event\":\"motor_stop\"}");
		break;
	case EVT_REMOTE_REACH:
		snprintf(para_buf, sizeof(para_buf), "{\"event\":\"remote_reach\"}");
		break;
	case EVT_ALARM:
		snprintf(para_buf, sizeof(para_buf), "{\"event\":\"alarm\"}");
		break;
	default:
		return 0;
	}
	return event_push_json(para_buf);
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
