#ifndef BOOT_H
#define BOOT_H

#include "sys.h"
#include <stddef.h>
#include "stmflash.h"   /* 提供 STM32_FLASH_SIZE：分区断言用它核对片内 Flash 总容量 */
#include "24cxx.h"      /* 提供 EE_ADDR：分区断言用它核对 EEPROM 地址边界 */

// A1 应用入口函数指针类型：指向应用镜像的复位向量，调用它即完成跳转
typedef void (*app_entry_fn)(void);

// 芯片资源对照：分区推导与断言按 VET6（512K Flash / 2K 页）校验
// stm32f103vct6  flash:256k  ram:48k  页大小:2k
// stm32f103vet6  flash:512k  ram:64k  页大小:2k

#define  ST32_FLASH_SADDR    0x08000000   // 片内 Flash 基地址，三个分区的共同起点
#define  ST32_PAGE_SIZE      2048         // Flash 页大小：擦除与搬运的最小单位
#define  ST32_PAGE_NUM       256          // 片内 Flash 总页数（512K / 2K）
#define  ST32_B_PAGE_NUM     28           // B 区页数：BootLoader 自身占用 56KB（0x08000000~0x0800E000）
#define  ST32_A1_PAGE_NUM    100          // A1 区页数：正式应用运行区，共 200KB
#define  ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                              // A1 起始页号 = B 区页数（两区紧邻）
#define  ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE)     // A1 起始地址，由宏链推导，当前 0x0800E000
#define  ST32_A2_PAGE_NUM    100                                                          // A2 区页数：新固件下载暂存区，共 200KB
#define  ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                      // A2 起始页号 = A1 末页（两区紧邻）
#define  ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)     // A2 起始地址，由宏链推导，当前 0x08040000

#define  OTA_A1_FLAG         0xA1A1A1A1   // OTA_flag 取此值：A1 区为当前有效应用
#define  OTA_A2_FLAG         0xA2A2A2A2   // OTA_flag 取此值：A2 区存有待安装固件

#define  OTA_ADDR            200          // OTA 状态块在 AT24C02 中的存放起始地址

// OTA 状态块：整块存于 AT24C02（200~243 共 44 字节），上电读入 RAM，关键变化写回
typedef struct {
    u32 OTA_flag;      // 生效分区标志：OTA_A1_FLAG / OTA_A2_FLAG
    u8  OTA_ver[32];   // 版本串；全 0 = 本机没有可用的已安装版本（版本闸以此判定）
    u32 OTA_size;      // A2 中待安装固件的总字节数；0 = 无固件
    u8  UPDATE_flag;   // 升级闸：1 = 上电进入联网 OTA 流程；0 = 上电直接跳 A1
} OTA_InfoCB;
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB)   // 状态块总字节数，整块读写时使用

/* UPDATE_flag 的两个写入端：① 出厂初始化 paraToEeprom() 置 1，APP 参数协议也可改；
   ② 安装/自愈成功后由 reg.c 的 OtaCloseUpdateGate() 自动置 0；下次升级由平台经 A1 置回 1。
   24C02 自检失败时 reg.c 显式置 0——状态落不了盘，OTA 无法进行。 */

// 状态块各字段在 24C02 中的字节偏移：分字段读写时按此寻址
#define OTA_OFF_FLAG        0     // OTA_flag 偏移（4 字节）
#define OTA_OFF_VER         4     // OTA_ver 偏移（32 字节）
#define OTA_OFF_SIZE        36    // OTA_size 偏移（4 字节）
#define OTA_OFF_UPDATE      40    // UPDATE_flag 偏移（1 字节）

// 布局断言：结构体任一成员的实际偏移与上面宏不符即编译失败（负数组长度技巧），
// 防止日后改结构体导致分字段读写地址错位
typedef char ota_info_layout_check[
    (offsetof(OTA_InfoCB, OTA_flag)    == OTA_OFF_FLAG   &&
     offsetof(OTA_InfoCB, OTA_ver)     == OTA_OFF_VER    &&
     offsetof(OTA_InfoCB, OTA_size)    == OTA_OFF_SIZE   &&
     offsetof(OTA_InfoCB, UPDATE_flag) == OTA_OFF_UPDATE) ? 1 : -1];

// 自愈重搬失败计数：1 字节 EEPROM，地址 244，位于状态块之后的空闲段
// （0~63 留给 APP、64~191 为 MQTT 参数、250 是 EE_ADDR 标志）。
// 状态块一旦扩大会覆盖到它，ota_layout_check 会在编译期拦截
#define OTA_RETRY_ADDR      (OTA_ADDR + 44)
#define OTA_RETRY_MAX       3   // 自愈重搬连续失败上限：达到后不再擦写 A1

// 分区合法性编译期断言：全部由宏推导，任何一条不满足即因负数组长度编译失败
// ① A1 紧接 B 区；② A2 紧接 A1；③ A2 末页不越过片内 Flash；
// ④⑤ A1/A2 各恰为 200KB（OTA 固件上限不变）；⑥ B 区大小与 Keil IROM1 的 0xE000 一致；
// ⑦ 自愈计数地址不越过 EE_ADDR
typedef char ota_layout_check[
    (ST32_A1_START_PAGE == ST32_B_PAGE_NUM &&
     ST32_A2_START_PAGE == (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM) &&
     ((ST32_A2_START_PAGE + ST32_A2_PAGE_NUM) * ST32_PAGE_SIZE) <= (1024 * STM32_FLASH_SIZE) &&
     (ST32_A1_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&
     (ST32_A2_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024) &&
     (ST32_B_PAGE_NUM * ST32_PAGE_SIZE) == 0xE000 &&
     (OTA_RETRY_ADDR + 1) <= EE_ADDR) ? 1 : -1];

// A2→A1 搬运计划：Ota_CalcCopyPlan 按固件长度填写，Ota_CopyA2ToA1 按它执行
typedef struct {
    u8  PageBuf[ST32_PAGE_SIZE];   // 单页搬运缓冲：A2→RAM→A1 的中转区
    u8  PageNB;                    // 页块总数（含末块）；0 = 计划无效，不执行搬运
    u16 TailHalfwords;             // 末块长度（半字数）：末块通常不满一页
} OtaCopyPlan_CB;

extern OTA_InfoCB OTA_Info;   // OTA 状态块的 RAM 副本（定义在 boot.c，上电由 main 从 24C02 读入）

// LCD 错误提示登记：引导阶段屏幕尚未初始化，先在此记下错误，
// 由 main 在屏就绪后统一显示（配合 CLEARGDRAMH() 清背景）
extern u8 g_ota_lcd_err;    // 待显示的 OTA_ERR_* 错误码；0 = 无错误
extern u8 g_ota_lcd_hold;   // 1 = 显示后停留 3 秒再尝试进应用；0 = 显示即返回

/* ---- 上电决策 ---- */
// 上电三分支：① flag≠A1 → 校验后搬 A2→A1；② flag=A1 但 A1 损坏 → 自愈重搬；
// ③ 其余 → 直接返回，由 main 决定联网 OTA 还是跳应用
void BootLoader_Branch(void);

/* ---- 跳转 ---- */
__asm void MSR_MSP(uint32_t addr); // 将主栈指针 MSP 设为 addr（Keil 内联汇编）
void Boot_JumpToA1(uint32_t addr); // 跳转 A1 应用；A1 不可运行时打印提示后返回
void BootLoader_Clear(void);       // 复位 B 区占用的外设，跳转前调用

/* ---- A2→A1 搬运 ---- */
void Ota_CalcCopyPlan(void);  // 按 OTA_size 计算页块数与末块长度，写入 g_CopyPlan
void Ota_CopyA2ToA1(void);    // 按搬运计划逐页把 A2 内容复制到 A1
u8   OtaCopyVerify(void);     // 搬运校验：A1 与 A2 前 OTA_size 字节一致返回 1

#endif