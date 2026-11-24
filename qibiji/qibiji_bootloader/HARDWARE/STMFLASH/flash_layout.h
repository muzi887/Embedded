#ifndef __FLASH_LAYOUT_H__
#define __FLASH_LAYOUT_H__

/* Flash布局 */
#include "stmflash.h"   /* 取 STM32_FLASH_SIZE（片内容量，单位 KB）：A2 越界断言依赖它 */

/* 片内 Flash 物理参数（F103VET6：512KB，每页 2KB，共 256 页） */
#define ST32_FLASH_SADDR    0x08000000  /* Flash 基地址：三个分区的地址全部由它推出 */
#define ST32_PAGE_SIZE      2048        /* 每页字节数：F103 高容量型固定 2KB，擦除与搬运都以页为单位 */
#define ST32_PAGE_NUM       256         /* 总页数 = 512KB / 2KB */

/* B 区（BootLoader 自身）：从 Flash 基地址开始，大小由页数决定 */
#define ST32_B_PAGE_NUM     28          /* B 区页数：28 页 x 2KB = 56KB（0x08000000 ~ 0x0800E000），必须与 Keil 工程 IROM1 的 Size 一致 */

/* A1 区（应用运行区）：紧接 B 区，起始页号 = B 区总页数 */
#define ST32_A1_START_PAGE  ST32_B_PAGE_NUM                                            /* A1 起始页号：改 B 区页数它自动跟着动 */
#define ST32_A1_PAGE_NUM    100                                                        /* A1 页数：100 页 x 2KB = 200KB */
#define ST32_A1_SADDR       (ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE)   /* A1 起始地址（当前 = 0x0800E000），宏必须整体加括号后再参与运算 */

/* A2 区（下载固件暂存区）：紧接 A1 区 */
#define ST32_A2_START_PAGE  (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                    /* A2 起始页号 = A1 末页 + 1（当前 = 128） */
#define ST32_A2_PAGE_NUM    100                                                        /* A2 页数：100 页 x 2KB = 200KB */
#define ST32_A2_SADDR       (ST32_FLASH_SADDR + ST32_A2_START_PAGE * ST32_PAGE_SIZE)   /* A2 起始地址（当前 = 0x08040000） */

/* 分区编译期断言：全部是宏与字面量的常量表达式，编译期即可求值；
   任何一条不满足时数组长度为 -1，编译直接失败，把"烧到板上才发现分区错位"提前到编译窗口。
   宏全部整体加括号：不带括号时 *(volatile u32 *)ST32_A2_SADDR 这类转型会把指针运算截断。 */
typedef char flash_layout_check[
    (ST32_A1_START_PAGE == ST32_B_PAGE_NUM                                                     /* A1 紧接 B 区 */
     && ST32_A2_START_PAGE == (ST32_A1_START_PAGE + ST32_A1_PAGE_NUM)                          /* A2 紧接 A1 区 */
     && ((ST32_A2_START_PAGE + ST32_A2_PAGE_NUM) * ST32_PAGE_SIZE) <= (1024 * STM32_FLASH_SIZE) /* A2 末页不越过片内 Flash 尾部 */
     && (ST32_A1_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024)                                    /* A1 固定 200KB：OTA 固件上限依据 */
     && (ST32_A2_PAGE_NUM * ST32_PAGE_SIZE) == (200 * 1024)                                    /* A2 固定 200KB */
     && (ST32_B_PAGE_NUM * ST32_PAGE_SIZE) == 0xE000                                           /* B 区 56KB：必须与 Keil IROM1 的 Size 一致 */
    ) ? 1 : -1];

#endif
