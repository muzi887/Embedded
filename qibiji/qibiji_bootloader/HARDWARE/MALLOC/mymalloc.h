#ifndef __MYMALLOC_H
#define __MYMALLOC_H

#include "stm32f10x.h"   /* u8/u16/u32 基础类型 */

/* 内存池参数：10KB 池、32 字节块（芯片 STM32F103VE，RAM 64KB，池占用有余量） */
#define MEM1_BLOCK_SIZE        32                             /* 块大小 32 字节：最小分配粒度 */
#define MEM1_MAX_SIZE          10*1024                        /* 池总大小 10KB */
#define MEM1_ALLOC_TABLE_SIZE  MEM1_MAX_SIZE/MEM1_BLOCK_SIZE  /* 管理表项数 = 320 */

/* ---- 对外 API ---- */
void *mymalloc(u32 size);             /* 分配：返回池内地址；池满/size=0 返回 NULL */
void  myfree(void *ptr);              /* 释放：ptr 必须是 mymalloc 返回的地址；NULL 直接返回 */
void *myrealloc(void *ptr, u32 size); /* 重分配：新块+拷贝+释放旧块；失败返回 NULL 且旧块保留 */

#endif
