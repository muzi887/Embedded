#include "board_init.h"
#include "delay.h"
#include "usart1.h"
#include "usart2.h"
#include "usart3.h"
#include "uart4.h"
#include "uart5.h"
#include "iic.h"
#include "timer.h"
#include "bsp_hc595.h"
#include "ext_io.h"
#include "eeprom.h"
#include "data_handler.h"
#include "led.h"
#include "4G.h"
#include "rly.h"
#include "ds1302.h"

void board_init(void)
{
	delay_init(168);
	delay_ms(500);
	Led_Init();
	uart1_init(115200);    //pc
	
	RS485_Init(9600);     //uart2
	uart3_init(115200);   //4g
	uart4_init(9600);
	uart5_init(9600);	 //读头

	//BeepNum = 2;
	Beep_Init();
	TIM2_Int_Init(5000 - 1, 8400 - 1);
	TIM1_Int_Init(5000 - 1, 8400 - 1);
	TIM3_Int_Init(5000 - 1, 8400 - 1);
	TIM4_Int_Init(100 - 1, 8400 - 1);
	TIM5_Init();
	
	ds1302_init();
	Rly_Init();
	Rly_Set(0,0);
	GM4G_Init(); 
	
	
	delay_ms(10);
  AT24C32_I2C_Init();
	ReadKey();
	//(void)ParseDeviceModeRlyTimes(); /* g_device_mode → rly_1_time / rly_2_time */

	/* HC595 / 扩展 IO：启动流程跑到此处后再延时 3s，再上电初始化 */
	delay_ms(3000);
	HC595_Init();
	ExtIO_Init();
	Bsp_SetBeep(1);

}
