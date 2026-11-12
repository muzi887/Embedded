#ifndef __RLY_H
#define __RLY_H
#include "sys.h"

#define RLY_1H_ON   GPIO_SetBits(GPIOD,GPIO_Pin_1)
#define RLY_1H_OFF  GPIO_ResetBits(GPIOD,GPIO_Pin_1)
#define RLY_2H_ON   GPIO_SetBits(GPIOD,GPIO_Pin_3)
#define RLY_2H_OFF  GPIO_ResetBits(GPIOD,GPIO_Pin_3)


void RLY_Init(void);  		//IO初始化

#endif
