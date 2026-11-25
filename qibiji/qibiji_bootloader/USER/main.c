#include "stm32f10x.h"
#include "sys.h"    // NVIC_Configuration（中断分组）、PDout 位带输出宏
#include "delay.h" 
#include "led.h"   
#include "usart.h"
#include "stmflash.h"
#include "flash_layout.h"
#include "24cxx.h"
#include "reg.h"
#include "4G.h"

static char s_rx_buf[128];    /* 回环数据回显缓冲：每轮最多拷 128 字节，超出部分下一轮继续 */

int main(void)
{
  u16 avail;           /* 环形缓冲当前可读字节数 */
  u16 n;               /* 本轮实际拷出并消费的字节数 */
  u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试口：printf + PC 命令接收 */
  uart2_init(115200);    /* 4G 口 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  AT24CXX_Init();     /* I²C 引脚就绪 */

  printf("usart & 4G test start\r\n");
  printf("reset pulse on PA1, wait module boot ...\r\n");
  Gprs4G_Init();          /* 电平/EXTI 配置 + PA1 复位脉冲：模块从这里点火 */
  delay_ms(3000);         /* 给模块 3 秒开机时间再进轮询 */

  while (1)
  {
    delay_ms(500);            /* 半秒节拍 */
    LED_ERR = !LED_ERR;      /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
    blink_cnt++;             /* 心跳计数递增 */
    printf("4G alive, cnt=%u, LINK=%d, online=%u\r\n", 
        (unsigned int)blink_cnt, (int)GPRS_LINK_STATUS, (unsigned int)gprs_online);   /* 串口心跳：与灯光同拍，肉眼核对两条链路 */

    avail = Usart2_RxAvailable();          /* 回环数据检查 */
    while (avail > 0)
    {
        n = Usart2_RxCopy(s_rx_buf, sizeof(s_rx_buf));   /* 拷出（不消费） */
        Usart2_RxConsume(n);                             /* 消费 */
        s_rx_buf[n] = '\0';                              /* 补字符串结尾，按文本回显 */
        printf("echo %u bytes: %s\r\n", (unsigned int)n, s_rx_buf);
        avail = Usart2_RxAvailable();                    /* 一帧可能超过 128 字节，取到空为止 */
    }
  }
}
