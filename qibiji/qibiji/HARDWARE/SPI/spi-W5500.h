#ifndef __SPI_W5500_H
#define __SPI_W5500_H
#include "sys.h"			  	    													 

#include "stm32f10x.h"

#define SPI_W5500_CS_LOW()       GPIO_ResetBits(GPIOE, GPIO_Pin_0)
#define SPI_W5500_CS_HIGH()      GPIO_SetBits(GPIOE, GPIO_Pin_0)

void SPI_W5500_Init(void);
u8 SPI_W5500_SendByte(u8 byte);


#endif  
		

