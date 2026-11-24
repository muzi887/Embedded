#ifndef __STMFLASH_H__
#define __STMFLASH_H__

/* 驱动层：怎么往片内 Flash 读、写、擦 */
#include "stm32f10x.h"  /* vu16 类型与固件库声明 */

/* 片内 Flash 物理参数（本模块的越界检查与分页逻辑使用；flash_layout.h 的断言也引用容量值） */
#define STM32_FLASH_SIZE    512         /* 片内 Flash 总容量，单位 KB：F103VET6 = 512 */
#define STM32_FLASH_BASE    0x08000000  /* Flash 基地址：合法地址检查、扇区定位都相对它计算 */
#define STM32_FLASH_WREN    1           /* 写使能开关：1 = 编入全部写函数；0 = 只保留读函数（写函数整体被条件编译剔除） */

/* 读单个半字：地址必须是 2 的倍数（Flash 按半字内存映射） */
u16  Flash_ReadHalfWord(u32 faddr);

/* 按半字连续写入：自动按页分片，擦除前把整页旧内容读回合并重写；
   NumToWrite 的单位是"半字个数"（不是字节） */
void Flash_Write(u32 WriteAddr, u16 *pBuffer, u16 NumToWrite);

/* 按半字连续读出：NumToRead 的单位是"半字个数"（不是字节） */
void Flash_Read(u32 ReadAddr, u16 *pBuffer, u16 NumToRead);

#endif
