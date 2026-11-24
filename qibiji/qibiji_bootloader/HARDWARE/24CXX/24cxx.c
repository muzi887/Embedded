#include "24cxx.h"
#include <stddef.h>     /* NULL：Write/Read 入参判空用 */
#include "delay.h"      /* delay_ms：每次字节写后等内部写周期完成 */

/* 初始化：24CXX 自身无寄存器可配 */
void AT24CXX_Init(void)
{
    IIC_Init();
}

/* 单字节读：先写目标地址（写命令寻址），再重启总线转入读命令收数据。
   AT24C02 的 ReadAddr ≤ 255，走"器件地址 + 单字节地址"分支（双字节分支留作大容量兼容） */
u8 AT24CXX_ReadOneByte(u16 ReadAddr)
{
    u8 temp = 0;                        /* 接收到的数据 */

    IIC_Start();
    if (EE_TYPE > AT24C16)              /* 双字节地址寻址（AT24C32 及以上） */
    {
        IIC_Send_Byte(0xA0);            /* 器件地址 + 写命令 */
        IIC_Wait_Ack();
        IIC_Send_Byte(ReadAddr >> 8);   /* 地址高字节 */
        IIC_Wait_Ack();
    }
    else
        IIC_Send_Byte(0xA0 + ((ReadAddr / 256) << 1));  /* 器件地址 0xA0 带页位 + 写命令；AT24C02 恒为 0xA0 */

    IIC_Wait_Ack();
    IIC_Send_Byte(ReadAddr % 256);                      /* 地址低字节（AT24C02 即全部地址位） */
    IIC_Wait_Ack();
    IIC_Start();                                        /* 重启总线：写寻址切换到读 */
    IIC_Send_Byte(0xA1 + ((ReadAddr / 256) << 1));      /* 器件地址 + 读命令 */
    IIC_Wait_Ack();
    temp = IIC_Read_Byte(0);                            /* 只读一个字节：回 NACK 结束 */
    IIC_Stop();
    return temp;
}

/* 单字节写：写器件地址、字节地址、数据三步，每步等 ACK；任一步无应答立即停止返回失败。
   结尾 delay_ms(10) 是等 AT24C02 的内部写周期（约 5~10ms），期间芯片不响应新命令 */
u8 AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite)
{
    u8 ack_result;                          /* 每一步的应答结果 */

    IIC_Start();
    if (EE_TYPE > AT24C16)                  /* 双字节地址分支 */
    {
        IIC_Send_Byte(0xA0);
        ack_result = IIC_Wait_Ack();
        if (ack_result != 0)
        {
            IIC_Stop();
            return AT24CXX_ERROR;           /* 器件地址都无应答：芯片不在位 */
        }
        IIC_Send_Byte(WriteAddr >> 8);      /* 地址高字节 */
        ack_result = IIC_Wait_Ack();
        if (ack_result != 0)
        {
            IIC_Stop();
            return AT24CXX_ERROR;
        }
    }
    else
    {
        IIC_Send_Byte(0xA0 + ((WriteAddr / 256) << 1));     /* 器件地址 + 写命令 */
        ack_result = IIC_Wait_Ack();
        if (ack_result != 0)
        {
            IIC_Stop();
            return AT24CXX_ERROR;
        }
    }

    IIC_Send_Byte(WriteAddr % 256);         /* 字节地址 */
    ack_result = IIC_Wait_Ack();
    if (ack_result != 0)
    {
        IIC_Stop();
        return AT24CXX_ERROR;
    }

    IIC_Send_Byte(DataToWrite);             /* 数据字节 */
    ack_result = IIC_Wait_Ack();
    if (ack_result != 0)
    {
        IIC_Stop();
        return AT24CXX_ERROR;
    }

    IIC_Stop();
    delay_ms(10);                           /* 等内部写周期完成，期间该页不可访问 */
    return AT24CXX_OK;
}

/* 把一个 16/32 位整数按小端（低字节在前）逐字节写入连续地址 */
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len)
{
    u8 t;                                   /* 字节循环计数 */
    for (t = 0; t < Len; t++)
    {
        AT24CXX_WriteOneByte(WriteAddr + t, (DataToWrite >> (8 * t)) & 0xff);   /* 取第 t 个字节写入 */
    }
}

/* 把 Len 个连续字节按小端拼回一个 16/32 位整数；Len 取 2 或 4 */
u32 AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len)
{
    u8 t;                                   /* 字节循环计数 */
    u32 temp = 0;                           /* 拼装结果 */
    for (t = 0; t < Len; t++)
    {
        temp <<= 8;                         /* 先腾高位，再从最高地址字节开始装 */
        temp += AT24CXX_ReadOneByte(ReadAddr + Len - t - 1);
    }
    return temp;
}

/* 定长写：带空指针与地址越界检查；逐字节调 WriteOneByte，任一字节失败即中止返回 */
u8 AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite)
{
    u8 result;                              /* 单字节写的返回码 */
    u16 write_count = 0;                    /* 已成功写入的字节计数（保留调试信息用） */

    if (pBuffer == NULL)                    /* 空指针直接拒绝 */
    {
        return AT24CXX_ERROR;
    }
    if (WriteAddr + NumToWrite > EE_TYPE + 1)       /* 起始 + 长度不得越过芯片容量 */
    {
        return AT24CXX_ADDR_ERROR;
    }

    while (NumToWrite--)
    {
        result = AT24CXX_WriteOneByte(WriteAddr, *pBuffer);
        if (result != AT24CXX_OK)
        {
            return AT24CXX_ERROR;           /* 中途失败：直接报错（已写部分不回滚，调用方需知晓） */
        }
        WriteAddr++;
        pBuffer++;
        write_count++;
    }
    return AT24CXX_OK;
}

/* 定长读：逐字节调 ReadOneByte 顺次填入缓冲，无失败路径（读操作不涉及内部写周期） */
void AT24CXX_Read(u16 ReadAddr, u8 *pBuffer, u16 NumToRead)
{
    while (NumToRead)
    {
        *pBuffer++ = AT24CXX_ReadOneByte(ReadAddr++);
        NumToRead--;
    }
}

/* 上电自检 + 0x55 标志管理（播种动作上移给调用方）：
   返回 AT24CXX_OK(0)    = 标志已是 0x55，EEPROM 内容可信；
   返回 AT24CXX_FRESH(2) = 标志缺失但补写成功，是新芯片/被擦过，调用方应立即播种默认参数；
   返回 AT24CXX_ERROR(1) = 写标志后仍读不回 0x55，芯片读写链路故障 */
u8 AT24CXX_Check(void)
{
    u8 temp;                                /* 标志字节暂存 */

    temp = AT24CXX_ReadOneByte(EE_ADDR);    /* 每次上电都读一次标志 */
    if (temp == 0x55)
        return AT24CXX_OK;                  /* 已初始化过，直接可用 */
    else
    {
        AT24CXX_WriteOneByte(EE_ADDR, 0x55);        /*  EEPROM已被使用标志 缺失：补写 0x55 */
        temp = AT24CXX_ReadOneByte(EE_ADDR);        /* 回读验证补写是否真的成功 */
        if (temp == 0x55)
        {
            return AT24CXX_FRESH;           /* 补写成功：芯片硬件正常，只是内容为空，通知调用方播种 */
        }
    }
    return AT24CXX_ERROR;                   /* 写了读不回：芯片或总线故障 */
}
