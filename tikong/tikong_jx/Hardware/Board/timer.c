#include "timer.h"
#include "stm32f4xx_tim.h"
#include "usart1.h"
#include "Live_data.h"
#include "floor_ctrl.h"
#include "misc.h"
#include "4g.h"
#include "rly.h"
#include "data_handler.h"



u8 led_flag = 0;
u8 updateflag = 0;
u8 gettimeflag = 0;
u16 ctrl_time = 0; // 梯控延时计数，单位 100ms
u16 rly_time1 = 0; // 继电器延时计数，单位 100ms
u16 rly_time2 = 0;
u8 check_limit_time_flag = 0; // 是否检查限位时间标志

/* TIM2 为 32 位定时器：流程对齐 TIM5_Init，避免从模式/ARR 预装载等遗留状态导致不计数或不进中断 */
void TIM2_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  TIM_DeInit(TIM2);

  TIM_Cmd(TIM2, DISABLE);
  TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);

  TIM_TimeBaseInitStructure.TIM_Period = arr;
  TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
  TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;

  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

  TIM_ARRPreloadConfig(TIM2, DISABLE);
  TIM_SetAutoreload(TIM2, (uint32_t)arr);

  TIM_SetCounter(TIM2, 0);
  TIM_ClearFlag(TIM2, TIM_FLAG_Update);
  TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
  NVIC_ClearPendingIRQ(TIM2_IRQn);

  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x01;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x03;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  TIM_Cmd(TIM2, DISABLE);
}

void TIM2_ClearPendingAndEnable(void)
{
  /* 与 TIM5_ClearPendingAndEnable 一致；启动前再次保证 APB 时钟开启 */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

  rly_time2 = 0;

  TIM_Cmd(TIM2, DISABLE);
  TIM_SetCounter(TIM2, 0);
  TIM_ClearFlag(TIM2, TIM_FLAG_Update);
  TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
  NVIC_ClearPendingIRQ(TIM2_IRQn);
  TIM_Cmd(TIM2, ENABLE);
}

void TIM2_Update_ISR(void)
{
  if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
  {
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    rly_time2++;
    /* TIM2 单次溢出约 0.5s → 20 次 ≈ 10s */
    if (rly_time2 >= 2 * rly_2_time)
    {
      Rly_Set2(0);
      TIM_Cmd(TIM2, DISABLE);
      rly_time2 = 0;
    }
  }
}

/* TIM1：APB2 高级定时器；arr/psc 与 TIM2 相同便于对齐，实际 Tout 需结合 APB2 定时器时钟核算 */
void TIM1_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
  TIM_DeInit(TIM1);

  TIM_Cmd(TIM1, DISABLE);
  TIM_ITConfig(TIM1, TIM_IT_Update, DISABLE);

  TIM_TimeBaseInitStructure.TIM_Period = arr;
  TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
  TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;

  TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

  TIM_ARRPreloadConfig(TIM1, DISABLE);
  TIM_SetAutoreload(TIM1, (uint32_t)arr);

  TIM_SetCounter(TIM1, 0);
  TIM_ClearFlag(TIM1, TIM_FLAG_Update);
  TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
  NVIC_ClearPendingIRQ(TIM1_UP_TIM10_IRQn);

  TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_TIM10_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x01;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x03;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  TIM_Cmd(TIM1, DISABLE);
}

void TIM1_ClearPendingAndEnable(void)
{
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

  rly_time1 = 0;

  TIM_Cmd(TIM1, DISABLE);
  TIM_SetCounter(TIM1, 0);
  TIM_ClearFlag(TIM1, TIM_FLAG_Update);
  TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
  NVIC_ClearPendingIRQ(TIM1_UP_TIM10_IRQn);
  TIM_Cmd(TIM1, ENABLE);
}

void TIM1_Update_ISR(void)
{
  if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
  {
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
    rly_time1++;
    if (rly_time1 >= 2 * rly_1_time)
    {
      Rly_Set1(0);
      TIM_Cmd(TIM1, DISABLE);
      rly_time1 = 0;
    }
  }
}

// 通用定时器3中断初始化
// arr：自动重装值
// psc：定时器预分频系数
// 定时器溢出时间计算方法:Tout=((arr+1)*(psc+1))/Ft us.
// Ft=定时器时钟频率,单位:Mhz
// 这里使用的是定时器3!
void TIM3_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); /// 使能TIM3时钟

  TIM_TimeBaseInitStructure.TIM_Period = arr;                     // 自动重装值
  TIM_TimeBaseInitStructure.TIM_Prescaler = psc;                  // 定时器分频
  TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数模式
  TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;

  TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure); // 初始化TIM3

  TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE); // 使能定时器3更新中断
  TIM_Cmd(TIM3, ENABLE);                     // 使能定时器3

  NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;              // 定时器3中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x01; // 抢占优先级1
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x03;        // 响应优先级3
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

// 定时器3中断服务函数 TODO
void TIM3_IRQHandler(void)
{
  static u16 m_cnt = 0;
  static u8 h_cnt = 0;
  if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) // 检查中断
  {
    m_cnt++;
    if (g_gprs_linka_level && updateflag == 0) // 
    {
      updateflag = 1;
      gettimeflag=1;
      m_cnt = 0;
    }

    if(m_cnt % 120 == 0) // 每 60s
    {
      check_limit_time_flag = 1;
    }

    if (m_cnt >= 7200) // 1小时 7200
    {
      h_cnt++;
      m_cnt = 0;
    }

    if (h_cnt >= 24) // 24小时
    {
      h_cnt = 0;
      updateflag = 0;
    }
  }
  TIM_ClearITPendingBit(TIM3, TIM_IT_Update); // 清除中断标志位
}

//----蜂鸣器-------------------------
static u8 BeepNum = 0;

void Bsp_SetBeep(u8 count)
{
  BeepNum = count;
}

u8 Bsp_GetBeep(void)
{
  return BeepNum;
}

/* TIM4 节拍约 10ms（board_init: arr=99, psc=8399）；改分频时请同步 BEEP_TICK_MS */
#define BEEP_TICK_MS 10u
#define BEEP_SHORT_TICKS (300u / BEEP_TICK_MS) /* 300ms */
#define BEEP_LONG_TICKS (600u / BEEP_TICK_MS)  /* 600ms */
#define BEEP_ALARM_GAP_TICKS 15u /* 位与位之间静音约 150ms */

static u8 alarm_run = 0;
static u8 alarm_pat;
static s8 alarm_idx; /* 当前位 3..0 */
static u16 alarm_phase_cnt;

void TIM4_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE); /// 使能TIM4时钟

  TIM_TimeBaseInitStructure.TIM_Period = arr;                     // 自动重装值
  TIM_TimeBaseInitStructure.TIM_Prescaler = psc;                  // 定时器分频
  TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数模式
  TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;

  TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure); // 初始化TIM4

  TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE); // 使能定时器4更新中断
  TIM_Cmd(TIM4, ENABLE);                     // 使能定时器4

  NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;              // 定时器4中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x01; // 抢占优先级1
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x02;        // 响应优先级2
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}
/*
1
2
3
4
5
6
7
8   0X1000    密钥匹配失败！
9   0X1001
10  0X1010
*/

void beepAlarm(int num)
{
  if (num < 1 || num > 16)
    return;
  alarm_pat = (u8)(num & 0x0F);
  alarm_idx = 3;
  alarm_phase_cnt = 0;
  alarm_run = 1;
}

volatile uint32_t g_tim4_ms_tick = 0;

// 定时器4中断服务函数
void TIM4_IRQHandler(void)
{
  static u16 cnt = 0,ledcnt = 0;

  if (TIM_GetITStatus(TIM4, TIM_IT_Update) == SET) // 检查中断
  {
    g_tim4_ms_tick += 10u;
    if (alarm_run)
    {
      u8 bitv = (u8)((alarm_pat >> alarm_idx) & 1u);
      u16 on_ticks = bitv ? BEEP_LONG_TICKS : BEEP_SHORT_TICKS;
      u16 period_ticks = (u16)(on_ticks + BEEP_ALARM_GAP_TICKS);

      alarm_phase_cnt++;
      if (alarm_phase_cnt <= on_ticks)
        beepout = 1;
      else if (alarm_phase_cnt <= period_ticks)
        beepout = 0;
      else
      {
        alarm_phase_cnt = 0;
        alarm_idx--;
        if (alarm_idx < 0)
        {
          alarm_run = 0;
          beepout = 0;
        }
      }
    }
    else if (BeepNum)
    {
      cnt++;
      if (cnt <= 30) // 300ms
      {
        beepout = 1;
      }
      else
      {
        beepout = 0;
        if (cnt >= 50) // 1.5s间隔
        {
          cnt = 0;
          if (BeepNum > 0)
            BeepNum--;
        }
      }
    }
    else
    {
      cnt = 0;
      beepout = 0;
    }
		
		ledcnt++;
		if(ledcnt>=100)
		{
			led_flag = 1;
			ledcnt = 0;
		}
  }
  TIM_ClearITPendingBit(TIM4, TIM_IT_Update); // 清除中断标志位
}

// 初始化
void Beep_Init()
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE); // 使能GPIOD时钟

  // GPIOD11初始化配置
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;      // 普通输出模式
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;     // 推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz; // 100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;   // 无上下拉
  GPIO_Init(GPIOE, &GPIO_InitStructure);             // 初始化
	
  beepout = 0;
}

//----电梯权限触发-------------------------
// u8 Elevator_AuthCheck_Flag = 0; // 电梯权限检查标志

// 定时器5初始化
void TIM5_Init(void)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  /* 1. 使能 TIM5 时钟 */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);

  TIM_Cmd(TIM5, DISABLE);
  TIM_ITConfig(TIM5, TIM_IT_Update, DISABLE);

  /* 2. 定时器时基配置 */
  TIM_TimeBaseInitStructure.TIM_Prescaler = 84 - 1; /* APB1 定时器时钟经倍频后约 84MHz，84 分频 -> 1MHz 计数 */
  TIM_TimeBaseInitStructure.TIM_Period = 5000 - 1;  /* 5000 计数 -> 5ms 周期 */
  TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM5, &TIM_TimeBaseInitStructure);

  /* TimeBaseInit 可能置位 UIF：先关计数再清标志与 NVIC 悬挂 */
  TIM_SetCounter(TIM5, 0);
  TIM_ClearFlag(TIM5, TIM_FLAG_Update);
  TIM_ClearITPendingBit(TIM5, TIM_IT_Update);
  NVIC_ClearPendingIRQ(TIM5_IRQn);

  TIM_ITConfig(TIM5, TIM_IT_Update, ENABLE);

  /* 3. NVIC 配置 */
  NVIC_InitStructure.NVIC_IRQChannel = TIM5_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  TIM_Cmd(TIM5, DISABLE);
}

void TIM5_ClearPendingAndEnable(void)
{
  TIM_Cmd(TIM5, DISABLE);
  TIM_SetCounter(TIM5, 0);
  TIM_ClearFlag(TIM5, TIM_FLAG_Update);
  TIM_ClearITPendingBit(TIM5, TIM_IT_Update);
  NVIC_ClearPendingIRQ(TIM5_IRQn);
  TIM_Cmd(TIM5, ENABLE);
}

// 定时器5中断服务函数
void TIM5_IRQHandler(void)
{
  u8 limit5[5];
  u8 elevator_tail5_mixed_negation[5];
  if (TIM_GetITStatus(TIM5, TIM_IT_Update) != RESET)
  {
    TIM_ClearITPendingBit(TIM5, TIM_IT_Update);
    ctrl_time++;
    /* ===== 梯控延时控制 ===== */
    if (ctrl_time >= 1000) /* 到时：对 mixed 位图按字节取反后，在取反为 1 的位上关楼层 */
    {
      int bi;
      int bj;
      int bit_no;
      int ni;

      FloorCtrl_GetLimit(limit5);
      for (ni = 0; ni < 5; ni++)
        elevator_tail5_mixed_negation[ni] = (u8)(~limit5[ni]);

      bit_no = 0;
      for (bi = 4; bi >= 0; bi--)
      {
        for (bj = 0; bj < 8; bj++)
        {
          if (elevator_tail5_mixed_negation[bi] & (1u << (unsigned)bj))
            Floor_Off((uint8_t)(bit_no + 1));
          bit_no++;
        }
      }
      TIM_Cmd(TIM5, DISABLE);
      ctrl_time = 0;
    }
  }
}
