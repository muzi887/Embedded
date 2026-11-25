#ifndef __LCD12864_H
#define __LCD12864_H

/* LCD12864 (ST7920) 驱动层。
 * 职责边界：只管硬件访问——GPIO、串行时序、指令集、DDRAM/GDRAM 绘制、背光开关。。 */
#include "sys.h"    /* u8、stm32f10x.h、位带宏 */

/* 液晶 12864 串行驱动 IO：屏逻辑为 5V，STM32 的 IO 必须选 5V 兼容（FT）引脚，
 * 不可挪到非 FT 引脚。 */
#define LCD_CS      PEout(3)   /* PE3 片选 */
#define LCD_SID     PEout(4)   /* PE4 数据线 */
#define LCD_SCLK    PEout(5)   /* PE5 时钟线 */
#define LCD_BL      PAout(0)   /* PA0 背光控制 */

/* 背光极性 */
#define LCD_BL_ON   1u
#define LCD_BL_OFF  0u

/* 初始化：GPIO + 上电稳定期 1.5s（分片喂狗）+ 指令集/显示开 + 清两层 */
void LCD12864_Init(void);

/* 清文字层（DDRAM），图形层保留 */
void LCD12864_Clear(void);

/* 清图形层（GDRAM），文字层保留 */
void LCD12864_ClearGDRAM(void);

/* 文本行 row(0..3) 显示 ASCII 串：每地址 2 字符、落单补空格、超 16 截断、不预清行 */
void LCD12864_ShowString(u8 row, const char *str);

/* 文本行 row(0..3) 写原始 2 字节格：1 格 = 1 汉字 = 16×16 点，count 按格计、超 8 截断 */
void LCD12864_ShowHanzi(u8 row, const u8 *buf, u8 count);

/* GDRAM 像素矩形填充：y=首像素行 0..63，rows=块高，x_word=首字列 0..7，words=字列数 1..8，value=字节图案（0xFF 填满） */
void LCD12864_FillBlock(u8 y, u8 rows, u8 x_word, u8 words, u8 value);

/* 背光开关，极性由 LCD_BL_ON/LCD_BL_OFF 宏定 */
void LCD12864_SetBacklight(u8 on);

#endif
