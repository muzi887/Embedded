#ifndef __GPRS_H
#define __GPRS_H

#include "stm32f10x.h"
#include "cJSON.h"
#include "mymalloc.h"
#include "reg.h"
#include "main.h"
#include "stdlib.h"
//#include "adc.h"
#include "display.h"
//#include "timer.h"
#include "ds1302.h"
#include <string.h>
#include "4G.h"
#include "led.h"
#include "stmflash.h"
#include "24cxx.h" 

#define REPLAY    0
#define ENQUIRY   1 
#define SETTINH   2
#define WARNING   3
#define UPGRADE   4
#define SVERSION  5

#define Topic_Sub 1
#define Topic_Pub 1

#define BYTESIZE  512

#define bType      16
#define bClientid  12
#define bBack      8
#define bState     6
#define bRandom    19
#define bCmdName   16
#define bVer       10

// gprs_rx_datas() 返回值定义（用于 gprs_return 判定）
#define GPRS_RX_RET_PARSE_FAILED         0
#define GPRS_RX_RET_NO_UPDATE            1
#define GPRS_RX_RET_START_UPDATE         2
#define GPRS_RX_RET_DOWNLOAD_DONE        3
#define GPRS_RX_RET_DOWNLOAD_CONTINUE    4
#define GPRS_RX_RET_AT_OK				 5
#define GPRS_RX_RET_MQTTCID_OK			 6
#define GPRS_RX_RET_NET_ONLINE           10
#define GPRS_RX_RET_NO_DATA              -100

// 关键错误返回值
#define GPRS_RX_ERR_CRC_RETRY_EXCEEDED   -19
#define GPRS_RX_ERR_EEPROM_WRITE         -20
#define GPRS_RX_ERR_JSON_MIN             -41
#define GPRS_RX_ERR_JSON_MAX             -21
#define GPRS_RX_ERR_PACKET_SHORT         -42

	

typedef struct {
	u32  size;           // 固件文件大小（字节）
	char ver[32];    // 固件版本号（最大长度 31 字符 + 终止符）
	char signMethod[16]; // 签名方法，例如 "MD5"
	char dProtocol[16];  // 传输协议，如 "mqtt"
	u32  streamId;       // MQTT传输时的唯一标识
	u16  count;          //数据包总数
	u16  remaining;      //最后一个数据包字节数 
}OtaPara;


typedef struct {
	char type[bType];     	  //类型
	char clientid[bClientid]; //id
	char CmdName[bCmdName];   //cmd命令
	char back[bBack];         //是否需要回执
	char state[bState];       //状态
	char random[bRandom];     //随机值
	u16  offset;              //下载偏移
}CJsonPara;

extern char clientid[bClientid];


extern OtaPara    OtaParas;
extern CJsonPara  CParas;
//extern OTA_InfoCB OTA_Info;	//存储在24C02内的OTA信息回调的结构体
extern u8 atflag;
extern u8 SYS_state;
//函数声明
void cjson_pub_version(void);
//int validate_para_json_object1(cJSON *root);
//int validate_para_json_object2(cJSON *root);
	
void Gprs_Process(void);
int gprs_rx_datas(void);
void gprs_check(void);
void gprs_tx(void);

void gprs_AT(void);
void At_init(void);
void CID_init(void);
void handleAT(char *atdata,u16 rxcnt);
int simple_weekday(int year, int month, int day);

int handleMessage(char *message,u16 len);

u8 cjson_enquiry(void); //查询命令
u8 cjson_setting(cJSON *root,u8 reply); //设置参数控制
u8 cjson_version(void); //查询版本号


u8 cjson_sever_reply(cJSON *root,char *message);     //服务器回复命令执行
u8 cjson_cmd(cJSON *root);  					 //设备下发命令
u8 cjson_download_reply(cJSON *root,char *message,u16 message_len);  //下载数据回复

u8 cjson_upgrade(cJSON *root);   //升级命令执行

void cjson_pub_version(void);    //上报版本号
void cjson_pub_getbin(u16 offset);     //开始下载
void gprs_send_datas(u8 sta,CJsonPara para);
int gprs_resend_last_request(void);    //重发上一次请求（有缓存时返回1）
void gprs_download_context_reset(void); //清零下载上下文（addr/offset/retry）

void generate_random_string(char *output); //生成随机字符串

//时间格式化
void get_lasttime(char *output);
char *find_json_end_by_bracket_matching(char *start, u16 max_len);
int validate_server_reply_json(cJSON *root);

//字符串检查函数
u8 str_contains(const char *str, const char *substr);  //检查字符串中是否包含子字符串
u8 str_contains_wh_lte_7s1(const char *str);  //检查字符串中是否包含 "WH-LTE-7S1"
#endif
