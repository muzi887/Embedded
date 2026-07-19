#include "Live_data_r.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "RTC.h"
#include "rly.h"
#include "floor_ctrl.h"
#include "timer.h"

static void cjson_item_to_str(cJSON *item, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0)
        return;
    buf[0] = '\0';
    if (!item)
        return;
    if (item->type == cJSON_String && item->valuestring)
    {
        strncpy(buf, item->valuestring, bufsize - 1);
        buf[bufsize - 1] = '\0';
    }
    else if (item->type == cJSON_Number)
    {
        snprintf(buf, bufsize, "%lld", (long long)item->valuedouble);
    }
}
const char *SAVED_DEVICE_KEY = "e499e7738311484d81c";
const char *SAVED_PRODUCT_KEY = "fc5d79eea2334a71a8d";
int aa;
uint8_t buffer_data[128];
int dataLen;

RTC_Time currentTime1;

int caseNumber;

int item_count;

char jsonStr_1[2048];
/////////////////////////1//////////////
int baud_rate_data;
int data_length_1;
int data_start_length_1;
int data_stop_length_1;
char verify_1[2];           // 校验位，"0"或"1"
char data_1[128];
char deviceKey_1[32];
char key_1[32];
char productKey_1[32];
char requestId_1[8];
long long requestTimestamp_1;

/////////////////////////2.replay//////////////
char code_1[8];       // 状态码，如"200"
char msg_1[64];       // 消息内容，如"操作成功"
char messageId_1[32]; // 消息ID，如"1755139316330"
/////////////////////////3.提取黑名单//////////////
char deviceKey_3[32];         // 设备密钥
char key_3[32];               // 请求类型
char productKey_3[32];        // 产品密钥
char requestId_3[16];         // 请求ID
long long requestTimestamp_3; // 时间戳
/////////////////////////4.增加黑名单//////////////
char deviceKey_4[32];          // 设备密钥
char key_4[32];                // 请求类型
char productKey_4[32];         // 产品密钥
char requestId_4[16];          // 请求ID
long long requestTimestamp_4b; // 时间戳
char qrCodeIds_4[500][11];     // 二维码ID数组
int qrCodeIds_count_4 = 0;     // 二维码ID数量
/////////////////////////5.减少黑名单//////////////
char deviceKey_5[32];          // 设备密钥
char key_5[32];                // 请求类型
char productKey_5[32];         // 产品密钥
char requestId_5[16];          // 请求ID
long long requestTimestamp_5b; // 时间戳
char qrCodeIds_5[30][11];      // 二维码ID数组
int qrCodeIds_count_5 = 0;     // 二维码ID数量
/////////////////////////6.更新公钥//////////////
char deviceKey_6[32];          // 设备密钥
char key_6[32];                // 请求类型
char productKey_6[32];         // 产品密钥
char requestId_6[16];          // 请求ID
long long requestTimestamp_6b; // 时间戳
char publicKey_6[512];         // 公钥
/////////////////////////call_elevator（arguments.floorRelayNumbers）//////////////
int g_floor_relay_numbers[FLOOR_RELAY_NUMBERS_MAX];
int g_floor_relay_numbers_count = 0;
/** 非0：数组形态且解析后恰好只有一个有效数字（如 [3]） */
u8 g_floor_relay_array_has_single_number = 0;
/** 仅当 g_floor_relay_array_has_single_number 时为该唯一数值 */
int g_floor_relay_single_number_value = 0;

/* 全局：记录 readtoflash 读到的条数 */
int g_uuid_count1 = 0;
//构建UUID指针数组（用于某些命令处理），废弃
int BuildItemsFromUuidList1(char *items_out[], int max_items)
{
    int i;
    int n;

    n = g_uuid_count1;
    if (n > max_items)
    {
        n = max_items;
    }

    for (i = 0; i < n; i++)
    {
        items_out[i] = (char *)dataArray[i].data;
    }
    return n;
}

// 修改parse_rs485_json函数声明，接受cJSON*参数
// 将十六进制字符转换为数值
uint8_t hexCharToValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

// 将十六进制字符串转换为字节数组
int hexStringToBytes(const char *hexStr, uint8_t *bytes, int maxBytes)
{
    int len = strlen(hexStr);
    int count = 0;
    int ii;
    for (ii = 0; ii < len; ii += 2)
    {
        if (count >= maxBytes)
            break;

        if (ii + 1 < len)
        {
            bytes[count] = (hexCharToValue(hexStr[ii]) << 4) | hexCharToValue(hexStr[ii + 1]);
            count++;
        }
    }

    return count;
}
//提取RS485指令参数
int parse_rs485_json(cJSON *root)
{
    /*
    {
	"arguments": {
		"baud_rate": 19200,
		"data_length": 8,
		"data_start_length": 1,
		"data_stop_length": 1,
		"verify": "N",
		"data": "7E1A0001010ACC010F0202000000011000000000000000000000BD0D"
	},
	"deviceKey": "e499e7738311484d81c",
	"key": "data_transmit_RS485",
	"productKey": "fc5d79eea2334a71a8d",
	"requestId": "1",
	"requestTimestamp": 1755141246512
    }
    */
    cJSON *deviceKey, *key_item, *productKey, *requestId, *requestTimestamp, *arguments;
    cJSON *baud_rate, *data_length, *data_start_length, *data_stop_length, *verify, *data;

    // 直接从传入的cJSON对象解析，不需要再次调用cJSON_Parse
    deviceKey = cJSON_GetObjectItem(root, "deviceKey");
    key_item = cJSON_GetObjectItem(root, "key");
    productKey = cJSON_GetObjectItem(root, "productKey");
    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");

    arguments = cJSON_GetObjectItem(root, "arguments");
    if (arguments)
    {
        baud_rate = cJSON_GetObjectItem(arguments, "baud_rate");
        data_length = cJSON_GetObjectItem(arguments, "data_length");
        data_start_length = cJSON_GetObjectItem(arguments, "data_start_length");
        data_stop_length = cJSON_GetObjectItem(arguments, "data_stop_length");
        verify = cJSON_GetObjectItem(arguments, "verify");
        data = cJSON_GetObjectItem(arguments, "data");
    }
    // 检查所有必要的字段是否存在
    if (!baud_rate || !data_length || !data_start_length || !data_stop_length ||
        !verify || !data || !deviceKey || !key_item || !productKey ||
        !requestId || !requestTimestamp)
    {
        printf("Error: Missing required fields in JSON\n");
        return -1;
    }

    // 提取数值
    baud_rate_data = baud_rate->valueint;
    data_length_1 = data_length->valueint;
    data_start_length_1 = data_start_length->valueint;
    data_stop_length_1 = data_stop_length->valueint;

    //------------------以下是需要处理部分-----------------------------------------------------------

    // 提取字符串
    // strncpy(verify_1, verify->valuestring, sizeof(verify_1) - 1);
    // verify_1[sizeof(verify_1) - 1] = '\0';

    strncpy(data_1, data->valuestring, sizeof(data_1) - 1);
    data_1[sizeof(data_1) - 1] = '\0';

    // strncpy(deviceKey_1, deviceKey->valuestring, sizeof(deviceKey_1) - 1);
    // deviceKey_1[sizeof(deviceKey_1) - 1] = '\0';

    // strncpy(key_1, key_item->valuestring, sizeof(key_1) - 1);
    // key_1[sizeof(key_1) - 1] = '\0';

    // strncpy(productKey_1, productKey->valuestring, sizeof(productKey_1) - 1);
    // productKey_1[sizeof(productKey_1) - 1] = '\0';

    strncpy(requestId_1, requestId->valuestring, sizeof(requestId_1) - 1);
    requestId_1[sizeof(requestId_1) - 1] = '\0';

    // 处理时间戳（假设是double类型）
    requestTimestamp_1 = (long long)requestTimestamp->valuedouble;

    return 0;
}

// 提取黑名单
int parse_tiqublack_json(cJSON *root)
{
    /**
     * {
	"arguments": {},
	"deviceKey": "e499e7738311484d81c",
	"key": "get_qr_code_blacklist",
	"productKey": "fc5d79eea2334a71a8d",
	"requestId": "1",
	"requestTimestamp": 1755142157760
    }
     */
    cJSON *deviceKey, *key_item, *productKey, *requestId, *requestTimestamp;
    int deviceKey_match;
    int productKey_match;
    // 不需要再次解析JSON，直接使用传入的root对象
    if (!root)
    {
        printf("Error: NULL JSON root\n");
        return -1;
    }

    // 获取JSON对象中的字段
    deviceKey = cJSON_GetObjectItem(root, "deviceKey");
    key_item = cJSON_GetObjectItem(root, "key");
    productKey = cJSON_GetObjectItem(root, "productKey");
    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");
    //    arguments = cJSON_GetObjectItem(root, "arguments");

    // 检查必要的字段是否存在
    if (!deviceKey || !key_item || !productKey || !requestId || !requestTimestamp)
    {
        printf("Error: Missing required fields\n");
        return -1;
    }

    // 直接提取字段值（不检查类型）
     strncpy(deviceKey_3, deviceKey->valuestring, sizeof(deviceKey_3) - 1);
     deviceKey_3[sizeof(deviceKey_3) - 1] = '\0';

    // strncpy(key_3, key_item->valuestring, sizeof(key_3) - 1);
    // key_3[sizeof(key_3) - 1] = '\0';

     strncpy(productKey_3, productKey->valuestring, sizeof(productKey_3) - 1);
     productKey_3[sizeof(productKey_3) - 1] = '\0';

    strncpy(requestId_3, requestId->valuestring, sizeof(requestId_3) - 1);
    requestId_3[sizeof(requestId_3) - 1] = '\0';

    requestTimestamp_3 = (long long)requestTimestamp->valuedouble;

    // 打印解析结果
    // printf("deviceKey: %s\n", deviceKey_3);
    // printf("key: %s\n", key_3);
    // printf("productKey: %s\n", productKey_3);
    // printf("requestId: %s\n", requestId_3);
    // printf("requestTimestamp: %lld\n", requestTimestamp_3);
    deviceKey_match = (strcmp(deviceKey_3, SAVED_DEVICE_KEY) == 0);
    productKey_match = (strcmp(productKey_3, SAVED_PRODUCT_KEY) == 0);

    if (deviceKey_match && productKey_match)
        return 0;
    else
        return -1;
}

// 增加黑名单
int parse_addblack_json(cJSON *root)
{
    cJSON *deviceKey, *key_item, *productKey, *requestId, *requestTimestamp, *arguments;
    cJSON *qrCodeIds, *qrCodeId_item;
    int i;
    int deviceKey_match;
    int productKey_match;
    // 不需要再次解析JSON，直接使用传入的root对象
    if (!root)
    {
        printf("Error: NULL JSON root\n");
        return -1;
    }

    // 获取JSON对象中的字段
    deviceKey = cJSON_GetObjectItem(root, "deviceKey");
    key_item = cJSON_GetObjectItem(root, "key");
    productKey = cJSON_GetObjectItem(root, "productKey");
    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");
    arguments = cJSON_GetObjectItem(root, "arguments");

    // 检查必要的字段是否存在
    if (!deviceKey || !key_item || !productKey || !requestId || !requestTimestamp)
    {
        printf("Error: Missing required fields\n");
        return -1;
    }

    // 直接提取字段值
    strncpy(deviceKey_4, deviceKey->valuestring, sizeof(deviceKey_4) - 1);
    deviceKey_4[sizeof(deviceKey_4) - 1] = '\0';

    strncpy(key_4, key_item->valuestring, sizeof(key_4) - 1);
    key_4[sizeof(key_4) - 1] = '\0';

    strncpy(productKey_4, productKey->valuestring, sizeof(productKey_4) - 1);
    productKey_4[sizeof(productKey_4) - 1] = '\0';

    strncpy(requestId_4, requestId->valuestring, sizeof(requestId_4) - 1);
    requestId_4[sizeof(requestId_4) - 1] = '\0';

    requestTimestamp_4b = (long long)requestTimestamp->valuedouble;

    // 解析qrCodeIds数组
    if (arguments)
    {
        qrCodeIds = cJSON_GetObjectItem(arguments, "qrCodeIds");
        if (qrCodeIds && qrCodeIds->type == cJSON_Array)
        {
            int array_size = cJSON_GetArraySize(qrCodeIds);
            qrCodeIds_count_4 = array_size;

            for (i = 0; i < qrCodeIds_count_4; i++)
            {
                qrCodeId_item = cJSON_GetArrayItem(qrCodeIds, i);
                if (qrCodeId_item && qrCodeId_item->type == cJSON_String)
                {
                    strncpy(qrCodeIds_4[i], qrCodeId_item->valuestring, sizeof(qrCodeIds_4[i]) - 1);
                    qrCodeIds_4[i][sizeof(qrCodeIds_4[i]) - 1] = '\0';

                    // 调用函数将UUID添加到EEPROM中
                    printf("add UUID EEPROM: %s\n", qrCodeIds_4[i]);
                    addDataToBlacklist((u8 *)qrCodeIds_4[i]);
                }
            }
        }
    }

    for (i = 0; i < qrCodeIds_count_4; i++)
    {
        printf("qrCodeIds[%d]: %s\n", i, qrCodeIds_4[i]);
    }

    deviceKey_match = (strcmp(deviceKey_4, SAVED_DEVICE_KEY) == 0);
    productKey_match = (strcmp(productKey_4, SAVED_PRODUCT_KEY) == 0);

    if (deviceKey_match && productKey_match)
        return 0;

    else
        return -1;
}

// 删除黑名单
int parse_delblack_json(cJSON *root)
{
    cJSON *deviceKey, *key_item, *productKey, *requestId, *requestTimestamp, *arguments;
    cJSON *qrCodeIds, *qrCodeId_item;
    int i;
    int deviceKey_match;
    int productKey_match;
    // 不需要再次解析JSON，直接使用传入的root对象
    if (!root)
    {
        printf("Error: NULL JSON root\n");
        return -1;
    }

    // 获取JSON对象中的字段
    deviceKey = cJSON_GetObjectItem(root, "deviceKey");
    key_item = cJSON_GetObjectItem(root, "key");
    productKey = cJSON_GetObjectItem(root, "productKey");
    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");
    arguments = cJSON_GetObjectItem(root, "arguments");

    // 使用 type 字段检查类型
    if (deviceKey && deviceKey->type == cJSON_String)
    {
        strncpy(deviceKey_5, deviceKey->valuestring, sizeof(deviceKey_5) - 1);
        deviceKey_5[sizeof(deviceKey_5) - 1] = '\0';
    }
    else
    {
        deviceKey_5[0] = '\0';
        printf("Warning: deviceKey not found or not a string\n");
    }

    if (key_item && key_item->type == cJSON_String)
    {
        strncpy(key_5, key_item->valuestring, sizeof(key_5) - 1);
        key_5[sizeof(key_5) - 1] = '\0';
    }
    else
    {
        key_5[0] = '\0';
        printf("Warning: key not found or not a string\n");
    }

    if (productKey && productKey->type == cJSON_String)
    {
        strncpy(productKey_5, productKey->valuestring, sizeof(productKey_5) - 1);
        productKey_5[sizeof(productKey_5) - 1] = '\0';
    }
    else
    {
        productKey_5[0] = '\0';
        printf("Warning: productKey not found or not a string\n");
    }

    if (requestId && requestId->type == cJSON_String)
    {
        strncpy(requestId_5, requestId->valuestring, sizeof(requestId_5) - 1);
        requestId_5[sizeof(requestId_5) - 1] = '\0';
    }
    else
    {
        requestId_5[0] = '\0';
        printf("Warning: requestId not found or not a string\n");
    }

    if (requestTimestamp && requestTimestamp->type == cJSON_Number)
    {
        requestTimestamp_5b = (long long)requestTimestamp->valuedouble;
    }
    else
    {
        requestTimestamp_5b = 0;
        printf("Warning: requestTimestamp not found or not a number\n");
    }

    // 解析qrCodeIds数组
    if (arguments && arguments->type == cJSON_Object)
    {
        qrCodeIds = cJSON_GetObjectItem(arguments, "qrCodeIds");
        if (qrCodeIds && qrCodeIds->type == cJSON_Array)
        {
            int array_size = cJSON_GetArraySize(qrCodeIds);
            qrCodeIds_count_5 = array_size;

            for (i = 0; i < qrCodeIds_count_5; i++)
            {
                qrCodeId_item = cJSON_GetArrayItem(qrCodeIds, i);
                if (qrCodeId_item && cJSON_IsString(qrCodeId_item) &&
                    qrCodeId_item->valuestring && qrCodeId_item->valuestring[0])
                {
                    strncpy(qrCodeIds_5[i], qrCodeId_item->valuestring, sizeof(qrCodeIds_5[i]) - 1);
                    qrCodeIds_5[i][sizeof(qrCodeIds_5[i]) - 1] = '\0';
                }
                else
                {
                    qrCodeIds_5[i][0] = '\0';
                    printf("Warning: qrCodeIds[%d] not found or not a string\n", i);
                }
            }
        }
        else
        {
            qrCodeIds_count_5 = 0;
            printf("Warning: qrCodeIds not found or not an array\n");
        }
    }
    else
    {
        qrCodeIds_count_5 = 0;
        printf("Warning: arguments not found or not an object\n");
    }

    for (i = 0; i < qrCodeIds_count_5; i++)
    {
        printf("EEPROM  del  UUID: %s\n", qrCodeIds_5[i]);
        deleteDataFromBlacklist((u8 *)qrCodeIds_5[i]);
    }

    deviceKey_match = (strcmp(deviceKey_5, SAVED_DEVICE_KEY) == 0);
    productKey_match = (strcmp(productKey_5, SAVED_PRODUCT_KEY) == 0);

    if (deviceKey_match && productKey_match)
        return 0;
    else
        return -1;
}
//读取设备设置参数
int parse_readsetting_json(cJSON *root)
{
    cJSON *requestId,*requestTimestamp;
    char ts[24];
    char req_id_str[32];
    char req_ts_str[32];

    if (!root)
        return -1;

    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");

    cjson_item_to_str(requestId, req_id_str, sizeof(req_id_str));
    cjson_item_to_str(requestTimestamp, req_ts_str, sizeof(req_ts_str));

    RtcChip_TimestampMillisToStr(ts, sizeof(ts));
    call_readsetting_Reply(ts,req_id_str,req_ts_str);   
    return 0;
}
//更新时间
int parse_uptime_json(cJSON *root)
{
    /**
     * {
	"messageId": "1761204760350",
	"requestId": "1",
	"time": "20251023153240"
}
     */
    cJSON *timer;
    int year, month, date, hour, minute, second;
    struct tm time; // 声明 struct tm 类型
    char timestamp[15];
    RTC_Time currentTime; // 存放时间结构体
    if (!root)
    {
        printf("Error: NULL JSON root\n");
        return -1;
    }
    timer = cJSON_GetObjectItem(root, "time");
    // 使用 type 字段检查类型
    if (timer && timer->type == cJSON_String)
    {
        // 使用 sscanf 解析时间
        strncpy(timestamp, timer->valuestring, 14);
        //	printf("----%s---%s\n",timer->valuestring,timestamp);
        sscanf(timestamp, "%4d%2d%2d%2d%2d%2d", &year, &month, &date, &hour, &minute, &second);
        currentTime.year = year - 2000;
        currentTime.month = month;
        currentTime.date = date;
        currentTime.dayOfWeek = 2;
        currentTime.hour = hour;
        currentTime.minute = minute;
        currentTime.second = second;

        // 计算星期几
        memset(&time, 0, sizeof(time)); // 初始化结构体
        time.tm_year = year - 1900;     // tm_year 以1900为基准
        time.tm_mon = month - 1;        // tm_mon 以0为基准
        time.tm_mday = date;
        time.tm_hour = hour;
        time.tm_min = minute;
        time.tm_sec = second;

        currentTime.dayOfWeek = time.tm_wday == 0 ? 7 : time.tm_wday; // tm_wday 0=Sunday, 1=Monday, ..., 6=Saturday
        // 调用 DS1302_SetTime 设置时间
        RtcChip_SetTime(&currentTime);
        delay_ms(100);
        RtcChip_GetTime(&currentTime);
        printf("setting time is 20%02d-%02d-%02d %02d:%02d:%02d\n",
               currentTime.year, currentTime.month, currentTime.date,
               currentTime.hour, currentTime.minute, currentTime.second);
    }
    else
    {
        printf("Warning: time not found or not a string\n");
    }
    return 0;
}
//呼梯：
int parse_call_elevator_json(cJSON *root)
{
    cJSON *arguments;
    cJSON *requestId, *requestTimestamp;
    cJSON *floorRelayNumbers;
    cJSON *num_item;
    int n;
    char req_id_str[32];
    char req_ts_str[32];
    char ts[24];
    uint8_t elevator_limit5[PUBLIC_FLOORS_LIMIT_LEN];

    g_floor_relay_numbers_count = 0;
    g_floor_relay_array_has_single_number = 0;
    g_floor_relay_single_number_value = 0;

    if (!root)
        return -1;

    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");

    arguments = cJSON_GetObjectItem(root, "arguments");
    floorRelayNumbers = (arguments && arguments->type == cJSON_Object)
                            ? cJSON_GetObjectItem(arguments, "floorRelayNumbers")
                            : NULL;
    if (!floorRelayNumbers || floorRelayNumbers->type != cJSON_Array)
        return 0;

    n = cJSON_GetArraySize(floorRelayNumbers);
    if (n > 1)
    {
        printf("call_elevator_numbers is mutiple numbers!\r\n");
        return -1;
    }
    if (n == 0)
        return 0;

    num_item = cJSON_GetArrayItem(floorRelayNumbers, 0);
    if (!num_item || num_item->type != cJSON_Number)
        return 0;

    g_floor_relay_numbers[0] = (int)num_item->valuedouble;

    FloorCtrl_GetLimit(elevator_limit5);
    Floor_AuthCheck_limit_off(elevator_limit5);
    Floor_CallOne((uint8_t)g_floor_relay_numbers[0]);
    Floor_AuthCheck_limit(elevator_limit5);
    Bsp_SetBeep(1);
    // g_floor_relay_numbers_count = 1;
    // g_floor_relay_array_has_single_number = 1;
    // g_floor_relay_single_number_value = g_floor_relay_numbers[0];

    cjson_item_to_str(requestId, req_id_str, sizeof(req_id_str));
    cjson_item_to_str(requestTimestamp, req_ts_str, sizeof(req_ts_str));

    RtcChip_TimestampMillisToStr(ts, sizeof(ts));

    call_elevator_Reply(ts, req_id_str, req_ts_str);
    return 0;
}
//开门：
int parse_open_door_json(cJSON *root)
{
    cJSON *arguments;
    cJSON *requestId, *requestTimestamp;
    cJSON *doorRelayNumbers;
    cJSON *num_item0, *num_item1;
    int n;
    char req_id_str[32];
    char req_ts_str[32];
    char ts[24];

    g_floor_relay_numbers_count = 0;
    g_floor_relay_array_has_single_number = 0;
    g_floor_relay_single_number_value = 0;

    if (!root)
        return -1;

    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");

    arguments = cJSON_GetObjectItem(root, "arguments");
    doorRelayNumbers = (arguments && arguments->type == cJSON_Object)
                            ? cJSON_GetObjectItem(arguments, "doorRelayNumbers")
                            : NULL;
    if (!doorRelayNumbers || doorRelayNumbers->type != cJSON_Array)
        return 0;

    n = cJSON_GetArraySize(doorRelayNumbers);
    if (n != 2)
    {
        printf("open_door_numbers is error numbers!\r\n");
        return -1;
    }

    num_item0 = cJSON_GetArrayItem(doorRelayNumbers, 0);
    num_item1 = cJSON_GetArrayItem(doorRelayNumbers, 1);
    if (!num_item0 || num_item0->type != cJSON_Number ||
        !num_item1 || num_item1->type != cJSON_Number)
        return 0;

    g_floor_relay_numbers[0] = (int)num_item0->valuedouble;
    g_floor_relay_numbers[1] = (int)num_item1->valuedouble;

    printf("doorRelayNumbers: [%d, %d]\r\n", g_floor_relay_numbers[0], g_floor_relay_numbers[1]);
    
    //---------------------
    Rly_Set(g_floor_relay_numbers[0], g_floor_relay_numbers[1]);
    TIM1_ClearPendingAndEnable();
    TIM2_ClearPendingAndEnable(); 
    Bsp_SetBeep(1);
    //--------------------


    cjson_item_to_str(requestId, req_id_str, sizeof(req_id_str));
    cjson_item_to_str(requestTimestamp, req_ts_str, sizeof(req_ts_str));

   
    RtcChip_TimestampMillisToStr(ts, sizeof(ts));
    call_open_door_Reply(ts, req_id_str, req_ts_str);
    return 0;
}
int parse_upgongyao_json(cJSON *root)
{
    cJSON *deviceKey, *key_item, *productKey, *requestId, *requestTimestamp, *arguments;
    cJSON *publicKey_item;
    int deviceKey_match;
    int productKey_match;

    // 不需要再次解析JSON，直接使用传入的root对象
    if (!root)
    {
        printf("Error: NULL JSON root\n");
        return -1;
    }

    // 获取JSON对象中的字段
    deviceKey = cJSON_GetObjectItem(root, "deviceKey");
    key_item = cJSON_GetObjectItem(root, "key");
    productKey = cJSON_GetObjectItem(root, "productKey");
    requestId = cJSON_GetObjectItem(root, "requestId");
    requestTimestamp = cJSON_GetObjectItem(root, "requestTimestamp");
    arguments = cJSON_GetObjectItem(root, "arguments");

    // 使用 type 字段检查类型
    if (deviceKey && deviceKey->type == cJSON_String)
    {
        strncpy(deviceKey_6, deviceKey->valuestring, sizeof(deviceKey_6) - 1);
        deviceKey_6[sizeof(deviceKey_6) - 1] = '\0';
    }
    else
    {
        deviceKey_6[0] = '\0';
        printf("Warning: deviceKey not found or not a string\n");
    }

    if (key_item && key_item->type == cJSON_String)
    {
        strncpy(key_6, key_item->valuestring, sizeof(key_6) - 1);
        key_6[sizeof(key_6) - 1] = '\0';
    }
    else
    {
        key_6[0] = '\0';
        printf("Warning: key not found or not a string\n");
    }

    if (productKey && productKey->type == cJSON_String)
    {
        strncpy(productKey_6, productKey->valuestring, sizeof(productKey_6) - 1);
        productKey_6[sizeof(productKey_6) - 1] = '\0';
    }
    else
    {
        productKey_6[0] = '\0';
        printf("Warning: productKey not found or not a string\n");
    }

    if (requestId && requestId->type == cJSON_String)
    {
        strncpy(requestId_6, requestId->valuestring, sizeof(requestId_6) - 1);
        requestId_6[sizeof(requestId_6) - 1] = '\0';
    }
    else
    {
        requestId_6[0] = '\0';
        printf("Warning: requestId not found or not a string\n");
    }

    if (requestTimestamp && requestTimestamp->type == cJSON_Number)
    {
        requestTimestamp_6b = (long long)requestTimestamp->valuedouble;
    }
    else
    {
        requestTimestamp_6b = 0;
        printf("Warning: requestTimestamp not found or not a number\n");
    }

    // 解析publicKey
    if (arguments && arguments->type == cJSON_Object)
    {
        publicKey_item = cJSON_GetObjectItem(arguments, "publicKey");
        if (publicKey_item && publicKey_item->type == cJSON_String)
        {
            strncpy(publicKey_6, publicKey_item->valuestring, sizeof(publicKey_6) - 1);
            publicKey_6[sizeof(publicKey_6) - 1] = '\0';
        }
        else
        {
            publicKey_6[0] = '\0';
            printf("Warning: publicKey not found or not a string\n");
        }
    }
    else
    {
        publicKey_6[0] = '\0';
        printf("Warning: arguments not found or not an object\n");
    }
    printf("publicKey: %s\n", publicKey_6);
    printf("publicKey: %s\n", g_device_public_Key);
    if (publicKey_6[0] != '\0')
    {
        strncpy(g_device_public_Key, publicKey_6, sizeof(g_device_public_Key) - 1);
        g_device_public_Key[sizeof(g_device_public_Key) - 1] = '\0'; // 确保字符串以 '\0' 结尾
    }
    deviceKey_match = (strcmp(deviceKey_6, SAVED_DEVICE_KEY) == 0);
    productKey_match = (strcmp(productKey_6, SAVED_PRODUCT_KEY) == 0);

    if (deviceKey_match && productKey_match)
    {
        return 0;
    }
    else
        return -1;
}

// 服务器下发命令：解析、执行、上报结果
void parseSerialData(const char *data)
{
    int dataLength, ret;
    long long messageId;
    const char *jsonStr;

    cJSON *jsonData;
    char numberStr[2] = {0}; // 假设数字只有1位

    // 查找第一个逗号的位置
    char *commaPos = strchr(data, ',');
    if (commaPos == NULL)
    {
        printf("Invalid data format: no comma found\n");
        return;
    }

    // 提取数字部分
    dataLength = commaPos - data;

    // 末尾
    if (dataLength > 0 && dataLength < sizeof(numberStr))
    {
        strncpy(numberStr, data, dataLength);
        numberStr[dataLength] = '\0';
    }
    else
    {
        printf("Invalid number format\n");
        return;
    }

    // 提取JSON部分
    jsonStr = commaPos + 1;

    // 将数字字符串转换为整数
    caseNumber = atoi(numberStr);

    // 解析JSON
    jsonData = cJSON_Parse(jsonStr);
    if (jsonData == NULL)
    {
        const char *errorPtr = cJSON_GetErrorPtr();
        if (errorPtr != NULL)
        {
            printf("JSON parse error: %s\n", errorPtr);
        }
        return;
    }
    RtcChip_GetTime(&currentTime1);
    messageId = (long long)RtcChip_To_MillisTimestamp(&currentTime1);

    /* 转成指针数组 items[] */
    //item_count = BuildItemsFromUuidList1(items, 500);

    // 使用switch-case处理不同的情况
    switch (caseNumber)
    {
    case 101: // RS485透传
        printf("Processing case 1\n");
        ret = parse_rs485_json(jsonData); // requestTimestamp_1
        if (ret)
            Bsp_SetBeep(2);
        else
            Bsp_SetBeep(1);

        // 转换数据,data_1来自parse_rs485_json
        dataLen = hexStringToBytes(data_1, buffer_data, sizeof(buffer_data));
        //485发送数据
        RS485_SendData(buffer_data, dataLen);
        //4G发送数据,requestId_1, requestTimestamp_1来自parse_rs485_json
        First_Reply(messageId, requestId_1, requestTimestamp_1);
        break;

    case 104: // 提取黑名单
        printf("Processing case 4\n");
        ret = parse_tiqublack_json(jsonData); // requestId_3  requestTimestamp_3
        if (ret)
            Bsp_SetBeep(2);
        else
            Bsp_SetBeep(1);

        /* 发送 JSON */
        Four_Reply(messageId, requestId_3, requestTimestamp_3);
        break;

    case 105: // 增加黑名单
        printf("Processing case 5\n");
        ret = parse_addblack_json(jsonData);
        if (ret)
            Bsp_SetBeep(2);
        else
            Bsp_SetBeep(1);

        Fifth_Reply(messageId, requestId_4, requestTimestamp_4b);
        loadDataFromEEPROM(); // 读取黑名单
        break;

    case 106: // 减少黑名单
        printf("Processing case 6\n");
        ret = parse_delblack_json(jsonData);
        if (ret)
            Bsp_SetBeep(2);
        else
            Bsp_SetBeep(1);

        Sixth_Reply(messageId, requestId_5, requestTimestamp_5b);
        break;

    case 107: // 更新公钥
        printf("Processing case 7\n");
        ret = parse_upgongyao_json(jsonData);
        if (ret)
            Bsp_SetBeep(2);
        else
            Bsp_SetBeep(1);

       // Seventh_Reply(messageId, requestId_6, requestTimestamp_6b);
        break;

    case 1: // 在线更新时间
        printf("Processing case 1\n");
        ret = parse_uptime_json(jsonData);
        break;
    case 4: // 读取设备配置
        printf("Processing case 4\n");
        ret = parse_readsetting_json(jsonData);
        break;
    case 5: // 呼梯
        printf("Processing case 5\n");
        ret = parse_call_elevator_json(jsonData);
        break;
    case 6: // 开门
        printf("Processing case 6\n");
        ret = parse_open_door_json(jsonData);
        break;
    default:
        printf("Unknown case number: %d\n", caseNumber);
        break;
    }
    // 释放JSON对象
    cJSON_Delete(jsonData);
}
