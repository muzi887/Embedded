#include "gprs_config_store.h"   /* MQTT_*_BYTE_ADDR / MQTT_KEY_STR_LEN 等 */
#include "eeprom_drv.h"          /* AT24CXX_Read / AT24CXX_Write / AT24CXX_ReadOneByte */
#include "gprs_config.h"         /* GPRS_* 编译期 fallback 宏 */
#include <string.h>
#include <stdio.h>
#include "debug_uart.h"

/* ---- RAM 凭据全局变量定义（对应 gprs_config_store.h 里的 extern 声明） ---- */
char g_mqtt_productKey[MQTT_KEY_STR_LEN];
char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];
char g_mqtt_deviceSecret[MQTT_KEY_STR_LEN];
char g_mqtt_addr[MQTT_KEY_STR_LEN];

/* 读单个槽到 buf（buf 至少 MQTT_KEY_STR_LEN 字节）；返回 1=槽有效，0=空白 */
static uint8_t gprs_store_read_slot(uint16_t addr, char *buf)
{
    AT24CXX_Read(addr, (uint8_t *)buf, MQTT_KEY_EEPROM_LEN);
    buf[MQTT_KEY_EEPROM_LEN] = '\0';
    return (buf[0] != '\0' && (uint8_t)buf[0] != 0xFFu) ? 1u : 0u;
}

uint8_t gprs_store_read_pk(char *buf)     { return gprs_store_read_slot(MQTT_PRODUCTKEY_BYTE_ADDR, buf); }
uint8_t gprs_store_read_dk(char *buf)     { return gprs_store_read_slot(MQTT_DEVICEKEY_BYTE_ADDR, buf); }
uint8_t gprs_store_read_secret(char *buf) { return gprs_store_read_slot(MQTT_DEVICESECRET_BYTE_ADDR, buf); }
uint8_t gprs_store_read_addr(char *buf)   { return gprs_store_read_slot(MQTT_MQTTADDR_BYTE_ADDR, buf); }

/* 上电：EEPROM 有效则用 EEPROM，否则用 gprs_config.h 里的编译期宏（不写回 EEPROM） */
void gprs_config_load(void)
{
    char tmp[MQTT_KEY_STR_LEN];

    if (gprs_store_read_pk(tmp))
        memcpy(g_mqtt_productKey, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_productKey, GPRS_PRODUCT_KEY, MQTT_KEY_EEPROM_LEN);

    if (gprs_store_read_dk(tmp))
        memcpy(g_mqtt_deviceKey, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_deviceKey, GPRS_DEVICE_KEY, MQTT_KEY_EEPROM_LEN);

    if (gprs_store_read_secret(tmp))
        memcpy(g_mqtt_deviceSecret, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_deviceSecret, GPRS_DEVICE_SECRET, MQTT_KEY_EEPROM_LEN);

    if (gprs_store_read_addr(tmp))
        memcpy(g_mqtt_addr, tmp, MQTT_KEY_STR_LEN);
    else
        strncpy(g_mqtt_addr, GPRS_MQTT_ADDR, MQTT_KEY_EEPROM_LEN);

    /* 统一强制结尾，防止 strncpy 在源串长度 ≥ 32 时未补 '\0' */
    g_mqtt_productKey[MQTT_KEY_EEPROM_LEN]   = '\0';
    g_mqtt_deviceKey[MQTT_KEY_EEPROM_LEN]    = '\0';
    g_mqtt_deviceSecret[MQTT_KEY_EEPROM_LEN] = '\0';
    g_mqtt_addr[MQTT_KEY_EEPROM_LEN]         = '\0';

    {
        char line[160];
        (void)snprintf(line, sizeof(line), "mqtt load: pk=%s dk=%s addr=%s\r\n",
                       g_mqtt_productKey, g_mqtt_deviceKey, g_mqtt_addr);
        DEBUG_UART_SendString(line);
    }
}

/* 写凭据：只写空白槽（首字节非 0 且非 0xFF 则跳过，保护真实凭据不被默认值覆盖） */
void gprs_store_write_str(uint16_t addr, const char *s)
{
    uint8_t first = AT24CXX_ReadOneByte(addr);
    if (first != 0x00u && first != 0xFFu)
        return;   /* 槽已有值，不覆盖 */

    {
        uint8_t buf[MQTT_KEY_EEPROM_LEN];
        uint16_t i;
        uint16_t len = (uint16_t)strlen(s);
        if (len > MQTT_KEY_EEPROM_LEN)
            len = MQTT_KEY_EEPROM_LEN;
        for (i = 0; i < len; i++)
            buf[i] = (uint8_t)s[i];
        for (; i < MQTT_KEY_EEPROM_LEN; i++)
            buf[i] = 0u;
        AT24CXX_Write(addr, buf, MQTT_KEY_EEPROM_LEN);
    }
}