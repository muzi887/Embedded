#ifndef __24CXX_H
#define __24CXX_H

/* 芯片驱动层 */
#include "myiic.h"      /* AT24CXX 的所有读写都经模拟 I²C 总线进行 */

/* 24 系列芯片容量表（单位：字节，值为"最大地址"，即容量 - 1）。
   EE_TYPE 从中取一颗，地址寻址方式与越界检查都依赖它 */
#define AT24C01     127
#define AT24C02     255     /* 本工程实际使用：256 字节 */
#define AT24C04     511
#define AT24C08     1023
#define AT24C16     2047    /* 寻址分界：≤ 本值的芯片用"器件地址低位 + 单字节地址"寻址 */
#define AT24C32     4095    /*  > 本值则要双字节地址寻址（本工程不会走到该分支） */
#define AT24C64     8191
#define AT24C128    16383
#define AT24C256    32767

/* 读写函数返回码（写类函数与 AT24CXX_Check 共用这套取值） */
#define AT24CXX_OK          0   /* 成功 */
#define AT24CXX_ERROR       1   /* 失败：任一环节未收到从机 ACK（多为芯片不在位/总线故障） */
#define AT24CXX_FRESH       2   /* 仅 Check 使用：0x55 标志缺失但补写成功，调用方应播种默认参数 */
#define AT24CXX_ADDR_ERROR  3   /* 写地址区间越出芯片容量 */

/* 芯片型号：AT24C02（256 字节，8 位页地址寻址） */
#define EE_TYPE     AT24C02

/* 初始化标志地址：上电 Check 在这里读/写 0x55，判断 EEPROM 是否已初始化。
   250 位于 OTA 状态块（200~243）与自愈计数（244~249）之后，是 24C02 的最后一个约定槽位 */
#define EE_ADDR     250

void AT24CXX_Init(void);                                            /* 初始化 */
u8   AT24CXX_ReadOneByte(u16 ReadAddr);                             /* 单字节读（ReadAddr 为字节地址 0~255） */
u8   AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite);           /* 单字节写：任一环节无 ACK 即返回 AT24CXX_ERROR */
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len);  /* 把一个 16/32 位整数按小端逐字节写入 */
u32  AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len);                     /* 把 Len 个字节按小端拼回 16/32 位整数 */
u8   AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite);     /* 定长写：带 NULL 与越界检查 */
void AT24CXX_Read(u16 ReadAddr, u8 *pBuffer, u16 NumToRead);        /* 定长读 */
u8   AT24CXX_Check(void);                                           /* 上电自检 + 0x55 标志管理，返回 0/1/2（见 §5） */

#endif
