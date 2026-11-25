#ifndef BOOT_H
#define BOOT_H

#include "sys.h"
#include <stddef.h>
#include "stmflash.h"   /* 提供 STM32_FLASH_SIZE：分区断言用它核对片内 Flash 总容量 */
#include "24cxx.h"      /* 提供 EE_ADDR：分区断言用它核对 EEPROM 地址边界 */
#include "reg.h"

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
#define  ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                         // A1 起始页号 = B 区页数（两区紧邻）
#define  ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE) // A1 起始地址，由宏链推导，当前 0x0800E000
#define  ST32_A2_PAGE_NUM    100                                                          // A2 区页数：新固件下载暂存区，共 200KB
#define  ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                     // A2 起始页号 = A1 末页（两区紧邻）
#define  ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)    // A2 起始地址，由宏链推导，当前 0x08040000

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

/* ---- 上电决策（boot.c） ---- */
// 上电三分支：① flag≠A1 → 校验后搬 A2→A1；② flag=A1 但 A1 损坏 → 自愈重搬；
// ③ 其余 → 直接返回，由 main 决定联网 OTA 还是跳应用
void Boot_Branch(void);

/* ---- 跳转（boot.c） ---- */
void Boot_JumpToA1(uint32_t addr); // 跳转 A1 应用；A1 不可运行时打印提示后返回
void Boot_Clear(void);            // 复位 B 区占用的外设，跳转前调用

/* ---- A2→A1 搬运（ota_copy.c） ---- */
void Ota_CalcCopyPlan(void);  // 按 OTA_size 计算页块数与末块长度，写入 g_CopyPlan
void Ota_CopyA2ToA1(void);    // 按搬运计划逐页把 A2 内容复制到 A1
u8   Ota_CopyVerify(void);     // 搬运校验：A1 与 A2 前 OTA_size 字节一致返回 1

#endif
