#include "lcd_backlight.h"
#include "lcd12864.h"

/* 10 ms per tick -> 30000/10 = 3000 ticks for 30 s.
 * uint16_t holds 3000 fine; saturate at the limit so it never wraps. */
#define BL_TICKS  (LCD_BACKLIGHT_TIMEOUT_MS / 10u)   /* 3000u */

static uint16_t s_idle_ticks;   /* 10 ms ticks since last key activity */
static uint8_t  s_is_on;        /* mirror of the physical backlight state */

void LCD_BACKLIGHT_Init(void)
{
    s_idle_ticks = 0u;
    s_is_on = 1u;
    LCD12864_SetBacklight(1u);
}

void LCD_BACKLIGHT_Kick(void)
{
    s_idle_ticks = 0u;
    if (s_is_on == 0u)          /* only touch the pin when state changes */
    {
        s_is_on = 1u;
        LCD12864_SetBacklight(1u);
    }
}

void LCD_BACKLIGHT_Tick(void)
{
    if (s_is_on == 0u)
        return;                 /* already off: nothing to count */

    if (s_idle_ticks < BL_TICKS)
        s_idle_ticks++;

    if (s_idle_ticks >= BL_TICKS)
    {
        s_is_on = 0u;
        LCD12864_SetBacklight(0u);
    }
}