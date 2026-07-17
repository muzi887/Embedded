#include <stdint.h>
#include <stdbool.h>
#include "DS3231.h" // 对应的头文件

/*****************************************
   公共函数实现
*****************************************/

// 定义月份天数表（非闰年）
static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// 检查是否为闰年
static bool is_leap_year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

// BCD转十进制
uint8_t BCD_To_Dec(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

// 十进制转BCD
uint8_t Dec_To_BCD(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

// 读取DS3231时间
void DS3231_GetTime(DS3231_Time *time)
{
    uint8_t i = 0;
    uint8_t buffer[7];

    // 从寄存器0x00开始读取7字节（秒、分、时、星期、日、月、年）
    IIC_Start();
    IIC_Send_Byte(DS3231_ADDR | 0); // 写模式
    IIC_Wait_Ack();
    IIC_Send_Byte(0x00); // 起始寄存器地址
    IIC_Wait_Ack();
    IIC_Start();
    IIC_Send_Byte(DS3231_ADDR | 1); // 读模式
    IIC_Wait_Ack();

    for (i = 0; i < 7; i++)
    {
        buffer[i] = IIC_Read_Byte(i == 6 ? 0 : 1); // 最后一个字节NACK
    }
    IIC_Stop();

    time->second = BCD_To_Dec(buffer[0] & 0x7F); // 忽略最高位（时钟停止标志）
    time->minute = BCD_To_Dec(buffer[1]);
    time->hour = BCD_To_Dec(buffer[2] & 0x3F); // 24小时制，忽略12/24标志
    time->dayOfWeek = BCD_To_Dec(buffer[3]);
    time->date = BCD_To_Dec(buffer[4]);
    time->month = BCD_To_Dec(buffer[5] & 0x7F); // 忽略世纪位
    time->year = BCD_To_Dec(buffer[6]);
}

// 设置DS3231时间
void DS3231_SetTime(DS3231_Time *time)
{
    uint8_t i = 0;
    uint8_t buffer[7];

    buffer[0] = Dec_To_BCD(time->second);
    buffer[1] = Dec_To_BCD(time->minute);
    buffer[2] = Dec_To_BCD(time->hour); // 24小时制
    buffer[3] = Dec_To_BCD(time->dayOfWeek);
    buffer[4] = Dec_To_BCD(time->date);
    buffer[5] = Dec_To_BCD(time->month);
    buffer[6] = Dec_To_BCD(time->year);

    // 调试输出
    printf("Writing Time: ");
    for (i = 0; i < 7; i++)
    {
        printf("%02X ", buffer[i]);
    }
    printf("\n");

    IIC_Start();
    IIC_Send_Byte(DS3231_ADDR | 0); // 写模式
    if (IIC_Wait_Ack())
    { // 检查从机是否应答
      //  printf("I2C Addr ACK failed!\n");
        IIC_Stop(); // 失败时发送停止信号
        return;
    }

    IIC_Send_Byte(0x00); // 起始寄存器地址
    if (IIC_Wait_Ack())
    {
     //   printf("I2C Reg ACK failed!\n");
        IIC_Stop();
        return;
    }

    for (i = 0; i < 7; i++)
    {
        IIC_Send_Byte(buffer[i]);
        if (IIC_Wait_Ack())
        {
       //     printf("I2C Data ACK failed at byte %d\n", i);
            IIC_Stop();
            return;
        }
        delay_us(10); // 增加延时
    }

    IIC_Stop();
  //  printf("Time set complete.\n");
}

uint64_t DS3231_To_MillisTimestamp(DS3231_Time *time)
{
    uint8_t m;
    uint16_t y;
    uint16_t full_year = 2000 + time->year;
    uint64_t total_millis;
    // 计算从1970年到目标年份的总天数
    uint32_t total_days = 0;

    for (y = 1970; y < full_year; y++)
    {
        total_days += is_leap_year(y) ? 366 : 365;
    }

    for (m = 1; m < time->month; m++)
    {
        uint8_t days = month_days[m - 1];
        if (m == 2 && is_leap_year(full_year))
        {
            days = 29;
        }
        total_days += days;
    }

    total_days += (time->date - 1);

    // 一次性计算总毫秒数，然后减去1小时
    total_millis = ((uint64_t)total_days * 86400000ULL) +
                   (time->hour * 3600000ULL) +
                   (time->minute * 60000ULL) +
                   (time->second * 1000ULL);

    // 减去1小时（3600000毫秒）
    return total_millis - 8 * 3600000ULL;
}

// 将DS3231时间转换为Unix时间戳（秒级）
uint32_t DS3231_To_Timestamp_s(DS3231_Time *time)
{
    // 将两位年份转换为完整年份 (2000-2099)
    uint8_t m;
    uint16_t y;
    uint64_t timestamp_seconds;
    uint16_t full_year = 2000 + time->year;

    // 计算从1970年到目标年份的总天数
    uint32_t total_days = 0;

    // 计算1970年到目标年份-1年的总天数
    for (y = 1970; y < full_year; y++)
    {
        total_days += is_leap_year(y) ? 366 : 365;
    }

    // 计算当年已过去的天数
    for (m = 1; m < time->month; m++)
    {
        total_days += month_days[m - 1];
        // 如果是闰年且二月，增加一天
        if (m == 2 && is_leap_year(full_year))
        {
            total_days += 1;
        }
    }

    // 加上当月已过去的天数
    total_days += (time->date - 1);

    // 计算总秒数
    timestamp_seconds = total_days * 86400ULL;
    timestamp_seconds += time->hour * 3600ULL;
    timestamp_seconds += time->minute * 60ULL;
    timestamp_seconds += time->second;

    // 返回秒级时间戳
    return (uint32_t)timestamp_seconds;
}

// 将DS3231时间转换为Unix时间戳（毫秒级）
uint64_t DS3231_To_MillisTimestamp_s(DS3231_Time *time)
{
    // 将两位年份转换为完整年份 (2000-2099)
    uint8_t m;
    uint16_t y;
    uint64_t timestamp_millis;
    uint16_t full_year = 2000 + time->year;

    // 计算从1970年到目标年份的总天数
    uint32_t total_days = 0;

    // 计算1970年到目标年份-1年的总天数
    for (y = 1970; y < full_year; y++)
    {
        total_days += is_leap_year(y) ? 366 : 365;
    }

    // 计算当年已过去的天数
    for (m = 1; m < time->month; m++)
    {
        total_days += month_days[m - 1];
        // 如果是闰年且二月，增加一天
        if (m == 2 && is_leap_year(full_year))
        {
            total_days += 1;
        }
    }

    // 加上当月已过去的天数
    total_days += (time->date - 1);

    // 计算总毫秒数
    timestamp_millis = total_days * 86400ULL * 1000ULL; // 天数转为毫秒
    timestamp_millis += time->hour * 3600ULL * 1000ULL; // 小时转为毫秒
    timestamp_millis += time->minute * 60ULL * 1000ULL; // 分钟转为毫秒
    timestamp_millis += time->second * 1000ULL;         // 秒转为毫秒
    // 如果需要毫秒字段，可以加上：time->millisecond

    return timestamp_millis;
}
