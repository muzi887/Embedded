#include "main.h"
#include "data_handler.h"
#include "eeprom.h"
#include "timer.h"

extern u8 BeepNum;
DS3231_Time currentTime; // 存放时间结构体

int main(void)
{
	int i;
	// SystemInit();
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); // 设置系统中断优先级分组2

	delay_init(168);	// 初始化延时函数
	uart_init(115200);	// 初始化PC调试串口1
	RS485_Init(9600);	// 初始化新指令串口2
	uart3_init(115200); // 初始化4G串口3
	uart4_init(9600);	// 初始化二维码串口4
	uart5_init(9600);// 初始化原指令串口5
	
	BeepNum = 2;
	Beep_Init();					   // 蜂鸣器
	IIC_Init();						   // 初始化时钟IIC / eeprom
	TIM3_Int_Init(5000 - 1, 8400 - 1); // 定时器时钟84M，分频系数8400，所以84M/8400=10Khz的计数频率，计数5000次为500ms
	TIM4_Int_Init(100 - 1, 8400 - 1); // 定时器4 10ms
	ReadKey(); // 读取密钥
	//	clearPublicKeySpace(); //擦除黑名单
	loadDataFromEEPROM(); // 读取黑名单

	while (1)
	{
		// 串口1接收--PC调试
		PCProcess();
		// 串口2梯控板485；处理回执
		// Rs485Process();
		// 串口3接收--4G模块；接收指令及回执
		//G4GProcess();
		// 串口4二维码
		//QRProcess();
		
		if (UART5_RX_Complete)
		{
			 printf("RX[%d]: ", UART5_RX_CNT);
				for (i = 0; i < UART5_RX_CNT; i++)
				{
					printf("%c", UART5_RX_BUF[i]);
				}
				printf("\r\n");
		}
		UART5_RX_Complete=0;
	}
}
