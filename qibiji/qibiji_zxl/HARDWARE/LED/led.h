#ifndef __LED_H
#define __LED_H

/* ---------------------------------------------------------------------------
 * Board indicator LEDs
 *
 * Owns four indicator LEDs on the gate-control board:
 *   PD11 = UP    motor driving open
 *   PD12 = DN    motor driving close
 *   PD13 = LINE  4G link online (MQTT NORMAL)
 *   PD14 = ERR   fault (stall / timeout / interlock not calibrated)
 *
 * Responsibilities: clock/GPIO init once at boot; refresh all four outputs
 *   every 10 ms tick by querying DCMOTOR / GPRS state.
 *
 * Constraints: C90, ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"   /* GPIO / RCC */

/* ---- pin map (all on GPIOD, push-pull 50 MHz) ---- */
#define LED_PORT        GPIOD
#define LED_UP_PIN      GPIO_Pin_11
#define LED_DN_PIN      GPIO_Pin_12
#define LED_LINE_PIN    GPIO_Pin_13
#define LED_ERR_PIN     GPIO_Pin_14
#define LED_ALL_PINS    (LED_UP_PIN | LED_DN_PIN | LED_LINE_PIN | LED_ERR_PIN)

/* Set to 0 if the board LEDs are wired active-low (common anode). */
#define LED_ACTIVE_HIGH   1u

void LED_Init(void);   /* call once, after WDG_Init(), with the other GPIO inits */
void LED_Tick(void);   /* call once per 10 ms tick (inside Tick_10ms_Take()) */

#endif