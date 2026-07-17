#ifndef _TIMER_H
#define _TIMER_H
#include "sys.h"
 
#define beepout    PDout(11)  

void TIM3_Int_Init(u16 arr,u16 psc);
void TIM4_Int_Init(u16 arr,u16 psc);

void Beep_Init(void);

#endif
