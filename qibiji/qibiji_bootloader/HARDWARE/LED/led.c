#include "led.h"

// 把 PD11~PD14 一次批配置为推挽输出（50MHz），随后四路灯全部拉高点亮
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;   // 推挽输出：可主动输出稳定的高低电平，直接驱动 LED
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    GPIO_SetBits(GPIOD, GPIO_Pin_14);
    GPIO_SetBits(GPIOD, GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13);
}

// 四路 LED 全部写 0 熄灭
void LED_CLOSE(void)
{
    LED_UP   = 0;   // PD11 熄灭
    LED_DN   = 0;   // PD12 熄灭
    LED_LINE = 0;   // PD13 熄灭
    LED_ERR  = 0;   // PD14 熄灭
}