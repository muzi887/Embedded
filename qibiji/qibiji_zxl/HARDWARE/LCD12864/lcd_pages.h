#ifndef __LCD_PAGES_H
#define __LCD_PAGES_H

/* LCD pages module (business layer, round 2).
 * Owns: page state machine + the GB2312 text rows drawn on each page,
 *       plus set-page parameter editing (cursor + value edit) and a
 *       per-row RAM cache so only changed rows are re-sent to the LCD. */ 
#include "key.h"  /* key_code_t */

void LCD_PAGES_Init(void);
void LCD_PAGES_Process(key_code_t key);

/* Editable parameters, kept in RAM. EEPROM persistence is NOT in this
 * round - the module only guarantees the in-RAM value. */
typedef enum
{
    LCD_PARAM_GATE_MAX = 0,   /* up-limit position, 4-digit decimal     */
    LCD_PARAM_STALL_010A,     /* stall threshold in 0.1 A, 0..999      */
    LCD_PARAM_AUTO_FLAG,      /* 校准开关 calibration auto switch, 0 = off      */
    LCD_PARAM_COUNT
} lcd_param_t;

uint16_t LCD_PAGES_GetParam(lcd_param_t p);
void     LCD_PAGES_SetParam(lcd_param_t p, uint16_t v);
uint8_t LCD_PAGES_IsHome(void);   /* 1 if current page is home */

#endif