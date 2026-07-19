#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H
#include "sys.h"
#include "RTC.h"
#include "blackList.h"
#include "eeprom.h"

extern char g_device_id[PUBLIC_DEVICE_ID_LEN+1];
extern char g_device_password[PUBLIC_DEVICE_PASSWORD_LEN] ;
extern char g_device_type[PUBLIC_DEVICE_TYPE_LEN+1];
extern char g_device_code[PUBLIC_DEVICE_CODE_LEN+1];
extern char g_device_public_Key[PUBLIC_KEY_LEN+1] ; // 密钥长度
extern char g_device_mode[PUBLIC_DEVICE_MODE_LEN+1] ; // 设备模式长度
extern int rly_1_time;
extern int rly_2_time;

/** EEPROM 常开限位截止时间串（14 位 YYYYMMDDhhmmss + NUL） */
extern char g_floors_limit_time[PUBLIC_FLOORS_LIMIT_TIME_LEN + 1];

/** 解析 g_device_mode："seg1|seg2" → rly_1_time / rly_2_time。每段：若含 >=2 个 '_' 则第二个 '_' 后为整数（如 x_y_30）；若恰 1 个 '_' 则该 '_' 后为整数（如 0_30）；返回 0 成功 */
int ParseDeviceModeRlyTimes(void);
/** 同上，指定字符串（便于测试）；返回 0 成功，-1 格式错误 */
int ParseDeviceModeRlyTimesFrom(const char *mode_str);

extern char g_mqtt_addr[PUBLIC_MQTT_ADDR_LEN + 1];
extern char g_mqtt_productKey[PUBLIC_MQTT_PRODUCTKEY_LEN + 1];
extern char g_mqtt_deviceKey[PUBLIC_MQTT_DEVICEKEY_LEN + 1];
extern char g_mqtt_deviceSecret[PUBLIC_MQTT_DEVICESECRET_LEN + 1];
extern char g_device_drift_time[PUBLIC_DRIFT_TIME_LEN+1] ; // 密钥长度
#define FRAME_MAX_LEN 64
char *ExtractValidData_Bytes(const unsigned char *data, int len);
const char *ExtractIdFromValidData(const char *valid_data);
int IsCmd1(const char *valid_data);
int IsCmd2(const char *valid_data);
int IsCmd3(const char *valid_data);
int IsCmd4(const char *valid_data);
int IsCmd5(const char *valid_data);
char *BuildCvjJsonFromValidData12(const char *valid_data);
char *BuildCvjJsonFromValidData12_1(const char *valid_data, char *haxi);
char *BuildCvjJsonFromValidData3(const char *valid_data, char *haxi);
char *BuildCvjJsonFromValidData3_1(const char *valid_data, char *haxi, const char *strid);
char *BuildCvjJsonFromValidData4(const char *valid_data);
char *BuildCvjJsonFromValidData5(const char *valid_data);
char *BuildCvjJsonFromValidData6(const char *valid_data);
int VerifySignature(const char *sha1_hexstr, const char *valid_data);
int CheckValidPeriod_WithNow(const char *valid_data, const RTC_Time *now);
int AddBlacklistIds_FromPacket(const char *packet);
int DelBlacklistIds_FromPacket(const char *packet);

void build_frame_A2(uint16_t chardan, uint8_t chartihao,
                    const uint8_t *extra11, uint8_t *outbuf, int *outlen);

void build_frame_A1(uint16_t chardan, uint8_t chartihao,
                    const uint8_t *extra11, uint8_t *outbuf, int *outlen);

uint8_t sum_checksum(const uint8_t *data, uint16_t len);

void SetDeviceCredentials(const char *user, const char *pass);

int VerifyDeviceCredentials(const char *user, const char *pass);
void SetDevicePublicKey(const char *pk);

const char *GetDevicePublicKey(void);
int FloorsHexToJsonArray_FromField(const char *hex, int hexlen, char *outbuf, int outbuf_size);
int IsSingleFloor(const uint8_t floors[7]);

void adjust_begin_time(char *begin14);
void Get_newTime(char *time, int minute, int act);
char toLowerHex(char c);
void MakeTimestamp14(const RTC_Time *t, char out14[15]);
#endif
