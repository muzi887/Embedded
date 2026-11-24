#include "stm32f10x.h"
#include "sys.h"    // NVIC_Configuration（中断分组）、PDout 位带输出宏
#include "delay.h" 
#include "led.h"   
#include "usart.h"
#include "stmflash.h"
#include "flash_layout.h"
#include "24cxx.h"

#define TEST_ADDR_BYTE  16      /* 单字节测试地址（APP 参数区内） */
#define TEST_ADDR_LEN   24      /* 定长测试起始地址（连续 8 字节，仍在 APP 参数区内） */
#define TEST_LEN        8       /* 定长测试的字节数 */

static u8 s_save_buf[TEST_LEN];             /* 测试区原值暂存：跑完恢复，不留痕 */
static u8 s_wbuf[TEST_LEN];         /* 待写测试图案 */
static u8 s_rbuf[TEST_LEN];         /* 读回数据 */

int main(void)
{
  
  u32 i;               /* 页内半字循环计数 */
  u32 err_cnt;         /* 比对不一致的半字个数，0 = 通过 */
  u8 save_byte;        /* 单字节测试地址的原值 */
  u8 chk;              /* AT24CXX_Check 返回值 */
  u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试串口：PA9 TX，115200 8N1，仅发送 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮，led.c 内部基准态） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  AT24CXX_Init();     /* I²C 引脚就绪 */

  /* 验证 1：上电自检与标志管理 */
  chk = AT24CXX_Check();
  if (chk == AT24CXX_OK)          printf("check: OK (marker present)\r\n");
  else if (chk == AT24CXX_FRESH)  printf("check: FRESH (marker written, seeding is REG's job)\r\n");
  else                            printf("check: FAIL (eeprom not responding)\r\n");

  /* 验证 2：单字节写读比对（先存原值，跑完恢复） */
  save_byte = AT24CXX_ReadOneByte(TEST_ADDR_BYTE);
  if (AT24CXX_WriteOneByte(TEST_ADDR_BYTE, 0xA5) == AT24CXX_OK
      && AT24CXX_ReadOneByte(TEST_ADDR_BYTE) == 0xA5)
      printf("byte test PASS\r\n");
  else
      printf("byte test FAIL\r\n");
  AT24CXX_WriteOneByte(TEST_ADDR_BYTE, save_byte);                /* 恢复原值 */

  /* 验证 3：定长写读比对（同样先存原值） */
  AT24CXX_Read(TEST_ADDR_LEN, s_save_buf, TEST_LEN);              /* 存原值 */
  for (i = 0; i < TEST_LEN; i++) s_wbuf[i] = (u8)(i * 3 + 1);     /* 预填测试图案 */
  err_cnt = 0;
  if (AT24CXX_Write(TEST_ADDR_LEN, s_wbuf, TEST_LEN) == AT24CXX_OK)
  {
      AT24CXX_Read(TEST_ADDR_LEN, s_rbuf, TEST_LEN);
      for (i = 0; i < TEST_LEN; i++)
          if (s_rbuf[i] != s_wbuf[i]) err_cnt++;
      if (err_cnt == 0) printf("len test PASS (%u bytes)\r\n", (unsigned int)TEST_LEN);
      else          printf("len test FAIL: err_cnt=%u\r\n", (unsigned int)err_cnt);
  }
  else
      printf("len test FAIL: write err_cntor\r\n");
  AT24CXX_Write(TEST_ADDR_LEN, s_save_buf, TEST_LEN);             /* 恢复原值 */


  while (1)
  {
    delay_ms(500);            /* 半秒节拍 */
    LED_ERR = !LED_ERR;      /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
    blink_cnt++;             /* 心跳计数递增 */
    printf("skeleton alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
  }
}
