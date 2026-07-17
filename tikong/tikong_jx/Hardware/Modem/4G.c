#include "4G.h"
#include "delay.h"
#include "timer.h"
#include "misc.h"
#include "stm32f4xx_exti.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_syscfg.h"

u8 gprsonlineflag;
extern int online_count;
volatile u8 g_gprs_linka_changed;
volatile u8 g_gprs_linka_level;

/* PB0(LED1) 低电平亮：PB14 高 -> 灯亮，PB14 低 -> 灭 */
static void GM4G_ApplyLinkAToLed1(void)
{
    if (g_gprs_linka_level){
        GPIO_ResetBits(GPIOA, GPIO_Pin_7);
        Bsp_SetBeep(3);
    }
    else
        GPIO_SetBits(GPIOA, GPIO_Pin_7);
    
    
}

void GM4G_Init(void) //IO?????
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

    GPIO_InitStructure.GPIO_Pin    = GPIO_Pin_14 | GPIO_Pin_15;   //LINKA LINKB
    GPIO_InitStructure.GPIO_Mode   = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_OType  = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd   = GPIO_PuPd_DOWN;
    GPIO_InitStructure.GPIO_Speed  = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    //PB14(LINKA)
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource14);
    EXTI_InitStructure.EXTI_Line = EXTI_Line14;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    EXTI_ClearITPendingBit(EXTI_Line14);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    g_gprs_linka_changed = 0;
    g_gprs_linka_level = (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14) == Bit_SET) ? 1u : 0u;
    GM4G_ApplyLinkAToLed1();

    //4G--RST 
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_13;	 
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT; 		
    GPIO_Init(GPIOB, &GPIO_InitStructure);

	  //GM4G_Restart();
}


void GM4G_Restart(void)
{
	GPIO_SetBits(GPIOB,GPIO_Pin_13);
    delay_ms(500);
    GPIO_ResetBits(GPIOB,GPIO_Pin_13);
}

void GM4G_LinkA_Pin14_EXTI_Handler(void)
{
    g_gprs_linka_level = (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14) == Bit_SET) ? 1u : 0u;
    g_gprs_linka_changed = 1u;
    GM4G_ApplyLinkAToLed1();
    online_count++;
}

