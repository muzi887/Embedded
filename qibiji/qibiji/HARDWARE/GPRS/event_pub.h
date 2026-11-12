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

/* 电机停止原因，随 motor_stop 事件上报 */
typedef enum
{
	MOTOR_STOP_REASON_UNKNOWN = 0,
	MOTOR_STOP_REASON_TARGET_REACH,
	MOTOR_STOP_REASON_LIMIT_OPEN,
	MOTOR_STOP_REASON_LIMIT_CLOSE,
	MOTOR_STOP_REASON_MANUAL,
	MOTOR_STOP_REASON_STALL,
	MOTOR_STOP_REASON_EMERGENCY_STOP
} MotorStopReason;

/* 按事件类型发布，内部构建 para */
u8 EventPost(EvtType evt);

/* 电机堵转事件：携带触发时的校准电流，单位 mA */
u8 EventPostMotorStall(u16 motor_current_ma);

/* 电机停止事件：携带停止原因和停止时的开度 */
u8 EventPostMotorStop(MotorStopReason reason, u16 opening);

/* 简化版：仅类型，para 由内部根据类型生成（用于限位等无额外参数事件） */
u8 EventPostSimple(EvtType evt);

#endif
