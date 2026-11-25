/* DS1302 RTC 驱动）。
   三线位翻转时序：CLK 上升沿写、下降沿读；DAT 开漏作双向 IO。
   ds1302_dataread 出口为十进制（gprs_tx 时间戳依赖此契约）。 */
#include "ds1302.h"

/* 秒/分/时/日/月/周/年 的寄存器读、写地址（BCD 格式） */
static const u8 s_reg_read[7]  = {0x81,0x83,0x85,0x87,0x89,0x8b,0x8d};
static const u8 s_reg_write[7] = {0x80,0x82,0x84,0x86,0x88,0x8a,0x8c};

/* IO 初始化：CLK/RST 推挽输出，DAT 开漏（可读可写） */
static void ds1302_GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStruct.GPIO_Pin   = DS1302_CLK | DS1302_RST;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS1302_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin  = DS1302_DAT;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(DS1302_GPIO_PORT, &GPIO_InitStruct);
}

/* 初始化：只配 GPIO。RTC 内容靠 VBAT 掉电保持，boot 不播种时间；
   需要补种（换电池后）时显式调 ds1302_settime */
void ds1302_init(void)
{
    ds1302_GPIO_Configuration();
}

/* 写一个字节：SCK 上升沿写数据，低位在前 */
static void write_1302byte(u8 dat)
{
    u8 i;
    for (i = 0; i < 8; i++)
    {
        GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_CLK);
        if (dat & 0x01) GPIO_SetBits(DS1302_GPIO_PORT, DS1302_DAT);
        else            GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_DAT);
        GPIO_SetBits(DS1302_GPIO_PORT, DS1302_CLK);
        dat >>= 1;
    }
}

/* 读一个寄存器：先写寄存器地址，再降沿读 8 位 */
static u8 read_1302(u8 add)
{
    u8 i, dat1 = 0x00;

    GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_RST);
    GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_CLK);
    GPIO_SetBits(DS1302_GPIO_PORT, DS1302_RST);       /* 拉高 RST 开始传输 */

    write_1302byte(add);
    for (i = 0; i < 8; i++)
    {
        GPIO_SetBits(DS1302_GPIO_PORT, DS1302_CLK);
        dat1 >>= 1;
        GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_CLK); /* 下降沿后数据有效 */
        if (GPIO_ReadInputDataBit(DS1302_GPIO_PORT, DS1302_DAT) == 1)
            dat1 |= 0x80;
    }

    GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_RST);     /* 释放总线 */
    return dat1;
}

/* 写一个寄存器 */
static void write_1302(u8 add, u8 dat)
{
    GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_RST);
    GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_CLK);
    GPIO_SetBits(DS1302_GPIO_PORT, DS1302_RST);

    write_1302byte(add);
    write_1302byte(dat);
    GPIO_ResetBits(DS1302_GPIO_PORT, DS1302_RST);
}

/* 读 7 字节日历并转十进制：ttt[0..6]=秒/分/时/日/月/周/年 */
void ds1302_dataread(char *ttt)
{
    u8 i;
    char x, y;
    for (i = 0; i < 7; i++)
    {
        ttt[i] = read_1302(s_reg_read[i]);
        x = ttt[i] % 16;        /* BCD 低 4 位 → 个位 */
        y = ttt[i] / 16 * 10;   /* BCD 高 4 位 → 十位 */
        ttt[i] = x + y;
    }
}

/* 写 7 字节日历+回读校验（跳过秒位：秒在写读之间必然走动，比它恒失败）。
   返回 0=校验一致；0xFF=任一字段不一致（RTC 电池失效/芯片故障的典型信号） */
u8 ds1302_settime(u8 *dat)
{
    u8 i, res = 0;
    char ttt[7];

    write_1302(0x8e, 0x00);                 /* 去写保护 */
    for (i = 0; i < 7; i++) write_1302(s_reg_write[i], dat[i]);
    write_1302(0x8e, 0x80);                 /* 加写保护 */

    ds1302_dataread(ttt);
    for (i = 1; i < 7; i++)
    {
        if (ttt[i] != dat[i]) { res = 0xFF; break; }
    }
    return res;
}