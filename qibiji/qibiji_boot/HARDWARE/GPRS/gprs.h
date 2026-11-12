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
#define PUTERROR  6

#define Topic_Sub 1
#define Topic_Pub 1

#define BYTESIZE  512


#define	bid 			16
#define	bver   			16	  
#define	bts				13
#define	bproductKey 	32
#define	bdeviceKey      32
#define	btype      		8
#define	bOTACode  		16
#define	bversion 		16
#define	bcode 			8
#define	bmessage 		32
#define	boffset 		4


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
#define GPRS_RX_ERR_PACKET_SIZE          -43   // 单包长度非法（0 或超过 512）
#define GPRS_RX_ERR_LENGTH_OVERFLOW      -44   // 累计长度超出声明固件大小
#define GPRS_RX_ERR_SEQ_RETRY_EXCEEDED   -45   // 包序号不符重试超限（重复包/乱序包/服务端不连续）

	

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
	char id[bid];
	char ver[bver];     	  
	int64_t ts; //id
	char productKey[bproductKey];  
	char deviceKey[bdeviceKey];      
	char type[btype];       
	char OTACode[bOTACode];    
	char version[bversion]; 
	char code[bcode];
	char message[bmessage];  
	u16 offset; //数据包序号        
}CJsonPara;

//extern char clientid[bClientid];


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

int cjson_upgrade(cJSON *root);   //升级命令执行

void cjson_pub_version(void);    //上报版本号
void cjson_pub_getbin(u16 offset);     //开始下载
void gprs_send_datas(u8 sta,CJsonPara para);
int gprs_resend_last_request(void);    //重发上一次请求（有缓存时返回1）
void gprs_download_context_reset(void); //清零下载上下文（addr/offset/retry）
u8 gprs_download_pending(void);          //是否有一轮下载正在进行（掉线重连后据此重发在途请求）
void gprs_rx_frame_reset(void);          //丢弃未完成的接收帧，重新对齐 USART2 环形缓冲
int64_t gprs_unix_ms_now(void);              //13 位 Unix 毫秒时间戳（DS1302 日历推出，RTC 无效时退化）
void gprs_next_msg_id(char *buf, u16 buflen); //生成本次发送的消息 id（每次发送/重发都换新，避免平台按 id 去重丢弃）
u8 gprs_rx_defer_pending(void);               //掉线重连窗口里是否暂存到平台回包（见 gprs_rx.c / main.c）
	void cjson_pub_putError(void);
void generate_random_string(char *output); //生成随机字符串

//时间格式化
void get_lasttime(char *output);
char *find_json_end_by_bracket_matching(char *start, u16 max_len);
int validate_server_reply_json(cJSON *root);

//字符串检查函数
u8 str_contains(const char *str, const char *substr);  //检查字符串中是否包含子字符串
u8 str_contains_wh_lte_7s1(const char *str);  //检查字符串中是否包含 "WH-LTE-7S1"
#endif
