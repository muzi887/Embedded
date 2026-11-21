#include "led.h"
#include "dcmotor.h"    /* DCMOTOR_GetState / DCMOTOR_GetLastStopReason */
#include "gprs_at.h"    /* gprs_is_ready */
#include "adc.h"

#if (LED_ACTIVE_HIGH != 0)
  #define LED_PIN_ON(p)   GPIO_SetBits(LED_PORT, (p))
  #define LED_PIN_OFF(p)  GPIO_ResetBits(LED_PORT, (p))
#else
  #define LED_PIN_ON(p)   GPIO_ResetBits(LED_PORT, (p))
  #define LED_PIN_OFF(p)  GPIO_SetBits(LED_PORT, (p))
#endif

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LED_ALL_PINS;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;     // 推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    /* all off at boot; if a power-on self-test (all on for ~200 ms) is
     * wanted, do it here before returning -- see §5. */
    LED_PIN_OFF(LED_ALL_PINS);
}

/* Refresh every 10 ms: mirror motor direction + link state + last fault. */
void LED_Tick(void)
{
    uint8_t st = DCMOTOR_GetState();    /* requested state: STOP when idle */

    if (st == MOTOR_UP)        { LED_PIN_ON(LED_UP_PIN);  LED_PIN_OFF(LED_DN_PIN); }
    else if (st == MOTOR_DOWN) { LED_PIN_ON(LED_DN_PIN);  LED_PIN_OFF(LED_UP_PIN); }
    else                       { LED_PIN_OFF(LED_UP_PIN); LED_PIN_OFF(LED_DN_PIN); }

    /* link lamp: on only in MQTT NORMAL state */
    if (gprs_is_ready() != 0u) LED_PIN_ON(LED_LINE_PIN);
    else                       LED_PIN_OFF(LED_LINE_PIN);

    /* fault lamp: stall / timeout / interlock are faults;
     * reaching a limit (LIMIT_OPEN / LIMIT_CLOSE) is normal and must NOT
     * light the fault lamp. ADC link lost (chain stopped or sensor open
     * wire) lights it directly: a link dying while idle never produces a
     * stop_reason, so without this the fault would be invisible. */
    if ((ADC_IsLinkLost() != 0u) ||
        (DCMOTOR_GetLastStopReason() == MOTOR_STOP_STALL) ||
        (DCMOTOR_GetLastStopReason() == MOTOR_STOP_TIMEOUT) ||
        (DCMOTOR_GetLastStopReason() == MOTOR_STOP_INTERLOCK))
    {
        LED_PIN_ON(LED_ERR_PIN);
    }
    else
    {
        LED_PIN_OFF(LED_ERR_PIN);
    }
}