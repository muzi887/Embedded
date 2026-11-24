#include "stm32f10x.h"
#include "sys.h"    // NVIC_Configuration（中断分组）、PDout 位带输出宏
#include "delay.h" 
#include "led.h"   
#include "usart.h"

int main(void)
{
  u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试串口：PA9 TX，115200 8N1，仅发送 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮，led.c 内部基准态） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  while (1)
  {
    delay_ms(500);            /* 半秒节拍 */
    LED_ERR = !LED_ERR;      /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
    blink_cnt++;             /* 心跳计数递增 */
    printf("skeleton alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
  }
}
