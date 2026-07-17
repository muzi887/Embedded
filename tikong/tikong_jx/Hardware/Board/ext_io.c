#include "ext_io.h"
#include "bsp_hc595.h"

/*
 * 8 个 74HC595 = 64 bit
 * 我们只用前 40 bit
 *
 * buf[0] -> U1（IO1~8，最靠近 MCU）
 * buf[1] -> U2（IO9~16）
 * ...
 * buf[7] -> U8
 */
static uint8_t io_buf[8];

/* ===================== 初始化 ===================== */
void ExtIO_Init(void)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
        io_buf[i] = 0xFF;

    ExtIO_Refresh();
    HC595_OutputEnable(0);
}

/* ===================== 设置单个 IO ===================== */
/*
 * index: 1~40
 */
void ExtIO_Set(uint8_t index, uint8_t value)
{
    uint8_t chip;
    uint8_t bit;

    if (index < 1 || index > EXT_IO_NUM)
        return;

    index--;          /* 转成 0 基 */
    chip = index / 8; /* 第几个 595 */
    bit = index % 8;  /* 片内 bit */

    if (value)
        io_buf[chip] |= (1 << bit);
    else
        io_buf[chip] &= ~(1 << bit);

    ExtIO_Refresh();
}

/* ===================== 连续设置多个 IO ===================== */
void ExtIO_SetMulti(uint8_t start, uint8_t count, uint8_t value)
{
    uint8_t i;
   // OE_ENABLE();
    for (i = 0; i < count; i++)
    {
        ExtIO_Set(start + i, value);
    }
    //OE_DISABLE();
}

/* ===================== 刷新到 74HC595 ===================== */
void ExtIO_Refresh(void)
{
    int i;

    /*
     * 关键点：
     * 595 的 shift 先的数据会跑到最远端
     * 所以必须从 U8 -> U1 反向发送
     */
    for (i = 7; i >= 0; i--)
    {
        HC595_ShiftByte(io_buf[i]);
    }

    HC595_Latch();
}
