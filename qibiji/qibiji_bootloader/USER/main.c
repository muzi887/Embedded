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
#include "mymalloc.h"
#include "cJSON.h"
#include "ds1302.h"
#include <string.h>

static u32 blink_cnt = 0;     /* PD14 心跳计数 */
static u32 s_loop_cnt = 0;    /* 主循环心跳计数 */

// /* 分片等待：长延时切成 ≤1s 片，每片喂一次狗（单段 3s 必超 2.12s 最坏窗口） */
// static void s_WaitFeed(u16 ms)
// {
//     while (ms >= 1000u) { delay_ms(1000); WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; ms -= 1000u; }
//     if (ms > 0u)        { delay_ms(ms);   WDG_Feed(); LED_ERR = !LED_ERR; blink_cnt++; }
// }

int main(void)
{
  // u8 percent;            /* 进度条步进值 */
  u8    *p1, *p2, *p3;
  u8     i;
  char  *printed;        /* cJSON 打印缓冲（池内存，>256B 时走 realloc 修正路径） */
  cJSON *root, *parsed, *item;
  char   t[7];           /* DS1302 日历：秒/分/时/日/月/周/年（十进制） */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试口：printf + PC 命令接收 */
  uart2_init(115200);    /* 4G 口 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  printf("leaf test start\r\n");

  AT24CXX_Init();        /* I²C 引脚就绪 */
  WDG_Init();            /* 开 IWDG；内部记录并清除上一次复位来源 */
  Gprs4G_Init();         /* 电平/EXTI 配置 + PA1 复位脉冲：模块从这里点火 */
  LCD12864_Init();       /* 一步到位：GPIO + 1.5s 上电稳定期（内含 3 处 WDG_Feed）
                              + 指令序列 + 清 DDRAM/GDRAM + 背光点亮 */

  /* ① 内存池：首次 mymalloc 触发自动 init（生产路径同款），分配/写满/读回 */
  p1 = mymalloc(100);
  if (p1 != NULL)
  {
      for (i = 0; i < 100; i++) p1[i] = i;          /* 写满 100 字节 */
      for (i = 0; i < 100; i++)
          if (p1[i] != i) break;
      printf("pool alloc/write: %s\r\n", (i == 100) ? "PASS" : "FAIL");
      myfree(p1);
  }
  else printf("pool alloc FAIL\r\n");

  /* 释放后再分配同尺寸 → 应复用刚释放的块（free 生效的证据） */
  p2 = mymalloc(100);
  printf("pool free/reuse: %s\r\n", (p2 == p1) ? "PASS" : "FAIL");
  p3 = mymalloc(MEM1_MAX_SIZE + 1);                 /* 超池请求 → NULL */
  printf("pool overflow:  %s\r\n", (p3 == NULL) ? "PASS" : "FAIL");

  /* ② myrealloc：扩容保内容 */
  p1 = myrealloc(p2, 300); /* resize: 100~10240 */
  if (p1 != NULL && p1[99] == 99) printf("pool realloc:   PASS\r\n"); /* 新块的第 100 个字节 */
  else                            printf("pool realloc:   FAIL\r\n");
  myfree(p1);

  /* ③ cJSON：组包 → 打印（>256B）→ 解析回读 → 释放 */
  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "deviceKey", "dev001");
  cJSON_AddStringToObject(root, "productKey", "pk001");
  for (i = 0; i < 20; i++)                          /* 300 字节填充字段：逼出 realloc */
  {
      char tag[8];
      sprintf(tag, "pad%02u", (unsigned int)i);
      cJSON_AddStringToObject(root, tag, "0123456789012345");   /* 每项约 40B */
  }
  printed = cJSON_PrintUnformatted(root);           /* >256B → internal_realloc=myrealloc */
  if (printed != NULL)
  {
      printf("cjson print: %u bytes\r\n", (unsigned int)strlen(printed));
      parsed = cJSON_Parse(printed);                /* 解析路径（allocate） */
      item = (parsed != NULL) ? cJSON_GetObjectItem(parsed, "deviceKey") : NULL;
      printf("cjson roundtrip: %s\r\n",
              (item != NULL && strcmp(item->valuestring, "dev001") == 0) ? "PASS" : "FAIL");
      cJSON_Delete(parsed);
      cJSON_free(printed);                          /* 打印缓冲经钩子走 myfree，不能 free() */
  }
  else printf("cjson print FAIL\r\n");
  cJSON_Delete(root);

  /* ④ DS1302：读 7 字节日历打印（十进制，目检合法性；全 0 = RTC 从未写过，属正常） */
  ds1302_init();
  ds1302_dataread(t);
  printf("rtc: 20%02d-%02d-%02d %02d:%02d:%02d week%d\r\n",
          t[6], t[4], t[3], t[2], t[1], t[0], t[5]);

  while (1)
  {
    s_loop_cnt++;
    printf("lcd round %u\r\n", (unsigned int)s_loop_cnt);

    delay_ms(500);           /* 半秒节拍：喂狗间隔 0.5s << 最坏窗口 2.12s */
    // WDG_Feed();              /* 【验证复位时注释本行】停喂后约 3.2s（40kHz 标称）复位 */
    LED_ERR = !LED_ERR;      /* 灯光心跳 */
    blink_cnt++;             /* 心跳计数递增 */
    printf("leaf alive, cnt=%u, LINK=%d, online=%u\r\n", 
        (unsigned int)blink_cnt, (int)GPRS_LINK_STATUS, (unsigned int)g_gprs_online);   

    // /* 字符串 */
    // Ota_ClearText();                  
    // LCD12864_ClearGDRAM();
    // Lcd_ShowWaitNet();                      /* 第1行：wait net（第3字符位起） */
    // LCD12864_ShowString(1, "    LCD OK");   /* 第2行：LCD OK */
    // printf("act1 string ok\r\n");
    // s_WaitFeed(3000);

    // /* OTA 状态页 */
    // Ota_ShowConnecting();                   /* 在线升级 / 连接服务器中 */
    // s_WaitFeed(3000);
    // Ota_ShowRequestVer();                   /* 连接成功 / 申请最新固件 */
    // s_WaitFeed(3000);

    // /* 进度条 0 -> 100，步进 5% */
    // ProgressBar_Setup();                    /* 清图形层 + 画底框 + 0% */
    // for (percent = 0; percent <= 100; percent += 5)
    // {
    //     ProgressBar_Update(percent);
    //     s_WaitFeed(500);
    // }
    // printf("act3 progress ok\r\n");
    // s_WaitFeed(2000);

    // /* 错误页：固定展示两种（SIZE=6 排在 VERIFY=5 之后，升序循环永远进不去，
    //    故直接显式调用——正式流程由 BOOT/GPRS 登记、main 显示具体码） */
    // Ota_ShowError(OTA_ERR_SIZE);            /* 更新出错 + 固件大小错误 */
    // s_WaitFeed(3000);
    // Ota_ShowError(OTA_ERR_VERIFY);          /* 更新出错 + 写入校验失败 */
    // s_WaitFeed(3000);
  }
}
