#ifndef __EVENT_PUB_H
#define __EVENT_PUB_H

#include "gprs_config.h"

/* ---------------------------------------------------------------------------
 * 事件发布模块（event publish）—— 设备往平台推送离散事件
 *
 * 职责：EventPostSimple() / EventPostMotorStop() / EventPostMotorStall()
 *       构造 type: "event.{evtName}" 帧并走 UartTx_Push_JSON 发布。
 *
 * 调用方：DCMOTOR_Process() 的停机边沿、堵转检测、限位触发等
 *       业务层事件源。
 * --------------------------------------------------------------------------- */

typedef enum
{
    EVT_MOTOR_START_UP, EVT_MOTOR_START_DOWN, EVT_MOTOR_STOP,
    EVT_LIMIT_OPEN, EVT_LIMIT_CLOSE, EVT_ALARM, EVT_REMOTE_REACH, EVT_MAX
} EvtType;

typedef enum
{
    STOP_REASON_UNKNOWN = 0, STOP_REASON_TARGET_REACH, STOP_REASON_LIMIT_OPEN,
    STOP_REASON_LIMIT_CLOSE, STOP_REASON_MANUAL, STOP_REASON_STALL,
    STOP_REASON_EMERGENCY_STOP
} MotorStopReason;

/* Post a simple event (no extra output payload). Returns 1 if queued, 0 if dropped. */
uint8_t EventPostSimple(EvtType evt);

/* Post a motor.stop event with the given stop reason and current opening %. */
uint8_t EventPostMotorStop(MotorStopReason reason, uint16_t opening);

/* Post an alarm.stall event with the measured motor current in mA. */
uint8_t EventPostMotorStall(uint16_t motor_current_ma);

#endif