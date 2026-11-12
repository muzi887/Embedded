#include "boot.h"
#include "usart.h"
#include "delay.h"
#include "24cxx.h"
#include <string.h>
#include "stmflash.h"
#include "gprs.h"
#include "display.h"   /* OTA_ERR_* 错误类型（液晶提示用） */

load_a load_A;		  // 函数指针load_A
OTA_InfoCB OTA_Info;  // 存储在24C02内的OTA信息相关的结构体
UpDataA_CB UpDataA;	  // A区更新用到的结构体
uint32_t BootStaFlag; // 记录全局状态标志位

/* OTA 出错提示（LCD）：引导阶段登记，main 在 LCD 就绪后显示（见 boot.h 注释） */
u8 g_ota_lcd_err = 0;
u8 g_ota_lcd_hold = 0;

/*-------------------------------------------------*/
/*函数名：镜像头校验（A1/A2 通用）                   */
/*参  数：base = 镜像起始地址                        */
/*返回值：1 = 像一份可运行镜像，0 = 坏包             */
/*说明：SP 必须在 RAM 区(0x20000000~0x2000FFFF)，    */
/*      复位向量必须落在 A1 区(镜像按 A1 链接)。      */
/*      !! 地址宏整体必须加括号：ST32_A2_SADDR 展开为 */
/*      「0x08000000 + 0x3C000」，若写成               */
/*      *(volatile u32 *)ST32_A2_SADDR，转型只作用于   */
/*      0x08000000，后面变成「指针 + 0x3C000 个 u32」，*/
/*      实际读 0x080F0000（越界），会把好固件判成坏包。*/
/*-------------------------------------------------*/
static u8 OtaImageValidAt(u32 base)
{
	u32 sp = *(volatile u32 *)(base);
	u32 rv = *(volatile u32 *)(base + 4);

	if (sp < 0x20000000 || sp > 0x2000FFFF)
		return 0;
	if (rv < (u32)(ST32_A1_SADDR)
		|| rv >= ((u32)(ST32_A1_SADDR) + (u32)ST32_A1_PAGE_NUM * ST32_PAGE_SIZE))
		return 0;
	return 1;
}

static u8 OtaA2ImageValid(void)
{
	return OtaImageValidAt((u32)(ST32_A2_SADDR));
}

static u8 OtaA1ImageValid(void)
{
	return OtaImageValidAt((u32)(ST32_A1_SADDR));
}

/*-------------------------------------------------*/
/*函数名：搬运校验                                  */
/*返回值：1 = A1 与 A2 前 OTA_size 字节完全一致      */
/*说明：只比固件长度内的字节（搬运时末页按半字对齐   */
/*      多写的填充字节不在比较范围内）               */
/*-------------------------------------------------*/
static u8 OtaCopyVerify(void)
{
	u32 len = OTA_Info.OTA_size;

	if (len == 0 || len > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
		return 0;
	if (memcmp((const void *)((u32)(ST32_A2_SADDR)), (const void *)((u32)(ST32_A1_SADDR)), len) != 0)
		return 0;
	return 1;
}

/* 只写 OTA_flag（4 字节，偏移 OTA_OFF_FLAG） */
static u8 OtaWriteFlag(u32 flag)
{
	OTA_Info.OTA_flag = flag;
	return AT24CXX_Write(OTA_ADDR + OTA_OFF_FLAG, (uint8_t *)&OTA_Info.OTA_flag, 4);
}

/* 清掉 OTA_ver：表示「本机没有可用的已安装版本」。
   这样 putVersion 的版本闸不会再认为「已是最新」，下次联网会重新下载安装。 */
static void OtaClearInstalledVersion(void)
{
	memset(&OTA_Info.OTA_ver, 0, 32);
	AT24CXX_Write(OTA_ADDR + OTA_OFF_VER, (uint8_t *)&OTA_Info.OTA_ver[0], 32);
}

/* 安装成功 → 关闭升级标志（UPDATE_flag=0）：之后上电直接跑应用、不再联网核查。
   再次升级需由平台经 A1 应用把 UPDATE_flag 置 1（见 boot.h 注释）。 */
void OtaCloseUpdateGate(void)
{
	OTA_Info.UPDATE_flag = 0;
	AT24CXX_Write(OTA_ADDR + OTA_OFF_UPDATE, (uint8_t *)&OTA_Info.UPDATE_flag, 1);
}

/* 自愈重搬计数（1 字节 EEPROM）：成功清零，失败累加，达到 OTA_RETRY_MAX 后不再重搬 */
static u8 OtaRetryCount(void)
{
	u8 v = 0;
	AT24CXX_Read(OTA_RETRY_ADDR, &v, 1);
	return v;
}

static void OtaRetryCountSet(u8 v)
{
	AT24CXX_Write(OTA_RETRY_ADDR, &v, 1);
}

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
		/* 护栏1：OTA_size 必须是合法字节数。
		   - size=0 会让 Cal_Block 算出 BlockNB=0，Update_A1 里 BlockNB-1 下溢成 65535，
		     把 65535 页垃圾刷进 A1（变砖）；
		   - size 曾按「块数」写入时会只搬几百字节（残包），也不允许。 */
		if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
		{
			printf("bad OTA_size=%u\r\n", OTA_Info.OTA_size);
			OtaWriteFlag(OTA_A1_FLAG);
			OtaClearInstalledVersion();   /* 没有可安装的固件，别谎报“已是最新” */
			g_ota_lcd_err = OTA_ERR_SIZE;   /* 登记待显示：LCD 尚未初始化，交给 main 显示 */
			g_ota_lcd_hold = 1;
			return;
		}

		/* 护栏2：A2 里的镜像头必须是可运行镜像（SP/复位向量合法）。
		   拦掉「上传的是 .hex 文本」「下载被截断」等坏包，避免把垃圾搬进 A1。 */
		if (!OtaA2ImageValid())
		{
			printf("A2 hdr invalid\r\n");
			OtaWriteFlag(OTA_A1_FLAG);
			OtaClearInstalledVersion();
			g_ota_lcd_err = OTA_ERR_IMAGE;
			g_ota_lcd_hold = 1;
			return;
		}

		printf("loading progarm file A1\r\n");
		Cal_Block();
		Update_A1();

		/* (b) 搬运校验：A1 与 A2 内容一致才算安装成功 */
		if (!OtaCopyVerify())
		{
			OtaRetryCountSet(OtaRetryCount() + 1);   /* 计入失败次数，供自愈限流 */
			printf("A1 verify FAIL\r\n");
			g_ota_lcd_err = OTA_ERR_VERIFY;
			g_ota_lcd_hold = 1;
			return;      /* flag 保持 A2 → 下次开机重搬；OTA_ver 保持“待安装”语义 */
		}

		if (!OtaWriteFlag(OTA_A1_FLAG)) // 写入标志位：此刻才算真正“已安装”
		{
			OtaRetryCountSet(0);
			OtaCloseUpdateGate();       /* 关闭升级标志：以后上电直接跑应用 */

			printf("write progarm file success\r\n");
			LOAD_A((u32)(ST32_A1_SADDR));
		}
		else
			printf("write progarm file error\r\n");
		return;
	}

	/* 情况2：(a) 开机自愈。
	   flag 说「A1 已是最新」，但 A1 实际不可运行——常见于用烧写工具整片擦除后：
	   EEPROM 不会被擦，flag 仍是 A1、OTA_ver 仍是上次的目标版本，于是设备以为自己已升级。 */
	if (!OtaA1ImageValid())
	{
		printf("A1 invalid, recover\r\n");

		/* A2 里还留着同一份镜像 → 直接重搬（同样校验通过才置 flag=A1） */
		if (OTA_Info.OTA_size > 0
			&& OTA_Info.OTA_size <= (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE)
			&& OtaA2ImageValid())
		{
			/* 限流：连续失败达到上限就不再擦写 A1（保护 Flash 寿命）。
			   注意只在"A2 有镜像、要重搬"时生效；下面的"清 OTA_ver"不受它影响。 */
			if (OtaRetryCount() >= OTA_RETRY_MAX)
			{
				printf("recover limit\r\n");
				g_ota_lcd_err = OTA_ERR_VERIFY;
				g_ota_lcd_hold = 1;
				return;
			}

			printf("recover from A2\r\n");
			Cal_Block();
			Update_A1();
			if (OtaCopyVerify())
			{
				OtaWriteFlag(OTA_A1_FLAG);
				OtaRetryCountSet(0);
				OtaCloseUpdateGate();   /* 自愈成功同样关闭升级标志 */
				printf("recover OK, jump A1\r\n");
				LOAD_A((u32)(ST32_A1_SADDR));
				return;
			}
			OtaRetryCountSet(OtaRetryCount() + 1);
			printf("recover FAIL\r\n");
			g_ota_lcd_err = OTA_ERR_VERIFY;
			g_ota_lcd_hold = 1;
			return;
		}

		/* A2 也没有可用镜像 → 清版本标记，让版本闸失效，强制重新下载安装 */
		printf("A1/A2 invalid\r\n");
		OtaClearInstalledVersion();
		g_ota_lcd_err = OTA_ERR_IMAGE;
		g_ota_lcd_hold = 1;
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
	/* 护栏：size 非法时不计算（Update_A1 会跳过搬运），避免 BlockNB-1 下溢 */
	if (OTA_Info.OTA_size == 0 || OTA_Info.OTA_size > (u32)(ST32_A2_PAGE_NUM * ST32_PAGE_SIZE))
	{
		UpDataA.BlockNB = 0;
		UpDataA.size = 0;
		printf("Cal_Block: invalid OTA_size=%u, skip\r\n", OTA_Info.OTA_size);
		return;
	}

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

	if (UpDataA.BlockNB == 0)   /* 护栏：没有有效块就不搬运 */
	{
		printf("skip update A1: no valid block\r\n");
		return;
	}

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
