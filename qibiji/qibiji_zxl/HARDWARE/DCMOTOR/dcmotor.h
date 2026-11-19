#ifndef __DCMOTOR_H
#define __DCMOTOR_H

/* ---------------------------------------------------------------------------
 * DC motor (gate) control module 
 *
 * Scope: relay drive (PD1 main power, PD3 direction), request/executed state
 *        separation with edge-triggered action, current-zero interlock from
 *        the ADC module, stall stop, and a max-run-time safety fallback, limit 
 *        reaction and opening feedback.
 *
 * Not included : limit switches, rotary encoder / opening feedback, 
 *                auto-calibration, GPRS event reporting, EEPROM. 
 *                Those come when their modules are migrated.
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"

#define MOTOR_UP     0x01u
#define MOTOR_DOWN   0x02u
#define MOTOR_STOP   0x03u

/* auto-calibration result codes (DCMOTOR_GetCalResult) */
#define CAL_RESULT_IDLE     0u
#define CAL_RESULT_RUNNING  1u
#define CAL_RESULT_OK       2u
#define CAL_RESULT_FAIL     3u

/* stop reason, exposed for debug / display only */
typedef enum
{
    MOTOR_STOP_NONE = 0,        /* 无原因：初始值，也是启动时清成的值 */
    MOTOR_STOP_MANUAL,          /* 人工停机（兜底：没人认领的停机都记这里） */
    MOTOR_STOP_STALL,           /* 堵转：ADC 判电流超限，优先级最高 */
    MOTOR_STOP_TIMEOUT,         /* 超时：跑满 30s 强制停，说明前两道保护没拦住 */
    MOTOR_STOP_INTERLOCK,       /* 联锁：电流零点没标定，不许启动 */
    MOTOR_STOP_LIMIT_OPEN,      /* 到上限位：正常到位，不是故障 */
    MOTOR_STOP_LIMIT_CLOSE      /* 到下限位：正常到位，不是故障 */
} motor_stop_reason_t;

void                DCMOTOR_Init(void);
void                DCMOTOR_Request(uint8_t state);   /* set target state */
void                DCMOTOR_Process(void);            /* call each main-loop turn */
void                DCMOTOR_Tick(void);               /* call once per 10 ms tick */
uint8_t             DCMOTOR_GetState(void);
uint8_t             DCMOTOR_GetDir(void);
motor_stop_reason_t DCMOTOR_GetLastStopReason(void);
void                DCMOTOR_SetMaxRunMs(uint16_t ms);

/* opening feedback (limit state is queried from GATEIO directly) */
uint16_t            DCMOTOR_GetOpening(void);   /* opening 0..100 percent */
uint16_t            DCMOTOR_GetCount(void);     /* baseline-relative pulses */
void                DCMOTOR_SetMaxTurns(uint16_t turns); /* pulses for full travel */

/* auto calibration (RAM only) */
void    DCMOTOR_StartCalibration(void);   /* ignored unless STOP + ADC ready */
uint8_t DCMOTOR_IsCalibrating(void);      /* 1 while calibration owns the motor */
uint8_t DCMOTOR_IsCalibrated(void);       /* 1 if a full-travel cal is done (s_max_turns != 0) */
uint8_t DCMOTOR_GetCalResult(void);       /* return CAL_RESULT_* */

/* target opening control (closed loop on encoder feedback) */
uint8_t  DCMOTOR_SetTarget(uint16_t target);  /* 1 = accepted, 0 = rejected */
void     DCMOTOR_CancelTarget(void);
uint16_t DCMOTOR_GetTarget(void);             /* 0..100 */
uint8_t  DCMOTOR_IsTargetActive(void);        /* 1 = driving to a target */

#endif