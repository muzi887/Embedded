# USART 重构（usart1 回补接收路径 + usart2 入库）

> 来源：`qibiji_boot/HARDWARE/USART/usart1.c` + `usart.h` + `usart2.c`。
> 阶段 1 骨架版只带了 usart1 的发送路径（当时接收无消费者，且 IDLE 中断里调 `Usart2_Send`
> 会强制链接 usart2.c）。本模块把接收路径按原工程回补，并把 4G 口 usart2 一起入库。
> 逻辑等价：不改任何时序、优先级与缓冲长度，只做注释新写与死声明裁剪。
> 验证 main 替换《REG重构.md》的 REG 验证 main；与 4G 模块联测的 main 见《4G模块重构.md》§4。

## 1. 本模块产出清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/USART/usart.h` | 重写 | 裁掉 usart3/4/5、RS485 等未迁移声明，保留 usart1/2 全部内容（见 §2） |
| `HARDWARE/USART/usart1.c` | 重写 | 回补 PA10 RX + DMA1 通道 5 + IDLE 中断 + 转发逻辑，与原工程逐行等价（见 §3） |
| `HARDWARE/USART/usart2.c` | **新建** | 4G 口：DMA1 通道 6 + IDLE 中断 + 4KB 环形缓冲，与原工程逐行等价（见 §4） |
| `USER/main.c` | 替换 | 本模块验证代码：USART1→USART2 回环测试（见 §5），替换 REG 验证 main |
| Keil 工程 | 手动改 | HARDWARE 组加 `usart2.c`（usart1.c 已在工程，替换内容即可）；FWLib 增勾 `stm32f10x_dma.c`、`stm32f10x_usart.c`（见 §6） |

### 1.1 阶段 1 裁剪项的回补对照

| 阶段 1 骨架版裁剪项 | 本模块动作 |
|---|---|
| PA10 RX 引脚配置、`USART_Mode_Rx \| USART_Mode_Tx` | 回补进 `uart1_init`（与原工程一致） |
| RX DMA（DMA1 通道 5）+ `DMA_Rece_Buf1` + `MYDMA_Enable1` | 回补 |
| IDLE 中断使能 + `USART1_IRQHandler`（PC 数据转发 4G 口） | 回补；usart2.c 本模块入库，`Usart2_Send` 可正常链接 |
| `Usart1_Send` 阻塞数组发送 | 回补（原文件组成，保持完整） |
| usart.h 里 usart3/4/5、RS485 等声明 | 仍不迁：对应模块不在迁移计划内（见 §2 删除清单） |

## 2. usart.h（重写，全文）

```c
#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include "stdio.h"

/* ---- DMA 接收通道对照（本工程只用到 USART1/USART2 两路接收） ----
   USART1 RX = DMA1 通道 5；USART2 RX = DMA1 通道 6。
   原表还有 TX 通道与 USART3/4/5 的映射，对应模块未迁移，需要时查参考手册。 */

/* 接收缓冲长度：DMA 单帧最大字节数；IDLE 中断按「本帧实收字节数」取走数据后重启 DMA */
#define DMA_Rec1_Len  256    /* USART1 接收缓冲：调试口，PC 命令帧短，256 足够 */
#define DMA_Rec2_Len  1024   /* USART2 接收缓冲：4G 口透传帧可达 1KB */

/* USART2 接收环形缓冲总长：必须为 2 的幂（usart2.c 里有编译期检查强制保证）。
   DMA 单帧最多 1KB，环形缓冲给到 4KB，可容纳多个未消费帧不丢数据 */
#define USART2_RX_RING_SIZE  4096

/* printf 总开关（阶段 1 引入，原样保留）。
   1 = printf 正常输出（fputc 重定向到 USART1，实现见 usart1.c）；
   0 = printf 编译期替换为空操作，所有调用点被优化掉、不占代码空间，
       需要产出"静默"固件时改这一个数字即可，调用点不用动。 */
#define ENABLE_PRINTF  1

#if !ENABLE_PRINTF
#define printf(...)  ((void)0)   /* 空操作替换；仅在开关关闭时才会编译到此行 */
#endif

extern char DMA_Rece_Buf1[DMA_Rec1_Len];   /* USART1 DMA 接收缓冲：硬件逐字节自动写入 */
extern char DMA_Rece_Buf2[DMA_Rec2_Len];   /* USART2 DMA 接收缓冲：同上（4G 口） */

extern u16 Usart1_Rec_Cnt;   /* USART1 最近一帧实收字节数，IDLE 中断里更新 */
extern u16 Usart2_Rec_Cnt;   /* USART2 最近一帧实收字节数，同上 */

/* ---- 初始化 ---- */
void uart1_init(uint32_t bound);   /* 调试口：PA9 TX / PA10 RX，bound 为波特率（原工程 115200） */
void uart2_init(uint32_t bound);   /* 4G 口：PA2 TX / PA3 RX */

/* ---- 阻塞式数组发送：逐字节等 TC（发送完成）再发下一个 ---- */
void Usart1_Send(char *buf, u16 len);
void Usart2_Send(char *buf, u16 len);

/* ---- DMA 接收重启：IDLE 中断取走一帧后调用，让 DMA 从缓冲头重新写入 ---- */
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx);   /* 重启 USART1 的 DMA1 通道 5 */
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx);   /* 重启 USART2 的 DMA1 通道 6 */

/* ---- USART2 接收环形缓冲：中断只写入，业务层只读取/消费（实现见 usart2.c） ---- */
u16 Usart2_RxAvailable(void);                /* 当前可读字节数 */
u16 Usart2_RxCopy(char *dst, u16 max_len);   /* 拷出最多 max_len 字节（只读不消费） */
void Usart2_RxConsume(u16 len);              /* 消费 len 字节（移动读指针） */
void Usart2_RxClear(void);                   /* 清空缓冲（读指针追平写指针） */
u32  Usart2_RxOverflowCount(void);           /* 溢出累计：环形缓冲满时每帧记一次并丢弃本帧剩余字节 */

#endif
```

**删除的声明**（对应模块不迁移，grep 证实 B 区无引用）：
usart3/4/5 全套（`uart3/4/5_init`、`Usart3/4/5_Send`、`MYDMA_Enable3/4`、`DMA_Rec3/4_Len`、
`DMA_Rece_Buf3/4`、`Usart3/4/5_Rec_Cnt`）、通用 `MYDMA_Enable`、`Dma_Tx1..4_okflag`
（定义在未迁移的 usart3/4/5.c 里）、`RS485_CE4_H/L` 宏（PD6 方向控制，属未迁移的 RS485 电路）。

## 3. usart1.c（重写，全文）

```c
#include "usart.h"

/* ---- printf 重定向（不启用 MicroLIB 的标准做法） ----
   用 __use_no_semihosting 摆脱半主机模式：半主机下 printf 会触发 SWI 陷阱，
   脱离调试器单独运行时程序直接卡死，所以必须补齐标准库要求的符号。 */
#if 1
#pragma import(__use_no_semihosting)

/* 标准库要求 FILE 结构体可见且可实例化，这里只做占位 */
struct __FILE
{
    int handle;   /* 句柄占位成员，重定向下不使用具体值 */
};

FILE __stdout;   /* printf 的默认输出流对象，fputc 通过它被间接调用 */

/* 替换标准库的系统退出桩，防止链接器拉入半主机模式的退出实现；退出码不使用。
   显式补 void 返回类型：C99 模式下隐式 int 会报 #260-D。 */
void _sys_exit(int x)
{
    x = x;   /* 空实现，仅为满足符号引用、消除告警 */
}

/* printf 的底层单字符出口，重定向到 USART1 数据寄存器；f 为输出流指针，重定向下无实际用途 */
int fputc(int ch, FILE *f)
{
    while((USART1->SR & 0X40) == 0);   /* 轮询等待 TC（发送完成）标志：上一字符未发完就写 DR 会丢字 */
    USART1->DR = (u8) ch;              /* 写入数据寄存器即自动启动本次发送 */
    return ch;
}
#endif

/* USART1 接收侧数据 */
char DMA_Rece_Buf1[DMA_Rec1_Len];   /* DMA 硬件自动写入的接收缓冲 */
u16  Usart1_Rec_Cnt;                /* 最近一帧实收字节数，IDLE 中断里更新 */

/* 调试口初始化：PA9 TX / PA10 RX，接收走 DMA1 通道 5 + IDLE 空闲中断。
   IDLE = 总线连续一个字节时间无数据，即「一帧发完了」，此时 DMA 计数器里
   剩多少没写，缓冲里就是这一帧的实收长度——不需要约定帧尾字符。 */
void uart1_init(uint32_t bound)   /* bound 为波特率，原工程 main 调用值 115200 */
{
    GPIO_InitTypeDef GPIO_InitStructure;     /* 引脚配置：引脚号、速度、复用模式 */
    USART_InitTypeDef USART_InitStructure;   /* USART 参数：波特率、字长、停止位等六项 */
    NVIC_InitTypeDef NVIC_InitStructure;     /* 中断配置：通道、抢占/响应优先级 */
    DMA_InitTypeDef DMA_InitStructure;       /* DMA 配置：外设/内存地址、方向、长度等 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);   /* USART1 与 GPIOA 同挂 APB2，一并开时钟 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);                              /* DMA1 挂 AHB，接收搬运用 */

    USART_DeInit(USART1);   /* 复位 USART1 全部寄存器，从缺省状态开始配置，避免残留态 */

    /* TX：PA9 复用推挽，引脚控制权交给 USART 外设 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：PA10 浮空输入，电平完全由对方驱动 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 中断配置：抢占 1 / 响应 2（原工程值，与其它中断的相对关系不要随意改动） */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口参数：8N1 无流控，收发双向 */
    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);      /* 开 IDLE 空闲中断：一帧结束的信号源 */
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);      /* RX 数据流交给 DMA 搬运，不占 CPU */
    USART_Cmd(USART1, ENABLE);                          /* 使能外设 */

    /* DMA1 通道 5 = USART1 RX：外设→内存，字节宽度，普通模式（发满即停，靠中断重启） */
    DMA_DeInit(DMA1_Channel5);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART1->DR;       /* 源：USART1 数据寄存器 */
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)DMA_Rece_Buf1;     /* 目的：接收缓冲 */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;  /* 方向：外设 → 内存 */
    DMA_InitStructure.DMA_BufferSize         = DMA_Rec1_Len;           /* 缓冲总长 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;  /* 外设地址固定（同一寄存器） */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;   /* 内存地址递增（逐字节排入缓冲） */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  /* 8 位宽度 */
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;        /* 普通模式：计数到 0 停止，等中断重启 */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;        /* 非内存到内存 */
    DMA_Init(DMA1_Channel5, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel5, ENABLE);   /* 启动接收搬运 */
}

/* 阻塞式数组发送：逐字节等 TC 再发下一个，全部发完后再等一次 TC 确认收尾 */
void Usart1_Send(char *buf, u16 len)
{
    u16 t;   /* 字节循环计数 */
    for (t = 0; t < len; t++)
    {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, buf[t]);
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

/* USART1 中断服务：只处理 IDLE。
   读一次 DR 清掉 IDLE 的置位条件，取走本帧长度，把整帧转发给 4G 口（USART2），
   最后重启 DMA 让下一帧从头写缓冲。PC 发来的数据由此透传给 4G 模块。 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        USART_ReceiveData(USART1);   /* 读 DR：清 IDLE 置位条件（读动作本身即清除） */
        Usart1_Rec_Cnt = DMA_Rec1_Len - DMA_GetCurrDataCounter(DMA1_Channel5);   /* 实收 = 总长 - 剩余计数 */
        Usart2_Send((char *)DMA_Rece_Buf1, Usart1_Rec_Cnt);   /* 整帧转发 4G 口 */
        USART_ClearITPendingBit(USART1, USART_IT_IDLE);       /* 清中断标志 */
        MYDMA_Enable1(DMA1_Channel5);                         /* 重启 DMA，准备下一帧 */
    }
}

/* 重启接收 DMA：先关通道才能改计数器，重装满长度后再打开 */
void MYDMA_Enable1(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);                       /* 关通道：计数器仅在关闭时可写 */
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec1_Len);   /* 重装缓冲长度 */
    DMA_Cmd(DMA_CHx, ENABLE);                        /* 重新启动搬运 */
}
```

## 4. usart2.c（新建，全文）

```c
#include "usart.h"

/* USART2 接收侧数据 */
char DMA_Rece_Buf2[DMA_Rec2_Len];   /* DMA 硬件自动写入的接收缓冲 */
u16  Usart2_Rec_Cnt;                /* 最近一帧实收字节数，IDLE 中断里更新 */

/* ---- USART2 接收环形缓冲 ----
   中断只写（生产者），业务层只读/消费（消费者）， head/tail 单向前进不回退，
   用「写指针 - 读指针」的无符号回绕差值表达在途字节数。
   临界保护用 PRIMASK 现场保存/恢复：只关中断的瞬间取快照，不在临界区里做拷贝。 */
static char         s_usart2_rx_ring[USART2_RX_RING_SIZE];   /* 环形存储区 */
static volatile u16 s_usart2_rx_head = 0;                    /* 写指针：中断里前进 */
static volatile u16 s_usart2_rx_tail = 0;                    /* 读指针：业务层消费时前进 */
static volatile u32 s_usart2_rx_overflow_count = 0;          /* 溢出累计：环满丢帧时递增 */

/* 下标回绕掩码：length 为 2 的幂时，index & mask 等价于 index % length */
#define USART2_RX_RING_MASK ((u16)(USART2_RX_RING_SIZE - 1))

/* 编译期强制「长度为 2 的幂」：非 2 的幂会让掩码回绕失效，直接编译失败 */
#if (USART2_RX_RING_SIZE & (USART2_RX_RING_SIZE - 1)) != 0
#error "USART2_RX_RING_SIZE must be a power of two"
#endif

/* 任意下标折算成环内下标 */
static u16 Usart2_RxUseMask(u16 index)
{
    return (u16)(index & USART2_RX_RING_MASK);
}

/* 当前可读字节数：临界区内取 head/tail 快照，差值用无符号回绕计算 */
u16 Usart2_RxAvailable(void)
{
    u16 head;
    u16 tail;
    u32 primask = __get_PRIMASK();

    __disable_irq();
    head = s_usart2_rx_head;
    tail = s_usart2_rx_tail;
    __set_PRIMASK(primask);

    return (u16)((head - tail) & USART2_RX_RING_MASK);
}

/* 拷出最多 max_len 字节：临界区内取快照并限长，拷贝动作在临界区外，
   DMA 缓冲→环形缓冲只发生在中断里，环形缓冲本身在读指针之前不会被覆盖 */
u16 Usart2_RxCopy(char *dst, u16 max_len)
{
    u16 head;
    u16 tail;
    u16 available;
    u16 i;
    u32 primask;

    if (dst == 0 || max_len == 0)
        return 0;   /* 参数非法：拷 0 字节 */

    primask = __get_PRIMASK();
    __disable_irq();
    head = s_usart2_rx_head;
    tail = s_usart2_rx_tail;
    available = (u16)((head - tail) & USART2_RX_RING_MASK);   /* 实际可读量 */
    if (available > max_len)
        available = max_len;                                  /* 不超过调用方缓冲 */
    for (i = 0; i < available; i++)
    {
        dst[i] = s_usart2_rx_ring[Usart2_RxUseMask((u16)(tail + i))];
    }
    __set_PRIMASK(primask);

    return available;
}

/* 消费 len 字节：只移动读指针，超出可读量的部分按可读量截断 */
void Usart2_RxConsume(u16 len)
{
    u16 available;
    u32 primask = __get_PRIMASK();

    __disable_irq();
    available = (u16)((s_usart2_rx_head - s_usart2_rx_tail) & USART2_RX_RING_MASK);
    if (len > available)
        len = available;   /* 防御：消费量不得超过在途数据 */
    s_usart2_rx_tail = Usart2_RxUseMask((u16)(s_usart2_rx_tail + len));
    __set_PRIMASK(primask);
}

/* 清空缓冲：读指针追平写指针，效果等于丢弃全部未读数据 */
void Usart2_RxClear(void)
{
    u32 primask = __get_PRIMASK();

    __disable_irq();
    s_usart2_rx_tail = s_usart2_rx_head;
    __set_PRIMASK(primask);
}

/* 读溢出累计值 */
u32 Usart2_RxOverflowCount(void)
{
    return s_usart2_rx_overflow_count;
}

/* 4G 模块通信口初始化：PA2 TX / PA3 RX，接收走 DMA1 通道 6 + IDLE 空闲中断，
   配置结构与 uart1_init 同构，只有引脚、外设、通道与优先级不同 */
void uart2_init(uint32_t bound)   /* bound 为波特率，原工程 GPRS 调用值 115200 */
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);   /* USART2 挂 APB1（与 USART1 的 APB2 不同） */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    USART_DeInit(USART2);   /* 复位后从缺省状态开始配置 */

    /* TX：PA2 复用推挽 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：PA3 浮空输入 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 中断配置：抢占 1 / 响应 1（比 USART1 的响应优先级更高，4G 数据不能等） */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口参数：8N1 无流控，收发双向 */
    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);      /* IDLE：一帧结束信号 */
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);      /* RX 交给 DMA */
    USART_Cmd(USART2, ENABLE);

    /* DMA1 通道 6 = USART2 RX，配置与通道 5 同构 */
    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)DMA_Rece_Buf2;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = DMA_Rec2_Len;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE);
}

/* 阻塞式数组发送（与 Usart1_Send 同构，目标 USART2） */
void Usart2_Send(char *buf, u16 len)
{
    u16 t;
    for (t = 0; t < len; t++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, buf[t]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

/* USART2 中断服务：IDLE 到来即「4G 发来一帧」。
   DMA 缓冲仍由硬件按帧填充，软件立即把它追加进环形缓冲；
   环满时丢弃本帧剩余字节并计一次溢出，不覆盖未消费数据 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        u16 len;   /* 本帧实收字节数 */
        u16 i;     /* 逐字节入环计数 */

        USART_ReceiveData(USART2);   /* 读 DR：清 IDLE 置位条件 */
        len = (u16)(DMA_Rec2_Len - DMA_GetCurrDataCounter(DMA1_Channel6));
        if (len > DMA_Rec2_Len)
            len = DMA_Rec2_Len;      /* 防御：计数异常时不越界 */
        Usart2_Rec_Cnt = len;

        for (i = 0; i < len; i++)
        {
            u16 next = Usart2_RxUseMask((u16)(s_usart2_rx_head + 1));   /* 写指针的下一位置 */
            if (next == s_usart2_rx_tail)   /* 环满：追上读指针 */
            {
                s_usart2_rx_overflow_count++;   /* 记一次溢出 */
                break;                          /* 丢弃本帧剩余字节，绝不覆盖未读数据 */
            }
            s_usart2_rx_ring[s_usart2_rx_head] = DMA_Rece_Buf2[i];
            s_usart2_rx_head = next;
        }

        USART_ClearITPendingBit(USART2, USART_IT_IDLE);
        MYDMA_Enable2(DMA1_Channel6);   /* 重启 DMA，准备下一帧 */
    }
}

/* 重启接收 DMA（与 MYDMA_Enable1 同构） */
void MYDMA_Enable2(DMA_Channel_TypeDef *DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);
    DMA_SetCurrDataCounter(DMA_CHx, DMA_Rec2_Len);
    DMA_Cmd(DMA_CHx, ENABLE);
}
```

## 5. 本模块验证 main.c（替换 REG 验证 main，全文）

验证一条完整链路：**PC → USART1 接收（DMA+IDLE）→ 中断转发 USART2 发送 → 4G 模块 →
模块应答 → USART2 接收（DMA+IDLE）→ 环形缓冲 → main 轮询取出 → printf 回显**。
一条链路同时覆盖 usart1 接收回补与 usart2 全部新代码。

**本板 PA2/PA3 直连 4G 模块座、未引出排针，跳线回环不可行**——验证时以 4G 模块为对端：
装上模块上电即可（模块自带 VBAT 会自行启动），echo 出现的就是模块的启动信息与应答。
更推荐直接采用《4G模块重构.md》§4 的联测 main（自带模块点火，echo 循环与本 main 相同）。

```c
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"

static u32  blink_cnt = 0;    /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */
static char s_rx_buf[128];    /* 回环数据回显缓冲：每轮最多拷 128 字节，超出部分下一轮继续 */

int main(void)
{
    u16 avail;   /* 环形缓冲当前可读字节数 */
    u16 n;       /* 本轮实际拷出并消费的字节数 */

    delay_init();               /* SysTick 延时（阶段 1 已验证） */
    NVIC_Configuration();       /* 中断分组（阶段 1 已验证） */
    uart1_init(115200);         /* 调试口：printf + PC 命令接收 */
    uart2_init(115200);         /* 4G 口：本模块验证对象 */
    LED_Init();
    LED_CLOSE();                /* 全灭基准态 */

    printf("usart test start\r\n");
    printf("peer: 4G module on usart2, send text from pc\r\n");   /* 本板 PA2/PA3 直连模块座无排针，提示语按实际对端写 */

    while (1)
    {
        delay_ms(500);          /* 半秒节拍 */
        LED_ERR = !LED_ERR;     /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
        blink_cnt++;            /* 心跳计数递增 */
        printf("usart alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍 */

        avail = Usart2_RxAvailable();          /* 回环数据检查 */
        while (avail > 0)
        {
            n = Usart2_RxCopy(s_rx_buf, sizeof(s_rx_buf));   /* 拷出（不消费） */
            Usart2_RxConsume(n);                             /* 消费 */
            s_rx_buf[n] = '\0';                              /* 补字符串结尾，按文本回显 */
            printf("echo %u bytes: %s\r\n", (unsigned int)n, s_rx_buf);
            avail = Usart2_RxAvailable();                    /* 一帧可能超过 128 字节，取到空为止 */
        }
    }
}
```

测试方法：SSCOM 连调试口发一行 ASCII（如 `hello`，结尾的回车换行也算字节），
应收到 `echo N bytes: hello`（N 含 `\r\n`）。连发多行可顺带验证环形缓冲的跨帧排队。

## 6. Keil 配置要点（USART）

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `usart2.c` | usart1.c 已在工程，内容替换为本文 §3 即可 |
| Include Paths | 不变 | `..\HARDWARE\USART` 阶段 1 已配 |
| FWLib 勾选 | 增勾 `stm32f10x_dma.c`、`stm32f10x_usart.c` | DMA 配置函数与 USART 收发函数都在这两个文件；misc.c（NVIC）阶段 1 已勾 |
| stm32f10x_conf.h | 确认包含 `stm32f10x_dma.h`、`stm32f10x_usart.h` | 原工程 conf.h 已包含，原样拷贝即可 |

## 7. 验证清单（USART，全部通过才算完成）

1. 编译 0 Error（环形缓冲 2 的幂检查随编译一起验证）；
2. 串口收到 `usart test start` + 对端提示；
3. SSCOM 发一行文本（模块在位时直接可见模块应答被回显），收到对应 `echo N bytes: ...`；
4. 连发多行不丢行（环形缓冲排队生效）；
5. 心跳 `cnt` 每 0.5 秒递增、PD14 约 1 秒一次闪烁；
6. map 文件确认：`usart2.o` 已链接，`Usart2_RxAvailable / USART2_IRQHandler` 符号存在。

## 8. 跨阶段边界（USART 部分）

1. **阶段 10 GPRS**：`usart2.c` 的环形缓冲 API 是 gprs_rx 的取数入口（`Usart2_RxCopy/Consume`）；
2. **阶段 11 BOOT**：`BootLoader_Clear()` 跳转前 `USART_DeInit(USART1/2)`——届时需确认跳转前
   先关两路 IDLE 中断，避免跳转瞬间中断向量切走后来不及退出（阶段 11 文档处理）；
3. usart3/4/5 与 RS485 不在迁移计划内，usart.h 里的相关声明已裁掉，后续若要用再按原工程回补。

---

本文档只生成/修订 md，不改任何 `.c/.h` 工程文件；`usart.h / usart1.c / usart2.c / main.c`
均由你按本文档落盘，落盘前提：《REG重构.md》已编译验证通过。
