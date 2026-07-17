#include "eeprom.h"
#include "data_handler.h"

// 向EEPROM写入一个字节
void EEPROM_WriteByte(uint16_t eeaddress, uint8_t data)
{
  IIC_Start();
  IIC_Send_Byte(EEPROM_DEVICE_ADDR << 1); // 写入模式
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddress >> 8)); // 地址高字节
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddress & 0xFF)); // 地址低字节
  IIC_Wait_Ack();

  IIC_Send_Byte(data); // 写入数据
  IIC_Wait_Ack();

  IIC_Stop();
  delay_ms(5); // EEPROM写入需要时间
}

// 向EEPROM写入一页数据（最多30字节）
void EEPROM_WritePage(uint16_t eeaddresspage, uint8_t *data, uint8_t length)
{
  uint8_t c;
  // 移除长度限制，允许更大值
  // if(length > 30) length = 30; // 注释掉以允许更大的值

  IIC_Start();
  IIC_Send_Byte(EEPROM_DEVICE_ADDR << 1);
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddresspage >> 8));
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddresspage & 0xFF));
  IIC_Wait_Ack();

  for (c = 0; c < length; c++)
  {
    IIC_Send_Byte(data[c]);
    IIC_Wait_Ack();
  }

  IIC_Stop();
  delay_ms(5); // 根据EEPROM型号的写入延时
}
// 从EEPROM读取一个字节
uint8_t EEPROM_ReadByte(uint16_t eeaddress)
{
  uint8_t rdata = 0xFF;

  // 先发送地址
  IIC_Start();
  IIC_Send_Byte(EEPROM_DEVICE_ADDR << 1); // 写入模式，设置地址
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddress >> 8)); // 地址高字节
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddress & 0xFF)); // 地址低字节
  IIC_Wait_Ack();

  // 重新开始读取数据
  IIC_Start();
  IIC_Send_Byte((EEPROM_DEVICE_ADDR << 1) | 0x01); // 读取模式
  IIC_Wait_Ack();

  rdata = IIC_Read_Byte(0); // 读取数据，不发送ACK

  IIC_Stop();

  return rdata;
}

// 从EEPROM读取多个字节
void EEPROM_ReadBuffer(uint16_t eeaddress, uint8_t *buffer, uint16_t length)
{
  uint16_t c;
  // 先发送地址
  IIC_Start();
  IIC_Send_Byte(EEPROM_DEVICE_ADDR << 1); // 写入模式，设置地址
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddress >> 8)); // 地址高字节
  IIC_Wait_Ack();

  IIC_Send_Byte((uint8_t)(eeaddress & 0xFF)); // 地址低字节
  IIC_Wait_Ack();

  // 重新开始读取数据
  IIC_Start();
  IIC_Send_Byte((EEPROM_DEVICE_ADDR << 1) | 0x01); // 读取模式
  IIC_Wait_Ack();

  for (c = 0; c < length; c++)
  {
    if (c == length - 1)
      buffer[c] = IIC_Read_Byte(0); // 最后一个字节，不发送ACK
    else
      buffer[c] = IIC_Read_Byte(1); // 发送ACK，继续读取
  }

  IIC_Stop();
}

// 写入8字节公钥
void WriteKey(const char *public_key)
{
  uint8_t k = 0;

  for (k = 0; k < 8; k++)
  {
    EEPROM_WriteByte(PUBLICKEY_ADR + k, public_key[k]);
  }
}

void ReadKey(void)
{
  uint8_t k = 0, val = 0;

  val = EEPROM_ReadByte(PUBLICKEY_ADR);
  if (val != 0xFF) // eeprom中已经存储了密钥
  {
    for (k = 0; k < 8; k++)
    {
      g_device_publicKey[k] = EEPROM_ReadByte(PUBLICKEY_ADR + k);
      // delay_ms(1); // 小延时
      printf("%c ", g_device_publicKey[k]);
    }
  }
}
