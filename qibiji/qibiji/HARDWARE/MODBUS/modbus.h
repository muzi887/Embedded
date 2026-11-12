#ifndef	_MODBUS_H_
#define	_MODBUS_H_

#include "stm32f10x.h"

#define  ADDR_0x10_LEN   0x17

#define  ADDR_10H_RMAX   0x1017



//gprs数据传输标志结构体
typedef struct 
{
	u8 time_set;
	u8 valve;
	u8 sec;			
	u16 w_year;
	u8  w_month;
	u8  w_date;
	u8  week;		 
}_gprs_obj;	

extern _gprs_obj gprs_flag;

//时间设置结构体
typedef struct 
{
	u16 vtime;
	u16 utime;
	u16 htime;
}_time_set_obj;		

extern _time_set_obj time_set;	

extern u8  clearnflag;
extern u8  eepromerr;

//数据处理函数
void Receive3DataHost(void);
void Send3DataHost(u8 housecnt);
void checkvalvesta(u8 num);                          //查询阀门的状态

void datas_fill(u8 *buf);

u16 crc16(u8 *puchMsg,u16 usDataLen);               //crc校验
u16 CharToInt(u8 *pChar);                           //字符转整数

void read_flash(void);
u16 read_system(u16 id);
void read_reg(u16 read_start,u16 read_end,u16 *p_data);


u8 Rs485_TxData(u8 *buf);     	//rs485-2数据发送
void Rs485_RxData(void);				//485接收数据处理


void Rs485_Rx_Data(void);       //485接收数据处理
void Modbus_03_Slave(u8 *buf);  //读多个寄存器
void Modbus_06_Slave(u8 *buf);  //写一个寄存器
void Modbus_10_Slave(u8 *buf);  //写多个寄存器

void para_init(void);                //读取eeprom参数初始化

//重发超时判断函数
void resend(void);

#endif	//_MODBUS_H_
