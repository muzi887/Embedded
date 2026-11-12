#ifndef __4G_H
#define __4G_H
#include "sys.h"

extern u8 gprsonlineflag;

#define GPRS_LINKA  PCin(5)

/**
 * 距上次模块硬复位是否已达到 min_interval_ms（从未复位过返回 1）。
 * 各恢复路径共用同一个"上次模块复位时刻"，避免两条路径各自节流导致短时间连续复位；
 * 各自的最小间隔仍保留：离线路径传 5min，应用层探活/校时传 15min。
 */
u8 GM4G_ResetAllowed(u32 min_interval_ms);

/**
 * 距上次模块硬复位还要等多久才允许再复位（毫秒）；0 = 现在允许。
 * 用于把"本该复位但被节流挡住"打到日志上（现场可见）。
 */
u32 GM4G_ResetWaitRemainingMs(u32 min_interval_ms);

void GM4G_Init(void);  		//IO初始化
/* 触发来源见 gprs_AT.c：当前仅在 gprs_state_machine 的 NORMAL->OFFLINE 分支调用。 */
void GM4G_Reset(void);		//硬件复位 4G 模块（RST 引脚）

#endif
