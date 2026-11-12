#include "Lcd12864.h"
#include "delay.h"
#include <string.h>
unsigned char p1[24] = {
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0F, 0x00,
    0x10, 0x80, 0x16, 0x80, 0x16, 0x80, 0x16, 0x80,
    0x16, 0x80, 0x16, 0x80, 0x10, 0x80, 0x0F, 0x00};
unsigned char p2[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x0E,
    0x00, 0xCE, 0x00, 0xCE, 0x0C, 0xCE, 0x0C, 0xCE,
    0xCC, 0xCE, 0xCC, 0xCE, 0xCC, 0xCE, 0xCC, 0xCE,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 写命令
void WRCommandH(uint8_t CMD)
{
    CS = 0;
    CS = 1;
    SPIWH(CMD, 0);
    delay_us(100); // 延时90us 89S52的模拟串口通信,延时,配合89S52时序
    CS = 0;
}

// 写数据
void WRDataH(uint8_t Data)
{
    CS = 0;
    CS = 1;

    SPIWH(Data, 1);
    delay_us(20);
    CS = 0;
}

// 写数据到LCD
void  SPIWH(uint8_t Wdata, uint8_t WRS)
{
    SendByteLCDH(0xf8 + (WRS << 1)); // 寄存器选择WRS
    SendByteLCDH(Wdata & 0xf0);
    SendByteLCDH((Wdata << 4) & 0xf0);
}

// 发送一个字节到LCD
void SendByteLCDH(uint8_t WLCDData)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        SCLK = 0;
        if ((WLCDData << i) & 0x80)
            SID = 1;
        else
            SID = 0;

        SCLK = 1;
    }
}

//-----------------------------------------------------------
// addr为字符显示位置,*hanzi为指针;count为输入汉字字符串
void ShowQQCharH(uint8_t addr, uint8_t *hanzi, uint8_t count)
{
    uint8_t i;
    i = 0;
	 WRCommandH(0x30); 
    for (i = 0; i < count; i++)
    {
        WRCommandH(addr); // 设定DDRAM地址
        WRDataH(hanzi[i * 2]);
        WRDataH(hanzi[i * 2 + 1]);
        addr++;
    }
}

// addr为起始地址，起始字符,i为起始字符地址偏移,count为需要显示字符个数
void ShowNUMCharH(uint8_t addr, uint8_t i, uint8_t count)
{
    uint8_t j;
    j = 0;
	  WRCommandH(0x30); 
    while (j < count)
    {
        WRCommandH(addr); // 设定DDRAM地址
        WRDataH(i + j);   // 因为两个16*8位字符拼接成一个16*16汉字显示
        j++;
        WRDataH(i + j);
        addr++;
        j++;
    }
}

// 自定义字符写入CGRAM
// data1是高字节,data2是低字节,一个汉字需要两个字节,第一个字节,第二个字节,依次累加,共16次
// 一个自定义字符为16*16点阵
// 第一个自定义字符为从40H开始,到4F结束为第一个自定义汉字, 之后自定义字符以8000H为始第一个
// addr为字符起始地址
void WRCGRAMH(uint8_t data1, uint8_t data2, uint8_t addr)
{
    uint8_t i;
    for (i = 0; i < 16; i++)
    {
        WRCommandH(addr + i); // 设定CGRAM地址
        WRDataH(data1);
        WRDataH(data1);
        i++;
        WRCommandH(addr + i); // 设定CGRAM地址
        WRDataH(data2);
        WRDataH(data2);
    }
}
// 自定义字符写入CGRAM
// 显示上半部分自定义字符,下半部分字符地址全为16*16
// addr为显示地址,自定义字符第一个字符地址,从8000H开始为第一个,
// 8100H为第二个,8200H为第三个,8300H为第四个,这个芯片只能自定义四个字符
// i为自定义字符字符地址,低字节地址,高字节地址
// IC限制,这个芯片限制,一个IC芯片只能显示16*2汉字
void ShowCGCharH(uint8_t addr, uint8_t i)
{
    uint8_t j;
    for (j = 0; j < 0x20;)
    {
        WRCommandH(addr + j); // 设定DDRAM地址
        WRDataH(0x00);        // 字符地址低字节
        WRDataH(i);           // 字符地址高字节
        j++;
    }
}

void WRGDRAM128X8(uint8_t x1, uint8_t y1, uint8_t d1)
{
    uint8_t j, i;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示
    for (j = 0; j < 16; j++)
    {
        WRCommandH(0x80 + y1 + j); // Y坐标轴,行地址
        WRCommandH(0x80 + x1);     // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        for (i = 0; i < 8; i++)    // 写入一行
        {
            WRDataH(d1);
            WRDataH(d1);
        }
    }
}

void WRGDRAM128X3(uint8_t x1, uint8_t y1, uint8_t d1)
{
    uint8_t j, i;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示
    for (j = 0; j < 16; j++)
    {
        WRCommandH(0x80 + y1 + j); // Y坐标轴,行地址
        WRCommandH(0x80 + x1);     // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        for (i = 0; i < 3; i++)    // 写入3字节
        {
            WRDataH(d1);
            WRDataH(d1);
        }
    }
}

// 上半部分绘图区域清除
void CLEARGDRAMH(uint8_t c)
{
    uint8_t j;
    uint8_t i;
    WRCommandH(0x34);
    WRCommandH(0x36);
    for (j = 0; j < 32; j++)
    {
        WRCommandH(0x80 + j);
        WRCommandH(0x80);        // X坐标
        for (i = 0; i < 16; i++) //
        {
            WRDataH(c);
            WRDataH(c);
        }
    }
}

// 画边框
void WRGDRAM_line()
{
    uint8_t i;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示

    WRCommandH(0x80 + 15);  // Y坐标轴,行地址
    WRCommandH(0x80);       // X坐标，列地址在当前行字节开始写入,80H为第一个字节
    for (i = 0; i < 8; i++) // 写入一行
    {
        WRDataH(0xff);
        WRDataH(0xff);
    }
}

void WRGDRAM(u8 x, u8 *buf)
{
    uint8_t i = 0;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示

    for (i = 0; i < 12; i++)
    {
        //			WRCommandH(0x80+i+19);  //Y坐标轴,行地址
        WRCommandH(0x80 + i + 1);
        WRCommandH(0x80 + x); // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        WRDataH(buf[i * 2]);
        WRDataH(buf[i * 2 + 1]);
    }
}
//void WRGDRAMCHR(u8 x, u8 *buf)
//{
//    uint8_t i = 0;
//    WRCommandH(0x34); // 去除扩展指令集
//    WRCommandH(0x36); // 打开绘图显示

//    for (i = 0; i <16; i++)
//    {
//        //			WRCommandH(0x80+i+19);  //Y坐标轴,行地址
//        WRCommandH(0x80 + i);
//        WRCommandH(0x80 + x); // X坐标，列地址在当前行字节开始写入,80H为第一个字节
//        WRDataH(buf[i]);
//        WRDataH(buf[i*2 + 1]);
//    }
//}

/*---------------------------------------------------------*/
// 画图标
void WRGDRAMp3()
{
    uint8_t i = 0, j;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示

    for (i = 0; i < 12; i++)
    {
        WRCommandH(0x80 + i); // Y坐标轴,行地址
        WRCommandH(0x80 + 7); // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        WRDataH(p1[i * 2]);
        WRDataH(p1[i * 2 + 1]);
    }
}

// 画图标
void WRGDRAMp4()
{
    uint8_t i = 0, j;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示

    for (i = 0; i < 14; i++)
    {
        WRCommandH(0x80 + i); // Y坐标轴,行地址
        WRCommandH(0x80 + 7); // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        WRDataH(p2[i * 2]);
        WRDataH(p2[i * 2 + 1]);
    }
}

// 画图标  电池电量
void WRGDRAMp1() // 电池
{
    uint8_t i = 0, j;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示

    for (i = 0; i < 12; i++)
    {
        WRCommandH(0x80 + 18 + i); // Y坐标轴,行地址
        WRCommandH(0x80 + 8);      // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        WRDataH(p1[i * 2]);
        WRDataH(p1[i * 2 + 1]);
    }
}

// 画图标  4g信号强度
void WRGDRAMp2()
{
    uint8_t i = 0, j;
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示

    for (i = 0; i < 16; i++)
    {
        WRCommandH(0x80 + 19 + i); // Y坐标轴,行地址
        WRCommandH(0x80 + 9);      // X坐标，列地址在当前行字节开始写入,80H为第一个字节
        WRDataH(p2[i * 2]);
        WRDataH(p2[i * 2 + 1]);
    }
}
void GenerateProgressBar(u8 start, u8 end,u8 *segment)
{
    u8 i;
    u8 fullSegments;  // 完整填充的段数
    u8 remainder;     // 剩余点数
    u8 currentSegment; // 当前处理的段索引
	 u8 highRemainder;
    
    // 限制长度范围
    if (start > 128)
        start = 128;
    if (start == 0)
        start = 0;
    
    // 初始化所有段为0
    memset(segment, 0, 16);
    
    // 第一部分：计算完整填充的段数
    fullSegments = start / 16;  // 取整，得到完整填充的段数
    
    // 第二部分：计算剩余点数
    remainder = start % 16;     // 取余，得到剩余的点数
    
    // 第一部分处理：完整填充的段全部置1
    for (i = 0; i < fullSegments && i < 8; i++)
    {
        segment[i * 2] = 0xFF;     // 低字节全1（点0-7）
        segment[i * 2 + 1] = 0xFF; // 高字节全1（点8-15）
    }
    
    // 第二部分处理：剩余点数填充到下一个段
    if (remainder > 0 && fullSegments < 8)
    {
        currentSegment = fullSegments;
        
        if (remainder <= 8)
        {
            // 剩余点数在低字节范围内（点0-7）
            // 高位在右、低位在左：反转位顺序
            segment[currentSegment * 2] = (0xFF << (8 - remainder)) & 0xFF;   // 高位在右，前remainder个点置1
            segment[currentSegment * 2 + 1] = 0x00;             // 高字节全0
        }
        else
        {
            // 剩余点数跨越两个字节
            segment[currentSegment * 2] = 0xFF;                  // 低字节全1（点0-7）
            // 高字节高位在右、低位在左：反转位顺序
            highRemainder = remainder - 8;
            segment[currentSegment * 2 + 1] = (0xFF << (8 - highRemainder)) & 0xFF; // 高字节前(remainder-8)个点置1，高位在右
        }
    }
    
    // 第三部分：剩余部分已经在memset中置0，无需额外处理
    
    // 显示到LCD
    WRCommandH(0x34); // 去除扩展指令集
    WRCommandH(0x36); // 打开绘图显示
    
    WRCommandH(0x80);  // Y坐标轴,行地址
    WRCommandH(0x80);  // X坐标，列地址在当前行字节开始写入,80H为第一个字节
    for (i = 0; i < 8; i++) // 写入一行
    {
        WRDataH(segment[i*2]);
        WRDataH(segment[i*2+1]);
    }
}

/**
 * @brief 将指定范围的线段转换为8段显示数据
 * @param curr 当前要显示的线段份数，范围1到all
 * @param all 将线段分割的份数，范围1到N
 * @param segment 输出缓冲区，16字节（8段 × 2字节/段）
 * @note 线段范围0-127，分为8段，每段16个点，每段用2字节表示
 *       根据curr和all计算显示范围，只对对应位置置1，其他位置置0
 *       高位在右、低位在左
 */
 void GenerateProgressBar1(u8 curr, u8 all, u8 *segment)
 {
     u8 i;
     u8 start;           // 计算得到的起始位置
     u8 end;             // 计算得到的结束位置
     u8 startSegment;    // 起始段索引
     u8 endSegment;      // 结束段索引
     u8 startOffset;     // 起始段内的偏移（0-15）
     u8 endOffset;       // 结束段内的偏移（0-15）
     u8 tempByte;
	 u8 highFillCount ;
     
     // 参数校验和限制
     if (all == 0)
         all = 1;  // 防止除零错误
     if (curr == 0)
         curr = 1;  // curr最小为1
     if (curr > all)
         curr = all;  // curr不能超过all
     
     // 根据curr和all计算start和end
     // 线段总长度128（0-127），平均分成all份
     start = (curr - 1) * 128 / all;
     if (curr == all)
     {
         // 最后一份要包含到127
         end = 127;
     }
     else
     {
         end = curr * 128 / all - 1;
     }
     
     // 确保end不超过127
     if (end > 127)
         end = 127;
     if (start > end)
     {
         // 如果start > end，交换它们
         u8 temp = start;
         start = end;
         end = temp;
     }
     
     // 初始化所有段为0
     memset(segment, 0, 16);
     
     // 如果范围为空，直接返回
     if (start == end && start == 0)
         return;
     
     // 计算起始段和结束段
     startSegment = start / 16;
     endSegment = end / 16;
     startOffset = start % 16;
     endOffset = end % 16;
     
     // 如果起始段和结束段相同（在同一段内）
     if (startSegment == endSegment)
     {
         // 计算需要填充的点数
         u8 fillCount = endOffset - startOffset + 1;
         
         if (fillCount <= 8)
         {
             // 只填充低字节
             tempByte = (0xFF << (8 - fillCount)) & 0xFF;
             tempByte = tempByte >> startOffset;  // 右移到起始位置
             segment[startSegment * 2] = tempByte;
         }
         else
         {
             // 填充低字节和高字节
             // 低字节：从startOffset到7
             tempByte = 0xFF >> startOffset;
             segment[startSegment * 2] = tempByte;
             
             // 高字节：从0到endOffset
             highFillCount = fillCount - (8 - startOffset);
             tempByte = (0xFF << (8 - highFillCount)) & 0xFF;
             segment[startSegment * 2 + 1] = tempByte;
         }
     }
     else
     {
         // 处理起始段（部分填充）
         if (startOffset < 8)
         {
             // 起始点在低字节
             tempByte = 0xFF >> startOffset;  // 从startOffset到7全部置1
             segment[startSegment * 2] = tempByte;
             segment[startSegment * 2 + 1] = 0xff;  // 高字节全1
         }
         else
         {
             // 起始点在高字节
            segment[startSegment * 2] = 0x00;  // 低字节全0
            tempByte = 0xFF >> (startOffset - 8);  // 高位在右，低位在左
            segment[startSegment * 2 + 1] = tempByte;  // 高字节从startOffset到15置1
         }
         
         // 处理中间的完整段（全部置1）
         for (i = startSegment + 1; i < endSegment && i < 8; i++)
         {
             segment[i * 2] = 0xFF;
             segment[i * 2 + 1] = 0xFF;
         }
         
         // 处理结束段（部分填充）
         if (endOffset < 8)
         {
             // 结束点在低字节
            tempByte = (0xFF << (8 - (endOffset + 1))) & 0xFF;  // 高位在右，低位在左，从0到endOffset置1
            segment[endSegment * 2] = tempByte;
            segment[endSegment * 2 + 1] = 0x00;  // 高字节全0
         }
         else
         {
             // 结束点在高字节
             segment[endSegment * 2] = 0xff;  // 低字节全1
             highFillCount = endOffset + 1;
             tempByte = (0xFF << (16 - highFillCount)) & 0xff;  // 从0到endOffset置1
             segment[endSegment * 2 + 1] = tempByte;
         }
     }
     
     // 显示到LCD
     WRCommandH(0x34); // 去除扩展指令集
     WRCommandH(0x36); // 打开绘图显示
     
     WRCommandH(0x80+14);  // Y坐标轴,行地址
     WRCommandH(0x80);  // X坐标，列地址在当前行字节开始写入,80H为第一个字节
     for (i = 0; i < 8; i++) // 写入一行
     {
         WRDataH(segment[i*2]);
         WRDataH(segment[i*2+1]);
     }
 }