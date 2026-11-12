#include "switch.h"
#include "sys.h"
#include "usart.h"
#include "reg.h"
#include "main.h"
#include "24cxx.h"
#include "event_pub.h"
#include "property_report.h"
#include <string.h>
#include <stdlib.h>
#include "dc_motor.h"

/*-----------------------------------------
 * IN1 上限位
 * IN2 下限位
 * IN3 行程
 * IN4 行程
 * IN5
 * IN6
------------------------------------------*/

u8 FLAG_FORWORD = 0, FLAG_BACKWORD = 0;
u8 FLAG_A = 0, FLAG_B = 0;
u16 Num_count = 0;
u16 Num_count_up = 0;
u16 Num_count_down = 0;
//u8 kaival = 0;
u8 changeFlag = 0;

void SWITCH_Init(void) // IO初始化
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO, ENABLE); // 使能时钟

	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7; // IN_1
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_0 | GPIO_Pin_1; // IN_2 IN_3 IN_4 IN_5
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15; // IN6
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOE, &GPIO_InitStructure);
}

// 转数判断  CODE1--IN3 PB4    CODE2--IN4 PB0
void JudeCount(void)
{
	static u8 cnt = 0, cnt1 = 0, flag = 1, flag1 = 1;

	if (CODE1 == 0) // 低电平有效
	{
		cnt++;
		if ((cnt >= 1) && (flag == 0)) // 10ms检测
		{
			cnt = 0;
			flag = 1;
			// printf("code1--\n");
			ChangeReality();
		}
	}
	else
	{
		cnt = 0;
		flag = 0;
	}

	if (CODE2 == 0) // 低电平有效
	{
		cnt1++;
		if ((cnt1 >= 1) && (flag1 == 0)) // 10ms检测
		{
			cnt1 = 0;
			flag1 = 1;
			ChangeReality();
			//	printf("code2--\n");
		}
	}
	else
	{
		cnt1 = 0;
		flag1 = 0;
	}
}

void ChangeReality(void)
{
	u8 is_moving = 0;

	if (sys_moter_state == MOTOR_RESET)
		return;
	// 电机下降时开始计数jian
	if (sys_moter_state == MOTOR_DOWN && !FLAG_OPENALL && !FLAG_CLOSEALL)
	{
		if (Num_count > 0)
		{
			Num_count--;
		}
		else
		{
			Num_count = 0;
		}
		is_moving = 1;
	}
	// 电机上升时开始计数jia
	if (sys_moter_state == MOTOR_UP && !FLAG_OPENALL && !FLAG_CLOSEALL)
	{
		if (Num_count < armReg.gate_maxturns || autoFlag)
		{
				Num_count++;
		}
		is_moving = 1;

	}

	if (!is_moving)
		return;

	armReg.gate_kaidu = Num_count * 100 / armReg.gate_maxturns;
	//armReg.range_reality = kaival;

	PropertyReport_SetPending(); /* 置位，主循环中处理，避免中断栈溢出 */


	changeFlag = 1;
	//printf("%d--%d---%d\n",Num_count,armReg.gate_maxturns,armReg.gate_kaidu);
}

// 计算开度 每 500ms 由 TIM2 调用
void JudeKaidu(void)
{
	static u16 LastNumCount = 0;

	if ((armReg.motor_on_off == MOTOR_STOP) && (changeFlag))
	{
		if (LastNumCount != Num_count)
		{
			LastNumCount = Num_count;
		}
		else
		{
			changeFlag = 0;

			// kaival = Num_count *100 / armReg.gate_maxturns;
			//  if(kaival <= 100)
			//  	kaival = 100 - kaival;
			//  else
			//  	kaival = 0;

			//armReg.gate_kaidu = kaival;
			armReg.gate_kaidu_count = Num_count;

			//			if(FLAG_OPENALL)
			//				armReg.gate_kaidu = 100;
			//
			//			if(FLAG_CLOSEALL)
			//				armReg.gate_kaidu = 0;
			//

			Witeflag = 1;
			EepromAddr = 0x0006; // 存储当前值
			//printf("gate_kaidu_count = %d\n",Num_count);
		}
	}
}

// 限位判断
void JudeDir(void)
{
	// 判断上限位标志,UP_LIMIT是传感器, 接近时为低电平, 离开时为高电平
	if (UP_LIMIT && FLAG_OPENALL) // 闸门向下启动离开上限位时
	{
		if (sys_moter_dir == MOTOR_DOWN) // 限位标志为真，闸门下降时
		{
			// 全开标志清零
			FLAG_OPENALL = 0;
			Num_count = armReg.gate_maxturns;
			//	printf("---------------");
		}
	}
	if (!UP_LIMIT && !FLAG_OPENALL) // 闸门向上运行通过上限位时
	{
		// printf("-OPEN--ON");
		// 全开标志置1，目的是全开时的动作只执行一次
		FLAG_OPENALL = 1;
		//EventPostSimple(EVT_LIMIT_OPEN);
		// 闸门行程计数清零
		// Num_count=0;
		if (autoFlag)
		{
			armReg.gate_maxturns = Num_count;
			printf("save maxtruns= %d",Num_count);
			Witeflag = 1;
			EepromAddr = 0x0004; // 存储当前值
		}
	}

	//------------------------------------------------------------------------
	// 判断下限位标志,DN_LIMIT是传感器 , 接近时为低电平, 离开时为高电平
	if (DN_LIMIT && FLAG_CLOSEALL) // 闸门向上启动离开下限位时
	{
		if (sys_moter_dir == MOTOR_UP)
		{
			Num_count = 0;
			FLAG_CLOSEALL = 0;
			//printf("-CLOSE--off");
		}
	}
	if (!DN_LIMIT && !FLAG_CLOSEALL) // 闸门向下运行通过下限位时
	{
		//printf("-CLOSE--on-");
		FLAG_CLOSEALL = 1;
		//EventPostSimple(EVT_LIMIT_CLOSE);
		Num_count = 0;
		// Num_count_down=0;
	}
}

// 卡转判断  定时器中断判断10ms中断一次
void JudeLocked(void)
{
	//	static u8  cnt = 0;
	//	static u16 lastCount = 0;
	//	if((sys_moter_dir==MOTOR_DOWN) || (sys_moter_dir==MOTOR_UP))
	//	{
	//		if(lastCount != Num_count)
	//		{
	//			cnt = 0;
	//			lastCount = Num_count;
	//		}
	//		else
	//		{
	//			cnt++;
	//			//printf("Locked=%d\n",cnt);
	//			if(cnt >= 100) //1s内检测不到转动，认为卡转
	//			{
	//				cnt = 0;
	//				dcmotor_stop();
	//				printf("-stop3");
	//				sys_moter_state=MOTOR_STOP;
	//			}
	//		}
	//	}
}
