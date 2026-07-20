#include "rtc_util.h"
#include <stdio.h>

/* currentTime 按小时平移（dh 可为负），日月年自动进位；分秒与星期不变 */
static int ds1302_days_in_month(int full_year, int month)
{
	static const int md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int d;

	if (month < 1 || month > 12)
		return 31;
	d = md[month - 1];
	if (month == 2)
	{
		int leap = ((full_year % 4 == 0) && (full_year % 100 != 0)) || (full_year % 400 == 0);
		if (leap)
			d = 29;
	}
	return d;
}


/* 按分钟平移（dm 为正则加、负则减），时分日期自动进位；秒与星期沿用 in（与 ds1302_shift_hours 一致） */
void ds1302_shift_minutes(RTC_Time *out, const RTC_Time *in, int dm)
{
	int y = 2000 + (int)in->year;
	int mo = (int)in->month;
	int da = (int)in->date;
	int ho = (int)in->hour;
	int mi = (int)in->minute + dm;

	while (mi < 0)
	{
		mi += 60;
		ho--;
	}
	while (mi >= 60)
	{
		mi -= 60;
		ho++;
	}

	while (ho < 0)
	{
		ho += 24;
		da--;
		if (da < 1)
		{
			mo--;
			if (mo < 1)
			{
				mo = 12;
				y--;
			}
			da = ds1302_days_in_month(y, mo);
		}
	}
	while (ho >= 24)
	{
		ho -= 24;
		da++;
		{
			int dim = ds1302_days_in_month(y, mo);
			if (da > dim)
			{
				da = 1;
				mo++;
				if (mo > 12)
				{
					mo = 1;
					y++;
				}
			}
		}
	}
	if (y < 2000)
		y = 2000;
	if (y > 2099)
		y = 2099;

	out->year = (uint8_t)(y - 2000);
	out->month = (uint8_t)mo;
	out->date = (uint8_t)da;
	out->hour = (uint8_t)ho;
	out->minute = (uint8_t)mi;
	out->second = in->second;
	out->dayOfWeek = in->dayOfWeek;
}

/* 由 DS1302_Time 生成 "YYYYMMDDhhmm" 字符串（内部静态缓冲，勿嵌套多次调用依赖其值） */
char *create_ymdhm(const RTC_Time *currenttim)
{
	static char ymdh_buf[13];
	int full_year;

	if (!currenttim)
	{
		ymdh_buf[0] = '\0';
		return ymdh_buf;
	}
	full_year = 2000 + (int)currenttim->year;
	snprintf(ymdh_buf, sizeof(ymdh_buf), "%04d%02d%02d%02d%02d",
			 full_year,
			 (int)currenttim->month,
			 (int)currenttim->date,
			 (int)currenttim->hour,
			 (int)currenttim->minute);
	return ymdh_buf;
}
