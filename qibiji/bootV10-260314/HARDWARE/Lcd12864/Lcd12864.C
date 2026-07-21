#include "Lcd12864.h"
#include "delay.h"

unsigned char p1[24]= {
	0x00,0x00,0x00,0x00,0x06,0x00,0x0F,0x00,
	0x10,0x80,0x16,0x80,0x16,0x80,0x16,0x80,
	0x16,0x80,0x16,0x80,0x10,0x80,0x0F,0x00
};

unsigned char p2[32]= {
	0x00,0x00,0x00,0x00,0x00,0x0E,0x00,0x0E,
	0x00,0xCE,0x00,0xCE,0x0C,0xCE,0x0C,0xCE,
	0xCC,0xCE,0xCC,0xCE,0xCC,0xCE,0xCC,0xCE,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};


// 写命令
void WRCommandH(uint8_t CMD)
{
    CS=0;
    CS=1;
    SPIWH(CMD,0);
    delay_us(100);  //延时90us 89S52单片机串口通信,延时,参考89S52延时
		CS=0;
}

// 写数据
void WRDataH(uint8_t Data)
{
		CS=0;
    CS=1;
    SPIWH(Data,1);
		delay_us(20);
		CS=0;
}

void SPIWH(uint8_t Wdata,uint8_t WRS)
{
    SendByteLCDH(0xf8+(WRS<<1));//命令字节选择WRS
    SendByteLCDH(Wdata&0xf0);
    SendByteLCDH((Wdata<<4)&0xf0);
}


void SendByteLCDH(uint8_t WLCDData)
{
    uint8_t i;
    for(i=0; i<8; i++)
    {
			  SCLK=0;
        if((WLCDData<<i)&0x80)
					SID=1;
        else 
					SID=0;
       
        SCLK=1;
    }
}

//-----------------------------------------------------------
//addr为汉字显示位置,*hanzi为指针;count为要输入汉字的个数
void ShowQQCharH(uint8_t addr,uint8_t *hanzi,uint8_t count)
{
    uint8_t i;
		i=0;
    for(i=0; i<count;i++)
    {
				WRCommandH(addr);	//设置DDRAM地址
        WRDataH(hanzi[i*2]);
        WRDataH(hanzi[i*2+1]);
				addr++;	  
   }
}

//addr为起始地址和字符,i为字符库中字符编号,count为要显示字符的个数
void ShowNUMCharH(uint8_t addr,uint8_t i,uint8_t count)
{
    uint8_t j;
	  j = 0;
    while(j<count)
    {
        WRCommandH(addr);	//设置DDRAM地址
        WRDataH(i+j);//因为字符16*8位字符拼成一个16*16汉字显示
        j++;
        WRDataH(i+j);
        addr++;
        j++;
    }
}

//自定义字符写入CGRAM
//data1是高字节,data2是低字节,一个汉字需要两个字节,一个字符需要两个字节,一个汉字共32字节,共16行
//一个自定义字符为16*16点阵
//第一个自定义字符字节为从40H开始,到4F结束为第一个自定义汉字, 之后自定义字符从8000H开始第一个
//addr为起始首地址
void WRCGRAMH(uint8_t data1,uint8_t data2,uint8_t addr)
{
    uint8_t i;
    for(i=0; i<16;i++)
    {
        WRCommandH(addr+i);  	//设置CGRAM地址
        WRDataH(data1);
        WRDataH(data1);
        i++;
        WRCommandH(addr+i);  	//设置CGRAM地址
        WRDataH(data2);
        WRDataH(data2);   
    }
}
//自定义字符写入CGRAM
//显示上半部分自定义字符,下半部分字符地址全部16*16
//addr为显示地址,自定义字符的第一个字符的值从,从8000H开始为第一个,
//8100H为第二个,8200H为第三个,8300H为第四个,这个芯片只能自定义四个字符
//i为自定义字符的字符地址,高字节地址,低字节地址
//IC限制,这个芯片限制,一个IC芯片只能显示16*2个汉字
void ShowCGCharH(uint8_t addr,uint8_t i)
{
    uint8_t j;
    for(j=0; j<0x20;)
    {
        WRCommandH(addr+j);	//设置DDRAM地址
        WRDataH(0x00);//字符库字符低字节
        WRDataH(i);//字符库字符高字节
        j++;
    }
}

void WRGDRAM128X8(uint8_t x1,uint8_t y1,uint8_t d1 )
{
    uint8_t j,i;
    WRCommandH(0x34);			//关闭扩展指令集寄存器
    WRCommandH(0x36);			//打开绘图和文字显示
    for(j=0; j<16; j++)				//
    {
        WRCommandH(0x80+y1+j);  //Y坐标设置,设置行地址
        WRCommandH(0x80+x1);	//X坐标，设置列地址在中间字节开始写入,80H为第一个字节
        for(i=0; i<8; i++)	//写入一行
        {
            WRDataH(d1);
            WRDataH(d1);
        }
    }
}

//上半部分清图下半部分
void CLEARGDRAMH(uint8_t c)
{
    uint8_t j;
    uint8_t i;
    WRCommandH(0x34);
    WRCommandH(0x36);
    for(j=0; j<32; j++)
    {
        WRCommandH(0x80+j);
        WRCommandH(0x80);//X坐标
        for(i=0; i<16; i++) //
        {
            WRDataH(c);
            WRDataH(c);
        }
    }

}

//画图1
void WRGDRAM1(uint8_t x,uint8_t l,uint8_t r)
{
		uint8_t i;
		WRCommandH(0x34);			//ȥ��չָ��Ĵ���
		WRCommandH(0x36);			//�򿪻�ͼ����

		WRCommandH(0x80+15);  //Y坐标设置,设置行地址
		WRCommandH(0x80);	  //X坐标，设置列地址在中间字节开始写入,80H为第一个字节
		for(i=0; i<8; i++)	  //写入一行
		{
			WRDataH(x);
			WRDataH(x);
		}
}

void WRGDRAM(u8 x, u8 *buf)
{
		uint8_t i=0;
    WRCommandH(0x34);			//关闭扩展指令集寄存器
    WRCommandH(0x36);			//打开绘图和文字显示
 
	  for(i=0;i<12;i++)
	  {
//			WRCommandH(0x80+i+19);  //Y������,���ڼ���
			WRCommandH(0x80+i+2);
	    WRCommandH(0x80+x);	  //X坐标，设置列地址在中间字节开始写入,80H为第一个字节
      WRDataH(buf[i*2]);
      WRDataH(buf[i*2+1]);
		}
}

/*---------------------------------------------------------*/
//画图1
void WRGDRAMp3()
{
		uint8_t i=0,j;
    WRCommandH(0x34);			//关闭扩展指令集寄存器
    WRCommandH(0x36);			//打开绘图和文字显示
 
	  for(i=0;i<12;i++)
	  {
			WRCommandH(0x80+i);  //Y坐标设置,设置行地址
	    WRCommandH(0x80+7);	  //X坐标，设置列地址在中间字节开始写入,80H为第一个字节
      WRDataH(p1[i*2]);
      WRDataH(p1[i*2+1]);
		}
}

//画图2
void WRGDRAMp4()
{
		uint8_t i=0,j;
    WRCommandH(0x34);			//关闭扩展指令集寄存器
    WRCommandH(0x36);			//打开绘图和文字显示
 
	  for(i=0;i<14;i++)
	  {
			WRCommandH(0x80+i);  //Y坐标设置,设置行地址
	    WRCommandH(0x80+7);	  //X坐标，设置列地址在中间字节开始写入,80H为第一个字节
      WRDataH(p2[i*2]);
      WRDataH(p2[i*2+1]);
		}
}

//画图3  闸门打开
void WRGDRAMp1()  //打开
{
		uint8_t i=0,j;
    WRCommandH(0x34);			//关闭扩展指令集寄存器
    WRCommandH(0x36);			//打开绘图和文字显示
 
	  for(i=0;i<12;i++)
	  {
			WRCommandH(0x80+18+i);  //Y坐标设置,设置行地址
	    WRCommandH(0x80+8);	  //X坐标，设置列地址在中间字节开始写入,80H为第一个字节
      WRDataH(p1[i*2]);
      WRDataH(p1[i*2+1]);
		}
}

//画图4  4g信号强度
void WRGDRAMp2()
{
		uint8_t i=0,j;
    WRCommandH(0x34);			//关闭扩展指令集寄存器
    WRCommandH(0x36);			//打开绘图和文字显示
 
	  for(i=0;i<16;i++)
	  {
			WRCommandH(0x80+19+i);  //Y坐标设置,设置行地址
	    WRCommandH(0x80+9);	  //X坐标，设置列地址在中间字节开始写入,80H为第一个字节
      WRDataH(p2[i*2]);
      WRDataH(p2[i*2+1]);
		}
}



