#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H
#include "sys.h"
#include "DS3231.h"
#include "blackList.h"
#include "eeprom.h"

#define PUBLIC_KEY_LEN 20
extern char g_device_user[32];
extern char g_device_pass[32];
extern char g_device_publicKey[PUBLIC_KEY_LEN+1];
#define FRAME_MAX_LEN 64
char* ExtractValidData_Bytes(const unsigned char *data, int len);
const char* ExtractIdFromValidData(const char *valid_data);
int IsCvjFormat(const char *s);
int IsCmd1(const char *valid_data);
int IsCmd2(const char *valid_data);
int IsCmd3(const char *valid_data);
int IsCmd4(const char *valid_data);
int IsCmd5(const char *valid_data);
char* BuildCvjJsonFromValidData12(const char *valid_data);
char* BuildCvjJsonFromValidData12_1(const char *valid_data, char *haxi);
char* BuildCvjJsonFromValidData3(const char *valid_data,char *haxi);
char* BuildCvjJsonFromValidData3_1(const char *valid_data,char *haxi,const char *strid);
char* BuildCvjJsonFromValidData4(const char *valid_data);
char* BuildCvjJsonFromValidData5(const char *valid_data);
char* BuildCvjJsonFromValidData6(const char *valid_data);
int VerifySignature(const char *sha1_hexstr, const char *valid_data);
int CheckValidPeriod_WithNow(const char *valid_data, const DS3231_Time *now);
int AddBlacklistIds_FromPacket(const char *packet);
int DelBlacklistIds_FromPacket(const char *packet);

void reverse_hex_by_bytes(char* hex_str);

unsigned char hex_char_to_value(char c);

int hex_string_to_bytes(const char* hex_str, unsigned char* bytes, int max_bytes);


void build_26byte_packet(const char* hex_data, const char* hex_chardan, unsigned char device_id, unsigned char* output);


void print_hex_array(const unsigned char* data, int length);

void build_frame_A2(uint16_t chardan, uint8_t chartihao,
                 const uint8_t *extra11, uint8_t *outbuf, int *outlen);

void build_frame_A1(uint16_t chardan, uint8_t chartihao,
                 const uint8_t *extra11, uint8_t *outbuf, int *outlen);



int hex2val_local(char c);

int hex14_to_bytes7(const char *hex14, uint8_t out7[7]);

void reverse_bytes(char *hex, int hex_len);

uint8_t sum_checksum(const uint8_t *data, uint16_t len);

void SetDeviceCredentials(const char *user, const char *pass);

int VerifyDeviceCredentials(const char *user, const char *pass);
void SetDevicePublicKey(const char *pk);

const char* GetDevicePublicKey(void);
int FloorsHexToJsonArray_FromField(const char *hex, int hexlen, char *outbuf, int outbuf_size);
int IsSingleFloor(const uint8_t floors[7]);

char IsCmd(const char *valid_data);

void adjust_begin_time(char *begin14);


#endif
