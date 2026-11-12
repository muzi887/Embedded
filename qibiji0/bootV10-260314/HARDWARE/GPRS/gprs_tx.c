#include "usart.h"
//#include "mqtt.h"
#include "gprs.h"
#include "boot.h"
extern OTA_InfoCB OTA_Info;

static u8 g_last_sta = 0;
static CJsonPara g_last_para;
static u8 g_last_valid = 0;
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
   char json_str[512] = {'\0'};
  float test = 15.52;
   char LastTime[20] = {'\0'};

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
			sprintf(json_str,
				"%d,{"
				"\"type\":\"status\","
				"\"clientid\":%s,"
				"\"status\":\"error1\","
				"\"random\":\"1234567\""
				"}"
				,Topic_Pub,para.clientid
			 );
			break;
			
		case UPGRADE: //请求bin文件块序号
//		  printf("---%d\n",para.offset);
			sprintf(json_str,
				"%d,{"
				"\"type\":\"%s\","
				"\"back\":\"%s\","
				"\"clientid\":\"%s\","
		    	"\"CmdName\":\"%s\","
				"\"para\":{"
           		"\"offset\":%d"
				"}," 
				"\"random\":\"%s\""
				"}"
				,Topic_Pub,para.type,para.back,para.clientid,para.CmdName,para.offset,para.random
			 );
			break;
		
		case SVERSION: //版本号上报（服务器会根据情况告诉客户端是否可以更新）
			sprintf(json_str,
				"%d,{"
				"\"type\":\"%s\","
				"\"back\":\"%s\","
				"\"clientid\":\"%s\","
		    	"\"CmdName\":\"%s\","
				"\"para\":{"
            	"\"version\":\"%s\""
				"}," 
				"\"random\":\"%s\""
				"}"
				,Topic_Pub,para.type,para.back,para.clientid,para.CmdName,OTA_Info.OTA_ver,para.random
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