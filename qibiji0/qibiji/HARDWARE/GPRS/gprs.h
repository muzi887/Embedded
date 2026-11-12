#ifndef __GPRS_H
#define __GPRS_H

#include "stm32f10x.h"
#include "cJSON.h"
#include "mymalloc.h"
#include "reg.h"
#include "main.h"
#include "stdlib.h"
#include "adc.h"
#include "display.h"
#include "timer.h"
#include "ds1302.h"
#include <string.h>
#include "4G.h"
#include "led.h"
#include "stmflash.h"
#include "24cxx.h"

// #define REPLAY 0   // 回执
// #define ENQUIRY 1  // 查询刷新
// #define SETTINH 2  // 设置
// #define WARNING 3  // 警告
// #define UPGRADE 4  // 设备下载
// #define SVERSION 5 // 版本号上报

#define Topic_Sub 1 // 订阅主题ID
#define Topic_Pub 1 // 发布主题ID

#define BYTESIZE 512 // 每帧字节数

#define bType 16	 // 类型长度
#define bClientid 12 // id
#define bBack 8		 // 是否需要回执
#define bState 6	 // 状态
#define bRandom 19	 // 随机值
#define bCmdName 16	 // cmd名称
#define bVer 10		 // 版本号

// #define ST32_FLASH_SADDR 0x08000000				// FLASH起始地址
// #define ST32_A1_SADDR ST32_FLASH_SADDR + 0xA000 // A1区起始地址  ST32_FLASH_SADDR + ST32_A1_START_PAGE * ST32_PAGE_SIZE

// #define ST32_A2_SADDR ST32_FLASH_SADDR + 0X03C000 // A2区起始地址 ST32_FLASH_SADDR + (ST32_A1_START_PAGE+100) * ST32_PAGE_SIZE


#define OTA_A1_FLAG 0xA1A1A1A1 // 如果OTA_flag等于该值，说明当前在A1区
#define OTA_A2_FLAG 0xA2A2A2A2 // 如果OTA_flag等于该值，说明当前在A2区

/* 定时推送：默认 24 小时，若 armReg.reportT > 0 则用 reportT 秒 */
#define GPRS_PERIODIC_PUSH_INTERVAL_MS (24UL * 60 * 60 * 1000) /* 24 小时 */

typedef struct
{
	u32 OTA_flag;						   // 标志性的变量，当等于OTA_SET_FLAG该值时，说明当前需要OTA更新A区
	u8  OTA_ver[32];						   // 下载包版本号
	u32 OTA_size;						   // 下载包字节数
	u8  UPDATE_flag;	// 0 无需ota ，1 需要联网ota
} OTA_InfoCB;							   // OTA回调的信息结构体，需要存储到24C02
#define OTA_INFOCB_SIZE sizeof(OTA_InfoCB) // OTA回调的信息结构体占用的字节长度

// typedef struct
// {
// 	u32 size;			 // 下载包文件大小，字节
// 	char ver[32];		 // 下载包版本号，数组长度 31 字符 + 终止符
// 	char signMethod[16]; // 签名方法，如 "MD5"
// 	char dProtocol[16];	 // 下载协议，如 "mqtt"
// 	u32 streamId;		 // MQTT传输时的唯一标识
// 	u16 count;			 // 下载包总帧数
// 	u16 remaining;		 // 最后一帧字节数
// } OtaPara;

// 设备信息结构体，待补充
typedef struct
{
	char clientid[bClientid]; // id
} CJsonEquipment;

/* enquiry 回执的 para 结构体，按预定结构赋值后序列化为 JSON */
#define ENQUIRY_LASTTIME_LEN 20
typedef struct
{
	float MotorCurrent;
	float MotorVoltage;
	float BatteryCurrent;
	float BatteryVoltage;
	float Traffic;
	char LastTime[ENQUIRY_LASTTIME_LEN];
	u8 Opening;
} EnquiryPara;

#define JSON_STATE_SIZE	10
typedef struct
{
	char state[JSON_STATE_SIZE];
} StatePara;

#define JSON_VERSION_SIZE	20
typedef struct
{
	char version[JSON_VERSION_SIZE];
} VersionPara;

// #define JSON_PARA_BUF_SIZE	512
// typedef struct
// {
// 	u16 topic;
// 	char type[bType];
// 	char back[bBack];
// 	char CmdName[bCmdName];
// 	char para[JSON_PARA_BUF_SIZE];
// 	char random[bRandom];
// } JsonEnvelope;

//extern OtaPara OtaParas;	// ota参数结构体
extern CJsonEquipment Cjsonequipment;	// cjson参数结构体
extern OTA_InfoCB OTA_Info; // 存储在24C02内的OTA信息回调的结构体
extern u8 atflag_online;
extern u8 OTA_Enable;
// 函数声明

void Gprs_Process(void);
void gprs_rx_datas(void);
void Parse_Process(void);
void gprs_check(void);

// void gprs_AT_init(void);
void gprs_state_machine(void);
u8 gprs_is_ready(void); /* 是否处于正常工作状态 */

void handleAT_ICCID(char *atdata, u16 rxcnt);
void handleAT_MQTTCID(char *atdata, u16 rxcnt);
void handleAT_CSQ(char *atdata, u16 rxcnt);
void handleAT_CCLK(char *atdata, u16 rxcnt);

int simple_weekday(int year, int month, int day);

// int handleMessage(char *message, u16 len);

void generateJsonString(char *buf, size_t buf_size, const char *type, const char *back, const char *clientid, const char *CmdName, const char *random, const char *para);
void state_para_to_json(const StatePara *p, char *buf, size_t buf_size);
void version_para_to_json(const VersionPara *p, char *buf, size_t buf_size);

void Send_cjson_clock(void);


void reply_cjson_clock(char *data, u16 len);
u8 reply_cjson_enquiry(cJSON *root, u8 reply);					 // 查询刷新
u8 reply_cjson_setting(cJSON *root, u8 reply); // 设置闸门开度
u8 reply_cjson_version(cJSON *root, u8 reply);					 // 查询版本号
u8 reply_cjson_update(cJSON *root, u8 reply);
// u8 cjson_sever_reply(cJSON *root, char *message);	 // 服务器发送的回执
 u8 cjson_cmd(cJSON *root);							 // 设备命令处理
// u8 cjson_download_reply(cJSON *root, char *message); // 下载接收回复

// u8 cjson_upgrade(cJSON *root); // 设备升级回执

// void cjson_pub_version(void);	   // 上报版本号
// void cjson_pub_getbin(u16 offset); // 开始下载
//void gprs_send_datas(u8 sta, Cjsonequipment para);

void generate_random_string(char *output); // 生成随机字符串

// 时间格式化
void get_lasttime(char *output);

// enquiry para 结构体序列化为 JSON 字符串
void enquiry_para_to_json(const EnquiryPara *p, char *buf, size_t buf_size);

#endif
