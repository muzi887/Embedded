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
 * GM4G_Reset 的唯一触发入口仍在本文件 gprs_state_machine() 内，但已改为分级恢复：
 *   main() -> Gprs_Process() -> gprs_state_machine() -> GM4G_Reset()
 *
 * 掉线判定与分级恢复：
 * 1) GPRS_LINKA 先经软件去抖：低电平持续 >= GPRS_LINK_DOWN_DEBOUNCE_MS 才判定掉线，
 *    高电平持续 >= GPRS_LINK_UP_DEBOUNCE_MS 才判定恢复，短抖动不改变链路状态；
 * 2) 判定掉线后只标记离线并转状态1，不再立即复位模块，先给模块自愈机会；
 * 3) 状态1 中离线持续 >= GPRS_OFFLINE_HARD_RESET_MS 才允许硬复位一次，
 *    且两次硬复位间隔不小于 GPRS_RESET_MIN_INTERVAL_MS，避免复位风暴。
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

/* 链路去抖与分级恢复参数 */
#define GPRS_LINK_DOWN_DEBOUNCE_MS 2000				 /* LINKA 连续为低多久才判定掉线 */
#define GPRS_LINK_UP_DEBOUNCE_MS 500				 /* LINKA 连续为高多久才判定恢复 */
#define GPRS_OFFLINE_HARD_RESET_MS 60000			 /* 离线持续多久才允许硬复位模块 */
#define GPRS_RESET_MIN_INTERVAL_MS (5UL * 60 * 1000) /* 两次硬复位最小间隔（5分钟） */


static u8 gprs_state = GPRS_STATE_OFFLINE;
static u8 gprs_normal_entry = 0;	 /* 3->4 迁移置位，状态4 消费后清零 */
static u8 offline_entry = 1;		 /* 1=刚进入离线，状态1 打印一次（上电即为离线） */
static u8 link_down_occurred = 0;	 /* 曾因掉线进入离线，再上线时可凭链路恢复 */
static u8 at_init_retry_cnt = 0;	 /* 当前重试次数 */
static u32 at_init_fail_tick = 0;	 /* 最近一次失败时刻（sys_ms） */
static u32 at_init_backoff_tick = 0; /* 进入 backoff 状态时刻（sys_ms） */
static u32 gprs_csq_period_tick = 0; /* 状态4下上次发送周期 CSQ 的时刻（sys_ms） */

/* 链路去抖与分级恢复状态 */
static u8 link_state = 0;			 /* 去抖后的链路状态：1=在线，0=离线 */
static u8 link_raw_prev = 0;		 /* 上一次原始采样值 */
static u32 link_raw_change_tick = 0; /* 原始电平最近一次变化时刻（sys_ms） */
static u32 offline_since_ms = 0;	 /* 进入离线（去抖确认）时刻（sys_ms） */
/* 模块硬复位的节流时刻由 GM4G_Reset() 统一记录（4G.c），这里不再单独维护 */

/* 等待放行期间的日志去抖：记录上次打印过的三个条件值，只在变化时打印 */
static u8 wait_last_link = 0;
static u8 wait_last_atflag = 0;
static u8 wait_last_down = 0;

/**
 * LINKA 软件去抖：低电平需持续 GPRS_LINK_DOWN_DEBOUNCE_MS、
 * 高电平需持续 GPRS_LINK_UP_DEBOUNCE_MS 才提交链路状态变化。
 * @return 1 表示本次调用确认了新的“掉线”边沿
 */
static u8 gprs_link_debounce_update(void)
{
	u8 raw = GPRS_LINKA ? 1 : 0;

	if (raw != link_raw_prev)
	{
		link_raw_prev = raw;
		link_raw_change_tick = sys_ms;
		return 0;
	}

	if (raw)
	{
		if (!link_state && (u32)(sys_ms - link_raw_change_tick) >= GPRS_LINK_UP_DEBOUNCE_MS)
			link_state = 1;
	}
	else
	{
		if (link_state && (u32)(sys_ms - link_raw_change_tick) >= GPRS_LINK_DOWN_DEBOUNCE_MS)
		{
			link_state = 0;
			return 1;
		}
	}
	return 0;
}

/**
 * GPRS 状态机驱动
 * 状态1: 网络未联通 → 去抖确认链路恢复且条件满足则转状态2
 * 状态2: 发送 ICCID/MQTTCID → 转状态3
 * 状态3: 等待 AT 响应 → 全完成则转状态4，失败则转 5 或 6
 * 状态5: 失败后等待 10 秒 → 转状态2 重发
 * 状态6: 最大重试(3次)后等待半小时 → 转状态2 再次尝试
 * 状态4: 正常工作，进入时立即发 CSQ，之后每 10 分钟发 usr.cn#AT+CSQ?\r\n
 * 掉线: 去抖确认 LINKA 持续为低 → 任一状态转状态1（不再立即硬复位）
 */
void gprs_state_machine(void)
{
	const char *cmd;
	u8 cmd_len;
	const char *resp_key;
	u32 link_low_ms;

	/* 1. 链路去抖：只有去抖确认掉线才进入离线流程，短抖动不改变状态 */
	link_low_ms = (u32)(sys_ms - link_raw_change_tick);
	if (gprs_link_debounce_update())
	{
		MY4G_printf("[GPRS] Link down confirmed (LINKA low %ums)\n", (unsigned)link_low_ms);
		if (gprs_state != GPRS_STATE_OFFLINE)
		{
			/*
			 * [Reset Trigger / only trigger]
			 * 此处只标记离线，不再直接调用 GM4G_Reset()：
			 * 真正的硬复位推迟到状态1 中离线持续 GPRS_OFFLINE_HARD_RESET_MS 之后。
			 */
			gprs_state = GPRS_STATE_OFFLINE;
			offline_entry = 1;
			offline_since_ms = sys_ms;
			atflag_online = 0;
			link_down_occurred = 1;
			ATflag_ICCID = 0;
			ATflag_MQTTCID = 0;
			ATflag_CSQ = 0;
			at_init_retry_cnt = 0;
			LED_LINE = 0;
			/* 丢弃在途 AT/JSON 条目与在途对时，避免掉线期间继续重发旧内容 */
			UartTx_Queue_Init();
			Gprs_ClockSync_Abort();
		}
	}

	switch (gprs_state)
	{
	case GPRS_STATE_OFFLINE: {
		u8 reset_allowed;

		if (offline_entry)
		{
			offline_entry = 0;
			/* 只加打印：把"在等什么"和三个条件的当前值一起打出来，便于现场确认等待逻辑在工作 */
			MY4G_printf("[GPRS] State1: Offline, waiting (link_state=%u atflag=%u down_flag=%u)\n",
						(unsigned)link_state, (unsigned)atflag_online, (unsigned)link_down_occurred);
			wait_last_link = link_state;
			wait_last_atflag = atflag_online;
			wait_last_down = link_down_occurred;
		}
		else if (link_state != wait_last_link ||
				 atflag_online != wait_last_atflag ||
				 link_down_occurred != wait_last_down)
		{
			/* 三个条件任一变化时打一次（不做周期性滴答，避免长期无信号刷屏） */
			wait_last_link = link_state;
			wait_last_atflag = atflag_online;
			wait_last_down = link_down_occurred;
			MY4G_printf("[GPRS] wait: link_state=%u atflag=%u down_flag=%u (elapsed %us)\n",
						(unsigned)link_state, (unsigned)atflag_online, (unsigned)link_down_occurred,
						(unsigned)((sys_ms - offline_since_ms) / 1000U));
		}

		/* 网络联通条件：atflag_online（首次上电收到 WH-LTE-7S1）或 link_down_occurred（曾掉线后链路恢复） */
		if (link_state && (atflag_online || link_down_occurred))
		{
			link_down_occurred = 0; /* 清除，下次掉线再置位 */
			gprs_state = GPRS_STATE_AT_INIT_PUSH;
			MY4G_printf("[GPRS] State1->2: Link up, start AT init (waited %us, reason=%s)\n",
						(unsigned)((sys_ms - offline_since_ms) / 1000U),
						atflag_online ? "atflag_online" : "link_down_occurred");
			break;
		}

		/* 分级恢复：离线足够久且距上次硬复位达到最小间隔时，才复位 4G 模块 */
		reset_allowed = ((u32)(sys_ms - offline_since_ms) >= GPRS_OFFLINE_HARD_RESET_MS) &&
						GM4G_ResetAllowed(GPRS_RESET_MIN_INTERVAL_MS);
		if (reset_allowed)
		{
			MY4G_printf("[GPRS] State1: Offline %us, hard reset 4G module\n",
						(unsigned)((sys_ms - offline_since_ms) / 1000U));
			UartTx_Queue_Init();
			Gprs_ClockSync_Abort();
			GM4G_Reset();
			offline_since_ms = sys_ms;
			atflag_online = 0;
			link_down_occurred = 1;
			ATflag_ICCID = 0;
			ATflag_MQTTCID = 0;
			ATflag_CSQ = 0;
			at_init_retry_cnt = 0;
		}
		break;
	}

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

		/* 第三条：CSQ。必须在进 NORMAL 前完成，这样"3 条 AT → 校时 → 心跳"的顺序才成立 */
		cmd = "usr.cn#AT+CSQ?\r\n";
		cmd_len = 16;
		resp_key = "+CSQ";
		UartTx_Push_AT(cmd, cmd_len, resp_key, handleAT_CSQ, UART_TX_AT_TIMEOUT_MS, UART_TX_AT_MAX_RETRY);

		gprs_state = GPRS_STATE_AT_INIT_WAIT;
		//MY4G_printf("")
		MY4G_printf("[GPRS] State2->3: Queued, waiting for AT response\n");
		break;

	case GPRS_STATE_AT_INIT_WAIT:
		/* 队列空表示三条 AT 均已处理完毕 */
		if (UartTx_IsEmpty())
		{
			if (ATflag_ICCID && ATflag_MQTTCID && ATflag_CSQ)
			{
				gprs_state = GPRS_STATE_NORMAL;
				gprs_normal_entry = 1;
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
		/* 失败后间隔 10 秒再重发 */
		if ((sys_ms - at_init_fail_tick) >= GPRS_AT_INIT_RETRY_MS)
		{
			gprs_state = GPRS_STATE_AT_INIT_PUSH;
			MY4G_printf("[GPRS] State5->2: Delay done, resend AT\n");
		}
		break;

	case GPRS_STATE_AT_INIT_BACKOFF:
		/* 最大重试次数达到后，等待半小时再次尝试联通 */
		if ((sys_ms - at_init_backoff_tick) >= GPRS_AT_INIT_BACKOFF_MS)
		{
			at_init_retry_cnt = 0;
			gprs_state = GPRS_STATE_AT_INIT_PUSH;
			MY4G_printf("[GPRS] State6->2: 30min done, retry AT init\n");
		}
		break;

	case GPRS_STATE_NORMAL: {
		u8 just_entered = gprs_normal_entry;

		gprs_normal_entry = 0;
		 LED_LINE = 1;       // 在线灯亮
		 
		if (just_entered)
		{
			MY4G_printf("[GPRS] State4: Normal operation\n");
			/* CSQ 已在 AT_INIT 阶段（第三条 AT）查过：这里只把 10 分钟周期重新计时，
			   不再立即补发，避免刚查完又发一条、也避免打乱"3 条 AT → 校时 → 心跳"的顺序 */
			gprs_csq_period_tick = sys_ms;
		}
		/* 正常态下每隔 10 分钟向发送队列添加一次 CSQ 查询 */
		if ((sys_ms - gprs_csq_period_tick) >= GPRS_CSQ_PERIOD_MS)
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
	u16 remain;
	u8 time[7];
	int year;
	int month;
	int day;

	if (atdata == NULL || rxcnt == 0)
		return;

	if (strstr(atdata, "OK") == NULL)
		return;

	// 判断是否为+ICCID 响应
	pos = strstr(atdata, "+ICCID:");

	if (pos != NULL)
	{
		// 边界保护：帧内剩余长度必须覆盖 pos[10..12] 与 pos[20..25]，
		// 且取值位置全部为数字，否则会读到帧尾之后的旧数据（液晶上表现为乱码）
		remain = (u16)((atdata + rxcnt) - pos);
		if (remain >= 26 &&
			pos[10] >= 0x30 && pos[10] <= 0x39 &&
			pos[11] >= 0x30 && pos[11] <= 0x39 &&
			pos[12] >= 0x30 && pos[12] <= 0x39 &&
			pos[20] >= 0x30 && pos[20] <= 0x39 &&
			pos[21] >= 0x30 && pos[21] <= 0x39 &&
			pos[22] >= 0x30 && pos[22] <= 0x39 &&
			pos[23] >= 0x30 && pos[23] <= 0x39 &&
			pos[24] >= 0x30 && pos[24] <= 0x39 &&
			pos[25] >= 0x30 && pos[25] <= 0x39)
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
	char *p;
	u8 i;
	u16 n;
	u8 time[7];
	int year;
	int month;
	int day;

	if (atdata == NULL || rxcnt == 0)
		return;

	if (strstr(atdata, "OK") == NULL)
		return;

	// 判断是否为+MQTTCID 响应
	pos = strstr(atdata, "+MQTTCID:");

	if (pos != NULL)
	{
		// 设备编号：跳过 "+MQTTCID:" 与冒号后的空格，按实际长度拷贝
		// order[0..5] 是"编号："标签，值区为 order[6..15]（10 字节 = 5 个汉字格）
		p = pos + 9;
		while (*p == ' ')
			p++;

		// 值区先整体补空格：编号不足 10 字节时不会残留旧内容
		for (i = 6; i < 16; i++)
			order[i] = 0x20;

		// 只取可打印 ASCII，遇到 CR/LF、逗号等分隔符或非打印字符即结束；
		// 并用 rxcnt 限定范围，避免把 AT 回复的行尾/OK 当作编号显示出来
		n = 0;
		while (p < atdata + rxcnt && *p >= 0x21 && *p <= 0x7E && *p != ',')
		{
			if (n < 10)
				order[6 + n] = (u8)*p;
			if (n < (u16)(bClientid - 1))
				Cjsonequipment.clientid[n] = *p;
			n++;
			p++;
		}
		Cjsonequipment.clientid[n < (u16)(bClientid - 1) ? n : (u16)(bClientid - 1)] = '\0';

		printf("[AT] MQTTCID = \"%s\" (%d bytes)\r\n", Cjsonequipment.clientid, n);

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
