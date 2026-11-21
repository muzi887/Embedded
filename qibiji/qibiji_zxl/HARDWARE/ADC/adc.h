#ifndef __ADC_H
#define __ADC_H

/* ---------------------------------------------------------------------------
 * ADC current-sense module 
 *
 * Scope: ADC1 + DMA1 circular sampling of PC0 (motor current), PC2 (solar
 *        voltage), PC3 (battery voltage). Converts raw codes to current in
 *        mA, calibrates the zero point at power-up, tracks slow zero drift,
 *        and raises a stall flag when current stays over a threshold.
 *
 * Timing: reuses the existing TIM3 10 ms tick. ADC_Tick() is called once per
 *         10 ms; internally it divides by 2 to get the 20 ms sampling slot. 
 *         No new timer is allocated.
 * 
 * Decoupling: this module never calls the motor code. A stall only sets a
 *             flag; the motor module consumes it with ADC_PopStall().
 *
 * Constraints: C90 (declarations at block start), ASCII-only comments/strings.
 * --------------------------------------------------------------------------- */
#include "sys.h"   /* uint8_t / uint16_t */

#define ADC_SAMPLE_COUNT   256u   /* DMA samples per channel per slot */
#define ADC_CHANNEL_COUNT  3u     /* CH10 current, CH12 solar, CH13 battery */

/* Default stall threshold in mA (matches the original project's 3500 mA). */
#define ADC_STALL_MA_DEFAULT  3500u

void     ADC1_Init(void);
void     ADC_Tick(void);                    /* call once per 10 ms system tick */
uint8_t  ADC_CurrentIsReady(void);          /* zero point calibrated? */
uint16_t ADC_GetCurrentMa(void);            /* 500 ms smoothed current, mA */
uint8_t  ADC_IsLinkLost(void);              /* 1 = chain stopped or sensor open */
uint16_t ADC_GetSampleMa(void);             /* latest 20 ms sample, mA */
uint16_t ADC_GetSolarMv(void);              /* solar voltage, mV */
uint16_t ADC_GetBatMv(void);                /* battery voltage, mV */
uint8_t  ADC_PopStall(void);                /* consume stall flag: 1 once, then 0 */
void     ADC_SetStallThresholdMa(uint16_t ma);
/* 电流零点跟踪的抑制标志,标定零点只能在电机停着的时候做 */
void     ADC_SetMotorRunning(uint8_t running);  /* freeze zero tracking while running */

/* DMA target buffer (written by DMA in firmware; writable by test harness). */
extern volatile uint16_t g_adc_buf[ADC_SAMPLE_COUNT][ADC_CHANNEL_COUNT];

#endif