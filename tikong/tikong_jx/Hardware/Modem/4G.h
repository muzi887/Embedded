#ifndef __4G_H
#define __4G_H
#include "sys.h"

extern u8 gprsonlineflag;
extern volatile u8 g_gprs_linka_changed;
extern volatile u8 g_gprs_linka_level;
#define GPRS_LINKA  PBin(14)

/** PB14 ˫ EXTIʱΪ 1Ӧÿɶ 0 */
extern volatile u8 g_gprs_linka_changed;
/** һ EXTI PB14 ƽBit_SET=1ߣ */
extern volatile u8 g_gprs_linka_level;

void GM4G_Init(void);  		//IOʼ PB14 ⲿжϣ
void GM4G_Restart(void);  //4Gģ��������λ
/** PB14(LINKA) EXTI �жϴ����ӳ����� EXTI15_10_IRQHandler ����� Line14 �������� */
void GM4G_LinkA_Pin14_EXTI_Handler(void);

#endif
