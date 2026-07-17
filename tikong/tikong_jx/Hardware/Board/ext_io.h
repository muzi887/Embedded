#ifndef __EXT_IO_H
#define __EXT_IO_H

#include <stdint.h>

/* 总 IO 数 */
#define EXT_IO_NUM 40

/* IO 电平 */
#define EXT_IO_LOW 0
#define EXT_IO_HIGH 1

void ExtIO_Init(void);
void ExtIO_Set(uint8_t index, uint8_t value);
void ExtIO_SetMulti(uint8_t start, uint8_t count, uint8_t value);
void ExtIO_Refresh(void);

#endif
