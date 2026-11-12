/**
 * @file property_report.h
 * @brief 属性数据上报：基于闸门开度差分变化触发，上报当前全量属性
 */
#ifndef __PROPERTY_REPORT_H
#define __PROPERTY_REPORT_H

#include "sys.h"

/* 开度变化阈值：|当前 - 上次上报| >= 该值 时触发上报，单位：百分比 0~100 */
#define PROP_OPENING_THRESHOLD 2

/* 最小上报间隔：两次变化上报之间至少间隔（毫秒），防止频繁上报 */
#define PROP_REPORT_MIN_INTERVAL_MS 1000

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

#endif
