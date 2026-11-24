# EEPROM 硬件访问层重构（迁移顺序阶段 5）

> 来源：`qibiji_boot/HARDWARE/IIC/myiic.c/.h` + `qibiji_boot/HARDWARE/24CXX/24cxx.c/.h`。
> 本阶段做：模块重排、删死代码、注释按现行规范全部新写；**AT24CXX/驱动函数名保持原名**（按既定"驱动原样平移"约定）。
> 唯一的逻辑边界调整：`AT24CXX_Check()` 不再调用 `paraToEeprom()`（消除 24CXX → REG 的反向依赖），详见 §5 与 §9。
> 只生成 md，不动工程文件。

## 1. 本阶段范围与产出清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/IIC/myiic.h` | 重写 | GPIO 模拟 I²C 的引脚宏与函数声明；删 2 个死声明（见 §9） |
| `HARDWARE/IIC/myiic.c` | 重写 | 时序逻辑逐行等价；删未使用 include 无（原本只用 delay.h，保留） |
| `HARDWARE/24CXX/24cxx.h` | 重写 | 返回码表调整：删无人使用的 `AT24CXX_TIMEOUT`，新增 `AT24CXX_FRESH`（与 Check 新契约配套，值同为 2，见 §5）；删 4 个死声明 |
| `HARDWARE/24CXX/24cxx.c` | 重写 | 读写字节/定长函数逐行等价；`AT24CXX_Check` 摘除播种调用；删 4 个死函数与 2 个死 include |
| `USER/main.c` | 替换 | 本阶段验证代码：字节写读比对 + 定长写读比对 + Check 结果打印（§6） |
| Keil 工程 | 手动加 | HARDWARE 组加 `myiic.c`、`24cxx.c`；Include Paths 加 `..\HARDWARE\IIC`、`..\HARDWARE\24CXX`（§7） |

**为什么 Check 要摘除播种**：原 `AT24CXX_Check()` 里直接调 `paraToEeprom()`（写 OTA 默认值 + MQTT 键），
这使"EEPROM 硬件访问层"反向依赖"业务参数层"——阶段 5 编译时 reg 还没入库。硬件层的职责只到
「检测芯片可用 + 维护 0x55 标志」为止；"新芯片要不要播种、播什么默认值"是 REG 层（阶段 6）的事。

## 2. myiic.h（重写，全文）

```c
#ifndef __MYIIC_H
#define __MYIIC_H
#include "sys.h"    /* PBout/PBin 位带宏：把引脚当普通变量读写 */

/* SDA 方向切换：I²C 的 SDA 是双向线，本模拟实现不做开漏，靠改 CRH 在输入/输出间切换。
   改的是 CRH 中管理 PB11 的半字节（bit12~15），其余引脚配置位用掩码保住 */
#define SDA_IN()  {GPIOB->CRH &= 0xFFFF0FFF; GPIOB->CRH |= 8 << 12;}   /* 8 = 上拉/下拉输入模式：读从机应答和数据时用 */
#define SDA_OUT() {GPIOB->CRH &= 0xFFFF0FFF; GPIOB->CRH |= 3 << 12;}   /* 3 = 通用推挽输出 50MHz：主动驱动 SDA 时用 */

/* 引脚到位带的映射（I²C 引脚固定分配在 PB10/PB11，换脚须连这里一起改） */
#define IIC_SCL    PBout(10)    /* SCL 时钟线：主机输出，恒为推挽 */
#define IIC_SDA    PBout(11)    /* SDA 数据线输出视角：主机主动写 0/1 */
#define READ_SDA   PBin(11)     /* SDA 数据线输入视角：读从机放上来的电平 */

void IIC_Init(void);                    /* 两引脚配置为推挽输出并拉高：I²C 空闲态两线均为高 */
void IIC_Start(void);                   /* 起始信号：SCL 高期间 SDA 由高变低 */
void IIC_Stop(void);                    /* 停止信号：SCL 高期间 SDA 由低变高 */
void IIC_Send_Byte(u8 txd);             /* 按位从高到低发一个字节；只发送，不收应答 */
u8   IIC_Read_Byte(unsigned char ack);  /* 按位收一个字节；ack=1 收完回 ACK，ack=0 回 NACK（最后一字节用） */
u8   IIC_Wait_Ack(void);                /* 等从机应答：返回 0 = 应答到，1 = 超时无应答 */
void IIC_Ack(void);                     /* 主机发 ACK：告诉从机"继续发" */
void IIC_NAck(void);                    /* 主机发 NACK：告诉从机"读完了" */

#endif
```

## 3. myiic.c（重写，全文）

```c
#include "myiic.h"
#include "delay.h"      /* delay_us：I²C 时序节拍，全部按微秒级手排延时 */

/* 两引脚配为推挽输出并拉高：上电后 I²C 总线进入空闲态（两线均为高电平） */
void IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;                                        /* GPIO 配置模板 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);                       /* 打开 GPIOB 端口时钟，不开启则配置无效 */

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;                    /* PB10 = SCL，PB11 = SDA */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;                            /* 推挽输出：模拟 I²C 由主机强驱动两线 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);                                      /* 应用配置 */
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_11);                             /* 两线拉高 = 总线空闲 */
}

/* 起始信号：SCL 为高时把 SDA 从 1 拉到 0，从机看到即进入接收准备 */
void IIC_Start(void)
{
    SDA_OUT();              /* SDA 切输出：起始信号由主机驱动 */
    IIC_SDA = 1;
    IIC_SCL = 1;            /* 空闲态两线高，保证 SDA 下降沿发生在 SCL 高电平期间 */
    delay_us(4);            /* 建立时间：电平稳定后再动作 */
    IIC_SDA = 0;
    delay_us(4);
    IIC_SCL = 0;            /* 拉低时钟：钳住总线，随后可以开始送位 */
}

/* 停止信号：SCL 为高时把 SDA 从 0 抬到 1，从机看到即释放总线 */
void IIC_Stop(void)
{
    SDA_OUT();
    IIC_SCL = 0;
    IIC_SDA = 0;            /* 先保证 SDA 处于低，才有"低到高"的停止沿 */
    delay_us(4);
    IIC_SCL = 1;
    IIC_SDA = 1;            /* SCL 高期间的上升沿 = 停止 */
    delay_us(4);
}

/* 等待从机应答：第 9 个时钟把 SDA 交给从机，从机拉低表示 ACK。
   返回 0 = 收到应答；返回 1 = 约 250us 内 SDA 一直为高（无应答），顺带发停止释放总线 */
u8 IIC_Wait_Ack(void)
{
    u8 ucErrTime = 0;       /* 轮询计数：每圈约 2us，250 圈即超时上限 */

    SDA_IN();               /* SDA 切输入：电平由从机决定 */
    IIC_SDA = 1;            /* 先释放总线（输出锁存置 1，切回输出时不会突然拉低） */
    delay_us(1);
    IIC_SCL = 1;            /* 第 9 个时钟高电平：此刻读 SDA */
    delay_us(1);
    while (READ_SDA)        /* SDA 仍为高 = 还没应答 */
    {
        ucErrTime++;
        if (ucErrTime > 250)
        {
            IIC_Stop();     /* 无应答：发停止，把总线恢复到空闲，避免卡死 */
            return 1;
        }
    }
    IIC_SCL = 0;            /* 收到应答：拉低时钟，准备下一个字节 */
    return 0;
}

/* 主机发 ACK：第 9 个时钟期间主机把 SDA 拉低，告诉从机"还想继续读" */
void IIC_Ack(void)
{
    IIC_SCL = 0;
    SDA_OUT();
    IIC_SDA = 0;            /* 低电平 = ACK */
    delay_us(2);
    IIC_SCL = 1;            /* 从机在第 9 个时钟高电平期间采样 */
    delay_us(2);
    IIC_SCL = 0;            /* 时钟回落，一拍结束 */
}

/* 主机发 NACK：第 9 个时钟期间 SDA 保持高，告诉从机"这是最后一个字节" */
void IIC_NAck(void)
{
    IIC_SCL = 0;
    SDA_OUT();
    IIC_SDA = 1;            /* 高电平 = NACK */
    delay_us(2);
    IIC_SCL = 1;
    delay_us(2);
    IIC_SCL = 0;
}

/* 发送一个字节：MSB 在前逐位移出；每位移出后给一段时钟脉冲让从机采样。
   只负责发送 8 位，应答由调用方随后调 IIC_Wait_Ack() 收 */
void IIC_Send_Byte(u8 txd)
{
    u8 t;                   /* 位循环计数，8 位一个字节 */
    SDA_OUT();
    IIC_SCL = 0;            /* 时钟先拉低：数据在 SCL 低电平期间变化才合法 */
    for (t = 0; t < 8; t++)
    {
        if ((txd & 0x80) >> 7)      /* 取当前最高位 */
            IIC_SDA = 1;
        else
            IIC_SDA = 0;
        txd <<= 1;          /* 左移为下一位做准备 */
        delay_us(2);        /* 数据建立时间 */
        IIC_SCL = 1;        /* 时钟高：从机在此采样当前位 */
        delay_us(2);
        IIC_SCL = 0;        /* 时钟低：本位结束 */
        delay_us(2);
    }
}

/* 读一个字节：逐位采样 SDA 拼成字节（MSB 在前）；收完按 ack 决定回 ACK 还是 NACK */
u8 IIC_Read_Byte(unsigned char ack)
{
    unsigned char i, receive = 0;   /* i = 位循环计数；receive = 逐位拼装的接收值 */

    SDA_IN();               /* SDA 切输入：数据由从机驱动 */
    for (i = 0; i < 8; i++)
    {
        IIC_SCL = 0;        /* 时钟低：从机准备下一位 */
        delay_us(2);
        IIC_SCL = 1;        /* 时钟高：此刻采样合法 */
        receive <<= 1;      /* 腾出最低位 */
        if (READ_SDA) receive++;    /* SDA 高则该位置 1 */
        delay_us(1);
    }
    if (!ack)
        IIC_NAck();         /* 最后一字节：回 NACK 结束本次传输 */
    else
        IIC_Ack();          /* 还有后续字节：回 ACK 继续 */
    return receive;
}
```

## 4. 24cxx.h（重写，全文）

```c
#ifndef __24CXX_H
#define __24CXX_H
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

/* 本工程挂的芯片型号：AT24C02（256 字节，8 位页地址寻址） */
#define EE_TYPE     AT24C02

/* 初始化标志地址：上电 Check 在这里读/写 0x55，判断 EEPROM 是否已初始化。
   250 位于 OTA 状态块（200~243）与自愈计数（244~249）之后，是 24C02 的最后一个约定槽位 */
#define EE_ADDR     250

void AT24CXX_Init(void);                                            /* 初始化：就是调 IIC_Init */
u8   AT24CXX_ReadOneByte(u16 ReadAddr);                             /* 单字节读（ReadAddr 为字节地址 0~255） */
u8   AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite);           /* 单字节写：任一环节无 ACK 即返回 AT24CXX_ERROR */
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len);  /* 把一个 16/32 位整数按小端逐字节写入 */
u32  AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len);                     /* 把 Len 个字节按小端拼回 16/32 位整数 */
u8   AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite);     /* 定长写：带 NULL 与越界检查 */
void AT24CXX_Read(u16 ReadAddr, u8 *pBuffer, u16 NumToRead);        /* 定长读 */
u8   AT24CXX_Check(void);                                           /* 上电自检 + 0x55 标志管理，返回 0/1/2（见 §5） */

#endif
```

## 5. 24cxx.c（重写，全文）

读写字节/定长函数与原工程逐行等价；`AT24CXX_Check` 摘除了 `paraToEeprom()` 调用，
返回值从 0/1 扩为 0/1/2（"新芯片"返回 2，由调用方播种）。include 里删掉未使用的
`stdio.h`、`string.h` 和已不存在的对 `reg.h` 的依赖。

```c
#include "24cxx.h"
#include "delay.h"      /* delay_ms：每次字节写后等内部写周期完成 */
#include <stddef.h>     /* NULL：Write/Read 入参判空用（原文件靠 stdio.h 顺带带进来，裁掉后必须显式引入） */

/* 初始化：24CXX 自身无寄存器可配，实际工作全在 IIC_Init */
void AT24CXX_Init(void)
{
    IIC_Init();
}

/* 单字节读：先写目标地址（写命令寻址），再重启总线转入读命令收数据。
   AT24C02 的 ReadAddr ≤ 255，走"器件地址 + 单字节地址"分支（双字节分支留作大容量兼容） */
u8 AT24CXX_ReadOneByte(u16 ReadAddr)
{
    u8 temp = 0;                                                    /* 接收到的数据 */

    IIC_Start();
    if (EE_TYPE > AT24C16)      /* 双字节地址寻址（AT24C32 及以上），本工程不走到 */
    {
        IIC_Send_Byte(0xA0);        /* 器件地址 + 写命令 */
        IIC_Wait_Ack();
        IIC_Send_Byte(ReadAddr >> 8);       /* 地址高字节 */
        IIC_Wait_Ack();
    }
    else
        IIC_Send_Byte(0xA0 + ((ReadAddr / 256) << 1));  /* 器件地址 0xA0 带页位 + 写命令；AT24C02 恒为 0xA0 */

    IIC_Wait_Ack();
    IIC_Send_Byte(ReadAddr % 256);          /* 地址低字节（AT24C02 即全部地址位） */
    IIC_Wait_Ack();
    IIC_Start();                            /* 重启总线：写寻址切换到读 */
    IIC_Send_Byte(0xA1 + ((ReadAddr / 256) << 1));      /* 器件地址 + 读命令 */
    IIC_Wait_Ack();
    temp = IIC_Read_Byte(0);                /* 只读一个字节：回 NACK 结束 */
    IIC_Stop();
    return temp;
}

/* 单字节写：写器件地址、字节地址、数据三步，每步等 ACK；任一步无应答立即停止返回失败。
   结尾 delay_ms(10) 是等 AT24C02 的内部写周期（约 5~10ms），期间芯片不响应新命令 */
u8 AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite)
{
    u8 ack_result;                          /* 每一步的应答结果 */

    IIC_Start();
    if (EE_TYPE > AT24C16)                  /* 双字节地址分支（本工程不走到） */
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

/* 上电自检 + 0x55 标志管理（本阶段的关键调整：播种动作已上移给调用方）：
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
        AT24CXX_WriteOneByte(EE_ADDR, 0x55);        /* 标志缺失：补写 0x55 */
        temp = AT24CXX_ReadOneByte(EE_ADDR);        /* 回读验证补写是否真的成功 */
        if (temp == 0x55)
        {
            return AT24CXX_FRESH;           /* 补写成功：芯片硬件正常，只是内容为空，通知调用方播种 */
        }
    }
    return AT24CXX_ERROR;                   /* 写了读不回：芯片或总线故障 */
}
```

## 6. 本阶段验证 main.c（替换阶段 4 的 main，全文）

三项验证一次跑完：**Check 自检**（标志管理）、**单字节写读**、**定长写读**。
测试地址选 APP 参数区（0~63）内、且每次**先存原值再恢复**——不留痕，不碰 64 以上
的 MQTT 键区和 200 以上的 OTA 区。

```c
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "24cxx.h"

#define TEST_ADDR_BYTE  16      /* 单字节测试地址（APP 参数区内） */
#define TEST_ADDR_LEN   24      /* 定长测试起始地址（连续 8 字节，仍在 APP 参数区内） */
#define TEST_LEN        8       /* 定长测试的字节数 */

/* 定长测试缓冲放 static：8 字节虽小，与后续阶段验证代码风格保持一致 */
static u8 s_save_buf[TEST_LEN];     /* 测试区原值暂存：跑完恢复，不留痕 */
static u8 s_wbuf[TEST_LEN];         /* 待写测试图案 */
static u8 s_rbuf[TEST_LEN];         /* 读回数据 */

int main(void)
{
    u8 i;               /* 字节循环计数 */
    u8 save_byte;       /* 单字节测试地址的原值 */
    u8 chk;             /* AT24CXX_Check 返回值 */
    u8 err_cnt;         /* 定长比对不一致计数 */
    u32 blink_cnt = 0;  /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

    delay_init();
    NVIC_Configuration();
    uart1_init(115200);
    LED_Init();
    LED_CLOSE();

    AT24CXX_Init();     /* I²C 引脚就绪 */

    /* 验证 1：上电自检与标志管理 */
    chk = AT24CXX_Check();
    if (chk == AT24CXX_OK)          printf("check: OK (marker present)\r\n");
    else if (chk == AT24CXX_FRESH)  printf("check: FRESH (marker written, seeding is REG's job)\r\n");
    else                            printf("check: FAIL (eeprom not responding)\r\n");

    /* 验证 2：单字节写读比对（先存原值，跑完恢复） */
    save_byte = AT24CXX_ReadOneByte(TEST_ADDR_BYTE);
    if (AT24CXX_WriteOneByte(TEST_ADDR_BYTE, 0xA5) == AT24CXX_OK
        && AT24CXX_ReadOneByte(TEST_ADDR_BYTE) == 0xA5)
        printf("byte test PASS\r\n");
    else
        printf("byte test FAIL\r\n");
    AT24CXX_WriteOneByte(TEST_ADDR_BYTE, save_byte);        /* 恢复原值 */

    /* 验证 3：定长写读比对（同样先存原值） */
    AT24CXX_Read(TEST_ADDR_LEN, s_save_buf, TEST_LEN);      /* 存原值 */
    for (i = 0; i < TEST_LEN; i++) s_wbuf[i] = (u8)(i * 3 + 1);     /* 预填测试图案 */
    err_cnt = 0;
    if (AT24CXX_Write(TEST_ADDR_LEN, s_wbuf, TEST_LEN) == AT24CXX_OK)
    {
        AT24CXX_Read(TEST_ADDR_LEN, s_rbuf, TEST_LEN);
        for (i = 0; i < TEST_LEN; i++)
            if (s_rbuf[i] != s_wbuf[i]) err_cnt++;
        if (err_cnt == 0) printf("len test PASS (%u bytes)\r\n", (unsigned int)TEST_LEN);
        else          printf("len test FAIL: err_cnt=%u\r\n", (unsigned int)err_cnt);
    }
    else
        printf("len test FAIL: write error\r\n");
    AT24CXX_Write(TEST_ADDR_LEN, s_save_buf, TEST_LEN);     /* 恢复原值 */

    while (1)
    {
        delay_ms(500);              /* 半秒节拍 */
        LED_ERR = !LED_ERR;         /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
        blink_cnt++;                /* 心跳计数递增 */
        printf("eeprom alive, cnt=%u\r\n", (unsigned int)blink_cnt);    /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
    }
}
```

## 7. Keil 配置要点

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `myiic.c`、`24cxx.c` | 两个 .h 是头文件，不进编译清单 |
| Include Paths | 追加 `..\HARDWARE\IIC`、`..\HARDWARE\24CXX` | 24cxx.h include myiic.h，两条路径都要 |
| FWLib | 不变（rcc/gpio/usart/misc/flash） | 本阶段只用 GPIO，无新外设 |
| 依赖 | `delay_us/delay_ms`（阶段 1 已入库） | myiic.c 与 24cxx.c 都依赖 delay |

## 8. 验证清单（全部通过才算本阶段完成）

1. 编译 0 Error；
2. 串口收到 `check: OK (marker present)`——这块板跑过原工程，EE_ADDR 标志应为 0x55；
   若显示 `check: FRESH ...`，说明标志被清过，验证补写链路也正常（REG 播种在阶段 6 接入）；
   显示 `check: FAIL` 则查 I²C 接线（PB10/PB11）与上拉电阻；
3. `byte test PASS` 与 `len test PASS (8 bytes)`；
4. 断电重启后 `check` 仍为 OK（0x55 标志已落盘，掉电保持）；
5. PD14 心跳灯闪烁、`blink_cnt` 递增。

## 9. 死代码清单与跨阶段边界

**删除的死代码**（grep 证实零引用/零实现）：

| 项 | 出处 | 状态 |
|---|---|---|
| `AT24CXX_WriteOneByte_old` | 24cxx.c:43 / 24cxx.h:27 | 有实现但全工程无人调用 |
| `AT24CXX_Write_old` | 24cxx.c:210 / 24cxx.h:31 | 有实现但全工程无人调用 |
| `AT24CXX_Readdatas` | 24cxx.c:130 / 24cxx.h:35 | 有实现但全工程无人调用 |
| `AT24CXX_WriteWord` | 24cxx.c:158 / 24cxx.h:40 | 有实现但全工程无人调用 |
| `Eeprom_Wpara` | 24cxx.h:42 | **只有声明没有实现**（qibiji 主工程的函数，boot 工程从未实现） |
| `IIC_Write_One_Byte / IIC_Read_One_Byte` | myiic.h:24-25 | 只有声明没有实现 |
| `AT24CXX_TIMEOUT` 宏 | 24cxx.h:18 | 无任何函数返回过该值；其取值 2 由新契约的 `AT24CXX_FRESH` 接替 |
| `#include "reg.h"`、`<stdio.h>`、`<string.h>` | 24cxx.c:3-5 | reg.h 依赖随播种上移而消失；stdio/string 在文件内未使用，但 stdio.h 原本顺带提供了 NULL，裁掉后补 `<stddef.h>` 显式引入 |

**跨阶段边界（阶段 6 必须执行的联动）**：

1. **播种调用点移位**：原 `AT24CXX_Check()` 内部的 `paraToEeprom()` 移到 reg.c——阶段 6 的
   `Reg_ReadOtaInfo`（原 `AT24CXX_ReadOTAInfo`）开头改为：
   `chk = AT24CXX_Check(); if (chk == AT24CXX_FRESH) paraToEeprom(); else if (chk == AT24CXX_ERROR) { /* EEPROM 故障：显式置 UPDATE_flag=0 */ }`
2. **原 reg.c:48 的 `if(AT24CXX_Check())` 判断要重写**——旧契约"非 0 即失败"在新契约下会把
   `FRESH(2)` 也当成故障，必须按上面三态分别处理；
3. `AT24CXX_Write/Read` 是阶段 6 REG、阶段 10 GPRS（落盘 OTA_size/ver/flag）、阶段 11 BOOT
   （标志读写）的基础，函数名与返回码保持本阶段定义不变。
