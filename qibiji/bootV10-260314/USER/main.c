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
	const u8 req_timeout_tick_max = 30; // 30 * 100ms = 3s
	const u8 req_retry_max = 5;

	u8 online_tick=0;

	delay_init();		  // 延时函数初始化
	NVIC_Configuration(); // 设置NVIC中断分组2:2位抢占优先级，2位响应优先级

	uart1_init(460800);  // 与PC通信
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
		LOAD_A(ST32_A1_SADDR);	
	}

	IWDG_Init(4, 1990); // 3180MS 看门狗

	//progressbar_Init();
	while (1)
	{
		IWDG_Feed(); // 喂狗
		gprs_return=gprs_rx_datas();

		if(gprsonlineflag==0 && SYS_state!=OTA_STATE_WAIT_NET && SYS_state!=OTA_STATE_AT_OK && SYS_state!=OTA_STATE_MQTTCID_OK)
		{
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
			At_init();//先发送AT命令
			delay_ms(100);
			continue;
		}
		if(SYS_state==OTA_STATE_AT_OK)
		{
			if(gprs_return==GPRS_RX_RET_MQTTCID_OK)
			{
				SYS_state = OTA_STATE_MQTTCID_OK;
				continue;
			}
			CID_init();//收到AT正确回复后再发送MQTTCID
			delay_ms(200);
			continue;
		}
		if(SYS_state==OTA_STATE_MQTTCID_OK)
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
					printf("GPRS not online after waiting, rebooting...\r\n");
					//gprs_download_context_reset();
					//NVIC_SystemReset();
					LOAD_A(ST32_A1_SADDR);
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
				//if(gprsonlineflag==1 && gprs_rx_datas()==GPRS_RX_RET_NET_ONLINE){
					ota_step2();//发送版本询问
					delay_ms(1000);
					SYS_state=OTA_STATE_SEND_VERSION;
				//}
				break;
			case OTA_STATE_SEND_VERSION:
					cjson_pub_version();
					SYS_state=OTA_STATE_HANDLE_REPLY;
				break;
			case OTA_STATE_HANDLE_REPLY:
				//gprs_return=
			  if(gprs_return==GPRS_RX_RET_NO_DATA)
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
							ota_step5();
							printf("request timeout too many times, reboot\r\n");
							delay_ms(200);
							//gprs_download_context_reset();
							//NVIC_SystemReset();
							LOAD_A(ST32_A1_SADDR);
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
					ota_step5();//更新异常
					printf("crc failed, retry=%d/%d\r\n", crc_fail_count, crc_fail_max);
					if(crc_fail_count>=crc_fail_max)
					{
						printf("crc failed too many times, reboot\r\n");
						delay_ms(200);
						//gprs_download_context_reset();
						//NVIC_SystemReset();
						LOAD_A(ST32_A1_SADDR);
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
					ota_step5();
					printf("rx error=%d, retry=%d/%d\r\n", gprs_return, rx_err_count, rx_err_max);
					if(rx_err_count >= rx_err_max)
					{
						printf("rx error too many times, reboot\r\n");
						delay_ms(200);
						//gprs_download_context_reset();
						//NVIC_SystemReset();
						LOAD_A(ST32_A1_SADDR);
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
					LOAD_A(ST32_A1_SADDR);
				}
				 if(gprs_return==GPRS_RX_RET_START_UPDATE)
				 {
					 crc_fail_count = 0;
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
