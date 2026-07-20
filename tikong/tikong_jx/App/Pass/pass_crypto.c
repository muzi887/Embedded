#include "pass_crypto.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>


/* sha1_hexstr: 40个hex字符的SHA1字符串（可大写/小写）
 * valid_data : 逗号分隔字符串，最后一个字段为“验签前10位”（10个hex字符）
 * 返回: 1=通过, 0=不通过
 */
int VerifySignature(const char *sha1_hexstr, const char *valid_data)
{
  char sig_field[11]; /* 10 chars + '\0' */
  char calc10[11];
  char c;
  int i, len, last_comma_pos;

  if (sha1_hexstr == 0 || valid_data == 0)
    return 0;

  /* 取 valid_data 最后一个字段（验签前10位） */
  len = 0;
  while (valid_data[len] != '\0')
    len++;

  last_comma_pos = -1;
  for (i = len - 1; i >= 0; i--)
  {
    if (valid_data[i] == ',')
    {
      last_comma_pos = i;
      break;
    }
  }
  if (last_comma_pos < 0)
    return 0;

  for (i = 0; i < 10; i++)
  {
    c = valid_data[last_comma_pos + 1 + i];
    /* 去掉可能的 \r \n 空格（极端情况），遇到就截断 */
    if (c == '\r' || c == '\n' || c == ' ')
      break;
    // sig_field[i] = toLowerHex(c);
    sig_field[i] = c;
  }
  //printf("sig_field = %s\n", sig_field);
  sig_field[i] = '\0';

  /* 取 SHA2 的前10个hex字符（注意：不是10字节=20hex）并转小写 */
  for (i = 0; i < 10; i++)
  {
    char c = sha1_hexstr[i];
    calc10[i] = toLowerHex(c);
  }
  calc10[10] = '\0';

  /* 比较前10个hex字符 */
  return (strncmp(calc10, sig_field, 10) == 0) ? 1 : 0;
}

/* 大小写不敏感的 tolower，仅处理 A-F */
char toLowerHex(char c)
{
  if (c >= 'A' && c <= 'F')
    return (char)(c - 'A' + 'a');
  return c;
}

/* ====== 辅助：HEX 转数值 ====== */
int hex2val(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

/* ===== 从 valid_data 中提取第4/5段（validBegin/validEnd），并做区间判断 =====
   期望 valid_data 形如：
   3,2147483645,-1,20250101000000,20251231235959,81FFFFFFFFFFFFFFFFFFFF,59a474ecf7
   返回：1= begin<=now<=end；0=不在区间或解析失败
*/
int CheckValidPeriod_WithNow(const char *valid_data, const RTC_Time *now)
{
  int len;
  int i;
  int comma_cnt;
  int last;
  int pos[6];
  const char *f[7];
  int flen[7];
  char begin14[15];
  char end14[15];
  char now14[15];

  if (!valid_data || !now)
    return 0;

  /* 计算长度 */
  len = 0;
  while (valid_data[len] != '\0')
    len++;
  if (len <= 0)
    return 0;

  /* 找到 6 个逗号（共 7 段） */
  comma_cnt = 0;
  for (i = 0; i < len && comma_cnt < 6; i++)
  {
    if (valid_data[i] == ',')
      pos[comma_cnt++] = i;
  }
  if (comma_cnt != 6)
    return 0;

  /* 切分 7 段（不修改原串，用指针+长度） */
  last = -1;
  for (i = 0; i < 6; i++)
  {
    f[i] = valid_data + last + 1;
    flen[i] = pos[i] - (last + 1);
    last = pos[i];
  }
  f[6] = valid_data + last + 1;
  flen[6] = len - (last + 1);

  /* 第4段=validBegin(index=3)、第5段=validEnd(index=4) 都应是14位数字 */
  if (flen[3] != 14 || flen[4] != 14)
    return 0;

  /* 拷贝成以 '\0' 结尾的字符串 */
  for (i = 0; i < 14; i++)
    begin14[i] = f[3][i];
  begin14[14] = '\0';
  for (i = 0; i < 14; i++)
    end14[i] = f[4][i];
  end14[14] = '\0';
  printf("Adjusted begin time-1: %s\n", begin14);
  adjust_begin_time(begin14);
  /* 当前时间转 14 位 */
  printf("Adjusted begin time-2: %s\n", begin14);
  MakeTimestamp14(now, now14);

  /* 直接用字典序比较：YYYYMMDDhhmmss 天然可用 strcmp */
  if (strcmp(begin14, now14) <= 0 && strcmp(now14, end14) <= 0)
  {
    return 1; /* 在有效期内 */
  }
  return 0; /* 不在有效期 */
}

void adjust_begin_time(char *begin14)
{
  // 解析 begin14 时间字符串（格式为 YYYYMMDDhhmmss）
  struct tm begin_tm = {0};
  time_t begin_time;
  struct tm *new_begin_tm;
  // 手动解析 YYYYMMDDhhmmss 字符串
  sscanf(begin14, "%4d%2d%2d%2d%2d%2d",
         &begin_tm.tm_year,
         &begin_tm.tm_mon,
         &begin_tm.tm_mday,
         &begin_tm.tm_hour,
         &begin_tm.tm_min,
         &begin_tm.tm_sec);

  // tm_year 是从 1900 年开始的，所以需要加上 1900
  begin_tm.tm_year -= 1900;

  // tm_mon 是从 0 开始的，所以需要减去 1
  begin_tm.tm_mon -= 1;

  // 转换为 time_t 时间戳
  begin_time = mktime(&begin_tm);

  // 减去 30 秒
  begin_time -= 30;

  // 将减去 30 秒后的时间转换回 struct tm
  new_begin_tm = localtime(&begin_time);

  // 将新的时间转换回 YYYYMMDDhhmmss 格式
  strftime(begin14, 15, "%Y%m%d%H%M%S", new_begin_tm);
}

/* 将 14 位时间串 YYYYMMDDhhmmss 按分钟增减（act:1=增加,0=减少） */
void Get_newTime(char *time, int minute, int act)
{
  int i;
  struct tm tm_time = {0};
  time_t ts;
  struct tm *new_tm;

  if (!time)
    return;
  if (strlen(time) < 14)
    return;
  for (i = 0; i < 14; i++)
  {
    if (!isdigit((unsigned char)time[i]))
      return;
  }

  /* 解析 YYYYMMDDhhmmss */
  if (sscanf(time, "%4d%2d%2d%2d%2d%2d",
             &tm_time.tm_year,
             &tm_time.tm_mon,
             &tm_time.tm_mday,
             &tm_time.tm_hour,
             &tm_time.tm_min,
             &tm_time.tm_sec) != 6)
  {
    return;
  }

  tm_time.tm_year -= 1900;
  tm_time.tm_mon -= 1;

  ts = mktime(&tm_time);
  if (ts == (time_t)-1)
    return;

  if (act == 1)
    ts += (time_t)minute * 60;
  else if (act == 0)
    ts -= (time_t)minute * 60;
  else
    return;

  new_tm = localtime(&ts);
  if (!new_tm)
    return;

  strftime(time, 15, "%Y%m%d%H%M%S", new_tm);
}

/* ===== 将 RTC_Time 组装为 14 位时间戳 YYYYMMDDhhmmss ===== */
void MakeTimestamp14(const RTC_Time *t, char out14[15])
{
  int fullYear;
  if (!t || !out14)
    return;
  /* 你的 year 是 00..99，对应 2000+year（与你 main.c 的打印保持一致） */
  fullYear = 2000 + (int)t->year;

  sprintf(out14, "%04d%02d%02d%02d%02d%02d",
          fullYear,
          (int)t->month, (int)t->date,
          (int)t->hour, (int)t->minute, (int)t->second);
  out14[14] = '\0';
}
