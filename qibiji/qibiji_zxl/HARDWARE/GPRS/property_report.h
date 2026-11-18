#ifndef __PROPERTY_REPORT_H
#define __PROPERTY_REPORT_H

#include "gprs_config.h"

/* ---------------------------------------------------------------------------
 * 属性主动上报模块 —— 开度变化 → pending → Process → 节流发送
 *
 * 职责：SetPending() 由底层（DCMOTOR_Tick）在开度变化时调用；
 *       Process() 每轮检查 pending、应用最小间隔阈值、
 *       用 cJSON 拼 {opening/motor/flow} 快照发往队列。
 *
 * 用法：main 主循环每轮调 PropertyReport_Process()；
 *       DCMOTOR_Tick() 里检测 s_opening 变化后调 SetPending()；
 *       停机边沿调 Force() 触发强制上报。
 * --------------------------------------------------------------------------- */

#define PROP_OPENING_THRESHOLD 2u
#define PROP_REPORT_MIN_INTERVAL_MS 1000u
#define PROP_SNAPSHOT_BUF_SIZE 256u

void PropertyReport_SetPending(void);  /* called when opening changes (motor layer) */
void PropertyReport_Process(void);     /* main loop */
void PropertyReport_Force(void);       /* force report, e.g. on motor stop */
uint8_t PropertyReport_BuildSnapshot(char *buf, uint32_t buf_size);
uint8_t Send_property(const char *para_buf);

#endif