#include "usart.h"
//#include "mqtt.h"
#include "gprs.h"
#include "boot.h"
extern OTA_InfoCB OTA_Info;

static u8 g_last_sta = 0;
static CJsonPara g_last_para;
static u8 g_last_valid = 0;

/* ---- 消息标识：id 与 ts 每次发送（含超时重发）都重新生成 ----
   协议要求 id 在单设备时间窗内不重复、平台回包原样带回该 id（升级记录里的 MsgId 也取它）；
   A1 应用特意用“新的时间戳 id”重发，就是为了不让平台把重发当成同一条请求。
   原实现把 id 写死 "test-001"、ts 写死 1779771714791，5 次超时重发内容完全一样，
   平台侧既无法区分重发，升级记录也全是同一个 MsgId。 */
#define GPRS_UNIX_BASE_2000  946684800UL   /* 2000-01-01 00:00:00 UTC */
#define GPRS_TZ_OFFSET_SEC   28800UL       /* DS1302 存东八区本地时，换算 UTC 用 */

/* DS1302 日历（ds1302_dataread 已转十进制）→ Unix 秒；日历非法返回 0 */
static u32 Gprs_UnixSecNow(void)
{
	static const u8 dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	char t[7];
	u32 days = 0, local_sec;
	u8 year, month, day, hour, min, sec, leap;
	int y, m;

	ds1302_dataread(t);
	year  = (u8)t[6];
	month = (u8)t[4];
	day   = (u8)t[3];
	hour  = (u8)t[2];
	min   = (u8)t[1];
	sec   = (u8)t[0];

	if (month < 1u || month > 12u) return 0;
	if (day < 1u || day > 31u) return 0;
	if (hour > 23u || min > 59u || sec > 59u) return 0;

	/* 2000..2099：能被 4 整除即闰年（该区间不涉及 100/400 年规则） */
	for (y = 0; y < (int)year; y++)
		days += (u32)(((y & 3) == 0) ? 366 : 365);
	leap = ((year & 3u) == 0u) ? 1u : 0u;
	for (m = 1; m < (int)month; m++)
	{
		days += dim[m - 1];
		if (m == 2) days += leap;
	}
	days += (u32)(day - 1u);

	local_sec = days * 86400UL + (u32)hour * 3600UL + (u32)min * 60UL + (u32)sec;
	if (local_sec < GPRS_TZ_OFFSET_SEC) return 0;
	return local_sec - GPRS_TZ_OFFSET_SEC + GPRS_UNIX_BASE_2000;
}

/* 13 位 Unix 毫秒时间戳（信封 ts 用） */
int64_t gprs_unix_ms_now(void)
{
	u32 s = Gprs_UnixSecNow();

	if (s < GPRS_UNIX_BASE_2000)
		s = GPRS_UNIX_BASE_2000;   /* RTC 未对时/不可用：退化到基准值，仍然是合法毫秒戳 */
	return (int64_t)s * 1000LL;
}

/* 生成本次发送的消息 id（信封 id 用）：超时重发也必须拿到新 id */
void gprs_next_msg_id(char *buf, u16 buflen)
{
	static int64_t s_last_ms = 0;
	int64_t ms = gprs_unix_ms_now();

	if (ms <= s_last_ms)          /* 同一秒内的连续发送/重发：只保证递增且不重复 */
		ms = s_last_ms + 1;
	s_last_ms = ms;
	snprintf(buf, buflen, "%llu", (unsigned long long)ms);
}

//---------------------
void At_init(void)
{
	atflag = 0;
	// 发送AT指令，等待模块响应
	printf("Sending AT command\r\n");
	Usart2_Send("usr.cn#AT\r\n", 11);
	// 可以添加一个超时机制，如果在一定时间内没有收到响应，则认为模块不可用
}
void CID_init(void)
{
	printf("Sending MQTT CID\r\n");
	Usart2_Send("usr.cn#AT+MQTTCID?\r\n",20);
}
//-------------------------GPRS发送数据-----------------------------------//
void gprs_send_datas(u8 sta,CJsonPara para)
{	
	u16 length=0,num = 1001;
   static char json_str[512] = {'\0'}; // 静态化：512B 组包缓冲不放在栈上（见 P0-1）
   float test = 15.52;
   char LastTime[20] = {'\0'};

	/* 每次发送（含超时重发）都刷新 id / ts */
	if (sta == SVERSION || sta == UPGRADE || sta == PUTERROR)
	{
		gprs_next_msg_id(para.id, bid);
		para.ts = gprs_unix_ms_now();
	}

	/* 新请求/超时重发前丢弃可能卡住的半帧，避免旧数据被拼进下一帧。 */
	if (sta == SVERSION || sta == UPGRADE)
		gprs_rx_frame_reset();

	// 缓存最近一次可重发请求（版本请求/下载请求）
	if (sta == SVERSION || sta == UPGRADE)
	{
		g_last_sta = sta;
		g_last_para = para;
		g_last_valid = 1;
	}
	
	switch(sta)
	{			
		case WARNING: //警告
			// sprintf(json_str,
			// 	"%d,{"
			// 	"\"type\":\"status\","
			// 	"\"clientid\":%s,"
			// 	"\"status\":\"error1\","
			// 	"\"random\":\"1234567\""
			// 	"}"
			// 	,Topic_Pub,para.clientid
			//  );
			break;
			
		case UPGRADE: //请求bin文件块序号
//		  printf("---%d\n",para.offset);
			sprintf(json_str,
				"%d,{"
				"\"id\":\"%s\","
				"\"ver\":\"%s\","
				"\"ts\":%llu,"
				"\"productKey\":\"%s\","
				"\"deviceKey\":\"%s\","
				"\"type\":\"%s\","
				"\"body\":{"
				"\"OTACode\":\"%s\","
				"\"paras\":{"
				"\"offset\":%d"
				"}"
				"}" 
				"}"
				,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.offset
			 );
			break;
		case PUTERROR: //上报错误
			sprintf(json_str,
				"%d,{"
				"\"id\":\"%s\","
				"\"ver\":\"%s\","
				"\"ts\":%llu,"
				"\"productKey\":\"%s\","
				"\"deviceKey\":\"%s\","
				"\"type\":\"%s\","
				"\"body\":{"
				"\"OTACode\":\"%s\","
				"\"paras\":{"
				"\"code\":\"%s\","
				"\"message\":\"%s\""
				"}"
				"}" 
				"}"
				,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.code,para.message
			 );
			break;		
		case SVERSION: //版本号上报（服务器会根据情况告诉客户端是否可以更新）
			sprintf(json_str,
				"%d,{"
				"\"id\":\"%s\","
				"\"ver\":\"%s\","
				"\"ts\":%llu,"
				"\"productKey\":\"%s\","
				"\"deviceKey\":\"%s\","
				"\"type\":\"%s\","
				"\"body\":{"
				"\"OTACode\":\"%s\","
				"\"paras\":{"
				"\"version\":\"%s\""
				"}"
				"}" 
				"}"
				,7,para.id,para.ver,para.ts,para.productKey,para.deviceKey,para.type,para.OTACode,para.version
			 );
			break;
	}
	
  // 通过USART2发送JSON字符串
  length = strlen(json_str);
  printf("\ncJSON: %d, %s\n", length, json_str);
  Usart2_Send(json_str, length);
}

int gprs_resend_last_request(void)
{
	if (!g_last_valid)
	{
		return 0;
	}
	gprs_send_datas(g_last_sta, g_last_para);
	return 1;
}


//时间格式化
void get_lasttime(char *output) 
{
	char time[7];
	ds1302_dataread(time);
  sprintf(output, "20%02d-%02d-%02d %02d:%02d:%02d", //%04d
            time[6],time[4],time[3],time[2],time[1],time[0]);
          
}

//生成随机字符串
void generate_random_string(char *output) 
{
	char time[7];
	ds1302_dataread(time);
  sprintf(output, "20%02d%02d%02d%02d%02d%02d1000", //%04d
            time[6],time[4],time[3],time[2],time[1],time[0]);
           // rand() % 10000);
}
