/* ===========================================================================
 * LCD12864 (ST7920) driver layer - qibiji_zxl
 * =========================================================================== */
#include "lcd12864.h"
#include "delay.h"
#include "sys.h"
#include "stm32f10x.h"

/* --------------------- Pins (private to this file) ---------------------
 * PE3=CS, PE4=SID, PE5=SCLK,PA0=BackLight. These are 5V-tolerant pins
 *  (LCD logic is 5V); do not move them to non-FT pins. */
#define LCD_CS      PEout(3)   // PE.3  片选时为CS 片选信号
#define LCD_SID     PEout(4)   // PE.4  功能为SID  数据线
#define LCD_SCLK    PEout(5)   // PE.5  功能为SCLK 时钟线
#define LCD_BL      PAout(0)   // PA.0  功能为BackLight 背光控制

/* Backlight polarity. */
#define LCD_BL_ON   1u
#define LCD_BL_OFF  0u

/* --------------------------- ST7920 constants --------------------------- */
#define LCD_FRAME_CMD   0xF8u   /* start byte, RS = 0 */
#define LCD_FRAME_DAT   0xFAu   /* start byte, RS = 1 */

#define LCD_CMD_BASIC   0x30u   /* basic instruction set, RE = 0 */
#define LCD_CMD_EXT     0x34u   /* extended set, RE = 1, graphic OFF */
#define LCD_CMD_GDRAM   0x36u   /* extended set, RE = 1, graphic ON */
#define LCD_CMD_ENTRY   0x06u   /* AC auto-increment, no display shift 地址计数器自动加一；整屏不滚动 */
#define LCD_CMD_DISPON  0x0Cu   /* display ON, cursor OFF, blink OFF */
#define LCD_CMD_CLEAR   0x01u   /* clear DDRAM (slow command) */
#define LCD_CMD_HOME    0x02u   /* address return */

#define LCD_ROWS          4u
#define LCD_ADDR_PER_ROW  8u    /* 8 x 16-bit words = 16 ASCII chars */
#define LCD_GDRAM_ROWS   32u
#define LCD_GDRAM_WORDS  16u

/* DDRAM start address of each line (ST7920 is not linear) */
static const uint8_t s_row_addr[LCD_ROWS] = { 0x80u, 0x90u, 0x88u, 0x98u };

// unsigned char p1[24] = {
//     0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0F, 0x00,
//     0x10, 0x80, 0x16, 0x80, 0x16, 0x80, 0x16, 0x80,
//     0x16, 0x80, 0x16, 0x80, 0x10, 0x80, 0x0F, 0x00};
// unsigned char p2[32] = {
//     0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x0E,
//     0x00, 0xCE, 0x00, 0xCE, 0x0C, 0xCE, 0x0C, 0xCE,
//     0xCC, 0xCE, 0xCC, 0xCE, 0xCC, 0xCE, 0xCC, 0xCE,
//     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


/* ---------------------- Low-level serial timing ------------------------- */
// 把 1 个字节拆成 8 个 bit，最高位先出。
// SCLK 拉低 → 把这一位放到 SID 上 → SCLK 拉高（屏在这一瞬锁存）
static void s_SendByte(uint8_t dat)
{
    uint8_t i;
    for (i = 0; i < 8u; i++)
    {
        LCD_SCLK = 0;
        LCD_SID  = (uint8_t)((dat & 0x80u) != 0u ? 1u : 0u);
        LCD_SCLK = 1;                       /* latched on rising edge */
        dat = (uint8_t)(dat << 1);          /* MSB first */
    }
}

/* One ST7920 serial frame = start byte + high nibble + low nibble.
 * Each nibble occupies the upper 4 bits of one transmitted byte. */
static void s_Write(uint8_t dat, uint8_t is_data)
{
    LCD_CS = 0;
    LCD_CS = 1;         // 边沿，表示一帧开始
    s_SendByte((uint8_t)(is_data != 0u ? LCD_FRAME_DAT : LCD_FRAME_CMD)); // 起始字节：命令 0xF8 / 数据 0xFA
    s_SendByte((uint8_t)(dat & 0xF0u));                                   // 高 4 位照搬，低 4 位填 0
    s_SendByte((uint8_t)((uint8_t)(dat << 4) & 0xF0u));                   // 低 4 位挪到高 4 位，低位填 0
    if (is_data != 0u)  // 命令慢（尤其 0x01 清屏），数据快
        delay_us(20);
    else
        delay_us(100);
    LCD_CS = 0;         // 一帧结束，取消片选，准备下一帧。
}

static void s_WriteCmd(uint8_t cmd)  { s_Write(cmd, 0u); }
static void s_WriteData(uint8_t dat) { s_Write(dat, 1u); }

/* ----------------------------- Public API -------------------------------- */
void LCD12864_SetBacklight(uint8_t on)
{
    LCD_BL = (uint8_t)(on != 0u ? LCD_BL_ON : LCD_BL_OFF);
}

void LCD12864_Clear(void)
{
    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_CLEAR);
    delay_ms(2);                 /* 0x01 needs extra execution time */
    s_WriteCmd(LCD_CMD_HOME);
}

void LCD12864_ClearGDRAM(void)
{
    uint8_t row, word;

    s_WriteCmd(LCD_CMD_EXT);     /* 切到扩展指令集：RE = 1 */
    s_WriteCmd(LCD_CMD_GDRAM);   /*  打开绘图显示：graphic display ON to address GDRAM */
    for (row = 0; row < LCD_GDRAM_ROWS; row++)
    {
        s_WriteCmd((uint8_t)(0x80u + row));  /* Y address */
        s_WriteCmd(0x80u);                   /* X address = 0 */
        for (word = 0; word < LCD_GDRAM_WORDS; word++)
        {
            s_WriteData(0x00u);
            s_WriteData(0x00u);         /* 每个字地址写 2 个全 0 字节 */
        }
    } /* 总量：32 行 × 16 字 × 2 字节 = 1024 个数据帧，1024 × 8 = 8192 位，正好等于 128×64 = 8192 个点  */
    /* Return to basic instruction set AND turn graphic display off.*/
    s_WriteCmd(LCD_CMD_EXT);     /* 0x34: RE = 1, graphic OFF */
    s_WriteCmd(LCD_CMD_BASIC);   /* 0x30: RE = 0, basic set */
}
 
/* Write an ASCII string on one line. One DDRAM address holds two ASCII
 * characters, so two bytes are written per address; an odd last character
 * is padded with a space. Maximum 8 addresses (16 characters) per line. */
void LCD12864_ShowString(uint8_t row, const char *str)
{
    uint8_t addr;
    uint8_t used;
    const char *p;

    if ((row >= LCD_ROWS) || (str == (const char *)0))
        return;

    addr = s_row_addr[row]; /* 把行号换算成 DDRAM 地址 */
    used = 0;
    p = str;
    s_WriteCmd(LCD_CMD_BASIC);
    while ((*p != '\0') && (used < LCD_ADDR_PER_ROW))
    {
        uint8_t hi, lo;
        hi = (uint8_t)*p; 
        p++;            // 取第 1 个字符
        if (*p != '\0') // 还有第 2 个？
        {
            lo = (uint8_t)*p;
            p++;
        }
        else            // 没有 → 补一个空格
        {
            lo = (uint8_t)' ';
        }
        s_WriteCmd(addr);
        s_WriteData(hi);
        s_WriteData(lo);
        addr++;
        used++;
    }
}

/* Write raw 2-byte cells (GB2312 hanzi / custom glyph bytes) on one row.
 * One DDRAM address holds two bytes, so 'count' counts address cells
 * (1..8); bytes are written as-is, no padding, no ASCII pairing.
 * The auto-increment mode set by LCD_CMD_ENTRY at init makes the second
 * byte of each cell land on the adjacent half of the same address. */
void LCD12864_ShowHanzi(uint8_t row, const uint8_t *buf, uint8_t count)
{
    uint8_t addr;
    uint8_t i;

    /* 1 格 = 2 字节 = 1 个汉字 = 屏上 16×16 点。一行 8 格，超 8 截断。 */
    if ((row >= LCD_ROWS) || (buf == (const uint8_t *)0))
        return;
    if (count > LCD_ADDR_PER_ROW)
        count = LCD_ADDR_PER_ROW;

    addr = s_row_addr[row];
    s_WriteCmd(LCD_CMD_BASIC);
    for (i = 0u; i < count; i++)
    {
        s_WriteCmd(addr);   /* 每格重设地址 */
        s_WriteData(buf[i * 2u]);
        s_WriteData(buf[(i * 2u) + 1u]);
        addr++;
    }
}

// 上电调一次。配 GPIO、等屏上电稳定、发初始化指令、清 DDRAM 和 GDRAM、开背光。
void LCD12864_Init(void)
{
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;

	/*使能时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_GPIOA, ENABLE); // 使能GPIOB时钟

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;          /* PA0 = backlight */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

    LCD_CS   = 0;
    LCD_SID  = 0;
    LCD_SCLK = 0;
    LCD12864_SetBacklight(0u);

    /* ST7920 needs about 1.5 s after power-on before accepting commands.
    delay_ms() saturates at ~1864 ms @72 MHz, so wait in three 500 ms
    slices; feed the watchdog after each slice. */
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();
    delay_ms(500);
    WDG_Feed();

    s_WriteCmd(LCD_CMD_BASIC);
    s_WriteCmd(LCD_CMD_ENTRY);
    s_WriteCmd(LCD_CMD_DISPON);
    LCD12864_Clear();
    LCD12864_ClearGDRAM();
    LCD12864_SetBacklight(1u);
}

/* ------------------------- GDRAM drawing primitives 绘图原语 ---------------------- */

/* Write one 128-pixel row on the upper half: 16 bytes = 8 word columns
 * (X word 0..7) at GDRAM row y (0..31). X auto-increments every 2 bytes,
 * so 16 bytes fill only the upper half (X 0..7); the lower half (X 8..15)
 * needs 16 more bytes. The bar and separator are upper-half only.
 * 一次发 128 像素横线：从 X=0 连续写 16 字节，每 2 字节 X 自动 +1，
 * 只覆盖上半屏（X 0-7），不跨到下半屏（X 8~15，需另写 16 字节）。 */
void LCD12864_DrawRow(uint8_t y, const uint8_t *buf)
{
    uint8_t i;

    if ((y >= LCD_GDRAM_ROWS) || (buf == (const uint8_t *)0))
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);
    s_WriteCmd((uint8_t)(0x80u + y)); 
    s_WriteCmd(0x80u);          // 设置地址
    for (i = 0u; i < 16u; i++)
    {
        s_WriteData(buf[i]);
    }
    /* back to basic set ONLY. Do NOT send 0x34 here:
     * 0x34 closes the graphic layer and the banner/bar/line vanish. */
    s_WriteCmd(LCD_CMD_BASIC);
}

/* Write one vertical strip into GDRAM: 12 rows (GDRAM y=1..12, i.e. screen
 * pixel rows 2..25), 2 bytes per row = 16 pixels wide, at word column x
 * (0..15). Byte layout matches the original WRGDRAM(): buf[i*2] then
 * buf[i*2+1] per row, used for the page banner glyphs. 
 * 一次发一条 12 行 × 16 像素的竖条，X 参数是字地址（0~15，每字 16 像素宽），Y 固定 1~12*/
void LCD12864_DrawStrip(uint8_t x, const uint8_t *buf)
{
    uint8_t i;

    if ((x >= LCD_GDRAM_WORDS) || (buf == (const uint8_t *)0))
        return;

    s_WriteCmd(LCD_CMD_EXT);
    s_WriteCmd(LCD_CMD_GDRAM);
    for (i = 0u; i < 12u; i++)
    {
        s_WriteCmd((uint8_t)(0x80u + 1u + i));   /* GDRAM y = 1..12 -- Y 没有自动递增 */
        s_WriteCmd((uint8_t)(0x80u + x));
        s_WriteData(buf[i * 2u]);
        s_WriteData(buf[(i * 2u) + 1u]);
    }
     /* Same closing rule as DrawRow: basic set only, keep graphic display ON. */
    s_WriteCmd(LCD_CMD_BASIC);
}