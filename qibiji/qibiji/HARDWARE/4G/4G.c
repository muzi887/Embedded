#include "4G.h"
#include "sys.h"

#include "led.h"
#include "usart.h"
#include "delay.h"
#include "gprs.h"

u8 gprsonlineflag;

/*
 * 模块硬复位的共享节流时间线。
 *
 * 以前 gprs_state_machine()（离线硬复位，≥5min）和 net_liveness.c（探活/校时自愈，≥15min）
 * 各自维护一份"上次复位时刻"，于是可能"状态机刚复位过、应用层 90 秒后又复位一次"。
 * 现在所有走 GM4G_Reset() 的恢复路径共用同一时间线，各自的最小间隔仍保留：
 *   离线路径 ≥5min、应用层路径 ≥15min——任一路径复位后，另一路径也会看到这次复位。
 * 注意：GM4G_Init() 的上电 RST 脉冲**不计入**（否则 MCU 每次启动后第一次复位都要等间隔）。
 */
static u32 s_last_reset_ms = 0;
static u8 s_reset_ever = 0;

/* 距上次模块硬复位还要等多久才允许再复位；0 = 现在允许（从未复位过也算允许） */
u32 GM4G_ResetWaitRemainingMs(u32 min_interval_ms)
{
    u32 elapsed;

    if (!s_reset_ever)
        return 0u;

    elapsed = (u32)(sys_ms - s_last_reset_ms); /* 无符号差值，跨 sys_ms 回绕安全 */
    if (elapsed >= min_interval_ms)
        return 0u;
    return min_interval_ms - elapsed;
}

/* 距上次模块硬复位是否已达到 min_interval_ms（从未复位过则允许） */
u8 GM4G_ResetAllowed(u32 min_interval_ms)
{
    return (GM4G_ResetWaitRemainingMs(min_interval_ms) == 0u) ? 1u : 0u;
}

void GM4G_Init(void) // IO初始化
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE); // 使能PORTB,PORTE时钟

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4; // LINKB
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5; // LINKA
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 4G--RST
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;         // 4G复位
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  // 推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // IO速度为50MHz
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 中断配置
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5);
    EXTI_ClearITPendingBit(EXTI_Line5);                            // GPRS 中断标志位是否清除
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;                     // 选择中断线路7
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;            // 设置为中断模式，事件模式关闭
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling; // 外部中断触发方式为上、下降沿触发
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;                      // 外部中断使能
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /*
     * 上电初始化时会执行一次 RST 脉冲（500ms 高电平），
     * 此路径是 main()->GM4G_Init() 的启动时序，不属于 GM4G_Reset 触发路径。
     */
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
    delay_ms(500);
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

void GM4G_Reset(void) // 硬件复位 4G 模块（RST 引脚），用于 NORMAL->OFFLINE 后恢复
{
    /*
     * [Reset Timing Annotation]
     * 触发路径：gprs_state_machine()（离线硬复位）、net_liveness.c（探活/校时自愈）、
     *           uart3_at_sequence.c（重配置后复位）。三者共用 s_last_reset_ms 节流。
     *
     * 阻塞时序：
     * 1) RST 拉高并 delay_ms(500)
     * 2) RST 拉低
     * 3) 调用 GM4G_Init()，其内部再次 delay_ms(500) 且重新配置 EXTI/NVIC
     *
     * 因此总阻塞约 1s（不含寄存器配置开销），并运行在主循环上下文中。
     */
    s_last_reset_ms = sys_ms; /* 记录本次复位（阻塞的 1s 也算在间隔内） */
    s_reset_ever = 1u;
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
    delay_ms(1000);
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
    GM4G_Init();
}

// 外部中断5-9中断服务函数  // PC.5
void EXTI9_5_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line5)) // gprs中断标志位是否清除
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        if (GPRS_LINKA)
        {
            gprsonlineflag = 1; // gprs在线
           //
                                //	MQTT_ConnectPack();  //连接服务器
            // printf("gprsonlineflag = 1\n");
            // 自动上报版本号
            //cjson_pub_version();
            // cjson_pub_getbin(0);
        }
        else
        {
            gprsonlineflag = 0; // gprs离线
            LED_LINE = 0;
            // printf("gprsonlineflag = 0\n");
        }
    }
}
