#include "dcmotor.h"
#include "gate_io.h"
#include "adc.h"
#include "delay.h"
#include "property_report.h" 

/* ===========================================================================
 * DC motor implementation
 *
 * Kept:
 *  - relay sequence: set direction first, delay 5ms, then main power on
 *  - stop only drops main power (direction relay untouched)
 *  - request (state) vs executed (old) separation, action only on edge
 *  - limit switches (active-low) force stop; encoder pulses adjust a baseline count
 * Removed: armReg, remote flag, key-check (keys arrive via explicit Request), 
 * EEPROM persistence, auto-stop return travel.
 * Added: current-zero interlock + stall stop via ADC, max-run fallback,
 *  opening percentage from encoder count / full-travel pulse countt,
 *  auto full-travel calibration (RAM only, reuses the existing state machine).
 * =========================================================================== */

/* ---- relay outputs (GPIOD); sensors belong to GATEIO ---- */
/* PD1 主电：1=合闸。停机第一动作永远是它断开。 */
#define RLY_MAIN_ON()     GPIO_SetBits(GPIOD, GPIO_Pin_1)
#define RLY_MAIN_OFF()    GPIO_ResetBits(GPIOD, GPIO_Pin_1)

/* PD3 方向：1=上行，0=下行。必须断电切换，改完等 5~10ms 再合主电。 */
#define RLY_DIR_UP()      GPIO_SetBits(GPIOD, GPIO_Pin_3)
#define RLY_DIR_DOWN()    GPIO_ResetBits(GPIOD, GPIO_Pin_3)

#define MAX_RUN_MS_DEFAULT  30000u   /* 30s fallback if limit switch fails */

/* internal calibration phases */
#define CAL_PHASE_IDLE        0u
#define CAL_PHASE_SEEK_CLOSE  1u
#define CAL_PHASE_SEEK_OPEN   2u

static uint8_t  s_state;        /* requested state */
static uint8_t  s_old;          /* executed state */
static uint8_t  s_dir;          /* last physical direction */
static motor_stop_reason_t s_stop_reason;
static uint16_t s_max_run_ms;
static uint32_t s_run_ms;       /* ms spent in current run */
static uint8_t  s_stall_pending;/* stall seen, to be reported on STOP edge */

/* ---- limit + encoder state (replaces FLAG_OPENALL/CLOSEALL, Num_count) ---- */
static uint8_t  s_at_open;      /* 1 = fully-open limit latched 一个锁存标志，表示位于全开限位*/
static uint8_t  s_at_close;     /* 1 = fully-close limit latched */
static uint16_t s_count;        /* encoder pulses from the close baseline */
static uint16_t s_max_turns;    /* pulses for full travel 全行程脉冲数，0 = not set */
static uint16_t s_opening;      /* opening percent 0..100 开度百分比 */

/* ---- auto calibration state ---- */
static uint8_t  s_cal_active;   /* 1 = calibration owns the motor */
static uint8_t  s_cal_phase;    /* CAL_PHASE_* */
static uint8_t  s_cal_result;   /* CAL_RESULT_* */

/* ---- target-opening control ---- */
static uint16_t s_target;        /* requested opening 0..100 */
static uint8_t  s_target_active; /* 1 = target loop owns the direction */

static void DCMOTOR_RelayInit(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

  /* relay outputs: PD1 main, PD3 direction, PD4/PD5 485-side limit ctrl */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_3| GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	RLY_MAIN_OFF();
    RLY_DIR_DOWN();
}

/* 把方向继电器打到上行 */
static void DCMOTOR_DriveUp(void)
{
    RLY_DIR_UP();
    delay_ms(5);                /* direction relay must settle before main power */
    RLY_MAIN_ON();
    s_dir = MOTOR_UP;
}

/* 把方向继电器打到下行 */
static void DCMOTOR_DriveDown(void)
{
    RLY_DIR_DOWN();
    delay_ms(5);                /* same settle time before main power */
    RLY_MAIN_ON();
    s_dir = MOTOR_DOWN;
}

/* 只断主电，方向继电器保持原状（减少动作次数） */
static void DCMOTOR_DriveStop(void)
{
    RLY_MAIN_OFF();             /* only main power; direction relay unchanged */
}

/* s_count / s_max_turns * 100 算开度 */
static void DCMOTOR_UpdateOpening(void)
{
    uint16_t c;

    if (s_max_turns == 0u)
    {
        /* not calibrated yet: opening is only meaningful at the limits */
        s_opening = (s_at_open != 0u) ? 100u : 0u;
        return;
    }

    c = s_count;
    if (c > s_max_turns)
        c = s_max_turns;

    s_opening = (uint16_t)(((u32)c * 100u) / s_max_turns);
}

/* Read the debounced limit levels from GATEIO and maintain the latches.
 * GATEIO is direction-agnostic; the "leaving the limit" decision needs s_dir,
 * which is why it lives here and not in the hardware layer.
 * 直接读 PD7/PB3 + 判"到位/离开"（离开要看方向） */
static void DCMOTOR_UpdateLimits(void)
{
    if (GATEIO_IsOpenLimit() != 0u) /* just reached / still at fully open */
    {
        s_at_open = 1u;     /* level latch */
        if (s_max_turns != 0u) 
            s_count = s_max_turns;
    }
    else if ((s_at_open != 0u) && (s_dir == MOTOR_DOWN)) /* 限位无效且电机正往反方向走（s_dir）→ 清锁存。 */
    {
        s_at_open = 0u;         /* started moving away, downwards */
        s_count = s_max_turns;  /* resync counter to full travel */
    }

    if (GATEIO_IsCloseLimit() != 0u)
    {
        s_at_close = 1u;        /* just reached / still at fully closed */
        s_count = 0u;           /* opening baseline = 0 */
    }
    else if ((s_at_close != 0u) && (s_dir == MOTOR_UP))
    {
        s_at_close = 0u;        /* started moving away, upwards */
        s_count = 0u;           /* resync counter to bottom baseline */
    }
}

/* Consume the edge delta produced by GATEIO since the last tick. The sign
 * comes from the requested state, exactly like the original JudeCount().
 * 直接读 PB4/PB0 下降沿 + 按方向给计数加减 */
static void DCMOTOR_ConsumePulses(void)
{
    uint8_t n;
    uint8_t i;

    n = GATEIO_PopPulseDelta();
    if ((s_at_open != 0u) || (s_at_close != 0u))
        return;

    for (i = 0u; i < n; i++)
    {
        if (s_state == MOTOR_DOWN)
        {
            if (s_count > 0u)
                s_count--;
        }
        else if (s_state == MOTOR_UP)
        {
            /* max_turns==0 means "not set yet": let it count freely so the
             * full-travel value can be measured. */
            if ((s_max_turns == 0u) || (s_count < s_max_turns))
                s_count++;
        }
    }
}

/* Finish calibration: clear ownership, latch the result code.
 * Does not touch s_state - caller sets the motor state (normally STOP). */
static void s_CalFinish(uint8_t result)
{
    s_cal_active = 0u;
    s_cal_phase = CAL_PHASE_IDLE;
    s_cal_result = result;
}

void DCMOTOR_Init(void)
{
    s_state = MOTOR_STOP;
    s_old = MOTOR_STOP;
    s_dir = MOTOR_STOP;
    s_stop_reason = MOTOR_STOP_NONE;
    s_max_run_ms = MAX_RUN_MS_DEFAULT;
    s_run_ms = 0u;
    s_stall_pending = 0u;
    s_max_turns = 0u;
    s_opening = 0u;
    s_target = 0u;
    s_target_active = 0u;

    DCMOTOR_RelayInit();

    /* GATEIO_Init() must have run first; seed the latches so a gate parked at
     * a limit does not need a 10 ms tick before Request() starts guarding. */
    s_at_open  = GATEIO_IsOpenLimit();
    s_at_close = GATEIO_IsCloseLimit();
}

/* Start calibration only from rest with a valid ADC zero point.
 * Clears the old baseline first so the up-run can be measured freely. */
void DCMOTOR_StartCalibration(void)
{
    if (s_cal_active != 0u)
        return;
    if (s_state != MOTOR_STOP)
        return;
    if (ADC_CurrentIsReady() == 0u)
        return;

    s_count = 0u;
    s_max_turns = 0u;
    s_stop_reason = MOTOR_STOP_NONE;
    s_run_ms = 0u;
    s_cal_phase = CAL_PHASE_SEEK_CLOSE;
    s_cal_result = CAL_RESULT_RUNNING;
    s_target_active = 0u;   /* calibration takes over the motor */
    s_cal_active = 1u;
    s_state = MOTOR_DOWN;   /* internal set: bypasses Request guard */
}

uint8_t DCMOTOR_IsCalibrating(void) { return s_cal_active; }
uint8_t DCMOTOR_GetCalResult(void)  { return s_cal_result; }
uint8_t DCMOTOR_IsCalibrated(void) { return (s_max_turns != 0u) ? 1u : 0u; }

void DCMOTOR_Request(uint8_t state)
{
    /* refuse to drive into an already-tripped limit (same as the original
     * keycheck's "FLAG_OPENALL != 1 / FLAG_CLOSEALL != 1" guard). Without this,
     * a held UP key at the top would keep re-issuing UP and chatter the relay. */
    if ((state == MOTOR_UP)   && (s_at_open  != 0u)) return;
    if ((state == MOTOR_DOWN) && (s_at_close != 0u)) return;

    if ((state == MOTOR_UP) || (state == MOTOR_DOWN) || (state == MOTOR_STOP))
    {
        s_target_active = 0u;   /* manual command wins over a pending target */
        s_state = state;
    }
}

/* Advance calibration at the end of Process(), after edge handling, so the
 * limit STOP already happened and s_old is synced. Phase switches set s_state
 * but not s_old, so the next Process turn fires the drive edge naturally. */
static void s_CalibrationProcess(void)
{
    if (s_cal_active == 0u)
        return;

    if ((s_cal_phase == CAL_PHASE_SEEK_CLOSE) && (s_at_close != 0u))
    {
        s_count = 0u;                    /* close limit = zero baseline */
        s_cal_phase = CAL_PHASE_SEEK_OPEN;
        s_stop_reason = MOTOR_STOP_NONE;
        s_state = MOTOR_UP;              /* reverse and start measuring */
    }
    else if ((s_cal_phase == CAL_PHASE_SEEK_OPEN) && (s_at_open != 0u))
    {
        s_max_turns = s_count;           /* full travel measured */
        s_state = MOTOR_STOP;
        s_CalFinish(CAL_RESULT_OK);
    }
}

/* 电机模块的主状态机，10ms 一拍 */
void DCMOTOR_Process(void)
{
    /* 1) current-zero interlock 零点联锁: zero point not ready -> force stop */
    if (ADC_CurrentIsReady() == 0u) /* ADC 是否完成零点标定 */
    {
        s_state = MOTOR_STOP;
        s_old = MOTOR_STOP;      /* sync old: no phantom stop event on ready */
        s_stop_reason = MOTOR_STOP_INTERLOCK;
        if (s_cal_active != 0u)
            s_CalFinish(CAL_RESULT_FAIL);
        ADC_SetMotorRunning(0u); /* 标记电机为停止状态，可以进行零点标定 */
        DCMOTOR_DriveStop();
        return;
    }

    /* 2) consume stall flag raised by ADC */
    if (ADC_PopStall() != 0u)
    {
        s_stall_pending = 1u;   /* 未上报原因的堵转 */
        s_state = MOTOR_STOP;    /* request stop; reason set on edge below */
        if (s_cal_active != 0u)
            s_CalFinish(CAL_RESULT_FAIL);
        ADC_SetMotorRunning(0u);
        DCMOTOR_DriveStop();     /* immediate physical cut, do not wait */
    }

    /* 3) target-opening loop: choose direction from the encoder feedback.
     *    Runs before the limit interlock so a limit still wins. */
    if ((s_target_active != 0u) && (s_cal_active == 0u))
    {
        if (s_opening == s_target)
        {
            s_target_active = 0u;      /* arrived */
            s_state = MOTOR_STOP;
        }
        else if (s_opening < s_target)
        {
            if (s_state != MOTOR_UP)
                s_state = MOTOR_UP;
        }
        else
        {
            if (s_state != MOTOR_DOWN)
                s_state = MOTOR_DOWN;
        }
    }

    /* 4) limit interlock 限位联锁: never keep driving into a tripped limit.
     *    Reason is set here; the STOP edge below will not overwrite it
     *    (it only defaults to MANUAL when reason is still NONE). */
    if ((s_at_open != 0u) && (s_state == MOTOR_UP))
    {
        s_state = MOTOR_STOP;
        s_stop_reason = MOTOR_STOP_LIMIT_OPEN;
        s_opening = 100u;
    }
    else if ((s_at_close != 0u) && (s_state == MOTOR_DOWN))
    {
        s_state = MOTOR_STOP;
        s_stop_reason = MOTOR_STOP_LIMIT_CLOSE;
        s_opening = 0u;
    }

    /* 5) edge-triggered action */
    if (s_old != s_state)
    {
        switch (s_state)
        {
        case MOTOR_UP:
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_NONE;
            ADC_SetMotorRunning(1u);    /* freeze ADC zero tracking while running */
            DCMOTOR_DriveUp();
            break;
        case MOTOR_DOWN:
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_NONE;
            ADC_SetMotorRunning(1u);
            DCMOTOR_DriveDown();
            break;
        case MOTOR_STOP:
        default:
            ADC_SetMotorRunning(0u);    /* allow zero tracking after stop */
            if (s_stall_pending != 0u)
            {
                s_stop_reason = MOTOR_STOP_STALL;
                s_stall_pending = 0u;
            }
            else if (s_stop_reason == MOTOR_STOP_NONE)
            {
                s_stop_reason = MOTOR_STOP_MANUAL;
            }
            s_run_ms = 0u;
            s_target_active = 0u;      /* any stop cancels the target drive */
            DCMOTOR_DriveStop();
            break;
        }
        s_old = s_state;
    }

    s_CalibrationProcess();     /* advance calibration after the drive edge */
}

void DCMOTOR_Tick(void)
{
    static uint16_t s_last_opening_sent = 0xFFFFu;   

    DCMOTOR_UpdateLimits();     /* GATEIO levels -> latches, baseline sync */
    DCMOTOR_ConsumePulses();    /* GATEIO edge delta -> s_count */
    DCMOTOR_UpdateOpening();    /* s_count / s_max_turns -> s_opening */

    /* 开度变化 → 通知属性上报 */
    if (s_opening != s_last_opening_sent)
    {
        s_last_opening_sent = s_opening;
        PropertyReport_SetPending();
    }

    if ((s_state == MOTOR_UP) || (s_state == MOTOR_DOWN))
    {
        s_run_ms += 10u;
        if (s_run_ms >= (u32)s_max_run_ms)
        {
            s_run_ms = 0u;
            s_stop_reason = MOTOR_STOP_TIMEOUT;
            s_state = MOTOR_STOP;
        if (s_cal_active != 0u)
            s_CalFinish(CAL_RESULT_FAIL);
            ADC_SetMotorRunning(0u);
            DCMOTOR_DriveStop();    /* immediate cut on timeout */
        }
    }
}

uint8_t DCMOTOR_GetState(void)  { return s_state; }
uint8_t DCMOTOR_GetDir(void)    { return s_dir; }
motor_stop_reason_t DCMOTOR_GetLastStopReason(void) { return s_stop_reason; }
void DCMOTOR_SetMaxRunMs(uint16_t ms) { s_max_run_ms = ms; }
uint16_t DCMOTOR_GetOpening(void) { return s_opening; }
uint16_t DCMOTOR_GetCount(void)   { return s_count; }

/* 手动设定全行程脉冲数 */
void DCMOTOR_SetMaxTurns(uint16_t turns)
{
    s_max_turns = turns;
    DCMOTOR_UpdateOpening();
}

/* Accept a target only when the travel span is known: without s_max_turns the
 * opening value is meaningless, so closed-loop driving would never terminate. */
uint8_t DCMOTOR_SetTarget(uint16_t target)
{
    if (target > 100u)
        return 0u;
    if (s_cal_active != 0u)
        return 0u;
    if (s_max_turns == 0u)
        return 0u;              /* not calibrated: caller must reply an error */

    s_target = target;
    s_target_active = 1u;
    return 1u;
}

void DCMOTOR_CancelTarget(void)
{
    s_target_active = 0u;
}

uint16_t DCMOTOR_GetTarget(void)      { return s_target; }
uint8_t  DCMOTOR_IsTargetActive(void) { return s_target_active; }