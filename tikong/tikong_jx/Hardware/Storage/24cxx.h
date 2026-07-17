#ifndef __24CXX_H
#define __24CXX_H
#include "stm32f4xx.h"
#define AT24C32_I2C                I2C1
#define AT24C32_I2C_CLK            RCC_APB1Periph_I2C1
#define AT24C32_GPIO_CLK           RCC_AHB1Periph_GPIOB
#define AT24C32_GPIO_PORT          GPIOB
#define AT24C32_PIN_SCL            GPIO_Pin_6
#define AT24C32_PIN_SDA            GPIO_Pin_7
#define AT24C32_PINSRC_SCL         GPIO_PinSource6
#define AT24C32_PINSRC_SDA         GPIO_PinSource7
#define AT24C32_GPIO_AF            GPIO_AF_I2C1

#define AT24C32_ADDR_7BIT          0x50    // A2 A1 A0 = 0
#define AT24C32_PAGE_SIZE          32
#define I2C_TIMEOUT                0x3FFFF

void AT24C32_I2C_Init(void);
int AT24C32_Write(uint16_t mem_addr, const uint8_t *buf, uint16_t len);
int AT24C32_Read(uint16_t mem_addr, uint8_t *buf, uint16_t len);
#endif
















