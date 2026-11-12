#ifndef __REG_H
#define __REG_H
#include "sys.h"
 
#define eepromLength	10

void AT24CXX_ReadOTAInfo(void);
void paraToEeprom(void);
#endif
