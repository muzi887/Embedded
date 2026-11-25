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
#include "wdg.h"
#include "Display.h"
#include "ProgressBar.h" 

static u32 blink_cnt = 0;     /* PD14 心跳计数 */
static u32 s_loop_cnt = 0;    /* 主循环心跳计数 */

/* 分片等待：长延时切成 ≤1s 片，每片喂一次狗（单段 3s 必超 2.12s 最坏窗口） */
static void s_WaitFeed(u16 ms)
{
    while (ms >= 1000u) { delay_ms(1000); WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; ms -= 1000u; }
    if (ms > 0u)        { delay_ms(ms);   WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; }
}

int main(void)
{
  u8 percent;            /* 进度条步进值 */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试口：printf + PC 命令接收 */
  uart2_init(115200);    /* 4G 口 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  AT24CXX_Init();        /* I²C 引脚就绪 */
  WDG_Init();            /* 开 IWDG；内部记录并清除上一次复位来源 */
  printf("wdg test start, window=%ums, reset cause: %u\r\n",
          (unsigned int)WDG_WINDOW_MIN_MS, (unsigned int)g_wdg_reset_cause );
  Gprs4G_Init();         /* 电平/EXTI 配置 + PA1 复位脉冲：模块从这里点火 */
  LCD12864_Init();       /* 一步到位：GPIO + 1.5s 上电稳定期（内含 3 处 WDG_Feed）
                              + 指令序列 + 清 DDRAM/GDRAM + 背光点亮 */

  while (1)
  {
    s_loop_cnt++;
    printf("lcd round %u\r\n", (unsigned int)s_loop_cnt);

    delay_ms(500);           /* 半秒节拍：喂狗间隔 0.5s << 最坏窗口 2.12s */
    WDG_Feed();              /* 【验证复位时注释本行】停喂后约 3.2s（40kHz 标称）复位 */
    // LED_ERR = !LED_ERR;      /* 灯光心跳 */
    // blink_cnt++;             /* 心跳计数递增 */
    printf("4G alive, cnt=%u, LINK=%d, online=%u\r\n", 
        (unsigned int)blink_cnt, (int)GPRS_LINK_STATUS, (unsigned int)g_gprs_online);   

    /* 字符串 */
    Ota_ClearText();                  
    LCD12864_ClearGDRAM();
    Lcd_ShowWaitNet();                      /* 第1行：wait net（第3字符位起） */
    LCD12864_ShowString(1, "    LCD OK");   /* 第2行：LCD OK */
    printf("act1 string ok\r\n");
    s_WaitFeed(3000);

    /* OTA 状态页 */
    Ota_ShowConnecting();                   /* 在线升级 / 连接服务器中 */
    s_WaitFeed(3000);
    Ota_ShowRequestVer();                   /* 连接成功 / 申请最新固件 */
    s_WaitFeed(3000);

    /* 进度条 0 -> 100，步进 5% */
    ProgressBar_Setup();                    /* 清图形层 + 画底框 + 0% */
    for (percent = 0; percent <= 100; percent += 5)
    {
        ProgressBar_Update(percent);
        s_WaitFeed(500);
    }
    printf("act3 progress ok\r\n");
    s_WaitFeed(2000);

    /* 错误页：固定展示两种（SIZE=6 排在 VERIFY=5 之后，升序循环永远进不去，
       故直接显式调用——正式流程由 BOOT/GPRS 登记、main 显示具体码） */
    Ota_ShowError(OTA_ERR_SIZE);            /* 更新出错 + 固件大小错误 */
    s_WaitFeed(3000);
    Ota_ShowError(OTA_ERR_VERIFY);          /* 更新出错 + 写入校验失败 */
    s_WaitFeed(3000);
  }
}
