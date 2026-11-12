#ifndef __LED_H
#define __LED_H
#include "sys.h"
 
#define LED_UP 		PDout(11) 
#define LED_DN 		PDout(12) 
#define LED_LINE 	PDout(13) 
#define LED_ERR 	PDout(14) 

void LED_Init(void);//初始化
void LED_CLOSE(void);
#endif
