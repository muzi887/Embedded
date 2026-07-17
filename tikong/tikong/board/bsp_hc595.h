#ifndef __BSP_HC595_H
#define __BSP_HC595_H

#include "stm32f4xx.h"
#include <stdint.h>

/* ===================== User Hardware Config ===================== */
/* Adjust pins according to your board wiring. */

#define HC595_SER_PORT GPIOD
#define HC595_SER_PIN GPIO_Pin_7

#define HC595_SRCLK_PORT GPIOD
#define HC595_SRCLK_PIN GPIO_Pin_5

#define HC595_RCLK_PORT GPIOD
#define HC595_RCLK_PIN GPIO_Pin_6

/* ZD / OE is active low: enable 74HC595 outputs. */
#define HC595_OE_PORT GPIOD
#define HC595_OE_PIN GPIO_Pin_4

#define HC595_GPIOD_CLK() RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE)

/* ===================== GPIO Macros (StdPeriph style) ===================== */

/* SER */
#define SER_HIGH() (HC595_SER_PORT->BSRRL = HC595_SER_PIN)
#define SER_LOW() (HC595_SER_PORT->BSRRH = HC595_SER_PIN)

/* SRCLK */
#define SRCLK_HIGH() (HC595_SRCLK_PORT->BSRRL = HC595_SRCLK_PIN)
#define SRCLK_LOW() (HC595_SRCLK_PORT->BSRRH = HC595_SRCLK_PIN)

/* RCLK */
#define RCLK_HIGH() (HC595_RCLK_PORT->BSRRL = HC595_RCLK_PIN)
#define RCLK_LOW() (HC595_RCLK_PORT->BSRRH = HC595_RCLK_PIN)

/* OE / ZD active-low control */
#define OE_ENABLE() (HC595_OE_PORT->BSRRH = HC595_OE_PIN)  /* Enable output */
#define OE_DISABLE() (HC595_OE_PORT->BSRRL = HC595_OE_PIN) /* Disable output */

/* ===================== API ===================== */

void HC595_Init(void);
void HC595_OutputEnable(uint8_t en);
void HC595_ShiftByte(uint8_t dat);
void HC595_Latch(void);
void HC595_WriteBytes(uint8_t *buf, uint8_t len);

#endif
