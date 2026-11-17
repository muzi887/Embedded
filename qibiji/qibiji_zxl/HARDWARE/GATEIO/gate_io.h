#ifndef __GATE_IO_H
#define __GATE_IO_H

/* ---------------------------------------------------------------------------
 * Gate hardware I/O (limit switches + rotary encoder) 
 *
 * Owns every pin that senses the gate:
 *   PD7 = IN1, fully-open limit  (active low, internal pull-up)
 *   PB3 = IN2, fully-close limit (active low, internal pull-up, JTAG TDO)
 *   PB4 = CODE1, encoder channel A (active low, JTAG nTRST)
 *   PB0 = CODE2, encoder channel B (active low, ADC input pin)
 *
 * Responsibilities: clock/GPIO init, JTAG release, 10 ms sampling, 2-sample
 *   debounce on both limits, falling-edge counting on both encoder channels.
 * Not responsible: direction, opening percentage, motor decisions. This
 *   module does not know which way the gate moves; DCMOTOR interprets that.
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"

/* ---- pin map ---- */
#define GATEIO_OPEN_LIMIT_PORT   GPIOD
#define GATEIO_OPEN_LIMIT_PIN    GPIO_Pin_7
#define GATEIO_OPEN_LIMIT_BIT    7u

#define GATEIO_CLOSE_LIMIT_PORT  GPIOB
#define GATEIO_CLOSE_LIMIT_PIN   GPIO_Pin_3
#define GATEIO_CLOSE_LIMIT_BIT   3u

#define GATEIO_ENC_A_PORT        GPIOB
#define GATEIO_ENC_A_PIN         GPIO_Pin_4
#define GATEIO_ENC_A_BIT         4u

#define GATEIO_ENC_B_PORT        GPIOB
#define GATEIO_ENC_B_PIN         GPIO_Pin_0
#define GATEIO_ENC_B_BIT         0u

#define GATEIO_OPEN_LIMIT_LEVEL()   PDin(GATEIO_OPEN_LIMIT_BIT)
#define GATEIO_CLOSE_LIMIT_LEVEL()  PBin(GATEIO_CLOSE_LIMIT_BIT)
#define GATEIO_ENC_A_LEVEL()        PBin(GATEIO_ENC_A_BIT)
#define GATEIO_ENC_B_LEVEL()        PBin(GATEIO_ENC_B_BIT)

/* two equal samples at 10 ms = 20..30 ms of stable level */
#define GATEIO_DEBOUNCE_TICKS    2u

void     GATEIO_Init(void);      /* call once, before DCMOTOR_Init() */
void     GATEIO_Tick(void);      /* call once per 10 ms tick */

/* Debounced "at limit" flags: 1 = limit tripped (active low on the pin). */
uint8_t  GATEIO_IsOpenLimit(void);
uint8_t  GATEIO_IsCloseLimit(void);

/* Cumulative falling edges on A and B, saturates at 65535. */
uint16_t GATEIO_GetPulseCount(void);

/* Falling edges since the previous call; reading clears the accumulator. */
uint8_t  GATEIO_PopPulseDelta(void);

#endif