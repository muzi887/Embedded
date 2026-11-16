#ifndef __LCD12864_H
#define __LCD12864_H

/* LCD12864 (ST7920) driver layer.
 * Scope: hardware access only - GPIO, serial timing, command set,
 *        DDRAM/GDRAM clear, ASCII string display, backlight on/off.
 * Excluded: pages, parameter editing, key dispatch, backlight timeout,
 *           icons / progress bar. Those belong to an upper layer.
 */
#include "sys.h"   /* uint8_t, stm32f10x.h, bit-band macros */

void LCD12864_Init(void);
void LCD12864_Clear(void);
void LCD12864_ClearGDRAM(void);
void LCD12864_ShowString(uint8_t row, const char *str);
void LCD12864_ShowHanzi(uint8_t row, const uint8_t *buf, uint8_t count);
void LCD12864_DrawRow(uint8_t y, const uint8_t *buf);
void LCD12864_DrawStrip(uint8_t x, const uint8_t *buf);
void LCD12864_SetBacklight(uint8_t on);

#endif
