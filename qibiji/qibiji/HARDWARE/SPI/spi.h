#ifndef __SPI_H
#define __SPI_H
#include "sys.h"
 			  	    													  
#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "stm32f10x.h"

#define SPI_FLASH_CS_LOW()       GPIO_ResetBits(GPIOE, GPIO_Pin_0)
#define SPI_FLASH_CS_HIGH()      GPIO_SetBits(GPIOE, GPIO_Pin_0)

void SPI_FLASH_Init(void);
void SPI_FLASH_SectorErase(u32 SectorAddr);
void SPI_FLASH_BulkErase(void);
void SPI_FLASH_PageWrite(u8* pBuffer, u32 WriteAddr, u16 NumByteToWrite);
void SPI_FLASH_BufferWrite(u8* pBuffer, u32 WriteAddr, u16 NumByteToWrite);
void SPI_FLASH_BufferRead(u8* pBuffer, u32 ReadAddr, u16 NumByteToRead);
u32 SPI_FLASH_ReadID(void);
u32 SPI_FLASH_ReadDeviceID(void);
void SPI_FLASH_StartReadSequence(u32 ReadAddr);
void SPI_Flash_PowerDown(void);
void SPI_Flash_WAKEUP(void);


u8 SPI_FLASH_ReadByte(void);
u8 SPI_FLASH_SendByte(u8 byte);
u16 SPI_FLASH_SendHalfWord(u16 HalfWord);
void SPI_FLASH_WriteEnable(void);
void SPI_FLASH_WaitForWriteEnd(void);

void SpiFlash_Test(void);
uint8_t FLASH_AutoTest(void);

#endif /* __SPI_FLASH_H */
		 
#endif

