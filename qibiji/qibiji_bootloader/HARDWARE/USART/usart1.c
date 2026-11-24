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

/* 替换标准库的系统退出桩，防止链接器拉入半主机模式的退出实现；退出码不使用 */
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

/* USART1 调试口初始化 */
void uart1_init(uint32_t bound)   
{
    GPIO_InitTypeDef GPIO_InitStructure;     
    USART_InitTypeDef USART_InitStructure;   

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);   
    USART_DeInit(USART1);                                                           /* 复位 USART1 全部寄存器，从缺省状态开始配置，避免残留态 */

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
    USART_Init(USART1, &USART_InitStructure);                                         /* 参数写入 USART1 寄存器 */
    USART_Cmd(USART1, ENABLE);                                                        /* 使能外设，fputc 里的轮询从此有效 */
}