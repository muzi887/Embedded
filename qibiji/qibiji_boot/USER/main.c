#include "led.h"
#include "delay.h"
#include "usart.h"
#include "wdg.h"
#include "dc_motor.h"
#include "24cxx.h"
#include "reg.h"
#include "4g.h"
 #include "main.h"
#include "boot.h"
#include "display.h"
#include "gprs.h"

// OTA主状态机



int pub_version_flag=1;
u8 SYS_state=OTA_STATE_WAIT_NET;
int gprs_return;
extern u8 gprsonlineflag;

/* 交给 A1 应用运行（B 区程序到这里就结束自己的使命）。
   必须用 LOAD_A：它内部会先把 USART1/USART2 deinit 再跳转。
   不要改成 NVIC_SystemReset()：复位后 OTA_flag 仍是 A1、UPDATE_flag 仍是 1，
   B 区会重新进入 OTA 状态机，设备永远进不了应用（复位循环）。 */
static void JumpToApp(void)
{
	LOAD_A(ST32_A1_SADDR);
}

/* OTA 放弃升级：显示错误类型 → 停留 3 秒 → 直接跳 A1 应用（不返回）。
   ① 失败不改升级标志：UPDATE_flag 保持 1，平台/应用下次仍可继续升级。
      "关闭升级标志"只保留给两个成功路径（A2→A1 搬运校验通过、开机自愈成功，见 boot.c）。
   ② 不复位、不写 EEPROM：复位会让 B 区重新进入 OTA 状态机 → 再失败 → 再复位，
      平台长期不可用时设备永远进不了应用（复位循环）；直接跳转没有这个问题。
   ③ 3 秒拆两段并在中间喂狗，避开 3.18s 看门狗窗口，让现场看清错误类型。
   ④ 代价：不再复位 ⇒ GM4G_Init() 的 PA1 复位脉冲不执行，4G 模组不被硬复位（与既有
      "无需升级 → JumpToApp()"路径一致）；USART1/USART2 由 BootLoader_Clear() 复位、
      LCD 由 WRCommandH(0x01) 清屏，其余外设（DMA1_CH6/EXTI9_5/IWDG/SysTick）保持
      B 区状态，交由 A1 应用初始化。
   ⑤ LOAD_A 自带 SP 合法性保护：A1 不可运行时只打印 load progarm A is fail 并返回，
      于是落回 main 的 while(1) OTA 状态机继续联网重试（自愈语义保留）。 */
static void OtaGiveUpAndRunApp(uint8_t kind)
{
	ota_show_error(kind);      /* 先点亮错误类型：第3行"更新出错"、第4行类型 */
	delay_ms(1500);            /* 停留 3 秒，让现场看清原因（分段并喂狗） */
	IWDG_Feed();
	delay_ms(1500);
	LOAD_A(ST32_A1_SADDR);     /* 跳 A1：不关升级标志、不复位；函数不返回 */
}

int main(void)//TODO当这部分启动时断网，应该有相应的处理
{
	u8 crc_fail_count = 0;
	const u8 crc_fail_max = 5;
	u8 rx_err_count = 0;
	const u8 rx_err_max = 5;
	u8 at_tick = 0;
	u8 cid_tick = 0;
	const u8 at_tick_max = 2;   // 2*100ms=200ms
	const u8 cid_tick_max = 2;  // 2*100ms=200ms
	u8 req_timeout_tick = 0;
	u8 req_retry_count = 0;
	const u8 req_timeout_tick_max = 50; // 50 * 100ms = 5s
	const u8 req_retry_max = 5;

	u8 link_loss_count = 0;             // 升级过程中的掉线次数（会话级预算）
	const u8 link_loss_max = 5;         // 掉线 5 次仍失败 → 放弃升级，引导旧程序

	u8 online_tick=0;
	u16 at_wait_tick = 0;              // AT 应答等待计数（模组无应答保护）
	const u16 at_wait_max = 200;       // 200 * 300ms = 60s

	delay_init();		  // 延时函数初始化
	NVIC_Configuration(); // 设置NVIC中断分组2:2位抢占优先级，2位响应优先级

	uart1_init(115200);  // 与PC通信
	uart2_init(115200);  // 4G

	AT24CXX_Init();		   // 初始化iic
	AT24CXX_ReadOTAInfo(); // 读取eeprom
    ds1302_init();  
   //参数初始化，用于存储参数到eeprom ，只能执行一次，然后屏蔽即可
	//paraToEeprom();
	//TODO 若完成新固件更新，应反馈服务器
	BootLoader_Brance(); 

	LED_Init();		 	 // LED端口初始化
	LED_CLOSE();
    Lcd_Gpio_Init();// LCD初始化
	//Lcd_DisplayString("wait net", 0, 2); 
	GM4G_Init();		 // 4g初始化
		
 // clearall();
	
	CLEARGDRAMH(0x00); // 清空显示图形
	
	printf("bootloader is running!\r\n");

	/* 引导/EEPROM 检查阶段（LCD 初始化之前）登记的 OTA 错误：在这里补显示。
	   hold=1 → 停 3 秒再尝试运行应用；hold=0（如 EEPROM 异常，持久性错误）→ 只显示不等。 */
	if (g_ota_lcd_err != 0)
	{
		ota_show_error(g_ota_lcd_err);
		if (g_ota_lcd_hold)
		{
			delay_ms(3000);
			g_ota_lcd_err = 0;
			JumpToApp();
		}
		else
		{
			g_ota_lcd_err = 0;
		}
	}
	
	//若不需要ota，直接跳转
	if(OTA_Info.UPDATE_flag == 1)
	{
		ota_step1();
		printf("OTA is running!\r\n");
	}
	else
	{
		printf("jump to a1!\r\n");
		//BootLoader_Clear();
		JumpToApp();
	}

	IWDG_Init(4, 1990); // 3180MS 看门狗

	//progressbar_Init();
	while (1)
	{
		IWDG_Feed(); // 喂狗
		gprs_return=gprs_rx_datas();

		if(gprsonlineflag==0 && SYS_state!=OTA_STATE_WAIT_NET && SYS_state!=OTA_STATE_AT_OK)
		{
			// 升级过程中掉线：计入会话级预算，重连后从在途 offset 重发请求
			// （见 case OTA_STATE_ONLINE），掉线次数超限才放弃并引导旧程序
			if(SYS_state==OTA_STATE_SEND_VERSION || SYS_state==OTA_STATE_HANDLE_REPLY)
			{
				link_loss_count++;
				printf("link lost during update, retry %d/%d\r\n", link_loss_count, link_loss_max);
				if(link_loss_count>=link_loss_max)
				{
					ota_step5();
					printf("link lost too many times, boot old program\r\n");
					OtaGiveUpAndRunApp(OTA_ERR_LINK);
				}
			}
			SYS_state = OTA_STATE_WAIT_NET;
			req_timeout_tick = 0;
			req_retry_count = 0;
			rx_err_count = 0;
			continue;
		}
	
		if(SYS_state==OTA_STATE_WAIT_NET)
		{
			if(gprs_return==GPRS_RX_RET_AT_OK)
			{
				SYS_state = OTA_STATE_AT_OK;
				continue;
			}
			/* 模组无应答保护：未插卡 / 天线断 / 模组坏时不能无限等下去，
			   否则应用永远起不来。60s 无 AT 应答 → 先尝试直接运行应用；
			   若应用不可运行（LOAD_A 返回），约 30s 后再试一次。 */
			if(at_wait_tick >= at_wait_max)
			{
				printf("AT timeout\r\n");
				OtaGiveUpAndRunApp(OTA_ERR_NET);
				at_wait_tick = at_wait_max - 100;
				delay_ms(500);                 // 限速，避免空转刷屏
				continue;
			}
			at_wait_tick++;
			At_init();//先发送AT命令
			delay_ms(300);
			continue;
		}
		// if(SYS_state==OTA_STATE_AT_OK)
		// {
		// 	if(gprs_return==GPRS_RX_RET_MQTTCID_OK)
		// 	{
		// 		SYS_state = OTA_STATE_MQTTCID_OK;
		// 		continue;
		// 	}
		// 	CID_init();//收到AT正确回复后再发送MQTTCID
		// 	delay_ms(200);
		// 	continue;
		// }
		if(SYS_state==OTA_STATE_AT_OK)
		{
			// 得到MQTTCID后不再发送命令，仅等待网络在线
			if(gprsonlineflag==0)
			{
				delay_ms(200);
				online_tick++;
				if(online_tick >= 200)
				{
					online_tick = 0;
					ota_step5();
					printf("GPRS not online after waiting, give up, jump to A1\r\n");
					OtaGiveUpAndRunApp(OTA_ERR_NET);
				} // 等待一段时间后仍未上线
				continue;
			}
			if(gprsonlineflag==1)
			{
				SYS_state=OTA_STATE_ONLINE;
				printf("GPRS is online!\r\n");
			}
		}
		switch (SYS_state){
			case OTA_STATE_ONLINE:
				// 掉线重连时若已在重连窗口暂存到平台回包（putVersion 回执），
				// 直接消费它：不重新协商、不重发 putVersion
				// （gprs_rx_datas() 下一轮会解析该暂存帧）
				if(gprs_rx_defer_pending() && !gprs_download_pending())
				{
					printf("use stashed version reply\r\n");
					ota_step2();//液晶提示：连接成功/申请最新固件
					SYS_state=OTA_STATE_HANDLE_REPLY;
					break;
				}
				// 下载会话在途（例如掉线后重连）：直接重发在途的 offset 请求，
				// 不重新协商、不从 0 重来
				if(gprs_download_pending() && gprs_resend_last_request())
				{
					printf("resume pending offset request\r\n");
					SYS_state=OTA_STATE_HANDLE_REPLY;
				}
				else
				{
					ota_step2();//发送版本询问
					delay_ms(1000);
					SYS_state=OTA_STATE_SEND_VERSION;
				}
				break;
			case OTA_STATE_SEND_VERSION:
					cjson_pub_version();
					SYS_state=OTA_STATE_HANDLE_REPLY;
				break;
			case OTA_STATE_HANDLE_REPLY:
				//gprs_return=
			  if(gprs_return==GPRS_RX_RET_NO_DATA || gprs_return==GPRS_RX_RET_PARSE_FAILED)
				{
					req_timeout_tick++;
					if(req_timeout_tick>=req_timeout_tick_max)
					{
						req_timeout_tick = 0;
						req_retry_count++;
						if(gprs_resend_last_request())
						{
							printf("request timeout, resend last request (%d/%d)\r\n", req_retry_count, req_retry_max);
						}
						else
						{
							printf("request timeout, no cached request (%d/%d)\r\n", req_retry_count, req_retry_max);
						}

						if(req_retry_count>=req_retry_max)
						{
							printf("request timeout too many times, give up, jump to A1\r\n");
							OtaGiveUpAndRunApp(OTA_ERR_TIMEOUT);
						}
					}
					delay_ms(100);
					break;
				}
			  else
				{
					req_timeout_tick = 0;
					req_retry_count = 0;
				}
			  if(gprs_return==GPRS_RX_ERR_CRC_RETRY_EXCEEDED)
				{
					crc_fail_count++;
					ota_show_error(OTA_ERR_DATA);   /* 每次 CRC 失败都提示类型（不延时） */
					printf("crc failed, retry=%d/%d\r\n", crc_fail_count, crc_fail_max);
					if(crc_fail_count>=crc_fail_max)
					{
						printf("crc failed too many times, give up, jump to A1\r\n");
						OtaGiveUpAndRunApp(OTA_ERR_DATA);
					}
					break;
				}
			  else if(gprs_return==GPRS_RX_RET_DOWNLOAD_CONTINUE)
				{
					// 当前包写入成功，清零CRC失败计数
					crc_fail_count = 0;
				}
			  if(gprs_return < 0 && gprs_return != GPRS_RX_RET_NO_DATA && gprs_return != GPRS_RX_ERR_CRC_RETRY_EXCEEDED)
				{
					rx_err_count++;
					ota_show_error(OTA_ERR_DATA);   /* 每次接收错误都提示类型（不延时） */
					printf("rx error=%d, retry=%d/%d\r\n", gprs_return, rx_err_count, rx_err_max);
					if(rx_err_count >= rx_err_max)
					{
						printf("rx error too many times, give up, jump to A1\r\n");
						OtaGiveUpAndRunApp(OTA_ERR_DATA);
					}
					break;
				}
			  else if(gprs_return >= 0)
				{
					rx_err_count = 0;
				}
			  if(gprs_return==GPRS_RX_RET_NO_UPDATE)
				{
					crc_fail_count = 0;
					ota_step4();//无需更新
					delay_ms(1000);
					JumpToApp();
				}
				 if(gprs_return==GPRS_RX_RET_START_UPDATE)
				 {
					 crc_fail_count = 0;
					 link_loss_count = 0;   // 新一轮下载会话：重置会话级掉线预算
					 ota_step3();//开始更新
					 progressbar_Init();
					 //delay_ms(1000);
				 }
				 if(gprs_return==GPRS_RX_RET_DOWNLOAD_DONE)
				 {
					 crc_fail_count = 0;
					 ota_step6();//下载完成
					 delay_ms(1000);
					 ota_step7();
					 delay_ms(500);
					 NVIC_SystemReset(); // 设备重启
				 }
				 if(gprs_return==GPRS_RX_RET_DOWNLOAD_CONTINUE)
				 {
				 }
				break;
			case OTA_STATE_REBOOT:
				//LOAD_A(ST32_A1_SADDR);
				gprs_download_context_reset();
				NVIC_SystemReset();
				printf("rebooting...\r\n");
				break;
			
			}
	}
}
