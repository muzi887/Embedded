#include "usart.h"
//#include "mqtt.h"
#include "gprs.h"

//-------------------------检查是否在线-----------------------------------
void gprs_check()
{
	static u8 cnt=0;
	if(flag10ms)
	{
		if(GPRS_LINKA)
		{
			cnt++;
			if(cnt >= 10)
			{
				LED_LINE = 1;
				cnt = 0;
			}
		}
		else
		{
			LED_LINE = 0;
			cnt = 0;
		}
	}
}

//-------------------------查询AT指令-------------------------------------//
void gprs_AT()
{
	static u16 cnt = 0;
	static u8  flag = 0;
	char * cmd;

	if(atflag == 0 || flag1s == 0)  return;
	
	switch(cnt)
	{
		case 0:
		case 1:
			cmd="usr.cn#AT+ICCID?\r\n";
			Usart2_Send(cmd,18);
			break;
		
		case 2:  // 获取设备ID
		case 3:
			cmd="usr.cn#AT+MQTTCID?\r\n";
			Usart2_Send(cmd,20);
			break;
		
		case 4: // 获取时间
			cmd="usr.cn#AT+CCLK?\r\n";
			Usart2_Send(cmd,17);
			break;
		
		case 8: // 获取信号强度
			cmd="usr.cn#AT+CSQ?\r\n";
			Usart2_Send(cmd,16);
			break;
		default:  
			break;
	}
	cnt++; 
	if(cnt >= 3604)
		cnt = 4;
}


//处理AT返回
void handleAT(char *atdata,u16 rxcnt)
{
	char  *pos;
	u8 time[7];
	int year;
	int month;
	int day;
	
	if(strstr(atdata,"OK") == NULL) return;
	
	// 判断是否为+CSQ 响应
	pos = strstr(atdata,"+CSQ:");
	if(pos != NULL)
	{
		if(pos[6] >= 0x30 && pos[7] >= 0x30 && pos[6] <= 0x39 && pos[7] <= 0x39)
		{
			xinhao[6] = pos[6];
			xinhao[7] = pos[7];
		}
	}
	
	// 判断是否为+ICCID 响应
	pos = strstr(atdata,"+ICCID:");
	
	if(pos != NULL)
	{
		if(pos[10] >= 0x30 && pos[10] <= 0x39)
		{
			ccid[6]  = pos[10];
			ccid[7]  = pos[11];
			ccid[8]  = pos[12];
			ccid[9]  = 0X2D;
			ccid[10] = pos[20];
			ccid[11] = pos[21];
			ccid[12] = pos[22];
			ccid[13] = pos[23];
			ccid[14] = pos[24];
			ccid[15] = pos[25];			
		}
	}
	
	// 判断是否为+CCLK 响应
	pos = strstr(atdata,"+CCLK:");

	if(pos != NULL)
	{
		time[0] = ((pos[23]-0x30) << 4) + (pos[24]-0x30);// 秒
		time[1] = ((pos[20]-0x30) << 4) + (pos[21]-0x30);// 分
		time[2] = ((pos[17]-0x30) << 4) + (pos[18]-0x30);// 时
		time[3] = ((pos[14]-0x30) << 4) + (pos[15]-0x30);// 日
		time[4] = ((pos[11]-0x30) << 4) + (pos[12]-0x30);// 月
        
		time[6] = ((pos[8]-0x30) << 4) + (pos[9]-0x30);  // 年
        
		year  = 2000+ (pos[8]-0x30)*10 + (pos[9]-0x30); // 年
		month = ((pos[11]-0x30)*10) + (pos[12]-0x30);   // 月
		day   = ((pos[14]-0x30)*10) + (pos[15]-0x30);   // 日
        
		time[5] = simple_weekday(year,month,day) ;// 星期    

		//printf("%d,%d,%d--",year,month,day);
		//printf("time = %x %x %x %x %x %x %x\n",time[0],time[1],time[2],time[3],time[4],time[5],time[6]);
		
		ds1302_settime(time);
	}
	
	// 判断是否为+MQTTCID 响应
	pos = strstr(atdata,"+MQTTCID:");
	
	if(pos != NULL)
	{
		order[5] =pos[9];
		order[6] =pos[10];
		order[7] =pos[11];
		order[8] =pos[12];
		order[9] =pos[13];
		order[10]=pos[14];
		order[11]=pos[15];
		order[12]=pos[16];
		order[13]=pos[17];
		order[14]=pos[18];
		order[15]=pos[19];
		
		CParas.clientid[0] = pos[9];
		CParas.clientid[1] = pos[10];
		CParas.clientid[2] = pos[11];
		CParas.clientid[3] = pos[12];
		CParas.clientid[4] = pos[13];
		CParas.clientid[5] = pos[14];
		CParas.clientid[6] = pos[15];
		CParas.clientid[7] = pos[16];
		CParas.clientid[8] = pos[17];
		CParas.clientid[9] = pos[18];
		CParas.clientid[10]= pos[19];
		CParas.clientid[11]= '\0';
	}
}

// 计算星期
int simple_weekday(int year, int month, int day) {
	static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	if (month < 3) year--;
	return (year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;
}