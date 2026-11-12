#ifndef __REG_H
#define __REG_H
#include "sys.h"
 
#define eepromLength	10

/* EEPROM map (AT24C02, 256 bytes, byte addresses):
 *   0   - 19      gate parameter words  (eeprom_buf[0..9], u16 little endian)
 *   20  - 63      unassigned
 *   64  - 191     MQTT strings: productKey / deviceKey / deviceSecret / mqtt_addr,
 *                 four 32-byte slots at 64 / 96 / 128 / 160
 *   200 - 243     OTA_InfoCB (see boot.h)
 *   250           EEPROM presence marker (0x55, AT24CXX_Check)
 *   251 - 255     unassigned
 */

/* MQTT parameter slots (byte addresses, 32 bytes each). Same layout as the
 * application firmware (qibiji/HARDWARE/REG/reg.h); the Qt tool reads and
 * writes them through the SBF-20 "mqtt" config group. */
#define MQTT_PRODUCTKEY_BYTE_ADDR		64
#define MQTT_DEVICEKEY_BYTE_ADDR		96
#define MQTT_DEVICESECRET_BYTE_ADDR		128
#define MQTT_MQTTADDR_BYTE_ADDR			160
#define MQTT_KEY_EEPROM_LEN				32		/* bytes per slot, 32 chars max */
#define MQTT_KEY_STR_LEN				(MQTT_KEY_EEPROM_LEN + 1u) /* C string: 32 chars + '\0' */

void AT24CXX_ReadOTAInfo(void);
void paraToEeprom(void);
#endif
