#include "main.h"
#include "led.h"
#include "delay.h"
#include "key.h"
#include "usart.h"
#include "timer.h"
#include "adc.h"
#include "4g.h"
#include "wdg.h"
#include "dc_motor.h"
#include "switch.h"
#include "gprs.h"
#include "uart_queue.h"
#include "property_report.h"
#include "24cxx.h"
#include "reg.h"
#include "mymalloc.h"
#include "ds1302.h"
#include "display.h"
#include "485.h"
#include "uart3_at_sequence.h"
#include "sbf20.h"
#include "sbf20_app.h"

int main(void)
{
  delay_init();         // 延时函数初始化
  NVIC_Configuration(); // 配置NVIC中断分组2:2位抢占优先级，2位响应优先级
  IWDG_Init(4, 1990);   // 3180MS 看门狗初始化
  IWDG_Feed();          // 喂狗

  uart1_init(460800); // 与PC通讯
  uart2_init(115200); // 4G
  uart3_init(9600); // LANYA
  uart4_init(9600);   // RS485
  sbf20_app_init();

  LED_Init();     // LED端口初始化
  KEY_Init();     // 按键初始化
  dcmotor_init(); // 继电器IO初始化
  ADC1_Init();    // ADC
  Lcd_Init();     // LCD12864
  mem_init();     // 内存管理
  GM4G_Init();    // 4g
  UartTx_Queue_Init();
  UartRx_Queue_Init();
  SWITCH_Init(); // 开关量

  AT24CXX_Init(); // 初始化iic
  para_read();    // 读取eeprom
  initReg();      // 设定初始参数

  ds1302_init();           // 时钟初始化
  TIM2_Int_Init(7199, 50); // 5ms ADC采集
  TIM4_Int_Init(100 - 1, 8400 - 1);
  printf("sys init OK!");

  while (1)
  {
    IWDG_Feed(); // 喂狗
    sbf20_poll();//蓝牙

//---------------------------------------------------
// strncpy(g_mqtt_productKey, "...", sizeof(g_mqtt_productKey) - 1);
// strncpy(g_mqtt_deviceKey, "...", sizeof(g_mqtt_deviceKey) - 1);
// strncpy(g_mqtt_deviceSecret, "...", sizeof(g_mqtt_deviceSecret) - 1);
// strncpy(g_mqtt_addr, "x.x.x.x:1883", sizeof(g_mqtt_addr) - 1);
// uart3_at_sequence_request_set(1);

//     if (uart3_at_sequence_main_loop_tick())
//       continue;
//---------------------------------------------------

    KEY_Process();            // KEY
    AD_Process();             // 采集
    Dcmotor_Process();        // 电机
    Lcd_Process();            // 液晶显示
    PropertyReport_Process(); /* 处理中断中置位的属性上报，避免 enquiry_para_to_json 在中断中执行导致栈溢出 */
    Gprs_Process();           // 4G
    Eeprom_Wpara();           // 写EEPROM
    manage485();              // rs485
  }
}
