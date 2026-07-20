#include "eeprom.h"
#include "data_handler.h"
#include "pass_setting.h"
#include "floor_ctrl.h"

/* 写入区间须落在 [0, AT24C32_CAPACITY_BYTES) */
static int eeprom_write_range_invalid(int addr, int len)
{
  if (len <= 0)
    return 1;
  if (addr < 0)
    return 1;
  if (addr >= (int)AT24C32_CAPACITY_BYTES)
    return 1;
  if (len > (int)AT24C32_CAPACITY_BYTES)
    return 1;
  if (addr > (int)AT24C32_CAPACITY_BYTES - len)
    return 1;
  return 0;
}

static void EEPROM_ReadTextField(char *dst, int dst_size, int addr, int len, const char *tag)
{
  int i;
  uint8_t v;
  int n;

  if (!dst || dst_size <= 0 || len <= 0)
    return;

  n = (len < (dst_size - 1)) ? len : (dst_size - 1);
  for (i = 0; i < n; i++)
    dst[i] = '\0';
  dst[n] = '\0';

  v = EEPROM_ReadByte((uint16_t)addr);
  if (v == 0xFF)
  {
    printf("%s(empty)\r\n", tag);
    return;
  }

  for (i = 0; i < n; i++)
  {
    v = EEPROM_ReadByte((uint16_t)(addr + i));
    if (v == 0xFF || v == 0x00)
      break;
    if (v >= 0x20 && v <= 0x7E)
      dst[i] = (char)v;
    else
      dst[i] = '_';
  }
  dst[i] = '\0';
  printf("%s%s\r\n", tag, dst);
}

// 向EEPROM写入一个字节
void EEPROM_WriteByte(uint16_t eeaddress, uint8_t data)
{
  AT24C32_Write(eeaddress, &data, 1);
}

// 从EEPROM读取一个字节
uint8_t EEPROM_ReadByte(uint16_t eeaddress)
{
  uint8_t rdata = 0xFF;

  AT24C32_Read(eeaddress, &rdata, 1);

  return rdata;
}

void EEPROM_ReadBuffer(uint16_t eeaddress, uint8_t *buffer, uint16_t length, const char *tag)
{
  int i;
  int rr;

  if (!buffer || length == 0)
    return;

  rr = AT24C32_Read(eeaddress, buffer, length);
  if (rr < 0)
  {
    if (tag)
      printf("%sI2C read err %d\r\n", tag, rr);
    return;
  }

  if (!tag)
    return;

  /* 二进制区勿用首字节 0xFF 当「空」：未写入芯片与「全开」5×0xFF 首字节均为 FF */
  printf("%s", tag);
  for (i = 0; i < (int)length; i++)
    printf("%02X ", buffer[i]);
  printf("\r\n");
}

void WriteKey(const char *key_value, int addr, int len)
{
  int k;
  int n;

  if (eeprom_write_range_invalid(addr, len))
    return;

  for (k = 0; k < len; k++)
    EEPROM_WriteByte((uint16_t)(addr + k), 0x00);

  if (!key_value)
    return;

  n = 0;
  while (key_value[n] != '\0' && n < len)
  {
    EEPROM_WriteByte((uint16_t)(addr + n), (uint8_t)key_value[n]);
    n++;
  }
}

void WriteRawBytes(const uint8_t *data, int addr, int len)
{
  int k;

  if (eeprom_write_range_invalid(addr, len))
    return;

  if (!data)
  {
    for (k = 0; k < len; k++)
      EEPROM_WriteByte((uint16_t)(addr + k), 0x00);
    return;
  }

  (void)AT24C32_Write((uint16_t)addr, data, (uint16_t)len);
}

void EEPROM_ClearAllParams(void)
{
  /* 与 ReadKey 相同的地址/长度，保证配置区与 ReadKey 一致 */
  WriteKey(NULL, PUBLIC_DEVICE_ID_ADDR, PUBLIC_DEVICE_ID_LEN);
  WriteKey(NULL, PUBLIC_DEVICE_PASSWORD_ADDR, PUBLIC_DEVICE_PASSWORD_LEN);
  WriteKey(NULL, PUBLIC_DEVICE_TYPE_ADDR, PUBLIC_DEVICE_TYPE_LEN);
  WriteKey(NULL, PUBLIC_DEVICE_CODE_ADDR, PUBLIC_DEVICE_CODE_LEN);
  WriteKey(NULL, PUBLIC_KEY_ADDR, PUBLIC_KEY_LEN);
  WriteKey(NULL, PUBLIC_DEVICE_MODE_ADDR, PUBLIC_DEVICE_MODE_LEN);
  WriteKey(NULL, PUBLIC_DRIFT_TIME_ADDR, PUBLIC_DRIFT_TIME_LEN);
  WriteKey(NULL, PUBLIC_MQTT_ADDR_ADDR, PUBLIC_MQTT_ADDR_LEN);
  WriteKey(NULL, PUBLIC_MQTT_PRODUCTKEY_ADDR, PUBLIC_MQTT_PRODUCTKEY_LEN);
  WriteKey(NULL, PUBLIC_MQTT_DEVICEKEY_ADDR, PUBLIC_MQTT_DEVICEKEY_LEN);
  WriteKey(NULL, PUBLIC_MQTT_DEVICESECRET_ADDR, PUBLIC_MQTT_DEVICESECRET_LEN);
  WriteRawBytes(NULL, PUBLIC_FLOORS_LIMIT_ADDR, PUBLIC_FLOORS_LIMIT_LEN);
  WriteKey(NULL, PUBLIC_FLOORS_LIMIT_TIME_ADDR, PUBLIC_FLOORS_LIMIT_TIME_LEN);

  ReadKey();
}

/**
 * 上电从 EEPROM 装载设备配置到全局变量（不止公钥）。
 * 调用点：board_init() 在 AT24C32_I2C_Init() 之后。
 * 内容：设备 id/口令/类型/编号/公钥/模式/漂移时间、MQTT 四元组、
 *       限层位图（FloorCtrl_SetLimit）与限层截止时间；
 *       读完 mode 后 ParseDeviceModeRlyTimes() 解析继电器吸合时长。
 * 未写过的文本区首字节 0xFF 视为空（见 EEPROM_ReadTextField）。
 */
void ReadKey(void)
{
  printf("eeprom Reading keys...\r\n");
  EEPROM_ReadTextField(g_device_id, sizeof(g_device_id),
                       PUBLIC_DEVICE_ID_ADDR, PUBLIC_DEVICE_ID_LEN, "g_device_id->");
  EEPROM_ReadTextField(g_device_password, sizeof(g_device_password),
                       PUBLIC_DEVICE_PASSWORD_ADDR, PUBLIC_DEVICE_PASSWORD_LEN, "g_device_password->");
  EEPROM_ReadTextField(g_device_type, sizeof(g_device_type),
                       PUBLIC_DEVICE_TYPE_ADDR, PUBLIC_DEVICE_TYPE_LEN, "g_device_type->");
  EEPROM_ReadTextField(g_device_code, sizeof(g_device_code),
                       PUBLIC_DEVICE_CODE_ADDR, PUBLIC_DEVICE_CODE_LEN, "g_device_code->");
  EEPROM_ReadTextField(g_device_public_Key, sizeof(g_device_public_Key),
                       PUBLIC_KEY_ADDR, PUBLIC_KEY_LEN, "g_device_public_Key->");
  EEPROM_ReadTextField(g_device_mode, sizeof(g_device_mode),
                       PUBLIC_DEVICE_MODE_ADDR, PUBLIC_DEVICE_MODE_LEN, "g_device_mode->");
  EEPROM_ReadTextField(g_device_drift_time, sizeof(g_device_drift_time),
                       PUBLIC_DRIFT_TIME_ADDR, PUBLIC_DRIFT_TIME_LEN, "g_device_drift_time->");
  ParseDeviceModeRlyTimes();
  EEPROM_ReadTextField(g_mqtt_addr, sizeof(g_mqtt_addr),
                       PUBLIC_MQTT_ADDR_ADDR, PUBLIC_MQTT_ADDR_LEN, "g_mqtt_addr->");
  EEPROM_ReadTextField(g_mqtt_productKey, sizeof(g_mqtt_productKey),
                       PUBLIC_MQTT_PRODUCTKEY_ADDR, PUBLIC_MQTT_PRODUCTKEY_LEN, "g_mqtt_productKey->");
  EEPROM_ReadTextField(g_mqtt_deviceKey, sizeof(g_mqtt_deviceKey),
                       PUBLIC_MQTT_DEVICEKEY_ADDR, PUBLIC_MQTT_DEVICEKEY_LEN, "g_mqtt_deviceKey->");
  EEPROM_ReadTextField(g_mqtt_deviceSecret, sizeof(g_mqtt_deviceSecret),
                       PUBLIC_MQTT_DEVICESECRET_ADDR, PUBLIC_MQTT_DEVICESECRET_LEN, "g_mqtt_deviceSecret->");
  {
    u8 limit5[5];
    EEPROM_ReadBuffer((uint16_t)PUBLIC_FLOORS_LIMIT_ADDR, limit5,
                      (uint16_t)PUBLIC_FLOORS_LIMIT_LEN, "g_elevator_tails_limit_bitmap->");
    FloorCtrl_SetLimit(limit5);
  }
  EEPROM_ReadTextField(g_floors_limit_time, sizeof(g_floors_limit_time),
                       PUBLIC_FLOORS_LIMIT_TIME_ADDR, PUBLIC_FLOORS_LIMIT_TIME_LEN, "g_elevator_tails_limit_time->");
  printf("..............................\r\n");
}
