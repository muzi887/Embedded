#include "timer.h"

static volatile uint8_t s_tick_10ms;
volatile uint32_t g_sys_ms = 0;

void TIM3_10ms_Init(void)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); 

  /* TIM3 挂 APB1；APB1 预分频不为 1 时定时器时钟 x2 = 72MHz。
    * 72MHz / (7199+1) = 10kHz，(99+1) 个脉冲 = 10ms。 */
  TIM_TimeBaseStructure.TIM_Period = 99;                     // 设置在下一个更新事件装入活动的自动重装载寄存器周期的值	 例如1000为100ms
  TIM_TimeBaseStructure.TIM_Prescaler = 7199;                  // 设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;;                // 设置时钟分割:TDTS = Tck_tim
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
  TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);             // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

  TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE); // 使能指定的TIM3中断,允许更新中断

  /* NVIC 分组由 main() 里的 NVIC_Configuration() 统一设为 Group_2 */ 
  NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;           // TIM3中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 抢占优先级 1 级
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;        // 子优先级 1 级
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // IRQ通道被使能
  NVIC_Init(&NVIC_InitStructure);                           // 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

  s_tick_10ms = 0;
  TIM_Cmd(TIM3, ENABLE); // 使能TIMx外设
}

uint8_t Tick_10ms_Take(void)    /* 有未处理的节拍返回 1 并消费，否则返回 0 */
{
  if(s_tick_10ms == 0)
    return 0;
  s_tick_10ms = 0;   /* 若此刻恰好来一次中断，最多少采样一拍，不影响正确性 */
  return 1;
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        g_sys_ms += 10u;      /* 10 ms per tick -- the base of all GPRS timeouts */
        s_tick_10ms = 1;   /* 中断里只置标志，业务一律留给主循环，consumed by Tick_10ms_Take() */
    }
}