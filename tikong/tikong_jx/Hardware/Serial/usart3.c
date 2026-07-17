#include "usart3.h"
#include "usart1.h"

__align(4) u8 USART3_RX_BUF[USART3_REC_LEN];
__align(4) u8 USART3_RX_BUF2[USART3_REC_LEN];
u16 USART3_RX_CNT = 0;
u8 USART3_RX_Complete = 0;

// DMA接收初始化
static void USART3_DMA_RX_Init(void)
{
  DMA_InitTypeDef DMA_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

  DMA_DeInit(DMA1_Stream1); // USART3_RX使用DMA1 Stream1
  DMA_InitStructure.DMA_Channel = DMA_Channel_4;
  DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
  DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)USART3_RX_BUF;
  DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
  DMA_InitStructure.DMA_BufferSize = USART3_REC_LEN;
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;  // 普通模式
  DMA_InitStructure.DMA_Priority = DMA_Priority_High;
  DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
  DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
  DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
  DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
  DMA_Init(DMA1_Stream1, &DMA_InitStructure);

  // NVIC配置
  NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  // 使能DMA
  USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
  DMA_Cmd(DMA1_Stream1, ENABLE);
}


// USART3初始化
void uart3_init(u32 bound)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

  // 引脚复用
  GPIO_PinAFConfig(GPIOD, GPIO_PinSource8, GPIO_AF_USART3);
  GPIO_PinAFConfig(GPIOD, GPIO_PinSource9, GPIO_AF_USART3);

  // GPIO配置
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

  // USART3配置
  USART_InitStructure.USART_BaudRate = bound;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(USART3, &USART_InitStructure);

  // 使能空闲中断（检测帧结束）
  USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

  USART_Cmd(USART3, ENABLE);

  // 初始化DMA接收
  USART3_DMA_RX_Init();

  // 配置USART3中断
  NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

//void USART3_DMA_ReInit(void)
//{
//    /* 1. 关闭DMA通道 */
//    DMA_Cmd(DMA1_Stream1, DISABLE);
//
//    /* 2. 清除中断标志 (注意Stream1的标志位) */
//    DMA1->LIFCR = DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 | DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1;
//
//    /* 3. 配置DMA参数 */
//    DMA1_Stream1->NDTR = USART3_REC_LEN;                  // 设置数据长度
//    DMA1_Stream1->M0AR = (uint32_t)USART3_RX_BUF;         // 重新设置内存地址
//    DMA1_Stream1->CR |= DMA_SxCR_EN;                     // 重新使能通道
//
//}


/* 关键：确保 DMA 重新初始化 */
void USART3_DMA_ReInit(void)
{
  /* 兼容旧版本编译器 ARMCC */
  FunctionalState st;

  /* 1) 关闭 DMA Stream */
  DMA_Cmd(DMA1_Stream1, DISABLE);

  /* 2) 等待硬件真正将 EN 位清零，否则修改 NDTR/M0AR 可能无效 */
  do
  {
    st = DMA_GetCmdStatus(DMA1_Stream1);  /* ENABLE / DISABLE */
  }
  while (st != DISABLE);

  /* 3) 清除 Stream 所有中断标志（Stream1 属于 L 组） */
  DMA_ClearFlag(DMA1_Stream1,
                DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 |
                DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1);

  /* 4) 重新装载数据长度和内存地址（Normal 模式需要重新装载，Circular 可不装载） */
  DMA1_Stream1->NDTR = USART3_REC_LEN;
  DMA1_Stream1->M0AR = (uint32_t)USART3_RX_BUF;  /* 确保先写一次 */

  /* 5) 使能 DMA */
  DMA_Cmd(DMA1_Stream1, ENABLE);
}
void USART3_IRQHandler(void)
{
  uint32_t sr;
  uint32_t dr;
  uint16_t ndtr;
  uint16_t recv_len;

  if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
  {
    /* 1) 必须先读 SR，再读 DR 清除 IDLE 标志 */
    sr = USART3->SR;
    dr = USART3->DR;
    (void)sr;
    (void)dr;

    /* 2) 立即"冻结"DMA，防止 NDTR 在计算期间继续变化 */
    DMA_Cmd(DMA1_Stream1, DISABLE);
    while (DMA_GetCmdStatus(DMA1_Stream1) != DISABLE)
    {
      /* wait EN bit cleared */
    }

    /* 3) 先读 NDTR 计算本次接收的长度 注意 一定要在重新装载 NDTR 之前 */
    ndtr = DMA_GetCurrDataCounter(DMA1_Stream1);       /* 剩余 */
    recv_len = (uint16_t)(USART3_REC_LEN - ndtr);      /* 实际 */

    /* 4) 如果长度为 0，说明中断时/粘包边界导致没有接收到数据，直接复位 DMA 后返回 */
    if (recv_len == 0 || recv_len > USART3_REC_LEN)
    {
      /* 清除 Stream 所有中断标志（L 组和 H 组按对应 Stream 的） */
      DMA_ClearFlag(DMA1_Stream1,
                    DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 |
                    DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1);

      /* 装载 NDTR/M0AR 重新初始化（Normal 模式下需要） */
      DMA1_Stream1->NDTR = USART3_REC_LEN;
      DMA1_Stream1->M0AR = (uint32_t)USART3_RX_BUF;
      DMA_Cmd(DMA1_Stream1, ENABLE);

      /* 可选：记录一次"接收长度"，便于观察边界 */
      //printf("[IDLE] skip: len=0 (ndtr=%u)\r\n", (unsigned)ndtr);
      return;
    }

    /* 5) 将 DMA 接收到的前 recv_len 字节拷贝到工作缓冲区，添加 '\0' 便于调试打印 */
    memcpy(USART3_RX_BUF2, USART3_RX_BUF, recv_len);
    USART3_RX_BUF2[recv_len] = '\0';

    /* 6) 设置主循环标志位/长度，主循环去 parse */
    USART3_RX_CNT = recv_len;
    USART3_RX_Complete = 1;
    //printf("[IDLE] len=%u NDTR=%u\r\n", (unsigned)recv_len, (unsigned)ndtr);

    /* 7) 清除 Stream 所有中断标志，重新装载并启动 DMA，准备接收下一帧 */
    DMA_ClearFlag(DMA1_Stream1,
                  DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 |
                  DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1);
    DMA1_Stream1->NDTR = USART3_REC_LEN;
    DMA1_Stream1->M0AR = (uint32_t)USART3_RX_BUF;
    DMA_Cmd(DMA1_Stream1, ENABLE);
  }
}

/* USART3 中断服务函数（正确处理中断 + 计算长度 + 重新 DMA） */
//void USART3_IRQHandler(void)
//{
//    /* 兼容旧版本 */
//    uint32_t tmp;
//    uint16_t cnt;

//    if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
//    {
//
//
//			printf("[IDLE] len=%u NDTR=%u\r\n", (unsigned)USART3_RX_CNT, (unsigned)DMA_GetCurrDataCounter(DMA1_Stream1));

//
//        /* 清除 IDLE 唯一正确方法：先读 SR，再读 DR 清除 */
//        tmp = USART3->SR;
//        tmp = USART3->DR;
//        (void)tmp;          /* 防止编译器优化 */

//        /* 计算本次接收到的字节数（DMA 还没重新装载前的 NDTR） */
//        cnt = (uint16_t)(USART3_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream1));
//        USART3_RX_CNT = cnt;
//        USART3_RX_Complete = 1;

//        /* 重新配置 DMA（Normal 模式下必须） */
//        USART3_DMA_ReInit();
//    }
//}


//// USART3中断服务函数（处理空闲中断）
//void USART3_IRQHandler(void)
//{
//    if(USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
//    {
//        USART_ReceiveData(USART3); // 清除空闲中断标志
//
//        // 计算接收长度
//        USART3_RX_CNT = USART3_REC_LEN - DMA_GetCurrDataCounter(DMA1_Stream1);
//        USART3_RX_Complete = 1;
//
//        // 重新配置DMA
//        USART3_DMA_ReInit();
//    }
//}

/**
  * @brief  通过USART3发送数据，阻塞式
  * @param  data: 要发送的数据指针
  * @param  length: 数据长度
  * @retval None
  */
void USART3_SendData(uint8_t *data, uint16_t length)
{
  uint16_t i;
  for(i = 0; i < length; i++)
  {
    // 等待发送寄存器空
    while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);

    // 发送数据
    USART_SendData(USART3, data[i]);
  }

 //printf("------------------------\r\n");
  // 等待发送完成（可选）
  while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
}
