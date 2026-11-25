#ifndef __REG_H
#define __REG_H

#include "sys.h"        /* u8/u16/u32 基础类型 */
#include <stddef.h>     /* offsetof：布局断言取字段实际偏移 */
#include "24cxx.h"      /* EE_ADDR（EEPROM 域断言用）与 AT24CXX_* 读写函数 */

/* ---- 24C02 地址布局总表（REG 是布局属主，全工程只允许从这里取地址） ----
   0~63     APP 参数区：A1 应用自己的参数，B 区不使用
   64~95    productKey 槽位（32 字节）
   96~127   deviceKey 槽位（32 字节）
   128~199  MQTT 参数预留段（后续扩展用）
   200~243  OTA 状态块 OTA_InfoCB（44 字节）
   244      自愈重搬失败计数（1 字节）
   245~249  空闲
   250      0x55 初始化标志（AT24CXX_Check 维护，REG 只消费其结论） */

/* ---- MQTT 身份参数：24C02 布局与 A1 应用 reg.h 完全一致 ----
   平台会校验「消息体 productKey/deviceKey == 主题里的 productKey/deviceKey」，
   不一致直接判失败并且不回包，所以 B 区发出的 putVersion/getFirmware/otaError
   必须带 A1 应用同一套身份，否则现场表现是"putVersion 发出后一直 request timeout"。 */
#define MQTT_PRODUCTKEY_BYTE_ADDR   64                          /* productKey 槽位起始地址 */
#define MQTT_DEVICEKEY_BYTE_ADDR    96                          /* deviceKey 槽位起始地址 */
#define MQTT_KEY_EEPROM_LEN         32                          /* 每项槽位字节数 */
#define MQTT_KEY_STR_LEN            (MQTT_KEY_EEPROM_LEN + 1)   /* 32 字符 + '\0' */
#define MQTT_PRODUCTKEY_DEFAULT     "pk001"                     /* 槽位空/未写时的兜底值 */
#define MQTT_DEVICEKEY_DEFAULT      "dev001"

extern char g_mqtt_productKey[MQTT_KEY_STR_LEN];   /* 当前生效的 productKey（RAM 副本，空槽时为兜底值） */
extern char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];    /* 当前生效的 deviceKey（同上） */

/* ---- OTA 状态块：整块存于 24C02 的 200~243，上电读入 RAM，关键变化分字段写回 ---- */
#define OTA_A1_FLAG   0xA1A1A1A1   /* OTA_flag 取此值：A1 区为当前有效应用 */
#define OTA_A2_FLAG   0xA2A2A2A2   /* OTA_flag 取此值：A2 区存有待安装固件 */
#define OTA_ADDR      200          /* OTA 状态块在 24C02 中的存放起始地址 */

typedef struct {
    u32 OTA_flag;      /* 生效分区标志：OTA_A1_FLAG / OTA_A2_FLAG */
    u8  OTA_ver[32];   /* 版本串；全 0 = 本机没有可用的已安装版本（版本闸以此判定） */
    u32 OTA_size;      /* A2 中待安装固件的总字节数；0 = 无固件 */
    u8  UPDATE_flag;   /* 升级闸：1 = 上电进入联网 OTA 流程；0 = 上电直接跳 A1。
                          写入端：① 出厂默认 paraToEeprom() 置 1（APP 参数协议也可改）；
                          ② 安装/自愈成功后由 OtaCloseUpdateGate() 自动置 0，
                          再次升级需由平台经 A1 应用把它置回 1。
                          24C02 自检失败时 Reg_ReadOtaInfo 显式置 0（状态落不了盘，做不了 OTA）。 */
} OTA_InfoCB;
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)   /* 状态块总字节数 = 44（含对齐填充），整块读写时使用 */

/* 状态块各字段在 24C02 中的字节偏移：分字段读写（OtaWriteFlag 等）按此寻址 */
#define OTA_OFF_FLAG    0    
#define OTA_OFF_VER     4 
#define OTA_OFF_SIZE    36 
#define OTA_OFF_UPDATE  40   

/* 布局断言：结构体任一字段的实际偏移与上面宏不符即编译失败（负数组长度技巧），
   防止日后改结构体导致分字段读写地址错位 */
typedef char ota_info_layout_check[
    (offsetof(OTA_InfoCB, OTA_flag)    == OTA_OFF_FLAG   &&
     offsetof(OTA_InfoCB, OTA_ver)     == OTA_OFF_VER    &&
     offsetof(OTA_InfoCB, OTA_size)    == OTA_OFF_SIZE   &&
     offsetof(OTA_InfoCB, UPDATE_flag) == OTA_OFF_UPDATE) ? 1 : -1];

/* 自愈重搬失败计数：1 字节 EEPROM，紧跟状态块（地址 244），落在 244~249 空闲段。
   状态块一旦扩大会覆盖到它，下面的断言在编译期拦截 */
#define OTA_RETRY_ADDR  (OTA_ADDR + 44)   /* 计数字节地址 = 状态块末尾 + 1 */
#define OTA_RETRY_MAX   3                 /* 连续失败上限：达到后不再擦写 A1（保护 Flash 寿命） */

/* EEPROM 域断言：计数地址必须不越过 0x55 标志位。 */
typedef char reg_layout_check[
    ((OTA_RETRY_ADDR + 1) <= EE_ADDR) ? 1 : -1];

/* ---- OTA 错误码：登记处统一编号，main 在屏就绪后按码显示 ---- */
#define OTA_ERR_NET      0   /* 网络连接超时：AT 无应答 / 等待上线超时 */
#define OTA_ERR_TIMEOUT  1   /* 请求超时：等平台回包超时重试超限 */
#define OTA_ERR_LINK     2   /* 连接中断：升级过程中掉线超限 */
#define OTA_ERR_DATA     3   /* 数据校验失败：CRC / 长度 / 序号 */
#define OTA_ERR_IMAGE    4   /* 固件包无效：A2 镜像头非法 / A1、A2 均无可用镜像 */
#define OTA_ERR_VERIFY   5   /* 写入校验失败：搬运 memcmp 不一致 / 自愈校验失败 */
#define OTA_ERR_SIZE     6   /* 固件大小错误：OTA_size 非法 */
#define OTA_ERR_EEPROM   7   /* 存储异常：24C02 自检失败（OTA 被关闭） */

/* LCD 错误提示登记变量：任何模块在 LCD 初始化前发现错误先写这里，
   main 在屏就绪后统一显示。err = 0 表示无错误 */
extern u8 g_ota_lcd_err;    /* 待显示的 OTA_ERR_* 错误码；0 = 无错误 */
extern u8 g_ota_lcd_hold;   /* 1 = 显示后停留 3 秒再尝试进应用；0 = 显示即返回 */

/* OTA 状态块的 RAM 副本：上电由 Reg_ReadOtaInfo 从 24C02 读入（新芯片则播种生成） */
extern OTA_InfoCB OTA_Info;

/* ---- 对外函数 ---- */
void Reg_ReadOtaInfo(void);              /* 上电入口：三态自检分派（播种 / 故障 / 正常读取），填 OTA_Info */
void MQTT_ReadKeysFromEeprom(void);      /* 从 24C02 读 productKey/deviceKey，空槽保持兜底值 */
void paraToEeprom(void);                 /* 播种：把 OTA 状态块默认值写进 24C02（名字保留原工程） */

u8   OtaWriteFlag(u32 flag);             /* 只写 OTA_flag（4 字节）：传入 OTA_A1_FLAG / OTA_A2_FLAG */
void OtaClearInstalledVersion(void);     /* 清空 OTA_ver：版本闸失效，平台将重新下发固件 */
void OtaCloseUpdateGate(void);           /* 写 UPDATE_flag=0：关闭升级闸，上电不再进 OTA */
u8   OtaRetryCount(void);                /* 读自愈重搬失败计数（1 字节） */
void OtaRetryCountSet(u8 v);             /* 写自愈重搬失败计数（1 字节） */

#endif
