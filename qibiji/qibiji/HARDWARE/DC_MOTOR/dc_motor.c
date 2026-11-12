#include "reg.h"
#include "main.h"
#include "dc_motor.h"
#include "timer.h"
#include "delay.h"
#include "gprs.h"
#include "reg.h"
#include "key.h"
#include "event_pub.h"
#include "adc.h"
#include <stdbool.h>
#include "usart.h"

u8 sys_moter_state = 3;
u8 sys_moter_state_old = 3;
u8 sys_local_remote = 1;
u8 sys_moter_dir = 0;
static MotorStopReason pending_stop_reason = MOTOR_STOP_REASON_UNKNOWN;

u8 openflag = 0, closeflag = 0;
u8 FLAG_OPENALL = 0, FLAG_CLOSEALL = 0;

extern Register armReg;
extern u8 PageSta;	  // 0 :默认参数页面
extern u8 kaival;	  // 开度
extern u8 rxparagate; // 接收到的闸门开度
extern u8 remoteflag; // 远程控制

// IO初始化
void dcmotor_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_3; // 继电器控制 -- 继电器控制
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5; // 继电器控制 -- 485的 限位开关控制
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	RLY_1H_OFF;
	RLY_2H_OFF;

	V01_CTRL = 1; // 上升
	V02_CTRL = 1; // 下降
}

// 电机上升
void dcmotor_up(void)
{
	RLY_2H_ON; // 先继电器2
	delay_ms(5);
	RLY_1H_ON;

	FLAG_A = 0;
	FLAG_B = 0;
	sys_moter_dir = MOTOR_UP;
	// gprs_send_datas(PUBLISH,0);
	KeyLed_Ctl(0x01);
}

// 电机下降
void dcmotor_down(void)
{
	RLY_2H_OFF; // 先继电器2
	delay_ms(5);
	RLY_1H_ON;

	FLAG_A = 0;
	FLAG_B = 0;
	sys_moter_dir = MOTOR_DOWN;
	//	gprs_send_datas(PUBLISH,0);
	KeyLed_Ctl(0x02);
}

// 电机停止
void dcmotor_stop(void)
{
	RLY_1H_OFF; // 关继电器
	// gprs_send_datas(PUBLISH,0);
	KeyLed_Ctl(0x00);
	//	sys_moter_dir=MOTOR_STOP;
	armReg.motor_on_off = MOTOR_STOP;
	remoteflag = 0;
	// printf("\n----stop----\n");
}

void dcmotor_request_stop(MotorStopReason reason)
{
	pending_stop_reason = reason;
	sys_moter_state = MOTOR_STOP;
	dcmotor_stop();
}

// 电机处理函数
void Dcmotor_Process()
{
	static u8 current_blocked = 0;

	/* 电流零点完成前禁止电机运行，确保过流保护有有效基线。 */
	if (!ADC_CurrentIsReady())
	{
		if (!current_blocked || sys_moter_state != MOTOR_STOP)
		{
			sys_moter_state = MOTOR_STOP;
			sys_moter_state_old = MOTOR_STOP;
			armReg.motor_on_off = MOTOR_STOP;
			remoteflag = 0;
			dcmotor_stop();
		}
		current_blocked = 1;
		return;
	}
	current_blocked = 0;

	dcmotor_keycheck();
	dcmotor_ctrl();
	dcmotor_auto(); // 自动校准行程
}
void dcmotor_auto()
{
	static u8 step = 0, cnt = 0;
	if (autoFlag)
	{
		if ((flag300ms & 0x02) == 0)
			return;

		flag300ms &= 0xfd;

		switch (step)
		{
		case 0: // 下降
			if (FLAG_CLOSEALL != 1)
			{
				sys_moter_state = MOTOR_DOWN;
				armReg.motor_on_off = MOTOR_DOWN;
			}
			step = 1;
			break;
		case 1: // 停止
			if (FLAG_CLOSEALL == 1)
			{
				cnt++;
				if (cnt >= 10)
				{
					cnt = 0;
					step = 2;
				}
			}
			break;
		case 2: // 上升
			sys_moter_state = MOTOR_UP;
			armReg.motor_on_off = MOTOR_UP;
			step = 3;
			break;
		case 3: // 停止
			if (FLAG_OPENALL == 1)
			{
				cnt++;
				if (cnt >= 10)
				{
					cnt = 0;
					step = 4;
					// 存储圈数
				}
			}
			break;

		case 4: // 下降
			sys_moter_state = MOTOR_DOWN;
			armReg.motor_on_off = MOTOR_DOWN;
			step = 5;
			break;

		case 5: // 停止
			if (FLAG_CLOSEALL == 1)
			{
				step = 0;
				autoFlag = 0;
			}
			break;
		default:
			break;
		}
	}
	else
	{
		step = 0;
	}
}
//--------自动校准行程-----------
void dcmotor_auto_stop()
{
	static u8 step = 0, cnt = 0;
	if (autoFlag)
	{
		if ((flag300ms & 0x02) == 0)
			return;

		flag300ms &= 0xfd;

		switch (step)
		{
		case 0: // 上升
			if (FLAG_OPENALL != 1)
			{
				sys_moter_state = MOTOR_UP;
				armReg.motor_on_off = MOTOR_UP;
			}
			step = 1;
			break;

		case 1: // 停止  3秒后
			if (FLAG_OPENALL == 1)
			{
				cnt++;
				if (cnt >= 10)
				{
					cnt = 0;
					step = 4;
				}
			}
			break;

		case 4: // 下降
			sys_moter_state = MOTOR_DOWN;
			armReg.motor_on_off = MOTOR_DOWN;
			step = 5;
			break;

		case 5: // 停止
			if (FLAG_CLOSEALL == 1)
			{
				step = 0;
				autoFlag = 0;
			}
			break;
		default:
			break;
		}
	}
	else
	{
		step = 0;
	}
}

//-----------按键状态查询----------
void dcmotor_keycheck()
{
	if (PageSta == 0) // 默认参数页面
	{
		if (KeyNum == KEY_UP && armReg.motor_on_off == MOTOR_STOP && FLAG_OPENALL != 1 && autoFlag == 0) // 上升按键
		{
			sys_moter_state = MOTOR_UP;
			armReg.motor_on_off = MOTOR_UP;
		}
		else if (KeyNum == KEY_DOWN && armReg.motor_on_off == MOTOR_STOP && FLAG_CLOSEALL != 1 && autoFlag == 0) // 下降按键
		{
			sys_moter_state = MOTOR_DOWN;
			armReg.motor_on_off = MOTOR_DOWN;
		}

		if (KeyNum == KEY_ENTER) // 停止按键
		{
			sys_moter_state = MOTOR_STOP;
			armReg.motor_on_off = MOTOR_STOP;
			autoFlag = 0;
		}
	}
}

//-----------电机控制--------------
void dcmotor_ctrl()//TODO 待修改,关闭的逻辑较为复杂
{
	static u8 event_motor_up_posted = 0; // 标记 EVT_MOTOR_START 是否已上报，保证只上报一次
	static u8 event_motor_down_posted = 0; // 标记 EVT_MOTOR_START 是否已上报，保证只上报一次
	static u8 event_motor_stop_posted = 0; // 标记 EVT_MOTOR_START 是否已上报，保证只上报一次

	//应该考虑增加除限位以外的其他停止条件，如堵转（计时）、过流等
	bool t1 = false, t2 = false, t3 = false, t4 = false; // 电机停止条件判断
	MotorStopReason stop_reason;

	// 判断电机停止条件
	t1 = FLAG_CLOSEALL == 1 && (sys_moter_state == MOTOR_DOWN || sys_moter_state == MOTOR_RESET); // 下限位
	t2 = FLAG_OPENALL == 1 && (sys_moter_state == MOTOR_UP || sys_moter_state == MOTOR_RESET);	  // 上限位
	// t3 t4 目标值与外部命令有关 
	t3 = sys_moter_state == MOTOR_UP && armReg.gate_kaidu >= armReg.range_target && remoteflag;								 // 上升时到达目标
	t4 = sys_moter_state == MOTOR_DOWN && armReg.gate_kaidu <= armReg.range_target && armReg.range_target != 0 && remoteflag; // 关闭时到达目标

	stop_reason = pending_stop_reason;
	if (stop_reason == MOTOR_STOP_REASON_UNKNOWN)
	{
		if (t3 || t4 || (remoteflag && ((t1 && armReg.range_target == 0) || (t2 && armReg.range_target >= 100))))
			stop_reason = MOTOR_STOP_REASON_TARGET_REACH;
		else if (t1)
			stop_reason = MOTOR_STOP_REASON_LIMIT_CLOSE;
		else if (t2)
			stop_reason = MOTOR_STOP_REASON_LIMIT_OPEN;
		else
			stop_reason = MOTOR_STOP_REASON_MANUAL;
	}

	if (t1 || t2 || t3 || t4)
	{
		/* 到达物理限位时将开度归一化，确保停机后的强制属性上报为最终值 */
		if (t1)
			armReg.gate_kaidu = 0;
		else if (t2)
			armReg.gate_kaidu = 100;
		sys_moter_state = MOTOR_STOP;
	}
	//-------------------------------------------------------------------------------------------------------
	// 执行电机运行状态
	if (sys_moter_state_old != sys_moter_state)
	{
		switch (sys_moter_state)
		{
		case MOTOR_UP:	  // 上升
			if (!event_motor_up_posted) {
				EventPost(EVT_MOTOR_START_UP);
				event_motor_up_posted = 1;
				event_motor_stop_posted = 0;
			}
			dcmotor_up(); /* 电机上升 */
			break;
		case MOTOR_DOWN:	// 下降
			if (!event_motor_down_posted) {
				EventPost(EVT_MOTOR_START_DOWN);
				event_motor_down_posted = 1;
				event_motor_stop_posted = 0;
			}
			dcmotor_down(); /* 电机下降 */
			break;
		case MOTOR_STOP: // 停止
			event_motor_up_posted = 0; 
			event_motor_down_posted = 0; 
			if (!event_motor_stop_posted)
			{
				event_motor_stop_posted=1;
				EventPostMotorStop(stop_reason, (u16)armReg.gate_kaidu);
				pending_stop_reason = MOTOR_STOP_REASON_UNKNOWN;
				PropertyReport_SetPending();
			}
			dcmotor_stop();
			break;
		case MOTOR_RESET: // 复位
			dcmotor_down();
			break;
		default:
			break;
		}

		sys_moter_state_old = sys_moter_state;
	}

	if (sys_moter_state != MOTOR_STOP && flag1s == 1) // flag1s 由 485 设置，此处用于上传数据
	{
		// gprs_send_datas(PUBLISH,0);
	}
}
