#include "boot.h"
#include "usart.h"
#include "delay.h"
#include "24cxx.h"
#include <string.h>
#include "stmflash.h"
#include "gprs.h"

load_a load_A;		  // 函数指针load_A
OTA_InfoCB OTA_Info;  // 存储在24C02内的OTA信息相关的结构体
UpDataA_CB UpDataA;	  // A区更新用到的结构体
uint32_t BootStaFlag; // 记录全局状态标志位

/*-------------------------------------------------*/
/*函数名：BootLoader分支判断                       */
/*参  数：无                                       */
/*返回值：无                                       */
/*先检查更新标志位是否更改							*/
/*若更改则完成搬运工作								*/
/*若未更改则询问最新版本是否一致					*/
/*不一致则下载最新的固件							*/
/*一致则跳转至A1程序区								*/
/*-------------------------------------------------*/
void BootLoader_Brance(void)
{
	u8 sta=0;
	// if(OTA_Info.UPDATE_flag == 1){
	//	 if (OTA_Info.OTA_flag == OTA_A1_FLAG)
	//	{
	//		 printf("jump to progarm file A1\r\n");
	//	//	 BootLoader_Clear();
	//		 LOAD_A(ST32_A1_SADDR);
	//	}
	if (OTA_Info.OTA_flag != OTA_A1_FLAG)
	{
		printf("loading progarm file A1\r\n");
		Cal_Block();
		Update_A1();
		
		OTA_Info.OTA_flag = OTA_A1_FLAG;
		if (!AT24CXX_Write(OTA_ADDR, (uint8_t *)&OTA_Info.OTA_flag, 4)) // 写入标志位
		{
			printf("write progarm file success\r\n");
			LOAD_A(ST32_A1_SADDR);
		}
		else
			printf("write progarm file error\r\n");

		 
	}
	//}
}

/*-------------------------------------------------*/
/*函数名：设置栈指针SP                                   */
/*参  数：参数addr为栈指针初始值                     */
/*返回值：无                                       */
/*-------------------------------------------------*/
__asm void MSR_MSP(uint32_t addr)
{
	MSR MSP, r0			// addr的值传递到r0通用寄存器，然后通过MSR指令，将通用寄存器r0的值写入到MSP主栈指针
				 BX r14 // 返回到MSR_SP函数的调用处
}
/*-------------------------------------------------*/
/*函数名：跳转到A区                                */
/*参  数：参数addr为A区程序起始地址                      */
/*返回值：无                                       */
/*-------------------------------------------------*/
void LOAD_A(uint32_t addr)
{
	uint32_t vtor;
	printf("A zone %x,%x,%x\r\n", (*(vu32 *)(addr)), addr, (*(vu32 *)(addr + 4)));
	if ((*(uint32_t *)addr >= 0x20000000) && (*(uint32_t *)addr <= 0x2000FFFF)) // 判断sp栈指针的范围是否合法，在对应型号的RAM空间范围内
	{
		vtor = SCB->VTOR;
		printf("vtor %x\n", vtor);
		MSR_MSP(*(uint32_t *)addr);					// 设置SP
		load_A = (load_a) * (uint32_t *)(addr + 4); // 函数指针load_A指向A区的复位向量
		printf("load progarm A is ok\r\n");
		BootLoader_Clear();							// 清除B区使用的资源
		load_A();									// 调用函数指针load_A改变PC指针，从而跳转到A区的复位向量位置，实现跳转
	}
	else
		printf("load progarm A is fail\r\n");
}
/*-------------------------------------------------*/
/*函数名：清除B区使用的资源                        */
/*参  数：无                                       */
/*返回值：无                                       */
/*-------------------------------------------------*/
void BootLoader_Clear(void)
{
	USART_DeInit(USART1);
	USART_DeInit(USART2);
	//    usart_deinit(USART0);   //复位串口0
	//    gpio_deinit(GPIOA);     //复位GPIOA
	//    gpio_deinit(GPIOB);     //复位GPIOB

	WRCommandH(0x01);
}

void Cal_Block(void) // 计算读取的块数量以及最后一块的字节数
{
	if ((OTA_Info.OTA_size % ST32_PAGE_SIZE) == 0)
	{
		UpDataA.BlockNB = OTA_Info.OTA_size / ST32_PAGE_SIZE;
		UpDataA.size = ST32_PAGE_SIZE / 2;
	}
	else
	{
		UpDataA.BlockNB = OTA_Info.OTA_size / ST32_PAGE_SIZE + 1;
		UpDataA.size = OTA_Info.OTA_size % ST32_PAGE_SIZE;
		if ((UpDataA.size % 2) == 0)
		{
			UpDataA.size = (UpDataA.size / 2);
		}
		else
		{
			UpDataA.size = (UpDataA.size / 2) + 1;
		}
	}

	//printf("OTA_Info.OTA_size=%d, %d, %d\r\n", OTA_Info.OTA_size,UpDataA.BlockNB, UpDataA.size);
}

// 更新A1区
void Update_A1(void)
{
	u16 i = 0,j=0;
	u32 addrW = 0, addrR = 0;

	for (i = 0; i < UpDataA.BlockNB-1; i++)
	{
		// 读 flash
		addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;
		STMFLASH_Read(addrR, (u16 *)&UpDataA.Updatabuff[0], ST32_PAGE_SIZE / 2);

		// xie flash
		addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;
		STMFLASH_Write(addrW, (u16 *)&UpDataA.Updatabuff[0], ST32_PAGE_SIZE / 2);
	}

	// 写最后一块
	i = UpDataA.BlockNB - 1;
	addrR = ST32_A2_SADDR + i * ST32_PAGE_SIZE;
	STMFLASH_Read(addrR, (u16 *)&UpDataA.Updatabuff[0], UpDataA.size);

	addrW = ST32_A1_SADDR + i * ST32_PAGE_SIZE;
	STMFLASH_Write(addrW, (u16 *)&UpDataA.Updatabuff[0], UpDataA.size);
}
