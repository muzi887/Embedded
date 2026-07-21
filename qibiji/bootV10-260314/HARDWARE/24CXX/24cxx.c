#include "24cxx.h" 
#include "delay.h"
#include "reg.h"
#include <stdio.h>
#include <string.h>

//初始化IIC接口
void AT24CXX_Init(void)
{
	IIC_Init();
}

//在AT24CXX指定地址读出一个字节
//ReadAddr:开始读出的地址  
//返回值  :读到的数据
u8 AT24CXX_ReadOneByte(u16 ReadAddr)
{				  
	u8 temp=0;		  	    																 
  IIC_Start();  
	if(EE_TYPE>AT24C16)
	{
		IIC_Send_Byte(0xA0);	   //发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(ReadAddr>>8);//发送高地址
		IIC_Wait_Ack();		 
	}
	else 
		IIC_Send_Byte(0xA0+((ReadAddr/256)<<1));   //发送器件地址0xA0,写命令 	 

	IIC_Wait_Ack(); 
  IIC_Send_Byte(ReadAddr%256);   //发送低地址
	IIC_Wait_Ack();	    
	IIC_Start();  	 	   
	IIC_Send_Byte(0xA1+((ReadAddr/256)<<1));           //进入接收模式			   
	IIC_Wait_Ack();	 
  temp=IIC_Read_Byte(0);		   
  IIC_Stop();										//产生一个停止条件	    
	return temp;
}
//在AT24CXX指定地址写入一个字节
//WriteAddr  :写入数据的目的地址    
//DataToWrite:要写入的数据
void AT24CXX_WriteOneByte_old(u16 WriteAddr,u8 DataToWrite)
{				   	  	    																 
  IIC_Start();  
	if(EE_TYPE>AT24C16)
	{
		IIC_Send_Byte(0xA0);	    //发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(WriteAddr>>8);//发送高地址
 	}else
	{
		IIC_Send_Byte(0xA0+((WriteAddr/256)<<1));   //发送器件地址0XA0,写命令 
	}	 
	IIC_Wait_Ack();	   
  IIC_Send_Byte(WriteAddr%256);   //发送低地址
	IIC_Wait_Ack(); 	 										  		   
	IIC_Send_Byte(DataToWrite);     //发送字节							   
	IIC_Wait_Ack();  		    	   
  IIC_Stop();//产生一个停止条件 
	delay_ms(10);	 
}
// 改进 AT24CXX_WriteOneByte - 添加返回值
// WriteAddr  :写入数据的目的地址    
// DataToWrite:要写入的数据
// 返回值: 0-成功, 1-失败
u8 AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite)
{				   	  	    																 
    u8 ack_result;
    
    IIC_Start();  
    if(EE_TYPE > AT24C16)
    {
        IIC_Send_Byte(0xA0);	    //发送写命令
        ack_result = IIC_Wait_Ack();
        if(ack_result != 0) {
            IIC_Stop();
            return AT24CXX_ERROR;  // ACK失败
        }
        IIC_Send_Byte(WriteAddr>>8);  //发送高地址
        ack_result = IIC_Wait_Ack();
        if(ack_result != 0) {
            IIC_Stop();
            return AT24CXX_ERROR;
        }
    }
    else 
    {
        IIC_Send_Byte(0xA0+((WriteAddr/256)<<1));   //发送器件地址0XA0,写命令 
        ack_result = IIC_Wait_Ack();
        if(ack_result != 0) {
            IIC_Stop();
            return AT24CXX_ERROR;
        }
    }	 
    IIC_Send_Byte(WriteAddr%256);   //发送低地址
    ack_result = IIC_Wait_Ack();
    if(ack_result != 0) {
        IIC_Stop();
        return AT24CXX_ERROR;
    }
    
    IIC_Send_Byte(DataToWrite);     //发送字节							   
    ack_result = IIC_Wait_Ack();
    if(ack_result != 0) {
        IIC_Stop();
        return AT24CXX_ERROR;
    }
    
    IIC_Stop();  //产生一个停止条件 
    delay_ms(10);	 
    
    return AT24CXX_OK;  // 成功
}
//在AT24CXX里面的指定地址开始写入长度为Len的数据
//该函数用于写入16bit或者32bit的数据.
//WriteAddr  :开始写入的地址  
//DataToWrite:数据数组首地址
//Len        :要写入数据的长度2,4
void AT24CXX_WriteLenByte(u16 WriteAddr,u32 DataToWrite,u8 Len)
{  	
	u8 t;
	for(t=0;t<Len;t++)
	{
		AT24CXX_WriteOneByte(WriteAddr+t,(DataToWrite>>(8*t))&0xff);
	}												    
}

//一次读取多个16bit eeprom数据
void AT24CXX_Readdatas(u16 ReadAddr,u16 *Readbuf,u8 Len)
{
	u8 t;
	for(t=0;t<Len;t++)
	{
		Readbuf[t] = AT24CXX_ReadLenByte(ReadAddr,2);
		ReadAddr +=2;
	}
}

//在AT24CXX里面的指定地址开始读出长度为Len的数据
//该函数用于读出16bit或者32bit的数据.
//ReadAddr   :开始读出的地址 
//返回值     :数据
//Len        :要读出数据的长度2,4
u32 AT24CXX_ReadLenByte(u16 ReadAddr,u8 Len)
{  	
	u8 t;
	u32 temp=0;
	for(t=0;t<Len;t++)
	{
		temp<<=8;
		temp+=AT24CXX_ReadOneByte(ReadAddr+Len-t-1); 	 				   
	}
	return temp;												    
}

//指定地址写入一个字
void AT24CXX_WriteWord(u16 WriteAddr,u16 Data)
{
	u8 temp_l=(u8)(Data >> 8);
	u8 temp_h=(u8)(Data & 0x00ff);
	AT24CXX_WriteOneByte(WriteAddr,temp_h);
	AT24CXX_WriteOneByte(WriteAddr+1,temp_l);
}


//检查AT24CXX是否正常
//在AT24CXX最后一个地址(255)存储标志位.
//如果使用其他24C系列,该地址要修改
//返回1:检测失败
//返回0:检测成功
u8 AT24CXX_Check(void)
{
	u8 temp;
	
	temp=AT24CXX_ReadOneByte(EE_ADDR);//每次上电都检测写AT24CXX	
	if(temp==0x55) 
		return 0;		   
	else//排除第一次初始化的可能
	{
		AT24CXX_WriteOneByte(EE_ADDR,0x55);
	  temp=AT24CXX_ReadOneByte(EE_ADDR);	
		if(temp==0x55) 
		{
			//写入默认值
			paraToEeprom();
			return 0;
		}
	}
	return 1;											  
}

//在AT24CXX里面的指定地址开始读出指定个数的数据
//ReadAddr :开始读出的地址 以24c02为0~255
//pBuffer  :数据数组首地址
//NumToRead:要读出数据的个数
void AT24CXX_Read(u16 ReadAddr,u8 *pBuffer,u16 NumToRead)
{
    while(NumToRead)
    {
        *pBuffer++=AT24CXX_ReadOneByte(ReadAddr++);
        NumToRead--;
    }
}

//在AT24CXX里面的指定地址开始写入指定个数的数据
//WriteAddr :开始写入的地址 以24c02为0~255
//pBuffer   :数据数组首地址
//NumToWrite:要写入数据的个数
void AT24CXX_Write_old(u16 WriteAddr,u8 *pBuffer,u16 NumToWrite)
{
    while(NumToWrite--)
    {
        AT24CXX_WriteOneByte(WriteAddr,*pBuffer);
        WriteAddr++;
        pBuffer++;
    }
}

// 改进 AT24CXX_Write - 添加返回值
// WriteAddr  :开始写入的地址 以24c02为0~255
// pBuffer    :数据数组首地址
// NumToWrite :要写入数据的个数
// 返回值: 0-成功, 1-失败, 2-部分写入失败
u8 AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite)
{
    u8 result;
    u16 write_count = 0;
    
    // 参数检查
    if(pBuffer == NULL) {
        return AT24CXX_ERROR;
    }
    
    // 地址范围检查（根据EE_TYPE）
    if(WriteAddr + NumToWrite > EE_TYPE + 1) {
        return AT24CXX_ADDR_ERROR;
    }
    
    while(NumToWrite--)
    {
        result = AT24CXX_WriteOneByte(WriteAddr, *pBuffer);
        if(result != AT24CXX_OK) {
            // 写入失败，返回已写入的字节数（通过计算）
            return AT24CXX_ERROR;
        }
        WriteAddr++;
        pBuffer++;
        write_count++;
    }
    
    return AT24CXX_OK;  // 全部写入成功
}

