#ifndef APP_RUN_H
#define APP_RUN_H
#include "RTC.h"

void app_init(void);
void app_poll(void);

/** 获取当前时间副本（不可修改） */
RTC_Time Get_CurrentTime(void);
/** 设置当前时间（同时更新 RTC 芯片和内部状态） */
void Set_CurrentTime(const RTC_Time *t);
/** 从 RTC 芯片读取当前时间并更新内部状态 */
void Refresh_CurrentTime(void);
/** 获取时间漂移补偿副本 */
void Get_TimeWithDrift(int drift_min, RTC_Time *ahead, RTC_Time *behind);

#endif
