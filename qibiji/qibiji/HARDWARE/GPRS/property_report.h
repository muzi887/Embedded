/**
 * @file property_report.h
 * @brief 属性数据上报：基于闸门开度差分变化触发，上报当前全量属性
 */
#ifndef __PROPERTY_REPORT_H
#define __PROPERTY_REPORT_H

#include "sys.h"
#include <stddef.h>

/* 开度变化阈值：|当前 - 上次上报| >= 该值 时触发上报，单位：百分比 0~100 */
#define PROP_OPENING_THRESHOLD 2

/* 最小上报间隔：两次变化上报之间至少间隔（毫秒），防止频繁上报 */
#define PROP_REPORT_MIN_INTERVAL_MS 1000

/* property.get 回执与属性补发共用的字段串缓冲（不含最外层花括号；回执另需 +4 字节外壳） */
#define PROP_SNAPSHOT_JSON_BUF_SIZE 256

/**
 * @brief 开度变化检测与上报（仅主循环调用，勿在中断中调用）
 * @note 若 |range_reality - last_reported| >= 阈值 且 距上次上报 >= 最小间隔，
 *        则构建全量属性 JSON 入队发送
 */
void PropertyReport_OnOpeningChange(void);

/**
 * @brief 标记开度已变化，待主循环处理（供 ChangeReality 在中断中调用）
 */
void PropertyReport_SetPending(void);

/**
 * @brief 主循环中调用，处理待上报的属性（若 SetPending 被置位）
 */
void PropertyReport_Process(void);

/**
 * @brief 强制上报当前全量属性
 * @note 不受差分阈值和最小间隔限制，用于远程到达目标等需立即上报的场景
 */
void PropertyReport_Force(void);

/**
 * @brief 发送属性上报；para_buf 为 body 内字段串（无最外层 {}）
 * @return 1 入队成功，0 失败
 */
u8 Send_property(const char *para_buf);

/**
 * @brief 读取当前全量属性并序列化为 JSON 字段串（不含最外层花括号）
 * @note  供 down/property/get 的 service_reply.output 与随后的 up/property/post 补发共用，
 *        保证「读取」与「主动上报」两份数据同源
 * @param buf      输出缓冲
 * @param buf_size 缓冲字节数
 * @return 1 成功；0 参数非法或无内容
 */
u8 PropertyReport_BuildSnapshot(char *buf, size_t buf_size);

#endif
