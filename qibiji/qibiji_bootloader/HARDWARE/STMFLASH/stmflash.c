#include "stmflash.h"

/* 读单个半字：Flash 在地址空间内存映射，直接按 16 位指针取值即可 */
u16 Flash_ReadHalfWord(u32 faddr)
{
    return *(vu16 *)faddr;
}

#if STM32_FLASH_WREN            /* 写路径整体受写使能开关控制：开关为 0 时下面全部不编译 */

#define STM_SECTOR_SIZE 2048    /* 单个擦除页字节数：本芯片 512KB，512KB ÷ 2KB = 256 页 */

/* 整页暂存缓冲：写函数需要先读回整页旧内容、擦除、合入新数据后整页重写 */
static u16 s_sect_buf[STM_SECTOR_SIZE / 2];

/* 无检查连续写：逐半字调用固件库编程、地址按字节推进；
   不做擦除判断，调用方必须保证目标区间已经擦除 */
static void Flash_WriteNoCheck(u32 WriteAddr, u16 *pBuffer, u16 NumToWrite)
{
    u16 i;                                                  /* 半字循环计数 */
    for (i = 0; i < NumToWrite; i++)
    {
        FLASH_ProgramHalfWord(WriteAddr, pBuffer[i]);       /* 编程一个半字 */
        WriteAddr += 2;                                     /* 地址按 2 字节推进 */
    }
}

/* 按半字写入任意长度：以页为单位自动分片；
   对每一段先判断目标区间是否为空白（0xFFFF）：非空白则整页擦除、
   把新数据合入页缓冲后整页写回，从而只改目标区间、不破坏同页其它数据 */
void Flash_Write(u32 WriteAddr, u16 *pBuffer, u16 NumToWrite)
{
    u32 secpos;     /* 当前页序号（相对 Flash 基地址） */
    u16 secoff;     /* 目标在本页内的偏移，单位：半字 */
    u16 secremain;  /* 本轮写入长度，单位：半字（首轮取"页剩余容量"与"请求长度"的较小值） */
    u16 i;          /* 空白检查与数据合并的循环计数 */
    u32 offaddr;    /* 相对 Flash 基地址的字节偏移 */

    if (WriteAddr < STM32_FLASH_BASE || (WriteAddr >= (STM32_FLASH_BASE + 1024 * STM32_FLASH_SIZE))) return;   /* 越出片内 Flash 的地址直接丢弃 */

    FLASH_Unlock();                             /* 解锁：写寄存器序列被接受的前提 */
    offaddr = WriteAddr - STM32_FLASH_BASE;     /* 换算成片内偏移 */
    secpos = offaddr / STM_SECTOR_SIZE;         /* 起始页号 */
    secoff = (offaddr % STM_SECTOR_SIZE) / 2;   /* 页内字节偏移折算成半字数 */
    secremain = STM_SECTOR_SIZE / 2 - secoff;   /* 本页剩余可写半字数 */
    if (NumToWrite <= secremain) secremain = NumToWrite;    /* 请求不跨页时，本轮长度就是请求长度 */

    while (1)
    {
        Flash_Read(secpos * STM_SECTOR_SIZE + STM32_FLASH_BASE, s_sect_buf, STM_SECTOR_SIZE / 2);   /* 读回整页旧内容 */
        for (i = 0; i < secremain; i++)     /* 检查目标区间是否全为 0xFFFF（空白） */
        {
            if (s_sect_buf[secoff + i] != 0xFFFF) break;    /* 出现非空值 → 必须先擦除 */
        }
        if (i < secremain)                  /* 目标区间有旧数据：整页擦除后合入重写 */
        {
            FLASH_ErasePage(secpos * STM_SECTOR_SIZE + STM32_FLASH_BASE);   /* 擦除整页（内容全部回到 0xFFFF） */
            for (i = 0; i < secremain; i++)
            {
                s_sect_buf[i + secoff] = pBuffer[i];    /* 新数据覆盖进页缓冲的目标区间，页内其它数据保持读回值 */
            }
            Flash_WriteNoCheck(secpos * STM_SECTOR_SIZE + STM32_FLASH_BASE, s_sect_buf, STM_SECTOR_SIZE / 2);   /* 整页写回 */
        }
        else
        {
            Flash_WriteNoCheck(WriteAddr, pBuffer, secremain);  /* 目标区间已是空白：直接编程，省一次擦写损耗 */
        }
        if (NumToWrite == secremain) break;     /* 请求全部写完，收尾 */
        else                                    /* 还有剩余，切到下一页继续 */
        {
            secpos++;                           /* 页号 +1 */
            secoff = 0;                         /* 下一页从页首开始 */
            pBuffer += secremain;               /* 源指针滑过已写的半字数 */
            WriteAddr += secremain * 2;          /* 地址按字节推进：本轮写了 secremain 个半字 = secremain*2 字节 */
            NumToWrite -= secremain;            /* 剩余请求长度同步扣减 */
            if (NumToWrite > (STM_SECTOR_SIZE / 2)) secremain = STM_SECTOR_SIZE / 2;    /* 剩余仍超过一页：下一轮写满整页 */
            else secremain = NumToWrite;        /* 剩余不足一页：下一轮写完收尾 */
        }
    }
    FLASH_Lock();                               /* 重新上锁，防止后续代码误触发编程操作 */
}

#endif

/* 按半字连续读出：逐个取半字、地址按 2 字节推进 */
void Flash_Read(u32 ReadAddr, u16 *pBuffer, u16 NumToRead)
{
    u16 i;                                      /* 半字循环计数 */
    for (i = 0; i < NumToRead; i++)
    {
        pBuffer[i] = Flash_ReadHalfWord(ReadAddr);  /* 取一个半字 */
        ReadAddr += 2;                              /* 地址按 2 字节推进 */
    }
}
