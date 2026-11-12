#ifndef __EVENT_PUB_H
#define __EVENT_PUB_H

#include "sys.h"

/* 事件类型枚举 */
typedef enum
{
	EVT_MOTOR_START_UP,
	EVT_MOTOR_START_DOWN,
	EVT_MOTOR_STOP,
	EVT_LIMIT_OPEN,
	EVT_LIMIT_CLOSE,
	EVT_ALARM,
	EVT_REMOTE_REACH,
	EVT_MAX
} EvtType;

/* 按事件类型发布，内部构建 para */
u8 EventPost(EvtType evt);

/* 简化版：仅类型，para 由内部根据类型生成（用于限位等无额外参数事件） */
u8 EventPostSimple(EvtType evt);

#endif
