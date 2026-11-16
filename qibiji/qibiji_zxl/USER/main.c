#include "main.h"
#include "key.h"
#include "timer.h"
#include "debug_uart.h"
#include "delay.h"
#include "lcd12864.h"
#include "lcd_pages.h"

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
}

int main(void)
{
	key_code_t key;

	NVIC_Configuration(); // 配置NVIC中断分组2:2位抢占优先级，2位响应优先级
	DEBUG_UART_Init();      /* USART1 115200 8N1，PA9 = TX，只发不收 */
	KEY_Init();
	delay_init();
	LCD12864_Init();
	LCD_BACKLIGHT_Init();   /* 点亮背光、启动 30s 计时 */
	LCD_PAGES_Init(); 
	TIM3_10ms_Init();

	/* 上电提示：看到这两行说明程序已经跑起来、时钟和串口都对。 */
	DEBUG_UART_SendString("qibiji_zxl KEY test ready\r\n");
	DEBUG_UART_SendString("USART1 115200 8N1, PA9=TX\r\n");

	while (1)
	{
		/* 1) 固定节拍：每 10ms 采样一次，去抖时间与主循环速度无关；消费式，取走即清 */
		if (Tick_10ms_Take())
		{
			KEY_Tick();		
    	LCD_BACKLIGHT_Tick(); 
		}
		
		/* 2) 取走一个事件，取走即清；没有事件就什么都不做 */
		key = KEY_Pop();
		if (key != KEY_NONE)
			APP_DispatchKey(key);
	}
}
