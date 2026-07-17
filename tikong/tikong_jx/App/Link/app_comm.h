#ifndef APP_COMM_H
#define APP_COMM_H

#include "uart3_at_sequence.h"

void PCProcess(void);
void Rs485Process(void);
void G4GProcess(void);
void QRProcess(void);
void QRProcessUart4(void);

#endif
