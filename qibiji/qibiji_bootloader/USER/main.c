#include "stm32f10x.h"
#include "sys.h"    // NVIC_Configuration（中断分组）、PDout 位带输出宏
#include "delay.h" 
#include "led.h"   
#include "usart.h"
#include "stmflash.h"
#include "flash_layout.h"
#include "24cxx.h"
#include "reg.h"

#define TEST_ADDR_BYTE  16      /* 单字节测试地址（APP 参数区内） */
#define TEST_ADDR_LEN   24      /* 定长测试起始地址（连续 8 字节，仍在 APP 参数区内） */
#define TEST_LEN        8       /* 定长测试的字节数 */

static u8  s_block_buf[OTA_INFOCB_SIZE];         /* 状态块整块回读缓冲：验证落盘内容 */

int main(void)
{
  u32 flag_save;        /* 测试前 OTA_flag 原值（恢复用） */
  u8  update_save;      /* 测试前 UPDATE_flag 原值（恢复用） */
  u32 flag_back;        /* 回读出的 OTA_flag */
  u8  retry_back;       /* 回读出的自愈计数 */
  u8  gate_back;        /* 回读出的 UPDATE_flag */
  u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

  delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
  NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
  uart1_init(115200);    /* 调试串口：PA9 TX，115200 8N1，仅发送 */
  LED_Init();            /* 四路 LED 引脚初始化（上电全亮，led.c 内部基准态） */
  LED_CLOSE();           /* 全部熄灭，建立干净基准 */

  AT24CXX_Init();     /* I²C 引脚就绪 */

  printf("reg test start\r\n");

  /* ① 上电装载：老芯片走 OK 分支打印状态块与 MQTT 键；
      新芯片会先打 "set default para"（播种）再读出——两种打印都算通过 */
  Reg_ReadOtaInfo();

  flag_save  = OTA_Info.OTA_flag;     /* 存测试前原值 */
  update_save = OTA_Info.UPDATE_flag;

  /* ② 标志分字段写读：写 A2 → 整块回读 24C02 校验 → 恢复原值 */
  if (OtaWriteFlag(OTA_A2_FLAG) == AT24CXX_OK)
  {
      AT24CXX_Read(OTA_ADDR, s_block_buf, OTA_INFOCB_SIZE);   /* 从 24C02 整块读回（非 RAM 副本） */
      flag_back = (u32)s_block_buf[0]
                | ((u32)s_block_buf[1] << 8)
                | ((u32)s_block_buf[2] << 16)
                | ((u32)s_block_buf[3] << 24);                /* 按小端拼回 u32 */
      printf("flag write-back: %08X %s\r\n", (unsigned int)flag_back,
              (flag_back == OTA_A2_FLAG) ? "PASS" : "FAIL");
      OtaWriteFlag(flag_save);   /* 恢复测试前标志（可能是 A1，也可能本就挂着 A2 待安装） */
  }
  else
  {
      printf("flag write FAIL\r\n");
  }

  /* ③ 自愈计数往返：写 2 → 读回应为 2 → 清零复原 */
  OtaRetryCountSet(2);
  retry_back = OtaRetryCount();
  printf("retry roundtrip: %u %s\r\n", (unsigned int)retry_back,
          (retry_back == 2) ? "PASS" : "FAIL");
  OtaRetryCountSet(0);

  /* ④ 升级闸往返：置 0 → 回读 24C02 校验 → 恢复原值（不用 paraToEeprom 整块恢复，
      避免覆盖真实设备的版本记录与 OTA_size） */
  OtaCloseUpdateGate();
  AT24CXX_Read(OTA_ADDR + OTA_OFF_UPDATE, &gate_back, 1);
  printf("gate write-back: %u %s\r\n", (unsigned int)gate_back,
          (gate_back == 0) ? "PASS" : "FAIL");
  OTA_Info.UPDATE_flag = update_save;   /* RAM 副本同步恢复 */
  AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (u8 *)&OTA_Info.UPDATE_flag, 1);

  printf("reg test done\r\n");

  while (1)
  {
    delay_ms(500);            /* 半秒节拍 */
    LED_ERR = !LED_ERR;      /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
    blink_cnt++;             /* 心跳计数递增 */
    printf("reg alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
  }
}
