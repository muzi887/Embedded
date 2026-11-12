/**
 * @file property_report.c
 * @brief 属性数据上报：闸门开度差分变化时，上报当前全量属性
 */
#include "property_report.h"
#include "gprs.h"
#include "uart_queue.h"
#include "reg.h"
#include "adc.h"
#include "timer.h"
#include "uart3_at_sequence.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define PROP_JSON_BUF_SIZE 512
#define PARA_ENQUIRY_JSON_BUF_SIZE 256
#define PROP_FORCE_DEBOUNCE_MS 300

extern CJsonEquipment Cjsonequipment;

/* 上次上报的开度值、上次上报时刻 */
static u16 prop_last_opening = 0xFF; /* 0xFF 表示未初始化，首次变化即上报 */
static u32 prop_last_change_report_tick = 0; /* 开度变化上报节流（3s） */
static u32 prop_last_force_tick = 0;         /* STOP 强制上报防抖（短窗口） */
static u8 prop_pending = 0; /* 中断置位，主循环处理，避免在中断中调用 enquiry_para_to_json 导致栈溢出 */

/* 内部：按预定结构体读取当前全量属性（主动上报与 property.get 回执共用，保证两份数据同源） */
static void property_read_current(EnquiryPara *para)
{
	if (para == NULL)
		return;

	para->MotorCurrent = armReg.motor_curr / 1000.0f;
	para->MotorVoltage = g_adc_val[2] / 10.0f + 0.3f;
	para->BatteryCurrent = 0.0f;
	para->BatteryVoltage = 0.0f;
	para->Opening = (u8)armReg.gate_kaidu;
	para->MeasuredValue1 = 0.0f;
	para->MeasuredValue2 = 0.0f;
	para->MeasuredValue3 = 0.0f;
	para->MeasuredValue4 = 0.0f;
}

/* 内部：构建全量属性 JSON 并入队 */
static u8 property_push_json(void)
{
	EnquiryPara para;
	char para_buf[PARA_ENQUIRY_JSON_BUF_SIZE] = {'\0'};

	property_read_current(&para);
	enquiry_para_to_json(&para, para_buf, sizeof(para_buf));
	//printf("property-->kaidu = %d\r\n", armReg.gate_kaidu);
	return Send_property(para_buf);
}

/**
 * @brief 读取当前全量属性并序列化为 JSON 字段串（无最外层花括号）
 * @note  供 down/property/get 回执的 output 与随后的 up/property/post 补发共用；
 *        与 property_push_json 同源，避免两处取值不一致
 */
u8 PropertyReport_BuildSnapshot(char *buf, size_t buf_size)
{
	EnquiryPara para;

	if (buf == NULL || buf_size == 0)
		return 0;

	buf[0] = '\0';
	property_read_current(&para);
	enquiry_para_to_json(&para, buf, buf_size);
	return (u8)(buf[0] != '\0');
}

/**
 * @brief 发送属性上报 JSON
 * @param para_buf body 内属性字段串（无最外层 {}，由 enquiry_para_to_json 生成）
 */
u8 Send_property(const char *para_buf)
{
	char json_str[UART_TX_JSON_LEN];
	char id_buf[24];
	int64_t ts;

	if (para_buf == NULL)
		return 0;

	ts = unix_ms_now(rtc_get_unix_sec(), rtc_get_ms_of_sec());
	snprintf(id_buf, sizeof(id_buf), "%lld", (long long)ts);

	snprintf(json_str, sizeof(json_str),
			 "%d,{"
			 "\"id\":\"%s\","
			 "\"productKey\":\"%s\","
			 "\"deviceKey\":\"%s\","
			 "\"type\":\"property\","
			 "\"body\":{%s}"
			 "}",
			 Topic_propertyPub,
			 id_buf,
			 g_mqtt_productKey,
			 g_mqtt_deviceKey,
			 para_buf);

	printf("--Send_property--%s\r\n", json_str);
	return UartTx_Push_JSON(json_str, (u16)strlen(json_str), id_buf, NULL, UART_TX_JSON_TIMEOUT_MS, 0);
}
void PropertyReport_OnOpeningChange(void)
{
	u16 curr = (u16)armReg.gate_kaidu;
	u16 diff;
	u32 now = sys_ms;

	if (prop_last_opening == 0xFF)
	{
		prop_last_opening = curr;
		return;
	}
	diff = (curr >= prop_last_opening) ? (curr - prop_last_opening) : (prop_last_opening - curr);
	if (diff < PROP_OPENING_THRESHOLD)
		return;
	if ((now - prop_last_change_report_tick) < PROP_REPORT_MIN_INTERVAL_MS)
		return;
	if (!gprs_is_ready()||autoFlag==1)//autoFlag=1时不自动上报
		return;
	if (property_push_json())
	{
		prop_last_opening = curr;
		prop_last_change_report_tick = now;
	}
}

void PropertyReport_Force(void)
{
	u32 now = sys_ms;
	/* Force 不受 3 秒节流影响，仅做短防抖，抑制停止瞬间抖动重复 */
	if ((now - prop_last_force_tick) < PROP_FORCE_DEBOUNCE_MS)
		return;
	prop_last_force_tick = now;
	if (property_push_json())
		prop_last_opening = (u16)armReg.gate_kaidu;
}

void PropertyReport_SetPending(void)
{
	prop_pending = 1;
}

void PropertyReport_Process(void)
{
		if (!gprs_is_ready()||autoFlag==1)//autoFlag=1时不自动上报
		return;
	if (prop_pending)
	{
		prop_pending = 0;
		/* 电机停止时强制上报（无阈值/间隔限制），否则按开度变化上报 */
		if (sys_moter_state == MOTOR_STOP)
			PropertyReport_Force();
		else
			PropertyReport_OnOpeningChange();
	}
}
