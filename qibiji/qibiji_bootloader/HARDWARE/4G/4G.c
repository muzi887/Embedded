/* 4G 模块驱动：管三件事——
   ① 模块的网络状态线（PC4/PC5）读入与中断；
   ② 模块的复位脉冲（PA1，上电时给模块一个确定的启动起点）；
   ③ 在线标志 g_gprs_online 的实时维护（EXTI 中断里随电平翻转）。
   串口收发不在本文件——AT/数据通道归 usart2，协议归 GPRS 模块。 */

#include "4G.h"
#include "led.h"        /* LED_LINE（PD13）：在线指示灯，随 PC5 电平同步点亮/熄灭 */
#include "delay.h"      /* delay_ms：复位脉冲的高电平保持时长 */

/* 在线标志的定义：1 = 模块已注册网络；0 = 掉线/未注册 */
u8 g_gprs_online = 0;

/* 4G 模块初始化：配好两根状态输入脚 + 一根复位输出脚 + EXTI 双沿中断，
   最后打复位脉冲点火 */
void Gprs4G_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;    /* GPIO 配置块：输入脚与输出脚共用 */
    EXTI_InitTypeDef EXTI_InitStructure;    /* EXTI 配置块：PC5 双沿中断 */
    NVIC_InitTypeDef NVIC_InitStructure;    /* NVIC 配置块：EXTI9_5 中断优先级 */

    /* 三路时钟：GPIOC（PC4/PC5）、GPIOA（PA1）、AFIO（EXTI 引脚复用映射） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    /* PC4 = LINKB（模块第二状态脚）：下拉输入，本工程暂只留位不用 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* PC5 = GPRS_LINK_STATUS（网络状态脚）：下拉输入，既是电平读数口（GPRS_GPRS_LINK_STATUS 宏）也是 EXTI5 中断源 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* PA1 = 4G 模块复位脚：推挽输出，用于上电时给模块打复位脉冲 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* EXTI5 映射到 PC5：双沿触发——模块上线（上升沿）与掉线（下降沿）都要通知 CPU */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5);
    EXTI_ClearITPendingBit(EXTI_Line5);
    EXTI_InitStructure.EXTI_Line    = EXTI_Line5;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* EXTI9_5 中断通道：抢占 2、子优先级 3，不抢系统关键路径 */
    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 复位脉冲：PA1 拉高 500ms 再放低——给模块一个确定的开机起点 */
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
    delay_ms(500);
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

/* EXTI9_5 中断服务：PC5（EXTI5）电平一变就进——上线置标志亮灯，掉线清标志灭灯 */
void EXTI9_5_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line5))
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        if (GPRS_LINK_STATUS)         /* 读 PC5 实际电平：高 = 模块已注册网络 */
        {
            g_gprs_online = 1;    /* 在线：置标志，GPRS/main 据此启动联网动作 */
            LED_LINE = 1;       /* PD13 在线灯亮 */
        }
        else
        {
            g_gprs_online = 0;    /* 掉线：清标志 */
            LED_LINE = 0;       /* PD13 灭 */
        }
    }
}
