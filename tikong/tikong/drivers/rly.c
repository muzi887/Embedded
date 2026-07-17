#include "rly.h"

void Rly_Init()
{
  GPIO_InitTypeDef GPIO_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE); // 使能GPIOD时钟

  // GPIOD11初始化配置
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;      // 普通输出模式
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;     // 推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz; // 100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;   // 无上下拉
  GPIO_Init(GPIOE, &GPIO_InitStructure);             // 初始化

  rly1out = 0; // 1:继电器吸合   0：继电器断开
  rly2out = 0;
}

void Rly_Set(u8 rly1, u8 rly2)
{
  rly1out = rly1;
  rly2out = rly2;
}

void rly_AllOff(void)
{
  rly1out = 0;
  rly2out = 0;
}

void Rly_Set1(u8 rly1)
{
  rly1out = rly1;
}

void Rly_Set2(u8 rly2)
{
  rly2out = rly2;
}
