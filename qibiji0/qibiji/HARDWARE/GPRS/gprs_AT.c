#include "usart.h"
#include "gprs.h"
#include "uart_queue.h"
#include "main.h"
#include <stdio.h>

u8 atflag_online = 0;
u8 ATflag_CSQ = 0;
u8 ATflag_ICCID = 0;
u8 ATflag_MQTTCID = 0;
u8 ATflag_CCLK = 0;

/*==================== GPRS 状态机 ====================*/
/*
 * [Reset Path Annotation]
 * 当前版本中 GM4G_Reset 的唯一触发入口在本文件 gprs_state_machine() 内：
 *   main() -> Gprs_Process() -> gprs_state_machine() -> GM4G_Reset()
 *
 * 触发条件（全部满足才会触发）：
 * 1) 采样到 GPRS_LINKA == 0（掉线）；
 * 2) 当前状态 gprs_state != GPRS_STATE_OFFLINE（避免反复进入离线分支）；
 * 3) 且掉线前状态为 GPRS_STATE_NORMAL（仅 NORMAL->OFFLINE 时硬件复位）。
 *
 * 时序依赖：
 * - 该调用发生在主循环上下文（非中断）；
 * - 调用后会进入 GM4G_Reset() 的阻塞时序（见 4G.c）；
 * - 阻塞期间主循环其他任务（电机/按键/LCD 等）会被同步延后。
 */
#define GPRS_STATE_OFFLINE 1		 /* 网络未联通 */
#define GPRS_STATE_AT_INIT_PUSH 2	 /* 网络已联通，发送 AT 初始化命令 */
#define GPRS_STATE_AT_INIT_WAIT 3	 /* 等待 AT 初始化命令完成 */
#define GPRS_STATE_AT_INIT_DELAY 5	 /* 失败后等待间隔再重发 */
#define GPRS_STATE_AT_INIT_BACKOFF 6 /* 最大重试后等待半小时再次尝试 */
#define GPRS_STATE_NORMAL 4			 /* 正常工作状态 */

#define GPRS_AT_INIT_MAX_RETRY 3				   /* 最大重试次数 */
#define GPRS_AT_INIT_RETRY_MS 10000				   /* 每次失败到再次发送间隔（毫秒） */
#define GPRS_AT_INIT_BACKOFF_MS (30UL * 60 * 1000) /* 最大重试后等待时长（30分钟） */
#define GPRS_CSQ_PERIOD_MS (10UL * 60 * 1000)	   /* 状态4下周期 CSQ 间隔（10分钟） */


static u8 gprs_state = GPRS_STATE_OFFLINE;
static u8 gprs_state_prev = 0xFF;	 /* 上一状态，用于检测转换时打印 */
static u8 link_down_occurred = 0;	 /* 曾因 GPRS_LINKA=0 掉线，再上线时可凭 GPRS_LINKA=1 恢复 */
static u8 at_init_retry_cnt = 0;	 /* 当前重试次数 */
static u32 at_init_fail_tick = 0;	 /* 最近一次失败时刻（sys_ms） */
static u32 at_init_backoff_tick = 0; /* 进入 backoff 状态时刻（sys_ms） */
static u32 gprs_csq_period_tick = 0; /* 状态4下上次发送周期 CSQ 的时刻（sys_ms） */

#define GPRS_STATE_PRINT(s, msg)               \
	do                                         \
	{                                          \
		if (gprs_state != gprs_state_prev)     \
		{                                      \
			gprs_state_prev = gprs_state;      \
			MY4G_printf("[GPRS] %s\n", (msg)); \
		}                                      \
	} while (0)

/**
 * GPRS 状态机驱动
 * 状态1: 网络未联通 → 检测到 atflag_online 则转状态2
 * 状态2: 发送 ICCID/MQTTCID/CSQ → 转状态3
 * 状态3: 等待三条 AT 响应 → 全完成则转状态4，失败则转 5 或 6
 * 状态5: 失败后等待 10 秒 → 转状态2 重发
 * 状态6: 最大重试(3次)后等待半小时 → 转状态2 再次尝试
 * 状态4: 正常工作，进入时立即发 CSQ，之后每 10 分钟发 usr.cn#AT+CSQ?\r\n
 * 掉线(GPRS_LINKA=0): 任一状态 → 状态1
 */
void gprs_state_machine(void)
{
	const char *cmd;
	u8 cmd_len;
	const char *resp_key;

	/* 掉线则复位到状态1 */
	if (!GPRS_LINKA) // 网络标志，比atflag_online晚
	{
		if (gprs_state != GPRS_STATE_OFFLINE)
		{
			MY4G_printf("[GPRS] State1: Offline (link down)\n");
			/*
			 * [Reset Trigger #1 / only trigger]
			 * 仅在 NORMAL -> OFFLINE 的边沿触发 GM4G_Reset()，不在离线驻留期间重复触发。
			 * 进入 OFFLINE 后，后续循环因 gprs_state == OFFLINE 将不会再次执行本复位调用。
			 */
			if (gprs_state == GPRS_STATE_NORMAL)
			{
				GM4G_Reset();
			}
			gprs_state = GPRS_STATE_OFFLINE;
			gprs_state_prev = gprs_state;
			atflag_online = 0;
			link_down_occurred = 1;
			ATflag_ICCID = 0;
			ATflag_MQTTCID = 0;
			ATflag_CSQ = 0;
			at_init_retry_cnt = 0;
		}
	}

	switch (gprs_state)
	{
	case GPRS_STATE_OFFLINE:
		GPRS_STATE_PRINT(0, "State1: Offline, waiting for link");
		/* 网络联通条件：atflag_online（首次上电收到 WH-LTE-7S1）或 link_down_occurred（曾掉线后 GPRS_LINKA 恢复） */
		if (GPRS_LINKA && (atflag_online || link_down_occurred))
		{
			link_down_occurred = 0; /* 清除，下次掉线再置位 */
			gprs_state = GPRS_STATE_AT_INIT_PUSH;
			MY4G_printf("[GPRS] State1->2: Link up, start AT init\n");
		}
		break;

	case GPRS_STATE_AT_INIT_PUSH:
		MY4G_printf("[GPRS] State2: Sending AT init (ICCID/MQTTCID/CSQ)\n");
		/* 依次入队 ICCID、MQTTCID、CSQ */
		ATflag_ICCID = 0;
		ATflag_MQTTCID = 0;
		ATflag_CSQ = 0;

		cmd = "usr.cn#AT+ICCID?\r\n";
		cmd_len = 18;
		resp_key = "+ICCID";
		UartTx_Push_AT(cmd, cmd_len, resp_key, handleAT_ICCID, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);

		cmd = "usr.cn#AT+MQTTCID?\r\n";
		cmd_len = 20;
		resp_key = "+MQTTCID";
		UartTx_Push_AT(cmd, cmd_len, resp_key, handleAT_MQTTCID, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);

		// cmd = "usr.cn#AT+CSQ?\r\n";
		// cmd_len = 16;
		// resp_key = "+CSQ";
		// UartTx_Push_AT(cmd, cmd_len, resp_key, handleAT_CSQ, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);

		gprs_state = GPRS_STATE_AT_INIT_WAIT;
		//MY4G_printf("")
		MY4G_printf("[GPRS] State2->3: Queued, waiting for AT response\n");
		break;

	case GPRS_STATE_AT_INIT_WAIT:
		GPRS_STATE_PRINT(0, "State3: Waiting for AT init complete");
		/* 队列空表示三条 AT 均已处理完毕 */
		if (UartTx_IsEmpty())
		{
			if (ATflag_ICCID && ATflag_MQTTCID)
			{
				gprs_state = GPRS_STATE_NORMAL;
				at_init_retry_cnt = 0;
				MY4G_printf("[GPRS] State3->4: AT init done, entering normal\n");
			}
			else
			{
				/* 未全部成功，检查重试次数 */
				at_init_retry_cnt++;
				if (at_init_retry_cnt >= GPRS_AT_INIT_MAX_RETRY)
				{
					MY4G_printf("[GPRS] State3->6: Max retry(%d) reached, wait 30min\n", GPRS_AT_INIT_MAX_RETRY);
					at_init_backoff_tick = sys_ms;
					gprs_state = GPRS_STATE_AT_INIT_BACKOFF;
				}
				else
				{
					MY4G_printf("[GPRS] State3->5: Fail #%d, retry in 10s\n", at_init_retry_cnt);
					at_init_fail_tick = sys_ms;
					gprs_state = GPRS_STATE_AT_INIT_DELAY;
				}
			}
		}
		break;

	case GPRS_STATE_AT_INIT_DELAY:
		GPRS_STATE_PRINT(0, "State5: Wait 10s before retry");
		/* 失败后间隔 10 秒再重发 */
		if ((sys_ms - at_init_fail_tick) >= GPRS_AT_INIT_RETRY_MS)
		{
			gprs_state = GPRS_STATE_AT_INIT_PUSH;
			MY4G_printf("[GPRS] State5->2: Delay done, resend AT\n");
		}
		break;

	case GPRS_STATE_AT_INIT_BACKOFF:
		GPRS_STATE_PRINT(0, "State6: Max retry, wait 30min");
		/* 最大重试次数达到后，等待半小时再次尝试联通 */
		if ((sys_ms - at_init_backoff_tick) >= GPRS_AT_INIT_BACKOFF_MS)
		{
			at_init_retry_cnt = 0;
			gprs_state = GPRS_STATE_AT_INIT_PUSH;
			MY4G_printf("[GPRS] State6->2: 30min done, retry AT init\n");
		}
		break;

	case GPRS_STATE_NORMAL: {
		u8 just_entered = (gprs_state_prev != GPRS_STATE_NORMAL);

		 LED_LINE = 1;       // 在线灯亮
		 
		GPRS_STATE_PRINT(0, "State4: Normal operation");
		/* 进入时立即发一次，之后每隔 10 分钟向发送队列添加 CSQ 查询 */
		if (just_entered || (sys_ms - gprs_csq_period_tick) >= GPRS_CSQ_PERIOD_MS)
		{
			gprs_csq_period_tick = sys_ms;
			cmd = "usr.cn#AT+CSQ?\r\n";
			cmd_len = 16;
			resp_key = "+CSQ";
			UartTx_Push_AT(cmd, cmd_len, resp_key, handleAT_CSQ, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);
		}
		break;
	}

	default:
		MY4G_printf("[GPRS] Unknown state %d, reset to offline\n", gprs_state);
		gprs_state = GPRS_STATE_OFFLINE;
		break;
	}
}

/* 是否处于正常工作状态（可供其他模块查询） */
u8 gprs_is_ready(void)
{
	return (gprs_state == GPRS_STATE_NORMAL);
}

//-------------------------检查是否在线-----------------------------------
void gprs_check(void)
{
	static u8 cnt = 0;
	if (flag10ms)
	{
		if (GPRS_LINKA)
		{
			cnt++;
			if (cnt >= 10)
			{
				LED_LINE = 1;
				cnt = 0;
			}
		}
		else
		{
			LED_LINE = 0;
			cnt = 0;
		}
	}
}

/* 兼容旧接口，内部调用状态机 */
// void gprs_AT_init(void)
// {
// 	gprs_state_machine();
// }

// 处理AT返回
void handleAT_CSQ(char *atdata, u16 rxcnt)
{
	char *pos;
	u8 time[7];
	int year;
	int month;
	int day;

	if (strstr(atdata, "OK") == NULL)
		return;

	// 判断是否为+CSQ 响应
	pos = strstr(atdata, "+CSQ:");
	if (pos != NULL)
	{
		if (pos[6] >= 0x30 && pos[7] >= 0x30 && pos[6] <= 0x39 && pos[7] <= 0x39)
		{
			xinhao[6] = pos[6];
			xinhao[7] = pos[7];
		}
		ATflag_CSQ = 1;
	}
}

void handleAT_ICCID(char *atdata, u16 rxcnt)
{
	char *pos;
	u8 time[7];
	int year;
	int month;
	int day;

	if (strstr(atdata, "OK") == NULL)
		return;

	// 判断是否为+ICCID 响应
	pos = strstr(atdata, "+ICCID:");

	if (pos != NULL)
	{
		if (pos[10] >= 0x30 && pos[10] <= 0x39)
		{
			ccid[6] = pos[10];
			ccid[7] = pos[11];
			ccid[8] = pos[12];
			ccid[9] = 0X2D;
			ccid[10] = pos[20];
			ccid[11] = pos[21];
			ccid[12] = pos[22];
			ccid[13] = pos[23];
			ccid[14] = pos[24];
			ccid[15] = pos[25];
		}
		ATflag_ICCID = 1;
	}
}
void handleAT_CCLK(char *atdata, u16 rxcnt)
{
	char *pos;
	u8 time[7];
	int year;
	int month;
	int day;

	if (strstr(atdata, "OK") == NULL)
		return;

	// 判断是否为+CCLK 响应
	pos = strstr(atdata, "+CCLK:");

	if (pos != NULL)
	{
		time[0] = ((pos[23] - 0x30) << 4) + (pos[24] - 0x30); // 秒
		time[1] = ((pos[20] - 0x30) << 4) + (pos[21] - 0x30); // 分
		time[2] = ((pos[17] - 0x30) << 4) + (pos[18] - 0x30); // 时
		time[3] = ((pos[14] - 0x30) << 4) + (pos[15] - 0x30); // 日
		time[4] = ((pos[11] - 0x30) << 4) + (pos[12] - 0x30); // 月

		time[6] = ((pos[8] - 0x30) << 4) + (pos[9] - 0x30); // 年

		year = 2000 + (pos[8] - 0x30) * 10 + (pos[9] - 0x30); // 年
		month = ((pos[11] - 0x30) * 10) + (pos[12] - 0x30);	  // 月
		day = ((pos[14] - 0x30) * 10) + (pos[15] - 0x30);	  // 日

		time[5] = simple_weekday(year, month, day); // 星期

		// printf("%d,%d,%d--",year,month,day);
		// printf("time = %x %x %x %x %x %x %x\n",time[0],time[1],time[2],time[3],time[4],time[5],time[6]);

		ds1302_settime(time);
		ATflag_CCLK = 1;
	}
}
void handleAT_MQTTCID(char *atdata, u16 rxcnt)
{
	char *pos;
	u8 time[7];
	int year;
	int month;
	int day;

	if (strstr(atdata, "OK") == NULL)
		return;

	// 判断是否为+MQTTCID 响应
	pos = strstr(atdata, "+MQTTCID:");

	if (pos != NULL)
	{
		//设备编号
		order[5] = pos[9];
		order[6] = pos[10];
		order[7] = pos[11];
		order[8] = pos[12];
		order[9] = pos[13];
		order[10] = pos[14];
		order[11] = pos[15];
		order[12] = pos[16];
		order[13] = pos[17];
		order[14] = pos[18];
		order[15] = pos[19];

		Cjsonequipment.clientid[0] = pos[9];
		Cjsonequipment.clientid[1] = pos[10];
		Cjsonequipment.clientid[2] = pos[11];
		Cjsonequipment.clientid[3] = pos[12];
		Cjsonequipment.clientid[4] = pos[13];
		Cjsonequipment.clientid[5] = pos[14];
		Cjsonequipment.clientid[6] = pos[15];
		Cjsonequipment.clientid[7] = pos[16];
		Cjsonequipment.clientid[8] = pos[17];
		Cjsonequipment.clientid[9] = pos[18];
		Cjsonequipment.clientid[10] = pos[19];
		Cjsonequipment.clientid[11] = '\0';
		ATflag_MQTTCID = 1;
	}
}
// 计算星期
int simple_weekday(int year, int month, int day)
{
	static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	if (month < 3)
		year--;
	return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}