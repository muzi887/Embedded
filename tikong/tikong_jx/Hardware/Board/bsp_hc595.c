#include "bsp_hc595.h"

/* ===================== 初始化 ===================== */
void HC595_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    HC595_GPIOD_CLK();

    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;

    /* SER */
    GPIO_InitStruct.GPIO_Pin = HC595_SER_PIN;
    GPIO_Init(HC595_SER_PORT, &GPIO_InitStruct);

    /* SRCLK */
    GPIO_InitStruct.GPIO_Pin = HC595_SRCLK_PIN;
    GPIO_Init(HC595_SRCLK_PORT, &GPIO_InitStruct);

    /* RCLK */
    GPIO_InitStruct.GPIO_Pin = HC595_RCLK_PIN;
    GPIO_Init(HC595_RCLK_PORT, &GPIO_InitStruct);

    /* OE / ZD */
    GPIO_InitStruct.GPIO_Pin = HC595_OE_PIN;
    GPIO_Init(HC595_OE_PORT, &GPIO_InitStruct);

    /* 默认状态 */
    SER_LOW();
    SRCLK_LOW();
    RCLK_LOW();

    OE_DISABLE(); /* 上电先关闭输出，防止乱闪 */
}

/* ===================== 输出使能控制 ===================== */
void HC595_OutputEnable(uint8_t en)
{
    if (en)
        OE_ENABLE();
    else
        OE_DISABLE();
}

/* ===================== 移位 1 字节（MSB first） ===================== */
void HC595_ShiftByte(uint8_t dat)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        SRCLK_LOW();

        if (dat & 0x80)
            SER_HIGH();
        else
            SER_LOW();

        SRCLK_HIGH();
        dat <<= 1;
    }
}

/* ===================== 锁存输出 ===================== */
void HC595_Latch(void)
{
    RCLK_LOW();
    RCLK_HIGH();
}

/* ===================== 连续写多个字节（级联用） ===================== */
void HC595_WriteBytes(uint8_t *buf, uint8_t len)
{
    uint8_t i;

    RCLK_LOW();

    for (i = 0; i < len; i++)
    {
        HC595_ShiftByte(buf[i]);
    }

    RCLK_HIGH();
}
