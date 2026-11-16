#include "debug_uart.h"
#include "stm32f10x.h"  

/* ===========================================================================
 * 调试串口实现：USART1 只发不收，轮询发送。
 * 调试口：PA9 = TX，115200 8N1。
 * =========================================================================== */

#define DEBUG_UART_BAUDRATE  115200u

void DEBUG_UART_Init(void)
{
	// GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE); // 使能USART1和GPIOA时钟

	USART_DeInit(USART1); // 复位串口1
	// USART1_TX   PA.9
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; // PA.9
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure);			// 初始化PA9

	// USART1_RX	  PA.10
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);				  // 初始化PA10

	// USART 初始化设置
	USART_InitStructure.USART_BaudRate = DEBUG_UART_BAUDRATE;										// 一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;						// 字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;							// 一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;								// 无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Tx;					// 只发不收模式

	USART_Init(USART1, &USART_InitStructure);	   // 初始化串口
	USART_Cmd(USART1, ENABLE);					   // 使能串口
}

void DEBUG_UART_SendString(const char *str)
{
  const char *p;

  if(str == (const char *)0)
    return;

  p = str;
  while (*p != '\0')
  {
    /* TXE = 1 表示发送数据寄存器已空，可以写下一字节。
      * 读 SR + 写 DR 这个顺序同时会清掉 TC，所以末尾等 TC 才有意义。 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
    }
    USART_SendData(USART1, (uint16_t)(uint8_t)*p);
    p++;
  }
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
		;
}
