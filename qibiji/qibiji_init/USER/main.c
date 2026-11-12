/*
 * qibiji_init - STM32 configuration firmware (no OTA / no bootloader jump).
 *
 * USART1 115200 8N1 : PC / Qt upper computer, SBF-20 binary frames + JSON
 * USART2 115200 8N1 : WH-LTE-7S1 4G module (AT commands / MQTT payload)
 * AT24C02           : gate parameters (0x0004-0x000C) + OTA info (200-243)
 *                     legacy 4G/MQTT slots (64-191) are erased at boot and
 *                     left unassigned
 *
 * The MCU is always the SBF-20 responder; the PC application drives every
 * session (read / write / at). The 4G/MQTT parameters are no longer stored
 * here: the PC tool pushes them to the module with AT commands.
 */

#include "main.h"
#include "led.h"
#include "delay.h"
#include "usart.h"
#include "wdg.h"
#include "24cxx.h"
#include "reg.h"
#include "4g.h"
#include "timer.h"
#include "sbf20.h"
#include "sbf20_app.h"
#include "mymalloc.h"
#include <string.h>

/* Set to 1 to run the module self test once at boot (bring-up aid). */
#define MODULE_SELFTEST 0u

#if MODULE_SELFTEST
/*
 * Bring-up self test: drive the 4G module directly from the MCU and print every
 * reply on USART1, so the USART2 <-> module link can be verified without
 * depending on the PC -> MCU direction.
 */
static void module_at_selftest(void)
{
    static const char *const cmds[] = {
        "usr.cn#AT+SOCKRSTIM=30\r\n",
        "usr.cn#AT+SOCKRSNUM=65535\r\n",
        "usr.cn#AT+S\r\n",
        "usr.cn#AT+SOCKRSTIM?\r\n",
        "usr.cn#AT+SOCKRSNUM?\r\n"
    };
    u8 i;
    u8 attempt;
    u8 ok;
    u16 seen;
    u16 got;
    u32 t0;
    u32 now;
    u32 seen_ms;
    const char *acc;

    printf("\r\n===== USART2 <-> 4G module self test =====\r\n");

    delay_ms(12000); /* let the module finish booting before the first command */

    for (i = 0u; i < (u8)(sizeof(cmds) / sizeof(cmds[0])); i++)
    {
        ok = 0u;
        for (attempt = 0u; attempt < 4u && !ok; attempt++)
        {
            if (attempt > 0u)
                delay_ms(800);

            uart2_rx_acc_reset();
            Usart2_Send((char *)cmds[i], (u16)strlen(cmds[i]));

            t0 = g_ms_tick;
            seen = 0u;
            seen_ms = t0;

            for (;;)
            {
                IWDG_Feed();
                now = g_ms_tick;
                got = uart2_rx_acc_len();

                if (got != seen)
                {
                    seen = got;
                    seen_ms = now;
                }

                if (got > 0u)
                {
                    acc = uart2_rx_acc_data();
                    if (strstr(acc, "OK\r\n") != NULL || strstr(acc, "ERROR") != NULL)
                        break;
                    if ((u32)(now - seen_ms) >= 100u)
                        break;
                }

                if ((u32)(now - t0) >= 2500u)
                    break;
            }

            ok = (u8)(uart2_rx_acc_len() > 0u);
        }

        printf("[%02u] TX %s", (unsigned)(i + 1u), cmds[i]);
        printf("     RX %s\r\n", uart2_rx_acc_data());
    }

    printf("===== self test done =====\r\n\r\n");
}
#endif /* MODULE_SELFTEST */

int main(void)
{
    delay_init();         /* SysTick based delay_ms / delay_us */
    NVIC_Configuration(); /* 2 bits preemption, 2 bits sub priority */

    uart1_init(115200);   /* upper computer */
    uart2_init(115200);   /* 4G module */
    uart3_init(9600);     /* BLE module (DX-BT24) */

    TIM4_Int_Init(999, 71); /* 1ms tick used by SBF-20 and the AT sequence */

    AT24CXX_Init();
    AT24CXX_Check(); /* writes the 0x55 marker + defaults on a blank EEPROM */

    mem_init(); /* cJSON allocations are hooked to this pool */

    LED_Init();
    sbf20_app_init();
    GM4G_Init();

    IWDG_Init(4, 1990); /* ~3.18s watchdog */

    printf("qibiji_init config firmware ready\r\n");

#if MODULE_SELFTEST
    module_at_selftest();
#endif

    while (1)
    {
        IWDG_Feed();
        sbf20_poll(); /* PC link: frames in, JSON out */
    }
}
