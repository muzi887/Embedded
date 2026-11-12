/**
 * @file net_liveness.c
 * @brief 极简心跳探活 + 模块硬复位自愈（详见 net_liveness.h）。
 */
#include "net_liveness.h"
#include "gprs.h"
#include "uart_queue.h"
#include "4G.h"
#include "timer.h"
#include <stdio.h>

/*
 * 串口帧 "2,{}"：首位 2 是模块 MQTTPUBTP 主题槽位（up/sys/heartbeat），
 * 载荷为 2 字节 "{}"（不带信封：身份由 Topic 推导，平台按心跳处理并回执）。
 * 共 4 字节，帧边界由模块 UARTFT 空闲切分，不需要 CRLF。
 */
#define NET_LIVENESS_FRAME "2,{}"
#define NET_LIVENESS_FRAME_LEN 4u

/* 帧在发送队列中的驻留时间：发完即出队，不等任何串口回显 */
#define NET_LIVENESS_TX_TIMEOUT_MS 300u

static u8 s_pending = 0;      /* 1 = 已发出心跳，等待平台回执 */
static u8 s_ever_sent = 0;    /* 进入 NORMAL 后是否已发过（0 表示立即补发一帧） */
static u8 s_miss = 0;         /* 连续未确认次数 */
static u32 s_last_send_ms = 0;
/* 模块硬复位的"上次时刻"由 GM4G_Reset() 统一记录（4G.c），两条自愈路径共用同一条时间线 */
static u32 s_last_ack_ms = 0; /* 最近一次收到平台回执的时刻（sys_ms） */
static u8 s_ever_acked = 0;   /* 是否曾经收到过平台回执 */
static u8 s_wait_timesync = 1; /* 1 = 等 sys.timeSync ok 后才允许发首帧心跳（每次进 NORMAL 重新置位） */
static u8 s_wait_logged = 0;   /* 等待日志：进入等待打一次，之后每 60s 一条 */
static u32 s_wait_start_ms = 0;
static u32 s_wait_tick_ms = 0;
static u8 s_ts_fail = 0;       /* 等首帧心跳期间，对时连续失败次数 */
static u8 s_ts_fast = 1;       /* 1=等待期用 5s 快节奏（默认）；被复位节流挡住后退回 60s */
static u8 s_ts_throttle_logged = 0; /* "被节流跳过复位"只打印一次，避免刷屏 */

/**
 * 是否是紧凑心跳回执：平台对极简心跳只回一帧 `{}`（空对象）。
 * 收到其他任何 JSON 都交回原严格校验流程，避免误吞业务下行。
 */
u8 NetLiveness_IsAck(cJSON *root)
{
	if (root == NULL)
		return 0;
	if (!cJSON_IsObject(root))
		return 0;
	return (cJSON_GetArraySize(root) == 0) ? 1u : 0u;
}

/**
 * 回执到达：确认链路双向可用，清零失联计数与分级进度。
 * 没有在途心跳时忽略（例如平台多发的帧），避免误判为本轮确认。
 * 每次确认都打印一条（每 120s 一帧，日志量可接受）——这样"心跳是否被平台确认"在串口上是可见的，
 * 不必再靠"没有 miss 打印"来反推。
 */
void NetLiveness_OnAck(void)
{
	if (!s_pending)
		return;

	s_pending = 0;
	MY4G_printf("[LIVE] heartbeat ack ok (miss=%u)\n", (unsigned)s_miss);
	s_miss = 0;
	s_last_ack_ms = sys_ms;
	s_ever_acked = 1;
}

/**
 * 校时成功（DS1302 已写入平台时间）时调用：解锁首帧心跳。
 * 在拿到这一步之前一律不发心跳，保证"3 条 AT → 校时 → 心跳"的顺序。
 */
void NetLiveness_OnTimeSyncOk(void)
{
	if (!s_wait_timesync)
		return;

	s_wait_timesync = 0;
	s_wait_logged = 0;
	s_ts_fail = 0;
	s_ts_fast = 1;
	s_ts_throttle_logged = 0;
	MY4G_printf("[LIVE] timeSync ok -> first heartbeat released\n");
}

/* 是否正在等校时（对时模块据此决定"一轮失败后要不要自动重起一轮"） */
u8 NetLiveness_IsWaitingTimeSync(void)
{
	return s_wait_timesync;
}

/**
 * 对时是否用"等首帧心跳的快节奏"（5s）。
 * 只有"正在等首帧心跳"且"还没被复位节流挡住"时才用快节奏；
 * 一旦自愈复位被 15 分钟节流挡住（说明复位已经试过且没能解决），退回 60s，
 * 避免在长时间故障（例如平台侧地址配错）下每 10 秒发一帧探测。
 */
u8 NetLiveness_IsTimeSyncFast(void)
{
	return (s_wait_timesync && s_ts_fast) ? 1u : 0u;
}

/**
 * 一次对时尝试失败（探测超时 / 一轮放弃）时调用。
 * 只统计"等首帧心跳"期间的失败，达到阈值就由 liveness_tick() 硬复位模块——
 * 否则模块会话/订阅坏掉时，首帧心跳永远被压住、miss 永远为 0，设备就永久卡在校时上。
 */
void NetLiveness_OnTimeSyncFail(void)
{
	if (!s_wait_timesync)
		return;
	if (s_ts_fail < 0xFFu)
		s_ts_fail++;
}

/**
 * 平台是否已确认在线：曾经收到过回执，且距今不超过 2 个心跳周期（240s）。
 * 供 LCD 区分「模块已注网但平台未确认」与「平台已确认在线」两态。
 */
u8 NetLiveness_IsServerConfirmed(void)
{
	if (!s_ever_acked)
		return 0;
	return ((u32)(sys_ms - s_last_ack_ms) < (2UL * NET_LIVENESS_INTERVAL_MS)) ? 1u : 0u;
}

/**
 * 链路掉线或模块被硬复位时调用：放弃在途帧，回到 NORMAL 后立即补发。
 * 刻意保留 s_miss —— 复位本身会让链路闪断，若在这里清零失联计数，
 * 就会出现「复位 → 闪断清零 → 又立刻复位」的复位风暴。
 * 复位的最小间隔不在这里维护：它由 GM4G_Reset() 记录的共享时间线统一保证（见 4G.c）。
 */
void  NetLiveness_ResetState(void)
{
	s_pending = 0;
	s_ever_sent = 0;
	/* 每次重新联网都要重新等一次校时：首帧心跳的门槛按"每次上线"生效 */
	s_wait_timesync = 1;
	s_wait_logged = 0;
	/* 新一次上线：恢复快节奏，并允许重新打印一次"被节流"提示 */
	s_ts_fast = 1;
	s_ts_throttle_logged = 0;
}

static void liveness_send(void)
{
	if (!UartTx_Push_JSON(NET_LIVENESS_FRAME, NET_LIVENESS_FRAME_LEN, "", NULL,
						  NET_LIVENESS_TX_TIMEOUT_MS, 0))
	{
		/* 队列满：放弃本轮，不计失联（队列拥塞不等于链路失效） */
		MY4G_printf("[LIVE] tx queue full, heartbeat skipped\n");
		return;
	}
	s_pending = 1;
}

static void liveness_tick(void)
{
	/* 首帧心跳门槛：没拿到 sys.timeSync ok 之前不发心跳、不计 miss、不刷新周期起点 */
	if (s_wait_timesync)
	{
		if (!s_wait_logged)
		{
			s_wait_logged = 1;
			s_wait_start_ms = sys_ms;
			s_wait_tick_ms = sys_ms;
			MY4G_printf("[LIVE] first heartbeat held: waiting sys.timeSync ok\n");
		}
		else if ((u32)(sys_ms - s_wait_tick_ms) >= 60000UL)
		{
			s_wait_tick_ms = sys_ms;
			MY4G_printf("[LIVE] still waiting timeSync ok (elapsed %us)\n",
						(unsigned)((sys_ms - s_wait_start_ms) / 1000U));
		}

		/* 等待期的自愈：对时连续失败达到阈值 → 硬复位模块（复用 15 分钟节流） */
		if (s_ts_fail >= NET_LIVENESS_TSFAIL_THRESHOLD)
		{
			if (GM4G_ResetAllowed(NET_LIVENESS_L1_MIN_INTERVAL_MS))
			{
				s_ts_fail = 0;
				s_ts_fast = 1;
				s_ts_throttle_logged = 0;
				MY4G_printf("[LIVE] timeSync stuck %u times -> hard reset 4G module\n",
							(unsigned)NET_LIVENESS_TSFAIL_THRESHOLD);
				UartTx_Queue_Init(); /* 丢掉在途的探测帧 */
				s_pending = 0;
				GM4G_Reset();
			}
			else
			{
				u32 remain_ms = GM4G_ResetWaitRemainingMs(NET_LIVENESS_L1_MIN_INTERVAL_MS);

				/* 复位被节流挡住：退回 60s 节奏（靠 NetLiveness_IsTimeSyncFast 生效），
				   并把"为什么不复位"打印一次，避免现场看成又卡死了 */
				s_ts_fast = 0;
				if (!s_ts_throttle_logged)
				{
					s_ts_throttle_logged = 1;
					MY4G_printf("[LIVE] timeSync stuck but reset throttled (next reset in %us)\n",
								(unsigned)((remain_ms + 999U) / 1000U));
				}
			}
		}
		return;
	}

	if (s_ever_sent && (u32)(sys_ms - s_last_send_ms) < NET_LIVENESS_INTERVAL_MS)
		return;

	if (s_pending)
	{
		s_pending = 0;
		if (s_miss < 0xFFu)
			s_miss++;
		MY4G_printf("[LIVE] heartbeat not acked, miss=%u\n", (unsigned)s_miss);
	}

	s_ever_sent = 1;
	s_last_send_ms = sys_ms;
	liveness_send();
}

static void liveness_recover(void)
{
	if (s_miss < NET_LIVENESS_MISS_THRESHOLD)
		return;

	/* 15 分钟最小间隔：与状态机的离线硬复位共用同一条"上次复位时刻"时间线（GM4G_Reset 记录），
	   所以任一路径刚复位过，这里都不会立刻再复位；首次不受限。 */
	if (!GM4G_ResetAllowed(NET_LIVENESS_L1_MIN_INTERVAL_MS))
		return;

	/*
	 * 唯一的自动恢复动作：硬复位 4G 模块。
	 * 模块按自己已保存的 MQTT 参数重连重订阅；这里不校验、不下发、不保存任何模块配置，
	 * 也不重启 MCU（模块参数的重配置只走 BLE 人工一键配置）。
	 */
	s_miss = 0;
	MY4G_printf("[LIVE] recovery: hard reset 4G module (min interval %us)\n",
				(unsigned)(NET_LIVENESS_L1_MIN_INTERVAL_MS / 1000UL));
	UartTx_Queue_Init(); /* 丢弃在途旧帧，避免复位后成批重发 */
	s_pending = 0;
	GM4G_Reset();
}

/**
 * 主循环驱动：仅在 GPRS 正常态（状态 4）生效。
 * 非正常态交给 GPRS 状态机处理（它有自己的掉线判定与硬复位节流），这里只清在途状态。
 */
void NetLiveness_Process(void)
{
	if (!gprs_is_ready())
	{
		NetLiveness_ResetState();
		return;
	}

	liveness_tick();
	liveness_recover();
}
