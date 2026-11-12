#ifndef __4G_H
#define __4G_H
#include "sys.h"

#define GPRS_LINKA  PCin(5)

extern u8 gprsonlineflag;

void GM4G_Reset(void);

void GM4G_Init(void);  		//IO初始化

#endif
