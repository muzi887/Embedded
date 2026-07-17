#include "stdio.h"
#include <string.h> 
#include "delay.h"
#include "ds1302.h"

const uint8_t read[]={0x81,0x83,0x85,0x87,0x89,0x8b,0x8d};//读秒、分、时、日、月、周、年的寄存器地址
const uint8_t write[]={0x80,0x82,0x84,0x86,0x88,0x8a,0x8c};//写秒、分、时、日、月、周、年的寄存器地址
 

/*ds1302dat配置为开漏模式，此模式下能够实现真正的双向IO口*/

//初始化1302
void ds1302_init()
{
	GPIO_InitTypeDef GPIO_InitStruct;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin   = ds1302clk|ds1302rst;
	GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;//推挽输出
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;//上拉
	GPIO_Init(ds1302gpio, &GPIO_InitStruct);

	GPIO_InitStruct.GPIO_OType = GPIO_OType_OD;//开漏输出
	GPIO_InitStruct.GPIO_Pin   = ds1302dat;
	GPIO_Init(ds1302gpio, &GPIO_InitStruct);
	
	CE_L;
	SCLK_L;
}

//设置DS1302时钟
u8 ds1302_settime(uint8_t *dat)
{
	uint8_t i=0;
	 
	write_1302(0x8e,0x00);//去除写保护
	for(i=0;i<7;i++)//进行对时
	{
		write_1302(write[i],dat[i]);
	}
  write_1302(0x8e,0x80);//加写保护
	
	return 0;
}

//ds1302时钟读取
void ds1302_gettime(uint8_t *ttt)
{
  u8 i=0;
	char x;
	char y;
	
  for(i=0;i<7;i++)
	{
	  ttt[i]=read_1302(read[i]);
		
		x = ttt[i] % 16;      //转成十进制编码
		y = ttt[i] /16 * 10;
		ttt[i] = x + y;
	}

 /*	printf("sec = %d %x\n",ttt[0],ttt[0]);
 	printf("min = %d %x\n",ttt[1],ttt[1]);
 	printf("hour = %d %x\n",ttt[2],ttt[2]);
 	printf("day = %d %x\n",ttt[3],ttt[3]);
 	printf("mon = %d %x\n",ttt[4],ttt[4]);
 	printf("week = %d %x\n",ttt[5],ttt[5]);
 	printf("year = %d %x\n",ttt[6],ttt[6]);	*/
}

//写一个字节的数据sck上升沿写数据
void write_1302byte(uint8_t dat)
{
 	uint8_t i=0;
	
	SCLK_L;
 
 	for(i=0;i<8;i++)
	{
		SCLK_L;
			
		if(dat&0x01)
			DAT_H;
		else
			DAT_L;
		
		delay_us(2);
		
		SCLK_H;
    delay_us(2);
		dat>>=1;
	}	
	
}

//读一个字节数据
uint8_t read_1302(uint8_t add)
{
	uint8_t i=0,dat1=0x00;
	
	CE_L;
	SCLK_L;
	
	delay_us(1); 
  CE_H;
	 
	write_1302byte(add);//先写寄存器的地址

	for(i=0;i<8;i++)
	{
		SCLK_H; 
	
		dat1>>=1;
		delay_us(2);
		SCLK_L;
	  
		if(DAT_IN() == 1)  //数据线此时为高电平
		{
			dat1=dat1|0x80;
		}
		delay_us(2);
	}
	
	CE_L; 

	return dat1;
}

//向指定寄存器写入一个字节的数据
void write_1302(uint8_t add,uint8_t dat)
{
	CE_L;    
	SCLK_L; 
	
	CE_H;     
	
	delay_us(1); 
	write_1302byte(add);
	write_1302byte(dat);
	
	SCLK_L; 
	CE_L; 
}
