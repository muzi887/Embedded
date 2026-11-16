#ifndef __LCD_BACKLIGHT_H
#define __LCD_BACKLIGHT_H

/* LCD backlight auto-off module.
 * Scope: light the backlight on power-up / any key, turn it off after
 *        30 s of no key activity. Uses the existing 10 ms system tick;
 *        no new timer is allocated. Hardware on/off goes through the
 *        driver's LCD12864_SetBacklight(). */
#include "sys.h"   /* uint8_t */

/* Timeout in milliseconds. 30 s, matched to the original project's
 * BLight_Ctrl() (100 x 300 ms). */
#define LCD_BACKLIGHT_TIMEOUT_MS  30000u

void LCD_BACKLIGHT_Init(void);  /* power-up: light up, reset idle timer */
void LCD_BACKLIGHT_Tick(void);  /* call once per 10 ms system tick       */
void LCD_BACKLIGHT_Kick(void);  /* call on any key: light up + reset     */

#endif