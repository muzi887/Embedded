#ifndef __SWITCH_H
#define __SWITCH_H
#include "sys.h"

#define CODE2 PBin(0)
#define CODE1 PBin(4)

#define UP_LIMIT PDin(7) // IN1
#define DN_LIMIT PBin(3)

void JudeKaidu(void);
void SWITCH_Init(void); // IO初始化
void JudeCount(void);   // 转数判断
void JudeDir(void);
void JudeLocked(void);
void ChangeReality(void);
#define KEY_OPEN GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8)
#define KEY_CLOSE GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_9)
#endif
