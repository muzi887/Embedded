#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"
#include "adc.h"
#include "dcmotor.h"
#include "gprs_at.h"
#include "uart_queue.h"
#include "gprs_4g.h"
#include "gprs_port.h"
#include "gprs_json.h"
#include "gprs.h"
#include "gprs_rtc.h"
#include "dcmotor_param.h"
#include "eeprom_drv.h"
#include "gprs_config_store.h"
#include "wdg.h"
#include "led.h"

/* 调试观察用：最后一次被分发出去的键码，调试器里可直接查看 */
volatile key_code_t g_last_key = KEY_NONE;

/* 键码 -> 文本，仅供调试串口输出。 */
static const char * const s_key_name[] =
{
    "NONE", "UP", "DOWN", "STOP", "SET", "CANCEL"
};

/* 按键事件的唯一出口。
 * 以后接入页面 / 电机 / 背光时，在这里显式调用对应模块，
 * 不要让各模块自己去读一个全局键码变量。 */
static void APP_DispatchKey(key_code_t key)
{
	const char *name;

	g_last_key = key;

	if((key >= KEY_NONE) && (key <= KEY_CANCEL))
		name = s_key_name[key];
	else
		name = "UNKNOWN";

    /* 上板验证用：每分发一个按键事件，就在 PC 串口助手上看到一行 KEY: xxx。
     * 发送是轮询的，几个字节约 1ms，只在主循环里调用。 */
    DEBUG_UART_SendString("KEY: ");
    DEBUG_UART_SendString(name);
    DEBUG_UART_SendString("\r\n");

    LCD_PAGES_Process(key);     /* 页面状态机消费按键事件 */
    LCD_BACKLIGHT_Kick();       /* 任意按键点亮背光 + 重置 30s 计时 */

    /* only on the home page, same gating as the original PageSta==0 */
    if (LCD_PAGES_IsHome())
    {
			if (key == KEY_UP)
				DCMOTOR_Request(MOTOR_UP);
			else if (key == KEY_DOWN)
				DCMOTOR_Request(MOTOR_DOWN);
			else if (key == KEY_STOP)
				DCMOTOR_Request(MOTOR_STOP);
	}
}

int main(void)
{
	key_code_t key;

	NVIC_Configuration();																	/* 配置NVIC中断分组2:2位抢占优先级，2位响应优先级 */
	DEBUG_UART_Init();     																/* USART1 115200 8N1，PA9 = TX，只发不收 */
	WDG_Init();																						/* 3.18s@40kHz，最坏 2.12s */
	WDG_Feed();

	DEBUG_UART_SendString(g_wdg_reset_cause != 0u
	                      ? "[WDG] reset by IWDG\r\n"
	                      : "[WDG] no IWDG reset\r\n");

	KEY_Init();
	LED_Init();
	delay_init();
	LCD12864_Init();
	LCD_BACKLIGHT_Init();   															/* 点亮背光、启动 30s 计时 */
	LCD_PAGES_Init(); 
	ADC1_Init();
	GATEIO_Init();                     										/* hardware layer: GPIO + JTAG + seeds */
	DCMOTOR_Init();                    										/* decision layer: relays off, state reset */
	AT24CXX_Init();
	AT24CXX_Check();             			 										/* 真空白 EEPROM 时播种闸门默认值 */
	dcmotor_param_load();        			 										/* EEPROM → g_gate_maxturns / g_motor_warning_curr_upper */
	gprs_config_load();           												/* MQTT 凭据 → RAM（EEPROM 有效则用，否则用编译期宏） */
	DCMOTOR_SetMaxTurns(g_gate_maxturns);           			/* run calibration or measure */
	ADC_SetStallThresholdMa(g_motor_warning_curr_upper);  /* 堵转阈值推给 adc */
	GPRS_PORT_Init(115200); 					 										/* USART2 to 4G module: check your module's actual baud */
	GPRS_RTC_Init();				 					 										/* DS1302 RTC: bit-bang GPIO, no interrupts */
	GPRS_4G_Init();         					 										/* module reset + LINKA EXTI -- after UART so the boot banner is captured */
	UartTx_Queue_Init();
	UartRx_Queue_Init();
	TIM3_10ms_Init();

	/* 上电提示：看到这两行说明程序已经跑起来、时钟和串口都对。 */
	DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
	DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

	while (1)
	{
		WDG_Feed();                  /* 每轮一次，不随节拍条件跳 */
		
		/* 1) 固定节拍：每 10ms 采样一次，去抖时间与主循环速度无关；消费式，取走即清 */
		if (Tick_10ms_Take())
		{
			KEY_Tick();		
    	LCD_BACKLIGHT_Tick(); 
			ADC_Tick();
			GATEIO_Tick();          /* sample first: limits + encoder edges */
			DCMOTOR_Tick();         /* then consume: states/delta + run time */
			LED_Tick();							/* mirror direction / online / malfunction */
		}
		
		/* 2) 取走一个事件，取走即清；没有事件就什么都不做 */
		key = KEY_Pop();
		if (key != KEY_NONE)
			APP_DispatchKey(key);

		/* calibration trigger: set-page "auto" flag acts like a one-shot */
    if (LCD_PAGES_GetParam(LCD_PARAM_AUTO_FLAG) != 0u)
    {
        LCD_PAGES_SetParam(LCD_PARAM_AUTO_FLAG, 0u);   /* consume flag */
        DCMOTOR_StartCalibration();   /* ignores call if not STOP / ADC not ready */
    }

		LCD_PAGES_Refresh();  			/* 校准状态行 */
		DCMOTOR_Process();          /* 联锁+堵转+限位拦截+边沿执行 */

		/* ---- GPRS 调度：每轮调用，无节拍门控 ---- */
    GPRS_AT_RxPump();                    /* 1. staged frames -> rx queue */
    GPRS_JSON_ParseProcess();         
    GPRS_AT_StateMachine();              /* 3. link state machine */
		NetLiveness_Process();
		GPRS_ClockSync_Process();
    PropertyReport_Process();
    UartTx_Queue_Process(g_sys_ms);      /* 5. tx state machine */
		GPRS_Reset_Process();
	}
}
