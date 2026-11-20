#include "gprs_4g.h"
#include "gprs_config.h"
#include "delay.h"
#include "timer.h"

static volatile uint32_t s_last_reset_ms = 0u;
static volatile uint8_t  s_reset_ever = 0u;

uint8_t GPRS_4G_ResetAllowed(uint32_t min_interval_ms)
{
    uint32_t elapsed;
    if (!s_reset_ever)
        return 1u;
    elapsed = (uint32_t)(g_sys_ms - s_last_reset_ms);
    return (elapsed >= min_interval_ms) ? 1u : 0u;
}

void GPRS_4G_Init(void) 
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE); 

    /* PC4 / PC5: LINKA / LINKB, input pull-down */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* PA1: module reset, push-pull output */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;         // 4G复位
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  // 推挽输出
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* EXTI line 5 on PC5, both edges */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource5);
    EXTI_ClearITPendingBit(EXTI_Line5);                            // GPRS 中断标志位是否清除
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;                     // 选择中断线路5
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;            // 设置为中断模式，事件模式关闭
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling; // 外部中断触发方式为上、下降沿触发
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;                      // 外部中断使能
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2u;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3u;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* power-on reset pulse (not counted in the throttle timeline) */
    GPRS_RST_ON();
    delay_ms(500);
    GPRS_RST_OFF();
    WDG_Feed();                  /* 上电复位脉冲 500ms */
}

void GPRS_4G_Reset(void)
{
    /* all recovery paths share this timeline; blocking ~1s, runs in main loop */
    s_last_reset_ms = g_sys_ms;
    s_reset_ever = 1u;
    GPRS_RST_ON();
    delay_ms(1000);
    GPRS_RST_OFF();
    WDG_Feed();                  /* 运行期复位 1s */
    GPRS_4G_Init();              /* 内部 500ms 脉冲后已喂狗 */
}

void EXTI9_5_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        /* LINKA level is read directly by the state machine's debounce;
           the ISR only needs to clear the flag -- no shared online flag. */
    }
}

