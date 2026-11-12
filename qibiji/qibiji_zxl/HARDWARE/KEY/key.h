#ifndef __KEY_H
#define __KEY_H
#include "sys.h"

#define KEY_LEFT_VAL     PAin(12) 
#define KEY_RIGHT_VAL   	PCin(9) 
#define KEY_ENTER_VAL  		PAin(11) 
#define KEY_DOWN_VAL  		PAin(15) 
#define KEY_UP_VAL  	  	PDin(15)

#define KEY_UP 			1
#define KEY_DOWN		2
#define KEY_ENTER		3
#define KEY_LEFT		4
#define KEY_RIGHT	  5

extern u8 KeyNum;

void KEY_Init(void);  		//IO初始化
u8 KEY_Scan(void);  			//按键扫描函数
void KEY_Process(void);
void KeyLed_Ctl(u8 state);	//KEY_LED控制
void Get_KeyVal(void);      //按键值扫描

#endif
