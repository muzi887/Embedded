#include "timer.h"
#include "usart.h"
#include "Live_data.h"
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

// 定时器3中断服务函数
void TIM3_IRQHandler(void)
{
  static u16 m_cnt = 0;
  static u8 h_cnt = 0, updateflag = 0;
  if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) // 检查中断
  {
    m_cnt++;
    if (m_cnt == 40 && updateflag == 0) // 上电20s后获取网络时间
    {
      updateflag = 1;
      GetNetTime();
    }

    if (m_cnt >= 7200) // 1小时
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
u8 BeepNum = 0;

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

// 定时器4中断服务函数
void TIM4_IRQHandler(void)
{
  static u16 cnt = 0;

  if (TIM_GetITStatus(TIM4, TIM_IT_Update) == SET) // 检查中断
  {
    if (BeepNum)
    {
      cnt++;
      if (cnt <= 30) // 300ms
      {
        beepout = 1;
      }
      else
      {
        beepout = 0;
        if (cnt >= 150) // 1.5s间隔
        {
          cnt = 0;
          if (BeepNum > 0)
            BeepNum--;
        }
      }
    }
    else
    {
      beepout = 0;
    }
  }
  TIM_ClearITPendingBit(TIM4, TIM_IT_Update); // 清除中断标志位
}

// 初始化
void Beep_Init()
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE); // 使能GPIOD时钟

  // GPIOD11初始化配置
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;      // 普通输出模式
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;     // 推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz; // 100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;   // 无上下拉
  GPIO_Init(GPIOD, &GPIO_InitStructure);             // 初始化
  beepout = 0;
}
