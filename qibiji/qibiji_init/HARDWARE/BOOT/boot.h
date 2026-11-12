#ifndef BOOT_H
#define BOOT_H

#include "sys.h"

typedef void (*load_a)(void);                            //函数指针类型定义

//stm32f103vct6  flash:256k  ram:48k  页大小:2k
//stm32f103vet6  flash:512k  ram:64k  页大小:2k

#define  ST32_FLASH_SADDR    0x08000000                                              //FLASH起始地址
#define  ST32_PAGE_SIZE      2048                                                    //FLASH页大小
#define  ST32_PAGE_NUM       256                                                     //FLASH总页数
#define  ST32_B_PAGE_NUM     28                                                      //B区页数  56k（0x08000000~0x0800E000；与 BOOTV10 保持一致）
#define  ST32_A1_PAGE_NUM    100																                     //A1区页数  200k
#define  ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                         //A1区起始页号
#define  ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE) //A1区起始地址（随 B 区页数自动移动；当前 = 0x0800E000）

#define  ST32_A2_PAGE_NUM    100                                                           //A2区页数
#define  ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                   //A2区起始页号
#define  ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)  //A2区起始地址（随 A1 页数自动移动；当前 = 0x08040000）

#define  OTA_A1_FLAG        0xA1A1A1A1         //如果OTA_flag等于该值，说明运行A1区
#define  OTA_A2_FLAG        0xA2A2A2A2         //如果OTA_flag等于该值，说明运行A2区

#define  OTA_BYTE_ADDR           200

/* OTA_ver slot size inside OTA_InfoCB, and the C string buffer size used when
 * reading it back ('\0' lives at index 32, never inside the EEPROM slot). */
#define  OTA_VER_LEN        32
#define  OTA_VER_STR_LEN    (OTA_VER_LEN + 1)

typedef struct {
    u32 OTA_flag;           //标志性的变量，当OTA_SET_FLAG等于该值时，说明需要OTA更新A区
    u8  OTA_ver[32];
	u32 OTA_size;   
    u8  UPDATE_flag;	// 0 无需ota ，1 需要联网ota
} OTA_InfoCB;                                 //OTA相关的信息结构体，需要存储到24C02
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)    //OTA相关的信息结构体占用的字节长度

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


uint16_t Xmodem_CRC16(uint8_t *data, uint16_t datalen);  //函数声明

#endif



