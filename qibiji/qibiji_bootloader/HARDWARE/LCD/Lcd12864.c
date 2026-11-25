/* ===========================================================================
 * LCD12864 (ST7920) 驱动层 - qibiji_bootloader
 * 串行三线时序（LCD_CS/LCD_SID/LCD_SCLK）+ 两套指令集：
 * 0x30 基本指令集（文本层）、0x34/0x36 扩展指令集（绘图层）。
 * =========================================================================== */
#include "Lcd12864.h"
#include "delay.h"
#include "wdg.h"  

/* --------------------------- ST7920 常量 --------------------------- */
#define LCD_FRAME_CMD   0xF8u   /* 起始字节，RS = 0（命令） */
#define LCD_FRAME_DAT   0xFAu   /* 起始字节，RS = 1（数据） */

#define LCD_CMD_BASIC   0x30u   /* 基本指令集，RE = 0 */
#define LCD_CMD_EXT     0x34u   /* 扩展指令集，RE = 1，绘图关 */
#define LCD_CMD_GDRAM   0x36u   /* 扩展指令集，RE = 1，绘图开 */
#define LCD_CMD_ENTRY   0x06u   /* AC 自动加一；整屏不滚动 */
#define LCD_CMD_DISPON  0x0Cu   /* 显示开，光标关，闪烁关 */
#define LCD_CMD_CLEAR   0x01u   /* 清 DDRAM（慢指令） */
#define LCD_CMD_HOME    0x02u   /* 地址归位 */

#define LCD_ROWS          4u
#define LCD_ADDR_PER_ROW  8u    /* 每行 8 个 16 位字地址 = 16 个 ASCII 字符 */
#define LCD_GDRAM_ROWS   32u
#define LCD_GDRAM_WORDS  16u

/* 每行文本的 DDRAM 起始地址（ST7920 的 DDRAM 不线性） */
static const u8 s_row_addr[LCD_ROWS] = { 0x80u, 0x90u, 0x88u, 0x98u };

/* ---------------------- 底层串行时序 ------------------------- */
/* 把 1 个字节拆成 8 个 bit，最高位先出。
 * SCLK 拉低 → 把这一位放到 SID 上 → SCLK 拉高（屏在这一瞬锁存） */
static void s_SendByte(u8 dat)
{
    u8 i;
    for (i = 0; i < 8u; i++)
    {
        LCD_SCLK = 0;
        LCD_SID  = (u8)((dat & 0x80u) != 0u ? 1u : 0u);
        LCD_SCLK = 1;                       /* rising edge latches */
        dat = (u8)(dat << 1);               /* MSB first */
    }
}

/* 一个 ST7920 串行帧 = 起始字节 + 高半字节 + 低半字节。
 * 每个半字节占一个传输字节的高 4 位。GPIO 位绑发送，不是硬件 SPI。 */
static void s_Write(u8 dat, u8 is_data)
{
    LCD_CS = 0;
    LCD_CS = 1;         /* 边沿，表示一帧开始 */
    s_SendByte((u8)(is_data != 0u ? LCD_FRAME_DAT : LCD_FRAME_CMD));
    s_SendByte((u8)(dat & 0xF0u));            /* 高 4 位照搬，低 4 位填 0 */
    s_SendByte((u8)((u8)(dat << 4) & 0xF0u)); /* 低 4 位挪到高 4 位 */
    if (is_data != 0u)  /* 命令慢（尤其 0x01 清屏），数据快 */
        delay_us(20);
    else
        delay_us(100);
    LCD_CS = 0;         /* 一帧结束，取消片选 */
}

static void s_WriteCmd(u8 cmd)  { s_Write(cmd, 0u); }
static void s_WriteData(u8 dat) { s_Write(dat, 1u); }

/* ----------------------------- 对外接口 -------------------------------- */
void LCD12864_SetBacklight(u8 on)
{
    LCD_BL = (u8)(on != 0u ? LCD_BL_ON : LCD_BL_OFF);
}

void LCD12864_Clear(void)
{
    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_CLEAR);
    delay_ms(2);                 /* 0x01 需要额外执行时间 */
    s_WriteCmd(LCD_CMD_HOME);
}

/* GDRAM 全屏清空：32 行 × 16 字 × 2 字节 = 1024 字 = 8192 点，
 * 正好覆盖 128×64 整屏；只清图形层，文字层（DDRAM）不受影响 */
void LCD12864_ClearGDRAM(void)
{
    u8 row, word;

    s_WriteCmd(LCD_CMD_EXT);     /* 切到扩展指令集：RE = 1 */
    s_WriteCmd(LCD_CMD_GDRAM);   /* 打开绘图显示，进入 GDRAM 寻址 */
    for (row = 0; row < LCD_GDRAM_ROWS; row++)
    {
        s_WriteCmd((u8)(0x80u + row));  /* Y 行地址 */
        s_WriteCmd(0x80u);              /* X 地址 = 0 */
        for (word = 0; word < LCD_GDRAM_WORDS; word++)
        {
            s_WriteData(0x00u);
            s_WriteData(0x00u);         /* 每个字地址写 2 个全 0 字节 */
        }
    }
    /* 收尾：回基本指令集并且关闭绘图显示 */
    s_WriteCmd(LCD_CMD_EXT);     /* 0x34: RE = 1, graphic OFF */
    s_WriteCmd(LCD_CMD_BASIC);   /* 0x30: RE = 0, basic set */
}

/* 在一行上显示 ASCII 字符串。一个 DDRAM 地址放两个 ASCII 字符，
 * 所以每个地址写 2 字节；末尾落单的字符补空格。每行最多 8 个地址（16 字符）。 */
void LCD12864_ShowString(u8 row, const char *str)
{
    u8 addr;
    u8 used;
    const char *p;

    if ((row >= LCD_ROWS) || (str == (const char *)0))
        return;

    addr = s_row_addr[row];     /* 行号换算成 DDRAM 地址 */
    used = 0;
    p = str;
    s_WriteCmd(LCD_CMD_BASIC);
    while ((*p != '\0') && (used < LCD_ADDR_PER_ROW))
    {
        u8 hi, lo;
        hi = (u8)*p;
        p++;                    /* 取第 1 个字符 */
        if (*p != '\0')         /* 还有第 2 个？ */
        {
            lo = (u8)*p;
            p++;
        }
        else                    /* 没有 → 补一个空格 */
        {
            lo = (u8)' ';
        }
        s_WriteCmd(addr);
        s_WriteData(hi);
        s_WriteData(lo);
        addr++;
        used++;
    }
}

/* 在一行上写原始 2 字节格（GB2312 汉字码 / 自造字符字节）。
 * 一个 DDRAM 地址放两字节，count 按地址格计数（1..8）；字节原样写出、
 * 不做 ASCII 配对。Init 设的 AC 自动加一模式使每格第 2 字节落到同一地址的另一半。 */
void LCD12864_ShowHanzi(u8 row, const u8 *buf, u8 count)
{
    u8 addr;
    u8 i;

    /* 1 格 = 2 字节 = 1 个汉字 = 屏上 16×16 点。一行 8 格，超 8 截断。 */
    if ((row >= LCD_ROWS) || (buf == (const u8 *)0))
        return;
    if (count > LCD_ADDR_PER_ROW)
        count = LCD_ADDR_PER_ROW;

    addr = s_row_addr[row];
    s_WriteCmd(LCD_CMD_BASIC);
    for (i = 0u; i < count; i++)
    {
        s_WriteCmd(addr);       /* 每格重设地址 */
        s_WriteData(buf[i * 2u]);
        s_WriteData(buf[(i * 2u) + 1u]);
        addr++;
    }
}

/* 用常量字节填充一整块 16 像素宽的字列矩形。
 *   y      : 首个像素行，0..63
 *   rows   : 块高（像素行数）
 *   x_word : 半屏内的首个 16 像素字列，0..7
 *   words  : 字列数，1..8
 *   value  : 写入每个字的字节（0xFF=填满，0x00=清空）
 * 说明：ST7920 一个垂直地址管两行，h=0..7 → 像素行 v，h=8..15 → 像素行 v+32；
 *       本函数按像素行入参，内部自动加上半区偏移。 */
void LCD12864_FillBlock(u8 y, u8 rows,
                        u8 x_word, u8 words, u8 value)
{
    u8 j;
    u8 i;
    u8 py;
    u8 v;
    u8 h;

    if (rows == 0u || words == 0u || words > 8u || x_word > 7u)
        return;
    if ((u16)y + (u16)rows > 64u)
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);

    for (j = 0u; j < rows; j++)
    {
        py = (u8)(y + j);
        v  = (u8)(py & 0x1Fu);
        h  = (u8)(x_word + ((py >= 32u) ? 8u : 0u));

        s_WriteCmd((u8)(0x80u + v));
        s_WriteCmd((u8)(0x80u + h));

        for (i = 0u; i < words; i++)
        {
            s_WriteData(value);     /* 每字 16 像素宽 */
            s_WriteData(value);
        }
    }

    s_WriteCmd(LCD_CMD_BASIC);
}

/* 上电调一次。配 GPIO、等屏上电稳定、发初始化指令、清 DDRAM 和 GDRAM、开背光。 */
void LCD12864_Init(void)
{
    /* GPIO 初始化 */
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;     /* PA0 = 背光 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    LCD_CS   = 0;
    LCD_SID  = 0;
    LCD_SCLK = 0;
    LCD12864_SetBacklight(0u);

    /* ST7920 上电后约需 1.5 s 才受理指令。
     * delay_ms() 在 72MHz 下约 1864ms 饱和，所以分三个 500ms 分片，
     * 每片之后喂一次看门狗（正式 main 开 IWDG 时必需；未开狗时无害）。 */
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();

    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_ENTRY);
    s_WriteCmd(LCD_CMD_DISPON);
    LCD12864_Clear();           /* 清 DDRAM（0x01 + 地址归位） */
    LCD12864_ClearGDRAM();      /* 清图形层 */
    LCD12864_SetBacklight(1u);  /* 背光点亮 */
}
