#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Serial.h"

uint8_t RxData;

int main(void)
{
	Serial_Init();
	
	Serial_SendByte(0x41);
	
	uint8_t  MyArray[] = {0x42, 0x43, 0x44, 0x45};
	Serial_SendArray (MyArray, 4);
	
	Serial_SendString("Hello World!\r\n");
	
	Serial_SendNumber(12345, 5);
	
	printf("Num=%d\r\n", 666);
	
	char String[100];
	sprintf(String, "Num=%d\r\n", 666);
	Serial_SendString(String);
	
	Serial_Printf("Num=%d\r\n", 666);
	// Serial_Printf("你好世界！");  // --no-multibyte-chars
	
	while(1)
	{
		if(Serial_RxFlag == 1)
		{
			RxData = Serial_GetRxData();
			Serial_SendByte(RxData);
		}
	}
}

