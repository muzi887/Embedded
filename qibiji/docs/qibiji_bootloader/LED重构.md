# LED 重构（迁移顺序阶段 1：工程骨架）

> 对应《模块迁移顺序.md》阶段 1。目标：在 `qibiji_bootloader` 搭起最小可运行工程——
> 启动文件 + 固件库 + SysTick 延时 + LED 模块 + 调试串口（printf）+ 最小 main，编译烧写后能看到 LED 闪烁、串口周期输出。
> LED 与 USART1（仅发送路径）是本阶段入库的两个 HARDWARE 级模块：均零业务依赖，用它们同时验证「模块接入 Keil 工程」这条流程。
> 串口按「当前需要」裁剪：本阶段只要 printf 输出，接收路径与 4G 用的 USART2 留到后续阶段按需并入。
> 只生成 md，不动工程文件。

> 注释规范：统一用 `/* ... */`；注释直接描述语义与原因，不使用「函数功能/参数/返回值」之类的标签文字。

## 1. 文件清单（从 qibiji_boot 迁入）

| 目录 | 文件 | 处置 | 说明 |
|---|---|---|---|
| `CORE/` | `startup_stm32f10x_hd.s`、`core_cm3.c`、`core_cm3.h` | 原样拷贝 | HD（大容量）启动文件，芯片 STM32F103VE 用它；**`startup_stm32f10x_md.s` 不迁移**（MD 版无人引用） |
| `STM32F10x_FWLib/` | `inc/` 全部头文件 + `src/` 全部 23 个 .c | 原样拷贝，Keil 里先只勾 4 个 | 本阶段只编译 `stm32f10x_rcc.c`（时钟）、`stm32f10x_gpio.c`（LED/串口引脚）、`stm32f10x_usart.c`（USART1 外设）、`misc.c`（NVIC 分组）；其余留目录，后续阶段按需勾选 |
| `SYSTEM/sys/` | `sys.c`、`sys.h` | 原样拷贝 | `NVIC_Configuration()`（中断分组）+ 位带宏（PDout 等，led.h 依赖它） |
| `SYSTEM/delay/` | `delay.c`、`delay.h` | 原样拷贝 | `delay_init/delay_ms/delay_us`（SysTick 实现，72MHz） |
| `HARDWARE/LED/` | `led.c`、`led.h` | **迁移，注释按 §2 重写**（函数名、宏名保持原名） | 四路 LED 纯输出模块，仅依赖 sys.h 位带宏——第一个入库的 HARDWARE 模块 |
| `HARDWARE/USART/` | `usart1.c`、`usart.h` | **迁移，裁剪版**（见 §3：只保留 printf 发送路径，注释重写） | 调试串口 USART1（PA9=TX）。原文件的接收路径（DMA + 空闲中断 + 转发 4G 口）依赖 usart2.c，本阶段无消费者，不迁 |
| `USER/` | `main.c` | **新写骨架版**（见 §4） | 调 LED + printf 验证运行链路；正式 OTA 状态机在阶段 12 才写 |
| `USER/` | `stm32f10x.h`、`stm32f10x_conf.h`、`stm32f10x_it.c`、`stm32f10x_it.h`、`system_stm32f10x.c`、`system_stm32f10x.h` | 原样拷贝 | 工程本地寄存器定义 / 外设配置开关 / 中断空壳 / 系统时钟初始化；`stm32f10x_it.c` 的中断处理函数后续阶段往里加 |

**不迁移的串口相关文件（明确排除）**：

| 文件 | 理由 | 归属 |
|---|---|---|
| `SYSTEM/usart/usart.c`、`usart.h` | **遗留脏副本**：不在原工程 BOOTV10.uvprojx 编译清单里；其 `fputc` 误指 USART3、`uart_init` 被未定义的 `EN_USART3_RX`（预处理值 0）整块编译掉。全工程 grep 无引用 | 永不迁移 |
| `HARDWARE/USART/usart2.c` | 4G 模组通信口（`uart2_init`/`Usart2_Send`/环形缓冲 API），GPRS 全链路依赖它 | 后续 GPRS/4G 阶段 |
| `HARDWARE/USART/usart3.c` | 4G 口的另一套 DMA+空闲中断实现（完全重映射 PD8/PD9） | 后续 4G 阶段，按工程实际取舍 |
| `HARDWARE/USART/usart4.c`、`usart5.c` | boot 工程无业务引用（usart4 仅调试回环用 Usart1_Send） | 不迁，需用时另行评估 |

不迁移：`boot_dis*.txt`、`*.lst`、`*.map`、`*.uvguix.*` 等产物。

> 注：LED、USART1 原排在《模块迁移顺序.md》阶段 7（LED + 4G）/阶段 2（USART），现提前到阶段 1（只迁发送路径）；阶段 2 只剩 WDG，阶段 7 只剩 4G 收发侧。《模块迁移顺序.md》需要同步修订时说一声。

## 2. `HARDWARE/LED/` 全文（注释按现行规范重写，代码逻辑不变）

### 2.1 `led.h`

```c
#ifndef __LED_H
#define __LED_H

/* 位带输出宏：PD11~PD14 可像普通位变量一样直接赋值，省去读改写 ODR 寄存器 */
#include "sys.h"

/* 四路状态指示灯，均为 GPIOD 推挽输出，电平 1 = 亮、0 = 灭
   （依据：4G.c 中 LED_LINE = 1 时注释「在线灯亮」） */
#define LED_UP     PDout(11)   /* PD11 指示灯，boot 内无业务引用，随模块保留（APP 工程作闸门上行指示） */
#define LED_DN     PDout(12)   /* PD12 指示灯，boot 内无业务引用，随模块保留（APP 工程作闸门下行指示） */
#define LED_LINE   PDout(13)   /* PD13 网络在线指示灯，4G 连上平台后由 4G.c 置 1，掉线置 0 */
#define LED_ERR    PDout(14)   /* PD14 故障指示灯，boot 内预留，骨架阶段兼作运行心跳灯 */

/* 四路 LED 引脚初始化为推挽输出并全部点亮（上电默认态） */
void LED_Init(void);

/* 四路 LED 全部熄灭（统一写 0） */
void LED_CLOSE(void);

#endif
```

### 2.2 `led.c`

```c
#include "led.h"

/* 四路 LED 引脚一次批配置为推挽输出（50MHz），随后全部拉高点亮。
   四个引脚同属 GPIOD，共用一次 GPIO_Init；上电亮是原工程基准态，
   调用方随后调 LED_CLOSE() 熄灭，即得到「全灭、可控」的干净起点。 */
void LED_Init(void)
{
    GPIO_InitTypeDef gpio;   /* 引脚集合、工作模式、输出速度三项配置 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);   /* 开启 GPIOD 端口时钟，不开时钟则后续寄存器写入全部无效 */

    gpio.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;   /* 一次配置四个 LED 引脚 */
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;   /* 推挽输出，可主动输出稳定高低电平，直接驱动 LED */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;   /* LED 翻转频率极低，此档冗余但无害 */
    GPIO_Init(GPIOD, &gpio);

    GPIO_SetBits(GPIOD, GPIO_Pin_14);                             /* LED_ERR 置高（亮） */
    GPIO_SetBits(GPIOD, GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13); /* LED_UP/DN/LINE 置高（亮） */
}

/* 四路 LED 全部写 0 熄灭。
   原工程 main 上电初始化后调用一次，建立「全灭基准态」；
   骨架版 main 沿用同一上电序列（先 Init 全亮，再 CLOSE 全灭）。 */
void LED_CLOSE(void)
{
    LED_UP   = 0;   /* PD11 熄灭 */
    LED_DN   = 0;   /* PD12 熄灭 */
    LED_LINE = 0;   /* PD13 熄灭 */
    LED_ERR  = 0;   /* PD14 熄灭 */
}
```

## 3. `HARDWARE/USART/` 骨架版全文（调试口，仅发送路径）

### 3.1 裁剪对照（骨架版 vs 原工程 usart1.c）

| 原工程实现 | 骨架版 | 裁剪原因 |
|---|---|---|
| `fputc` 重定向 printf 到 USART1 | **保留** | 本阶段的核心需求 |
| `uart1_init`：PA9 TX + PA10 RX + 收发模式 | **裁剪**：只配 PA9 TX，模式 `USART_Mode_Tx` | 接收路径无消费者 |
| RX DMA（DMA1 通道 5）+ `DMA_Rece_Buf1` + `MYDMA_Enable1` | 不迁 | 属接收路径 |
| IDLE 中断使能 + `USART1_IRQHandler`（把 PC 来的数据转发给 4G 口） | 不迁 | 中断服务调用 `Usart2_Send`，会强制链接 usart2.c（4G 口，属后续阶段） |
| `Usart1_Send`（阻塞数组发送） | 不迁 | 本阶段无人调用，printf 已覆盖调试输出 |
| `uart2_init`/`Usart2_Send`/环形缓冲（usart2.c） | 属后续 4G 阶段 | 4G 链路专用 |
| usart.h 里 4 路串口的 extern 声明、DMA 长度宏、RS485 宏 | 裁剪为仅 `uart1_init` + `ENABLE_PRINTF` 开关 | 其余声明随各自阶段并入 |

> 裁剪是有意为之（用户明确「只加入当前需要的」），不是逻辑等价重排；后续阶段并入接收路径时，以原工程 usart1.c 为准回补。

### 3.2 `usart.h`（骨架版）

```c
#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include "stdio.h"

/* printf 总开关。
   1 = printf 正常输出（fputc 重定向到 USART1，实现见 usart1.c）；
   0 = printf 编译期替换为空操作，所有调用点被优化掉、不占代码空间，
       需要产出"静默"固件时改这一个数字即可，调用点不用动。 */
#define ENABLE_PRINTF  1

#if !ENABLE_PRINTF
#define printf(...)  ((void)0)   /* 空操作替换；可变参数宏依赖编译器扩展，仅在开关关闭时才会编译到此行 */
#endif

/* USART1 调试口初始化，骨架版仅发送路径；bound 为波特率，原工程调用值 115200 */
void uart1_init(uint32_t bound);

#endif
```

### 3.3 `usart1.c`（骨架版）

```c
#include "usart.h"

/* ---- printf 重定向（不启用 MicroLIB 的标准做法） ----
   用 __use_no_semihosting 摆脱半主机模式：半主机下 printf 会触发 SWI 陷阱，
   脱离调试器单独运行时程序直接卡死，所以必须补齐标准库要求的三个符号。 */
#if 1
#pragma import(__use_no_semihosting)

/* 标准库要求 FILE 结构体可见且可实例化，这里只做占位 */
struct __FILE
{
    int handle;   /* 句柄占位成员，重定向下不使用具体值 */
};

FILE __stdout;   /* printf 的默认输出流对象，fputc 通过它被间接调用 */

/* 替换标准库的系统退出桩，防止链接器拉入半主机模式的退出实现；退出码不使用。
   原工程写的是隐式 int 返回（_sys_exit(int x)），C90 下可过、C99 模式下报 #260-D，
   骨架版显式补 void，两种编译标准下都成立。 */
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

/* USART1 调试口初始化，骨架版仅配发送（PA9）。
   原工程版另含 PA10 RX 配置、DMA1 通道 5 接收、IDLE 空闲中断与
   「PC 数据转发 4G 口」的中断服务——均属接收路径，本阶段无消费者，
   后续需要串口接收时按原工程 usart1.c 回补。 */
void uart1_init(uint32_t bound)   /* bound 为波特率，原工程 main 调用值 115200 */
{
    GPIO_InitTypeDef GPIO_InitStructure;     /* PA9 引脚配置：引脚号、速度、复用模式 */
    USART_InitTypeDef USART_InitStructure;   /* USART1 参数配置：波特率、字长、停止位等六项 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);   /* 两者同挂 APB2，一并开时钟 */
    USART_DeInit(USART1);   /* 复位 USART1 全部寄存器，从缺省状态开始配置，避免残留态 */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;           /* PA9 = USART1_TX */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;      /* 复用推挽，引脚控制权交给 USART 外设 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = bound;                            /* 波特率，调用方传入 */
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;              /* 8 位数据位 */
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;                 /* 1 位停止位 */
    USART_InitStructure.USART_Parity              = USART_Parity_No;                  /* 无校验位 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;   /* 无硬件流控 */
    USART_InitStructure.USART_Mode                = USART_Mode_Tx;                    /* 仅发送（骨架裁剪，原工程为 Rx|Tx） */
    USART_Init(USART1, &USART_InitStructure);   /* 参数写入 USART1 寄存器 */
    USART_Cmd(USART1, ENABLE);                  /* 使能外设，fputc 里的轮询从此有效 */
}
```

## 4. `USER/main.c`（骨架版全文）

```c
/* 工程骨架的最小 main：验证「启动文件 + 固件库 + SysTick 延时 + LED 模块 + 调试串口」链路。
   LED 与 USART1（仅发送）是第一批入库的模块，零业务依赖，用它俩验证模块接入流程最合适。
   预期现象：PD14 约 1 秒周期闪烁；串口助手（115200 8N1）每 0.5 秒收到一行心跳输出。 */

#include "stm32f10x.h"
#include "sys.h"     /* NVIC_Configuration（中断分组）、PDout 位带宏（led.h 依赖它） */
#include "delay.h"   /* delay_init / delay_ms（SysTick 延时，72MHz 主频） */
#include "led.h"     /* LED_Init / LED_CLOSE / LED_ERR（本阶段入库模块一） */
#include "usart.h"   /* uart1_init + printf 重定向（本阶段入库模块二：USART1 调试口） */

/* 骨架入口：初始化延时、中断分组、调试串口与 LED 后进入无限循环，
   翻转 LED_ERR 作灯光心跳，printf 输出运行计数作串口心跳。 */
int main(void)
{
    u32 blink_cnt = 0;   /* 心跳计数：watch 窗口看它递增确认主循环在跑，printf 里同步输出 */

    delay_init();          /* SysTick 延时初始化（按 72MHz 系统时钟配节拍） */
    NVIC_Configuration();  /* 中断分组 2：2 位抢占优先级 + 2 位响应优先级 */
    uart1_init(115200);    /* 调试串口：PA9 TX，115200 8N1，仅发送（与原工程波特率一致） */
    LED_Init();            /* 四路 LED 引脚初始化（上电全亮，led.c 内部基准态） */
    LED_CLOSE();           /* 全部熄灭，建立干净基准——与原工程 main 的上电灯序一致 */

    while (1)
    {
        delay_ms(500);            /* 半秒节拍 */
        LED_ERR = !LED_ERR;      /* 位带翻转 LED_ERR：完整周期约 1 秒，灯光心跳 */
        blink_cnt++;             /* 心跳计数递增 */
        printf("skeleton alive, cnt=%u\r\n", (unsigned int)blink_cnt);   /* 串口心跳：与灯光同拍，肉眼核对两条链路 */
    }
}
```

## 5. Keil 工程配置要点（`qibiji_bootloader.uvprojx`，由你在 Keil 里操作）

| 配置项 | 值 | 说明 |
|---|---|---|
| Device | STM32F103VE | 与原工程一致 |
| 预处理符号 | `STM32F10X_HD, USE_STDPERIPH_DRIVER` | HD 型号 + 启用 conf.h；与原工程一致 |
| IROM1 | Start `0x08000000`，Size `0xE000` | **必须**与最终 BootLoader 目标一致（B 区 56KB），骨架阶段就按终态配置，避免后面改链接地址 |
| IRAM1 | Start `0x20000000`，Size `0x10000` | F103VE 64K RAM，默认值即可 |
| Include Paths | `.\;..\CORE;..\STM32F10x_FWLib\inc;..\SYSTEM\sys;..\SYSTEM\delay;..\HARDWARE\LED;..\HARDWARE\USART` | 后续阶段再补 HARDWARE 其余各目录 |
| 编译标准 | 与原工程相同（C90 默认） | `main.c`、`led.c`、`usart1.c` 里变量声明已在块首，C90 下可编译 |
| 分组 | CORE / FWLib / SYSTEM / HARDWARE / USER | **HARDWARE 组本阶段只加 `led.c`、`usart1.c`**；FWLib 组只勾 rcc、gpio、usart、misc 四个 .c |

注意三点：boot 从 0x08000000 运行，向量表偏移保持默认 0，**不需要**设置 `VECT_TAB_OFFSET`（那是 A1 应用才需要的）；`stm32f10x_conf.h` 若未放开 `#include "stm32f10x_usart.h"`，需勾上（原工程 conf.h 已包含，原样拷贝即可）；每个 .c/.h 文件末尾保留一个换行（最后一行是 `#endif` 或 `}` 后回车），否则编译报 `#1-D: last line of file ends without a newline` 告警——末行换行是 C 标准的硬性要求，缺了它预处理器做 `#include` 文本嵌入时，头文件最后一行可能与主文件后续代码拼成同一行（如吞掉 `#endif` 之后的语句），产生不知所云的报错。

## 6. 验证清单（全部通过才算本阶段完成）

1. 编译 0 Error；Warning 水平不高于原工程；
2. 烧写运行，PD14（LED_ERR）以约 1 秒周期闪烁；PD11/PD12/PD13 上电后保持熄灭（Init 全亮 → CLOSE 全灭序列生效）；
3. 串口助手连 PA9（115200-8-N-1），每 0.5 秒收到一行 `skeleton alive, cnt=N`，N 连续递增（printf → fputc → USART1 链路通）；
4. PC 向 PA10 发数据**无任何回显**——这是预期行为：接收路径本阶段未迁，别误判为故障；
5. 调试器 watch `blink_cnt` 持续递增（确认主循环 + SysTick 延时活着）；
6. 查 map 文件：`LED_Init/LED_CLOSE/uart1_init/fputc` 已链接、无 `Usart2_Send` 等未迁符号；代码起始地址 `0x08000000`，镜像远小于 0xE000 上限；
7. 复位键按下后现象重现（启动文件 + 时钟初始化稳定）。

通过后进入阶段 2（WDG），届时把 `HARDWARE/WDG/` 迁入并验证喂狗/复位；4G 用的 USART2 收发路径随 GPRS/4G 阶段再并入。
