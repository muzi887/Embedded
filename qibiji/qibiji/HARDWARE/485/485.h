#ifndef __485_H
#define __485_H
#include "sys.h"
void manage485(void);
void Rs485_Rx_Data(void);
void Rs485_Tx_data(u8 addr,u8 cmd,u8 regh,u8 regl,u16 val);

#endif
