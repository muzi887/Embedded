#include "key.h"

/* ===========================================================================
 * 面板按键模块实现
 *
 * 分三层：引脚映射表 -> 每个键独立的状态机 -> 模块私有的事件队列。
 * 对外只在 key.h 暴露 KEY_Init / KEY_Tick / KEY_Pop。
 * =========================================================================== */

/* ---------------------------- 可调参数 ---------------------------- */
#define KEY_COUNT          5
#define KEY_DEBOUNCE_TICKS 3      /* 3 x 10ms = 30ms，由 TIM3 提供节拍 */
#define KEY_QUEUE_SIZE     4      /* 环形事件队列深度 */

/* ------------------------- 引脚映射（本文件私有） -------------------------
 * 低电平 = 按下。硬件上每键已有 4.7K 上拉 + 100nF 滤波电容，
 * 所以 GPIO 配浮空输入即可，软件 30ms 去抖是第二道保险。
 * 位序 i = 0..4 必须与 s_key_code[] 一一对应。 */
#define KEY_PIN_UP      PDin(15)  /* K1 UP   */  // 位带操作读 GPIOD->IDR 的第 15 位，结果是 0 或 1
#define KEY_PIN_DOWN    PAin(15)  /* K2 DOWN */
#define KEY_PIN_STOP    PAin(11)  /* K5 STOP */
#define KEY_PIN_SET     PAin(12)  /* K4 SET  */
#define KEY_PIN_CANCEL  PCin(9)   /* K3 CAN  */

#define KEY_IDX_UP     0
#define KEY_IDX_DOWN   1
#define KEY_IDX_STOP   2
#define KEY_IDX_SET    3
#define KEY_IDX_CANCEL 4

/* ------------------------- 内部状态（全部 static） ------------------------- */
static const key_code_t s_key_code[KEY_COUNT] =
{
    KEY_UP, KEY_DOWN, KEY_STOP, KEY_SET, KEY_CANCEL
}; // 	一个占存储的数组对象

typedef enum
{
    KEY_ST_IDLE = 0,      /* 松开 */
    KEY_ST_DEBOUNCE,      /* 正在连续确认 */
    KEY_ST_PRESSED        /* 已确认按下并报过一次，等松开 */
} key_state_t;

typedef struct
{
    uint8_t state;
    uint8_t cnt;          /* 连续检测到按下的拍数，数到 3 就停止增加（饱和），不会溢出。 */
} key_fsm_t;

static key_fsm_t s_fsm[KEY_COUNT];

/* 环形队列：Push / Pop 都在主循环里，无并发访问 */
static key_code_t s_queue[KEY_QUEUE_SIZE];
static uint8_t    s_q_head;
static uint8_t    s_q_tail;
static uint8_t    s_q_count;

// 1. 初始化
void KEY_Init(void) 
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE); // 使能PORTA,C,D时钟

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_15; //  STOP/SET/DOWN
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; // CANCEL
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;			  // UP
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	s_q_head  = 0;
	s_q_tail  = 0;
	s_q_count = 0;
}

/* =================== 2. 硬件读取：5 路引脚 -> 位图 ===================
 * bit = 1 表示「该键此刻被按下」 */
static uint8_t KEY_ReadPins(void)
{
    uint8_t bits = 0;

    if (KEY_PIN_UP     == 0) bits |= (uint8_t)(1u << KEY_IDX_UP);  // 读 K1 UP 这颗键的引脚，若检测到按下（低电平），就把位图 bits 的第 0 位置 1。
    if (KEY_PIN_DOWN   == 0) bits |= (uint8_t)(1u << KEY_IDX_DOWN); // bits |= ...	按位或赋值，只把这一位置 1，不动其它位
    if (KEY_PIN_STOP   == 0) bits |= (uint8_t)(1u << KEY_IDX_STOP); // (uint8_t)	显式把移位结果收成 8 位
    if (KEY_PIN_SET    == 0) bits |= (uint8_t)(1u << KEY_IDX_SET);
    if (KEY_PIN_CANCEL == 0) bits |= (uint8_t)(1u << KEY_IDX_CANCEL);

    return bits;
}

/* ============================ 3. 事件队列 ============================ */
// 队列是「采样」和「处理」之间的缓冲：把一拍里产生的按键事件按顺序存住，等主循环一个个取走；取走即清，同一事件只返回一次。
// 解决了生产与消费节奏不匹配和事件需要「消费确认」的问题。
// 做的修改有：收成 static 私有、加 s_q_head / s_q_tail / s_q_count 变成 FIFO（保持发生顺序）、对外只留 KEY_Pop() 一个出口、取出即减计数。
static void KEY_QueuePush(key_code_t code)
{
	if(s_q_count >= KEY_QUEUE_SIZE)
		return;                 /* 队列满：丢弃新事件，绝不阻塞 */
	
	s_queue[s_q_tail] = code;
	s_q_tail = (uint8_t)((s_q_tail + 1u) % KEY_QUEUE_SIZE);
	s_q_count ++;
}

key_code_t KEY_Pop(void)
{
	key_code_t code;

	if(s_q_count == 0)
		return KEY_NONE;
	
	code = s_queue[s_q_head];
	s_q_head = (uint8_t)((s_q_head + 1u) % KEY_QUEUE_SIZE);
	s_q_count --;
	return code;		/* 取走即清 */
}

/* ============================ 4. 状态机 ============================
 * 每 10ms 由 KEY_Tick() 调用一次。
 *
 *   松开(IDLE) --连续 3 拍按下--> 报一次事件 + 锁住(PRESSED) --松开--> IDLE
 *   消抖期间只要有一拍松开，立刻退回 IDLE 重新数。
 *
 * 关键点：进入 PRESSED 后不再产生事件，计数也饱和不溢出，
 *         所以长按不会自动重复；必须完全松开才能触发下一次。
 * 每个键各自独立；同时按下两键会各报一次，由上层决定如何处理。 */
void KEY_Tick(void)
{
    uint8_t bits;
    uint8_t i;

    bits = KEY_ReadPins();

		for(i = 0; i < KEY_COUNT; i ++)
		{
			uint8_t down;
			key_fsm_t *f;

			down = (uint8_t)((bits >> i) & 1u); // 取出第 i 位，down = 1 表示该键此刻被按下
			f = &s_fsm[i]; // 取出第 i 个键的状态机对象

			switch(f -> state)
			{
			case KEY_ST_IDLE:
				if(down)
				{
					f -> state = KEY_ST_DEBOUNCE;
					f -> cnt = 1;
				}
				break;

			case KEY_ST_DEBOUNCE:
				if (!down)                       /* 没有被按下 */
				{
					f -> state = KEY_ST_IDLE;
					f -> cnt = 0;
					break;
				}
				// 检测到按下，计数 +1，直到 3 拍后确认按下并报事件
				if(f -> cnt < KEY_DEBOUNCE_TICKS)
					f -> cnt ++;
				if(f -> cnt >= KEY_DEBOUNCE_TICKS)
				{
					f -> state = KEY_ST_PRESSED;
					KEY_QueuePush(s_key_code[i]); /* 产生一次按键事件 */
				}
				break;

			case KEY_ST_PRESSED:
				if(!down)                       /* 松开后退回 IDLE */
				{
					f -> state = KEY_ST_IDLE;
					f -> cnt = 0;
				}
				break;
			
			default:
				f -> state = KEY_ST_IDLE;
				f -> cnt = 0;
				break;
			}
		}
}

// void KeyLed_Ctl(u8 state)
// {
// 	switch (state)
// 	{
// 	case 0x00: // 停止
// 		LED_UP = 0;
// 		LED_DN = 0;
// 		break;
// 	case 0x01: // 上升
// 		LED_UP = 1;
// 		LED_DN = 0;
// 		break;
// 	case 0x02: // 下降
// 		LED_UP = 0;
// 		LED_DN = 1;
// 		break;
// 	default:
// 		break;
// 	}
// }
