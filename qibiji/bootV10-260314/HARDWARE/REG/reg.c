#include "reg.h"
#include "delay.h"
#include "24cxx.h"
#include "boot.h"

void AT24CXX_ReadOTAInfo(void)
{
	memset(&OTA_Info,0,OTA_INFOCB_SIZE);  //清空OTA_Info结构体清零
	if(AT24CXX_Check()) 
	{
		OTA_Info.OTA_flag = OTA_A1_FLAG;
		sprintf(OTA_Info.OTA_ver,"1.0.0");
		printf("OTA_ver = %s\n",OTA_Info.OTA_ver);
	}
	else
	{                         
		AT24CXX_Read(OTA_ADDR,(uint8_t *)&OTA_Info,OTA_INFOCB_SIZE);        //从24C02读取数据，存储到OTA_Info结构体
		printf("OTA_ver = %s,%x,%d\n",OTA_Info.OTA_ver,OTA_Info.OTA_flag,OTA_Info.UPDATE_flag);
	}
}

//写入默认值
void paraToEeprom(void)
{
	 memset(&OTA_Info,0,OTA_INFOCB_SIZE);
     OTA_Info.OTA_flag = OTA_A1_FLAG;					// 标志位默认值，表示当前A区是最新的
	 OTA_Info.UPDATE_flag=0;							// 0 无需ota ，1 需要联网ota
	 sprintf(OTA_Info.OTA_ver,"v0.0.0");
	 printf("set default para\r\n");
	 printf("OTA_ver = %s,%x\n",OTA_Info.OTA_ver,OTA_Info.OTA_flag);
	 AT24CXX_Write(OTA_ADDR,(uint8_t *)&OTA_Info,OTA_INFOCB_SIZE);        //写入标志位
}