#ifndef _RLY_H
#define _RLY_H
#include "sys.h"

#define rly1out   PEout(14)
#define rly2out   PEout(13)

void Rly_Init(void);
void Rly_Set(u8 rly1, u8 rly2);
/** 仅改继电器1/2 输出，另一路保持当前电平（单端控制用） */
void Rly_Set1(u8 rly1);
void Rly_Set2(u8 rly2);
void rly_AllOff(void);
#endif
