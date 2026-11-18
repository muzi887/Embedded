#ifndef __NET_LIVENESS_H
#define __NET_LIVENESS_H

#include "gprs_config.h"
#include "cJSON.h"

/* ---------------------------------------------------------------------------
 * 网络保活模块 —— 心跳 ping + 服务器时间同步，两条独立通道
 *
 * 职责：Process() 定时发 liveness ping，接收端 OnAck() 清等待；
 *       时间同步走同一 Process() 但有前置门槛
 *       （3 次心跳 ack 后才进入 waiting phase）。
 *
 * 用法：main 主循环每轮调 NetLiveness_Process()；
 *       GPRS_JSON_ParseProcess() 里在 ValidateServerJson 之前
 *       先 NetLiveness_IsAck() 捞走 ack 包。
 * --------------------------------------------------------------------------- */

#define NET_LIVENESS_INTERVAL_MS (120UL * 1000UL)   /* heartbeat period */
#define NET_LIVENESS_MISS_THRESHOLD 3u              /* 3 misses -> recovery */
#define NET_LIVENESS_L1_MIN_INTERVAL_MS (15UL * 60UL * 1000UL)
#define NET_LIVENESS_TSFAIL_THRESHOLD 3u            /* waiting-phase timesync fails */

/* heartbeat */
/* Drive periodic liveness ping and time-sync send logic (call each loop). */
void NetLiveness_Process(void);

/* A valid server liveness ack is received; clears waiting flag. */
void NetLiveness_OnAck(void);

/* Parse a JSON frame and return whether it is a liveness ack (1) or not (0). */
uint8_t NetLiveness_IsAck(cJSON *root);

/* Return 1 if server has ever confirmed at least one liveness ack, else 0. */
uint8_t NetLiveness_IsServerConfirmed(void);

/* timesync */
/* A valid server time-sync reply is received. */
void NetLiveness_OnTimeSyncOk(void);

/* Return 1 if we are currently waiting for a time-sync reply, else 0. */
uint8_t NetLiveness_IsWaitingTimeSync(void);

/* A time-sync attempt times out or fails. */
void NetLiveness_OnTimeSyncFail(void);

/* Reset all liveness/time-sync internal state (e.g. on GPRS reconnect). */
void NetLiveness_ResetState(void);

#endif