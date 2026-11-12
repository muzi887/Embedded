#include "timer.h"
#include "switch.h"

u8 flag10ms, flag20ms, flag300ms, flag500ms, flag1s, flag1min;
u8 uploadflag = 0;
volatile u32 sys_ms = 0; /* TIM2 每 5ms += 5 */

// TIM2中断 10ms
void TIM2_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 时钟使能

  TIM_TimeBaseStructure.TIM_Period = arr;                     // 设置在下一个更新事件装入活动的自动重装载寄存器周期的值	 例如1000为100ms
  TIM_TimeBaseStructure.TIM_Prescaler = psc;                  // 设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;                // 设置时钟分割:TDTS = Tck_tim
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);             // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE); // 使能指定的TIM3中断,允许更新中断

  NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;           // TIM2中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级0级
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;        // 子优先级3级
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // IRQ通道被使能
  NVIC_Init(&NVIC_InitStructure);                           // 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

  TIM_Cmd(TIM2, ENABLE); // 使能TIMx外设
}

//// DIANCHI电池的发送器,
void TIM3_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); // 时钟使能

  TIM_TimeBaseStructure.TIM_Period = arr;                     // 设置在下一个更新事件装入活动的自动重装载寄存器周期的值	 例如1000为100ms
  TIM_TimeBaseStructure.TIM_Prescaler = psc;                  // 设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;                // 设置时钟分割:TDTS = Tck_tim
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
  TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);             // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

  TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE); // 使能指定的TIM3中断,允许更新中断

  NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;           // TIM3中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级0级
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;        // 子优先级3级
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // IRQ通道被使能
  NVIC_Init(&NVIC_InitStructure);                           // 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

  TIM_Cmd(TIM3, ENABLE); // 使能TIMx外设
}
// pwm 加减速控制，中断处理在DC_MOTOR
void TIM5_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE); // 时钟使能

  TIM_TimeBaseStructure.TIM_Period = arr;                     // 设置在下一个更新事件装入活动的自动重装载寄存器周期的值	 例如1000为100ms
  TIM_TimeBaseStructure.TIM_Prescaler = psc;                  // 设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;                // 设置时钟分割:TDTS = Tck_tim
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
  TIM_TimeBaseInit(TIM5, &TIM_TimeBaseStructure);             // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

  TIM_ITConfig(TIM5, TIM_IT_Update, ENABLE); // 使能指定的TIM3中断,允许更新中断

  NVIC_InitStructure.NVIC_IRQChannel = TIM5_IRQn;           // TIM3中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;        // 子优先级
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // IRQ通道被使能
  NVIC_Init(&NVIC_InitStructure);                           // 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

  TIM_Cmd(TIM5, DISABLE);

  // TIM_Cmd(TIM5, ENABLE);  //使能TIMx外设
}
// 4g电池的发送器,
void TIM6_Int_Init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE); // 时钟使能

  TIM_TimeBaseStructure.TIM_Period = arr;                     // 设置在下一个更新事件装入活动的自动重装载寄存器周期的值	 例如1000为100ms
  TIM_TimeBaseStructure.TIM_Prescaler = psc;                  // 设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;                // 设置时钟分割:TDTS = Tck_tim
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
  TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);             // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

  TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE); // 使能指定的TIM3中断,允许更新中断

  NVIC_InitStructure.NVIC_IRQChannel = TIM6_IRQn;           // TIM2中断
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级0级
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;        // 子优先级3级
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // IRQ通道被使能
  NVIC_Init(&NVIC_InitStructure);                           // 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

  TIM_Cmd(TIM6, ENABLE); // 使能TIMx外设
}
// PWM初始化
void TIM1_PWM_Init(u16 arr, u16 psc)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_OCInitTypeDef TIM_OCInitStructure;
  TIM_BDTRInitTypeDef TIM_BDTRStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE); // 使能定时器1时钟
  // 使能GPIO外设和AFIO复用功能模块时钟
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO, ENABLE);

  // GPIO_FullRemap_TIM1 完全重映射
  GPIO_PinRemapConfig(GPIO_FullRemap_TIM1, ENABLE);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;       // TIM_CH1
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOE, &GPIO_InitStructure); // 初始化GPIO

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;      // TIM_CH2
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOE, &GPIO_InitStructure); // 初始化GPIO

  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13; // SD_CTL 继电器控制
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;         // 推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOE, &GPIO_InitStructure); // 初始化GPIO

  GPIO_ResetBits(GPIOE, GPIO_Pin_12 | GPIO_Pin_13);

  // 初始化TIM1
  TIM_TimeBaseStructure.TIM_Period = arr;                     // 设置在下一个更新事件装入活动的自动重装载寄存器周期的值
  TIM_TimeBaseStructure.TIM_Prescaler = psc;                  // 设置用来作为TIMx时钟频率除数的预分频值
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;                // 设置时钟分割:TDTS = Tck_tim
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
  TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);             // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

  // 刹车和死区结构体
  TIM_BDTRStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Disable; // 禁止AOE位，禁止刹车自动输出功能
  TIM_BDTRStructure.TIM_BreakPolarity = TIM_BreakPolarity_Low;         // 刹车输入引脚极性低
  TIM_BDTRStructure.TIM_Break = TIM_Break_Disable;                     // 失能刹车输入
  TIM_BDTRStructure.TIM_DeadTime = 0;                                  // 在开启和关闭状态之间的死区时间 84-1us  168-2us
  TIM_BDTRStructure.TIM_LOCKLevel = TIM_LOCKLevel_OFF;                 // 锁定级别设置: 不锁定任何位
  TIM_BDTRStructure.TIM_OSSIState = TIM_OSSIState_Disable;             // 运行模式下的非工作状态选择
  TIM_BDTRStructure.TIM_OSSRState = TIM_OSSRState_Enable;              // 运行模式下的关闭状态选择
  TIM_BDTRConfig(TIM1, &TIM_BDTRStructure);

  // 初始化TIM1 Channel PWM模式
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;                // 选择定时器模式:TIM脉冲宽度调制模式2
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;    // 比较输出使能
  TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Disable; // 比较互补输出使能
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;         // 输出极性:TIM输出比较极性低
  TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCPolarity_Low;
  TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
  TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;

  TIM_OC1Init(TIM1, &TIM_OCInitStructure);          // 根据T指定的参数初始化外设TIM1 OC1
  TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable); // 使能TIM1在CCR1上的预装载寄存器

  TIM_OC2Init(TIM1, &TIM_OCInitStructure);          // 根据T指定的参数初始化外设TIM1 OC2
  TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable); // 使能TIM1在CCR1上的预装载寄存器

  TIM_Cmd(TIM1, ENABLE); // 使能TIM1
  TIM_SetCompare1(TIM1, 0);
  TIM_SetCompare2(TIM1, 0);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

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
volatile uint32_t g_tim4_ms_tick = 0;
// 定时器4中断服务函数
void TIM4_IRQHandler(void)
{
  static u16 cnt = 0,ledcnt = 0;

  if (TIM_GetITStatus(TIM4, TIM_IT_Update) == SET) // 检查中断
  {
    g_tim4_ms_tick += 10u;
  }
  TIM_ClearITPendingBit(TIM4, TIM_IT_Update); // 清除中断标志位
}

// 定时器
void TIM2_IRQHandler(void) // TIM2中断 5ms
{
  static u16 cnt = 5000;
  static u16 cnt_1 = 0, cnt_2 = 0;
  if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) // 检查指定的TIM中断发生与否:TIM 中断源
  {
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update); // 清除TIMx的中断待处理位:TIM 中断源

    sys_ms += 5; /* 5ms  tick */
    cnt++;

    JudeCount(); // 转数
    JudeDir();   // 限位+行程判断

    if ((cnt % 2) == 0)
    {
      flag10ms = 1;
      JudeLocked(); // 卡转
    }

    if ((cnt % 4) == 0)
      flag20ms = 1;

    if ((cnt % 60) == 0)
      flag300ms = 0x03;

    if ((cnt % 100) == 0)
    {
      flag500ms = 1;
      JudeKaidu();
    }

    if ((cnt % 200) == 0)
      flag1s = 1;

    /*	if((cnt % 100) == 0)
      {
          cnt_2++;
          if((cnt_2 >= armReg.reportT)&&(armReg.reportT))
          {
              cnt_2 = 0;
              uploadflag=1;
          }
      }*/

    if (cnt >= 12000)
    {
      flag1min = 1;
      cnt = 0;
    }
  }
}
