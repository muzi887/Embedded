#include "stm32f10x.h"
#include "sys.h"    // NVIC_Configuration（中断分组）、PDout 位带输出宏
#include "delay.h" 
#include "led.h"   
#include "usart.h"
#include "stmflash.h"
#include "flash_layout.h"

/* 两页验证缓冲放 static：合计 4KB，远超默认栈容量，不能作局部数组 */
static u16 s_wbuf[ST32_PAGE_SIZE / 2];      /* 待写页：预填测试图案 */
static u16 s_rbuf[ST32_PAGE_SIZE / 2];      /* 读回页：与待写页逐半字比对 */

int main(void)
{
  
  u32 i;               /* 页内半字循环计数 */
  u32 err_cnt;         /* 比对不一致的半字个数，0 = 通过 */
  u16 a1_before;       /* 验证前 A1 区首半字快照：证明写 A2 没波及 A1 */
  u16 a1_after;        /* 验证后 A1 区首半字快照 */
  u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试串口：PA9 TX，115200 8N1，仅发送 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮，led.c 内部基准态） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  /* 预填测试图案 */
  for (i = 0; i < (ST32_PAGE_SIZE / 2); i++)
  {
    s_wbuf[i] = (u16)(i * 2 + 1);
  }

  a1_before = Flash_ReadHalfWord(ST32_A1_SADDR);      /* 记录验证前 A1 首半字 */
  printf("flash test start: write A2 page0 @0x%08X\r\n", (unsigned int)ST32_A2_SADDR);

  Flash_Write(ST32_A2_SADDR, s_wbuf, ST32_PAGE_SIZE / 2);     /* 写满一页到 A2 第 0 页：页内含旧数据，走"擦除+重写"路径 */
  Flash_Read(ST32_A2_SADDR, s_rbuf, ST32_PAGE_SIZE / 2);      /* 读回整页 */

  err_cnt = 0;
  for (i = 0; i < (ST32_PAGE_SIZE / 2); i++)
  {
      if (s_rbuf[i] != s_wbuf[i])
      {
          err_cnt++;                              /* 记录不一致个数，不中断，一次看全 */
          if (err_cnt <= 8) printf("  diff @%u: want=%04X got=%04X\r\n", (unsigned int)i, (unsigned int)s_wbuf[i], (unsigned int)s_rbuf[i]);
      }
  }
  a1_after = Flash_ReadHalfWord(ST32_A1_SADDR);       /* 记录验证后 A1 首半字 */

  if (err_cnt == 0 && a1_before == a1_after) printf("flash test PASS (A1 untouched)\r\n");
  else printf("flash test FAIL: err=%u, a1 %04X -> %04X\r\n", (unsigned int)err_cnt, (unsigned int)a1_before, (unsigned int)a1_after);


  while (1)
  {
    delay_ms(500);            /* 半秒节拍 */
    LED_ERR = !LED_ERR;      /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
    blink_cnt++;             /* 心跳计数递增 */
    printf("skeleton alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
  }
}
