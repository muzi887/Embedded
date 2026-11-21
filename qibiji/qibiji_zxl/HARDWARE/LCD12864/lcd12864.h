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

/* Fill a rectangular band of whole 16-pixel word columns with a constant byte.
 *   y      : first pixel row, 0..63
 *   rows   : band height in pixel rows
 *   x_word : first 16-pixel word column, 0..7 (within the chosen half)
 *   words  : number of word columns, 1..8
 *   value  : byte written to every word (0xFF = fill, 0x00 = clear)
 * 说明：ST7920 一个垂直地址管两行，h=0..7 → 像素行 v，h=8..15 → 像素行 v+32；
 *       本函数按像素行入参，内部自动加上半区偏移。 */
void LCD12864_FillBlock(uint8_t y, uint8_t rows,
                        uint8_t x_word, uint8_t words, uint8_t value);

#endif
