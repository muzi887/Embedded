#ifndef BOOT_H
#define BOOT_H

#include "sys.h"
#include <stddef.h>
#include "stmflash.h"   /* 取 STM32_FLASH_SIZE：分区容量断言用，必须与片内 Flash 容量同步 */
#include "24cxx.h"      /* 取 EE_ADDR：EEPROM 地址断言用 */

typedef void (*load_a)(void);                            //函数指针类型定义

//stm32f103vct6  flash:256k  ram:48k  页大小:2k
//stm32f103vet6  flash:512k  ram:64k  页大小:2k

#define  ST32_FLASH_SADDR    0x08000000                                              //FLASH起始地址
#define  ST32_PAGE_SIZE      2048                                                    //FLASH页大小
#define  ST32_PAGE_NUM       256                                                     //FLASH总页数
#define  ST32_B_PAGE_NUM     28                                                      //B区页数  56k（0x08000000~0x0800E000）
#define  ST32_A1_PAGE_NUM    100																                     //A1区页数  200k
#define  ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                         //A1区起始页号
#define  ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE) //A1区起始地址（随 B 区页数自动移动；当前 = 0x0800E000）

#define  ST32_A2_PAGE_NUM    100                                                           //A2区页数
#define  ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                   //A2区起始页号
#define  ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)  //A2区起始地址（随 A1 页数自动移动；当前 = 0x08040000）

#define  OTA_A1_FLAG        0xA1A1A1A1         //如果OTA_flag等于该值，说明运行A1区
#define  OTA_A2_FLAG        0xA2A2A2A2         //如果OTA_flag等于该值，说明运行A2区

#define  OTA_ADDR           200

typedef struct {
    u32 OTA_flag;           //标志性的变量，当OTA_SET_FLAG等于该值时，说明需要OTA更新A区
    u8  OTA_ver[32];
	u32 OTA_size;   
    u8  UPDATE_flag;	/* 升级标志：0 = 关闭（上电直接运行 A1，不联网），1 = 开启（走联网 OTA）。
                           写入端有两处：① 出厂默认 paraToEeprom() 置 1（APP 参数协议也可改）；
                           ② 本工程在"安装成功后"自动置 0（关闭升级标志，见 boot.c OtaCloseUpdateGate），
                              再次升级需由平台经 A1 应用把它置回 1（开启升级标志）。
                           注意：24C02 自检失败时 reg.c 会显式置 0（状态无法落盘，做不了 OTA）。 */
} OTA_InfoCB;                                 //OTA相关的信息结构体，需要存储到24C02
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)    //OTA相关的信息结构体占用的字节长度

/* OTA_InfoCB 各字段在 24C02 内的字节偏移（分字段写入时用）。
   下面的编译期断言会在结构体布局变化时报错（负数组长度）。 */
#define OTA_OFF_FLAG        0     /* u32 OTA_flag */
#define OTA_OFF_VER         4     /* u8  OTA_ver[32] */
#define OTA_OFF_SIZE        36    /* u32 OTA_size */
#define OTA_OFF_UPDATE      40    /* u8  UPDATE_flag */
typedef char ota_info_layout_check[
    (offsetof(OTA_InfoCB, OTA_flag)    == OTA_OFF_FLAG   &&
     offsetof(OTA_InfoCB, OTA_ver)     == OTA_OFF_VER    &&
     offsetof(OTA_InfoCB, OTA_size)    == OTA_OFF_SIZE   &&
     offsetof(OTA_InfoCB, UPDATE_flag) == OTA_OFF_UPDATE) ? 1 : -1];

/* 自愈重搬的重试计数（1 字节，EEPROM）：紧邻 OTA 结构体（244），
   落在 244..249 空闲段（0..63 留给 APP、64..191 是 MQTT 参数、250 是 EE_ADDR 标志）。
   OTA 结构体一旦变大就会压到它，下面的断言会在编译期报错。 */
#define OTA_RETRY_ADDR      (OTA_ADDR + 44)
#define OTA_RETRY_MAX       3

/* 分区 / 容量编译期断言（宏全部加括号，避免被解析成指针运算）：
   - A1 紧接 B 区、A2 紧接 A1（不用手写地址，改页数即自动重排）；
   - A2 末不得越过片内 Flash 尾部（依赖 stmflash.h 的 STM32_FLASH_SIZE）；
   - A1 / A2 固定 200KB（OTA 固件上限不变）；
   - B 区大小必须与 Keil 工程 IROM1 的 Size 一致（当前 56KB = 0xE000）；
   - 自愈重试计数地址不越过 EE_ADDR。
   任何一项不满足都会因负数组长度而编译失败。 */
typedef char ota_layout_check[
    (ST32_A1_START_PAGE == ST32_B_PAGE_NUM &&
     ST32_A2_START_PAGE == (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM) &&
     ((ST32_A2_START_PAGE + ST32_A2_PAGE_NUM) * ST32_PAGE_SIZE) <= (1024 * STM32_FLASH_SIZE) &&
     (ST32_A1_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&
     (ST32_A2_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&
     (ST32_B_PAGE_NUM * ST32_PAGE_SIZE) == 0xE000 &&
     (OTA_RETRY_ADDR + 1) <= EE_ADDR) ? 1 : -1];

//typedef struct {
//    uint8_t  Updatabuff[ST32_PAGE_SIZE];      //更新A区时，用于保存从W25Q64中读取的数据
//    uint32_t W25Q64_BlockNB;                  //用于记录第几个W25Q64的块中读取数据
//    uint32_t XmodemTimer;                     //用于记录Xmdoem协议第一阶段发送到写C 的定时器
//    uint32_t XmodemNB;                        //用于记录Xmdoem协议接收到的数据包数量
//    uint32_t XmodemCRC;	                      //用于保存Xmdoem协议数据的CRC16校验值
//} UpDataA_CB;

typedef struct {
    u8  Updatabuff[ST32_PAGE_SIZE];      //更新A区时，用于保存从W25Q64中读取的数据
    u8  BlockNB;                         //用于记录多少块
    u16 size;                            //最后一块的字节数
} UpDataA_CB;

extern OTA_InfoCB OTA_Info;   //外部变量声明
extern UpDataA_CB UpDataA;    //外部变量声明
extern uint32_t BootStaFlag;  //外部变量声明

/* OTA 出错提示（LCD）：由引导/EEPROM 检查阶段登记（此时 LCD 还没初始化），
   由 main 在 CLEARGDRAMH() 之后统一显示。
   g_ota_lcd_err = 0 表示无；否则为 OTA_ERR_*。
   g_ota_lcd_hold = 1 → 显示后停留 3s 再尝试运行应用；0 → 只显示不等（持久性错误）。 */
extern u8 g_ota_lcd_err;
extern u8 g_ota_lcd_hold;

/* 关闭升级标志：把 UPDATE_flag 置 0 并落盘（1 字节）。
   安装成功后由引导程序调用；main.c 在"放弃升级"时也调用它（复位前先关闭升级标志，
   避免复位后又进 OTA 状态机反复重启）。再次升级需由平台经 A1 应用把该位置回 1（开启升级标志）。 */
void OtaCloseUpdateGate(void);

void Cal_Block(void);  //计算读取的块数量以及最后一块的字节数
void Update_A1(void);  //更新A1区
void BootLoader_Brance(void);                            //函数声明
__asm void MSR_MSP(uint32_t addr);                       //函数声明（实现名 MSR_MSP）
void LOAD_A(uint32_t addr);                              //函数声明
void BootLoader_Clear(void);                             //函数声明
uint8_t BootLoader_Enter(uint8_t timeout);               //函数声明
void BootLoader_Info(void);                              //函数声明
void BootLoader_Event(uint8_t *data, uint16_t datalen);  //函数声明
uint16_t Xmodem_CRC16(uint8_t *data, uint16_t datalen);  //函数声明

#endif



