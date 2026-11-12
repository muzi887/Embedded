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
#include <stdio.h>
#include <string.h>

#define PROP_JSON_BUF_SIZE 512
#define PARA_ENQUIRY_JSON_BUF_SIZE 256
#define PROP_FORCE_DEBOUNCE_MS 300

extern CJsonEquipment Cjsonequipment;

/* 上次上报的开度值、上次上报时刻 */
static u16 prop_last_opening = 0xFF; /* 0xFF 表示未初始化，首次变化即上报 */
static u32 prop_last_change_report_tick = 0; /* 开度变化上报节流（3s） */
static u32 prop_last_force_tick = 0;         /* STOP 强制上报防抖（短窗口） */
static u8 prop_pending = 0; /* 中断置位，主循环处理，避免在中断中调用 enquiry_para_to_json 导致栈溢出 */

/* 内部：构建全量属性 JSON 并入队 */
static u8 property_push_json(void)
{
	EnquiryPara para;
	char para_buf[PARA_ENQUIRY_JSON_BUF_SIZE] = {'\0'};
	char json_str[PROP_JSON_BUF_SIZE] = {'\0'};
	char random_buf[32];


	/* 按预定结构体赋值 */
	para.MotorCurrent = g_adc_val[0] / 100.0f;
	para.MotorVoltage = g_adc_val[2] / 10.0f + 0.3f;
	para.BatteryCurrent = 15.52f;
	para.BatteryVoltage = g_adc_val[2] / 10.0f + 0.3f;
	para.Traffic = 15.52f;
	get_lasttime(para.LastTime);
	para.Opening = (u8)armReg.gate_kaidu;

	enquiry_para_to_json(&para, para_buf, sizeof(para_buf));
	generate_random_string(random_buf);
	/* 格式与 event_pub 一致，CmdName="property" 表示属性上报 */
	snprintf(json_str, sizeof(json_str),
			 "%d,{\"type\":\"property\",\"back\":\"false\",\"clientid\":\"%s\",\"CmdName\":\"gateproperty\",\"para\":%s,\"random\":\"%s\"}",
			 Topic_Pub, Cjsonequipment.clientid, para_buf,random_buf);
    //printf("property-->%s\r\n",json_str);
	printf("property-->kaidu = %d\r\n",armReg.gate_kaidu);
	return UartTx_Push_JSON(json_str, (u16)strlen(json_str), random_buf, NULL, 0, 0);
	//return ;
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
