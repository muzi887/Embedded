/**
 * @file net_liveness.h
 * @brief 极简心跳探活 + 模块硬复位自愈，消除「两端都不认为设备掉线、实际链路已失效」的假连接。
 *
 * 机制：
 *   1) 进入 NORMAL 立即发一帧、之后每 NET_LIVENESS_INTERVAL_MS 发一帧紧凑心跳；
 *      上行 = 已有主题槽位 2（iot/v1/{pk}/{dk}/up/sys/heartbeat），载荷固定 "{}"（MQTT 2 字节）；
 *   2) 平台收到后向 down/request/reply 回一帧 "{}"，设备据此确认「上行真的到达了平台」；
 *   3) 连续 NET_LIVENESS_MISS_THRESHOLD 次收不到回执、且距上次复位 >= NET_LIVENESS_L1_MIN_INTERVAL_MS
 *      → 硬复位 4G 模块，让它按模块内已存的 MQTT 参数自己重连重订阅；可无限次重复。
 *
 * 注意：自动流程**不校验、不下发、不保存**任何模块配置，也不再重启 MCU。
 *       模块参数的重配置只保留在 BLE 近场一键配置（sbf20_app -> uart3_at_sequence）这条人工路径上。
 */
#ifndef __NET_LIVENESS_H
#define __NET_LIVENESS_H

#include "stm32f10x.h"
#include "cJSON.h"

/* 心跳周期：120s；与平台 360s 判离线阈值配合，容忍连续 3 次丢失 */
#define NET_LIVENESS_INTERVAL_MS (120UL * 1000UL)

/* 连续未确认次数达到该值才考虑复位模块 */
#define NET_LIVENESS_MISS_THRESHOLD 3u

/* 应用层（探活/校时）自愈复位模块的最小间隔；与状态机的 5 分钟节流共用同一条
   "上次模块复位时刻"时间线（由 GM4G_Reset() 记录），首次不受限 */
#define NET_LIVENESS_L1_MIN_INTERVAL_MS (15UL * 60UL * 1000UL)

/* 等首帧心跳期间，对时连续失败多少次就硬复位模块（把"校时拿不到"变成可自愈） */
#define NET_LIVENESS_TSFAIL_THRESHOLD 3u

/* 主循环驱动（在 Gprs_Process 内、发送状态机之前调用） */
void NetLiveness_Process(void);

/* 收到紧凑心跳回执（空对象 {}）时调用 */
void NetLiveness_OnAck(void);

/* 平台是否已确认在线（曾经收到回执，且距今 < 2 个心跳周期）；供 LCD 三态指示使用 */
u8 NetLiveness_IsServerConfirmed(void);

/* 校时成功（DS1302 已写入平台时间）时调用：解锁首帧心跳 */
void NetLiveness_OnTimeSyncOk(void);

/* 是否正在等校时（对时模块据此在"一轮失败"后自动重起一轮） */
u8 NetLiveness_IsWaitingTimeSync(void);

/* 对时是否用"等首帧心跳的快节奏"（5s）；被 15 分钟复位节流挡住后退回 60s 时返回 0 */
u8 NetLiveness_IsTimeSyncFast(void);

/* 一次对时尝试失败（探测超时 / 一轮放弃）时调用；用于等待期的自愈计数 */
void NetLiveness_OnTimeSyncFail(void);

/* 判断一帧 JSON 是否是紧凑心跳回执（空对象）；必须在严格信封校验之前调用 */
u8 NetLiveness_IsAck(cJSON *root);

/* 链路掉线/模块硬复位时清状态：放弃在途帧，回到 NORMAL 后立即补发 */
void NetLiveness_ResetState(void);

#endif
