#ifndef __LED_H
#define __LED_H

#include "sys.h"              // PDout 位带输出宏：把 PD11~PD14 当成普通位变量直接赋值，无需读改写整个 ODR 寄存器

/* 四路状态指示灯引脚定义。
   全部为 GPIOD 推挽输出，电平语义：1 = 亮，0 = 灭。 */
#define LED_UP     PDout(11)   // PD11 指示灯。
#define LED_DN     PDout(12)   // PD12 指示灯。
#define LED_LINE   PDout(13)   // PD13 网络在线指示灯：4G 连上平台后由 4G.c 置 1，掉线置 0
#define LED_ERR    PDout(14)   // PD14 故障指示灯：兼作运行心跳灯

// 初始化四路 LED 引脚为推挽输出，并把四路灯全部点亮（上电默认态）
void LED_Init(void);

// 四路 LED 全部熄灭（统一写 0）
void LED_CLOSE(void);

#endif
