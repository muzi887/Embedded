#ifndef __4G_H
#define __4G_H
#include "sys.h"

extern u8 gprsonlineflag;

#define GPRS_LINKA  PCin(5)

void GM4G_Init(void);  		//IO初始化
/* 触发来源见 gprs_AT.c：当前仅在 gprs_state_machine 的 NORMAL->OFFLINE 分支调用。 */
void GM4G_Reset(void);		//硬件复位 4G 模块（RST 引脚）

#endif
