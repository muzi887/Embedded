#ifndef __LCD_ORDER_H
#define __LCD_ORDER_H
#include "stm32f10x.h"
#include <stdbool.h>
// #include "key.h"
// #include "reg.h"
// extern Register armReg;
//-------电机控制--------------------
#define LCD_SET_UP_ON     \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x02; \
    Lcd_TxBuff[6] = 0x00; \
    Lcd_TxBuff[7] = 0X01;
#define LCD_SET_DOWN_ON   \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x02; \
    Lcd_TxBuff[6] = 0x00; \
    Lcd_TxBuff[7] = 0X02;
#define LCD_SET_STOP_ON   \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x02; \
    Lcd_TxBuff[6] = 0x00; \
    Lcd_TxBuff[7] = 0X03;
#define LCD_SET_OPEN_FLAG_ON \
    Lcd_TxBuff[0] = 0x5A;    \
    Lcd_TxBuff[1] = 0xA5;    \
    Lcd_TxBuff[2] = 0x05;    \
    Lcd_TxBuff[3] = 0x82;    \
    Lcd_TxBuff[4] = 0x15;    \
    Lcd_TxBuff[5] = 0x03;    \
    Lcd_TxBuff[6] = 0x00;    \
    Lcd_TxBuff[7] = 0X01;
#define LCD_SET_OPEN_FLAG_OFF \
    Lcd_TxBuff[0] = 0x5A;     \
    Lcd_TxBuff[1] = 0xA5;     \
    Lcd_TxBuff[2] = 0x05;     \
    Lcd_TxBuff[3] = 0x82;     \
    Lcd_TxBuff[4] = 0x15;     \
    Lcd_TxBuff[5] = 0x03;     \
    Lcd_TxBuff[6] = 0x00;     \
    Lcd_TxBuff[7] = 0X00;
#define LCD_SET_CLOSE_FLAG_ON \
    Lcd_TxBuff[0] = 0x5A;     \
    Lcd_TxBuff[1] = 0xA5;     \
    Lcd_TxBuff[2] = 0x05;     \
    Lcd_TxBuff[3] = 0x82;     \
    Lcd_TxBuff[4] = 0x15;     \
    Lcd_TxBuff[5] = 0x04;     \
    Lcd_TxBuff[6] = 0x00;     \
    Lcd_TxBuff[7] = 0X01;
#define LCD_SET_CLOSE_FLAG_OFF \
    Lcd_TxBuff[0] = 0x5A;      \
    Lcd_TxBuff[1] = 0xA5;      \
    Lcd_TxBuff[2] = 0x05;      \
    Lcd_TxBuff[3] = 0x82;      \
    Lcd_TxBuff[4] = 0x15;      \
    Lcd_TxBuff[5] = 0x04;      \
    Lcd_TxBuff[6] = 0x00;      \
    Lcd_TxBuff[7] = 0X00;
//---------远程就地切换----------------
#define LCD_SET_LOCALPIC  \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x07; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x00; \
    Lcd_TxBuff[5] = 0x84; \
    Lcd_TxBuff[6] = 0x5a; \
    Lcd_TxBuff[7] = 0X01; \
    Lcd_TxBuff[8] = 0x00; \
    Lcd_TxBuff[9] = 0x00;
#define LCD_SET_REMOTEPIC \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x07; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x00; \
    Lcd_TxBuff[5] = 0x84; \
    Lcd_TxBuff[6] = 0x5a; \
    Lcd_TxBuff[7] = 0X01; \
    Lcd_TxBuff[8] = 0x00; \
    Lcd_TxBuff[9] = 0x47; // 47

#define LCD_SET_SETTING   \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x07; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x00; \
    Lcd_TxBuff[5] = 0x84; \
    Lcd_TxBuff[6] = 0x5a; \
    Lcd_TxBuff[7] = 0X01; \
    Lcd_TxBuff[8] = 0x00; \
    Lcd_TxBuff[9] = 0x5b; // 5b
//-----------GPRS----------------------
#define LCD_SET_GPRS_ON   \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x11; \
    Lcd_TxBuff[6] = 0x00; \
    Lcd_TxBuff[7] = 0X01;
#define LCD_SET_GPRS_OFF  \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x11; \
    Lcd_TxBuff[6] = 0x00; \
    Lcd_TxBuff[7] = 0X00;
//-----------超限------------------
#define LCD_SET_UPERR     \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x10;

#define LCD_SET_DOWNERR   \
    Lcd_TxBuff[0] = 0x5A; \
    Lcd_TxBuff[1] = 0xA5; \
    Lcd_TxBuff[2] = 0x05; \
    Lcd_TxBuff[3] = 0x82; \
    Lcd_TxBuff[4] = 0x15; \
    Lcd_TxBuff[5] = 0x10;

u8 order_recv(void);
void setOpenFlag(bool flag);
void setCloseFlag(bool flag);
void ParaToLcd(void);
void ToLcd_int(char *str,int val);
void ToLcd_float(char *str,float val);
void ToLcd_string(char *name,char *str);
#endif
