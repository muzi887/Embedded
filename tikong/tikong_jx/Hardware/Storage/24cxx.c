#include "24cxx.h"
#include "delay.h" 
static int I2C_WaitEvent(uint32_t event)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (!I2C_CheckEvent(AT24C32_I2C, event)) {
        if (--timeout == 0) return -1;
    }
    return 0;
}

void AT24C32_I2C_Init(void)
{
    GPIO_InitTypeDef  gpio;
    I2C_InitTypeDef   i2c;

    RCC_AHB1PeriphClockCmd(AT24C32_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(AT24C32_I2C_CLK, ENABLE);

    GPIO_PinAFConfig(AT24C32_GPIO_PORT, AT24C32_PINSRC_SCL, AT24C32_GPIO_AF);
    GPIO_PinAFConfig(AT24C32_GPIO_PORT, AT24C32_PINSRC_SDA, AT24C32_GPIO_AF);

    gpio.GPIO_Pin   = AT24C32_PIN_SCL | AT24C32_PIN_SDA;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_OD;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(AT24C32_GPIO_PORT, &gpio);

    I2C_DeInit(AT24C32_I2C);
    i2c.I2C_Mode = I2C_Mode_I2C;
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1 = 0x00;
    i2c.I2C_Ack = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed = 100000; // 100kHz
    I2C_Init(AT24C32_I2C, &i2c);
    I2C_Cmd(AT24C32_I2C, ENABLE);
}

static int AT24C32_WritePage(uint16_t mem_addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    while (I2C_GetFlagStatus(AT24C32_I2C, I2C_FLAG_BUSY) == SET);

    I2C_GenerateSTART(AT24C32_I2C, ENABLE);
    if (I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) < 0) return -1;

    I2C_Send7bitAddress(AT24C32_I2C, AT24C32_ADDR_7BIT << 1, I2C_Direction_Transmitter);
    if (I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) < 0) return -2;

    I2C_SendData(AT24C32_I2C, (uint8_t)(mem_addr >> 8));
    if (I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) < 0) return -3;

    I2C_SendData(AT24C32_I2C, (uint8_t)(mem_addr & 0xFF));
    if (I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) < 0) return -4;

    for (i = 0; i < len; i++) {
        I2C_SendData(AT24C32_I2C, buf[i]);
        if (I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) < 0) return -5;
    }

    I2C_GenerateSTOP(AT24C32_I2C, ENABLE);
    return 0;
}

int AT24C32_Write(uint16_t mem_addr, const uint8_t *buf, uint16_t len)
{
    uint16_t wr_len;
    uint16_t page_off;
    int ret;
	  volatile uint32_t i = 0;

    while (len) {
        page_off = mem_addr % AT24C32_PAGE_SIZE;
        wr_len = AT24C32_PAGE_SIZE - page_off;
        if (wr_len > len) wr_len = len;

        ret = AT24C32_WritePage(mem_addr, buf, (uint8_t)wr_len);
        if (ret < 0) return ret;

        // 写周期等待：简单方式可 delay_ms(5)
        //for (i = 0; i < 300000; i++);
			  delay_ms(5);

        mem_addr += wr_len;
        buf      += wr_len;
        len      -= wr_len;
    }
    return 0;
}

int AT24C32_Read(uint16_t mem_addr, uint8_t *buf, uint16_t len)
{
    while (I2C_GetFlagStatus(AT24C32_I2C, I2C_FLAG_BUSY) == SET);

    I2C_GenerateSTART(AT24C32_I2C, ENABLE);
    if (I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) < 0) return -1;

    I2C_Send7bitAddress(AT24C32_I2C, AT24C32_ADDR_7BIT << 1, I2C_Direction_Transmitter);
    if (I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) < 0) return -2;

    I2C_SendData(AT24C32_I2C, (uint8_t)(mem_addr >> 8));
    if (I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) < 0) return -3;

    I2C_SendData(AT24C32_I2C, (uint8_t)(mem_addr & 0xFF));
    if (I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) < 0) return -4;

    I2C_GenerateSTART(AT24C32_I2C, ENABLE);
    if (I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) < 0) return -5;

    I2C_Send7bitAddress(AT24C32_I2C, AT24C32_ADDR_7BIT << 1, I2C_Direction_Receiver);
    if (I2C_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) < 0) return -6;

    while (len) {
        if (len == 1) {
            I2C_AcknowledgeConfig(AT24C32_I2C, DISABLE);
            I2C_GenerateSTOP(AT24C32_I2C, ENABLE);
        }
        if (I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_RECEIVED) < 0) return -7;
        *buf++ = I2C_ReceiveData(AT24C32_I2C);
        len--;
    }

    I2C_AcknowledgeConfig(AT24C32_I2C, ENABLE);
    return 0;
}
