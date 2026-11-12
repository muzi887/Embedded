#include "key.h"
#include "led.h"
#include "delay.h"
#include "dc_motor.h"
#include "main.h"
#include "timer.h"

u8 KeyNum = 0; // 按键返回值
u8 key_local_remote_change = 3;
u8 key_setting_change = 3;
u8 keyvalue[5];

// 按键初始化函数
void KEY_Init(void) // IO初始化
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE); // 使能PORTA,C,D时钟

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_15; // stop set down
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; // canl
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;			  // UP
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; //
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);
}

// 按键处理函数
void KEY_Process()
{
	Get_KeyVal();
	KeyNum = KEY_Scan();
}

// 按键扫描
uint8_t KEY_Scan()
{
	u8 keynum = 0, i = 0;

	keynum = 0;
	for (i = 0; i < 5; i++)
	{
		if (keyvalue[i])
		{
			keynum = keyvalue[i];
			keyvalue[i] = 0;
			break;
		}
	}
	return keynum; // 无按键按下返回0
}

// 获取按键值
void Get_KeyVal(void)
{
	static uint8_t key_up = 1; // 按键松开标志
	static u8 keyupcnt = 0, keydowncnt = 0, keyentercnt = 0, keyleftcnt = 0, keyrightcnt = 0;

	if (flag10ms == 0)
		return;

	flag10ms = 0;

	if (key_up && (KEY_UP_VAL == 0 || KEY_DOWN_VAL == 0 || KEY_ENTER_VAL == 0 || KEY_LEFT_VAL == 0 || KEY_RIGHT_VAL == 0))
	{
		if (KEY_UP_VAL == 0)
		{
			keydowncnt = 0;
			keyentercnt = 0;
			keyleftcnt = 0;
			keyrightcnt = 0;

			keyupcnt++;
			if (keyupcnt == 3)
			{
				keyupcnt = 3;
				keyvalue[0] = KEY_UP;
			}
		}
		else if (KEY_DOWN_VAL == 0)
		{
			keyupcnt = 0;
			keyentercnt = 0;
			keyleftcnt = 0;
			keyrightcnt = 0;

			keydowncnt++;
			if (keydowncnt == 3)
			{
				keydowncnt = 3;
				keyvalue[1] = KEY_DOWN;
			}
			//				printf("KEY_DOWN\n");
		}
		else if (KEY_ENTER_VAL == 0)
		{
			keyupcnt = 0;
			keydowncnt = 0;
			keyleftcnt = 0;
			keyrightcnt = 0;

			keyentercnt++;
			if (keyentercnt == 3)
			{
				keyentercnt = 3;
				keyvalue[2] = KEY_ENTER;
			}
							//printf("KEY_ENTER\n");
		}
		else if (KEY_LEFT_VAL == 0)
		{
			keydowncnt = 0;
			keyupcnt = 0;
			keyentercnt = 0;
			keyrightcnt = 0;

			keyleftcnt++;
			if (keyleftcnt == 3)
			{
				keyleftcnt = 3;
				keyvalue[3] = KEY_LEFT;
			}
			//				printf("KEY_LEFT\n");
		}
		else if (KEY_RIGHT_VAL == 0)
		{
			keyupcnt = 0;
			keydowncnt = 0;
			keyentercnt = 0;
			keyleftcnt = 0;

			keyrightcnt++;
			if (keyrightcnt == 3)
			{
				keyrightcnt = 3;
				keyvalue[4] = KEY_RIGHT;
			}
			//				printf("KEY_RIGHT\n");
		}
	}
	else if (KEY_UP_VAL == 1 && KEY_DOWN_VAL == 1 && KEY_ENTER_VAL == 1 && KEY_LEFT_VAL == 1 && KEY_RIGHT_VAL == 1)
	{
		key_up = 1;
		keyupcnt = 0;
		keydowncnt = 0;
		keyentercnt = 0;
		keyleftcnt = 0;
		keyrightcnt = 0;
	}
}

void KeyLed_Ctl(u8 state)
{
	switch (state)
	{
	case 0x00: // 停止
		LED_UP = 0;
		LED_DN = 0;
		break;
	case 0x01: // 上升
		LED_UP = 1;
		LED_DN = 0;
		break;
	case 0x02: // 下降
		LED_UP = 0;
		LED_DN = 1;
		break;
	default:
		break;
	}
}
