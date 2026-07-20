#include "data_handler.h"
#include "pass_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

int rly_1_time = 0;
int rly_2_time = 0;

char g_device_id[PUBLIC_DEVICE_ID_LEN+1];
char g_device_password[PUBLIC_DEVICE_PASSWORD_LEN] ;
char g_device_type[PUBLIC_DEVICE_TYPE_LEN+1];
char g_device_code[PUBLIC_DEVICE_CODE_LEN+1];
char g_device_public_Key[PUBLIC_KEY_LEN+1] ; // 密钥长度
char g_device_mode[PUBLIC_DEVICE_MODE_LEN+1] ; // 设备模式长度

char g_device_drift_time[PUBLIC_DRIFT_TIME_LEN+1] ; // 密钥长度


/* mode_str 中单段 "a_b_c"，取第二个 '_' 之后的字段为整数 */
static int parse_segment_third_underscore_int(const char *seg, int *out_val)
{
  const char *p;
  int underscore_count;
  long val;
  char *endptr;

  if (!seg || !out_val)
    return -1;

  underscore_count = 0;
  p = seg;
  while (*p && underscore_count < 2)
  {
    if (*p == '_')
      underscore_count++;
    p++;
  }

  if (underscore_count != 2 || *p == '\0')
    return -1;

  val = strtol(p, &endptr, 10);
  if (endptr == p)
    return -1;

  *out_val = (int)val;
  return 0;
}

/* '|' 单侧一段：>=2 个 '_' 同 parse_segment_third_underscore_int；恰好 1 个 '_' 则取第一个 '_' 后整数（如 0_30→30） */
static int parse_segment_rly_time_int(const char *seg, int *out_val)
{
  const char *s;
  const char *p;
  int nu;
  long val;
  char *endptr;

  if (!seg || !out_val)
    return -1;

  nu = 0;
  s = seg;
  while (*s != '\0')
  {
    if (*s == '_')
      nu++;
    s++;
  }

  if (nu >= 2)
    return parse_segment_third_underscore_int(seg, out_val);

  if (nu == 1)
  {
    p = seg;
    while (*p && *p != '_')
      p++;
    if (*p != '_' || p[1] == '\0')
      return -1;
    val = strtol(p + 1, &endptr, 10);
    if (endptr == p + 1)
      return -1;
    *out_val = (int)val;
    return 0;
  }

  return -1;
}

int ParseDeviceModeRlyTimesFrom(const char *mode_str)
{
  char buf[PUBLIC_DEVICE_MODE_LEN + 1];
  char *p;

  if (!mode_str || !mode_str[0])
    return -1;

  strncpy(buf, mode_str, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  p = strchr(buf, '|');
  if (!p)
    return -1;
  *p = '\0';

  if (parse_segment_rly_time_int(buf, &rly_1_time) != 0)
    return -1;
  if (parse_segment_rly_time_int(p + 1, &rly_2_time) != 0)
    return -1;

  return 0;
}

int ParseDeviceModeRlyTimes(void)
{
  return ParseDeviceModeRlyTimesFrom(g_device_mode);
}

/* 将 22位HEX 楼层位图 转成 JSON 数组字符串
 * 字节布局（按你的设备实际）：
 * bytes[0] : 负楼层位图，bit7→-8 ... bit0→-1
 * bytes[1..10] : 正楼层 1..80（按升序排列的80个bit）
 * 输出顺序：先负(-8..-1)，再正(80..1)，仅输出置位的
 */
int FloorsHexToJsonArray_FromField(const char *hex, int hexlen,
                                   char *outbuf, int outbuf_size)
{
  unsigned char bytes[11];
  int i, bit_index, outpos, first, hi, lo;
  int b, bi;
  unsigned char byte;

  outpos = 0;
  first = 1;

  if (hex == 0 || outbuf == 0 || outbuf_size <= 4)
    return 0;
  if (hexlen != 22)
    return 0;

  /* HEX -> 11 字节 */
  for (i = 0; i < 11; i++)
  {
    hi = hex2val(hex[i * 2]);
    lo = hex2val(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return 0;
    bytes[i] = (unsigned char)((hi << 4) | lo);
  }

  outpos += sprintf(outbuf + outpos, "[");

  /* 1) 负楼层：-8 → -1 */
  for (bit_index = 7; bit_index >= 0; bit_index--)
  {
    if (bytes[0] & (1 << bit_index))
    {
      if (!first)
        outpos += sprintf(outbuf + outpos, ",");
      outpos += sprintf(outbuf + outpos, "%d", -(bit_index + 1));
      first = 0;
    }
  }

  /* 2) 正楼层：按 1→80 输出，但块序反转解析 */
  for (bit_index = 0; bit_index < 80; bit_index++)
  {
    b = bit_index / 8;    /* 0..9 代表楼层块 */
    bi = bit_index % 8;   /* 每块内 bit 位置 */
    byte = bytes[10 - b]; /* ★ 关键：高块在前，低块在后 */

    if (byte & (1 << bi))
    {
      if (!first)
        outpos += sprintf(outbuf + outpos, ",");
      outpos += sprintf(outbuf + outpos, "%d", bit_index + 1);
      first = 0;
    }
  }

  outpos += sprintf(outbuf + outpos, "]");
  return 1;
}

// 判断传入楼层是否为单层
int IsSingleFloor(const uint8_t floors[7])
{
  int i, b;
  int one_cnt = 0;

  for (i = 0; i < 7; i++)
  {
    for (b = 0; b < 8; b++)
    {
      if (floors[i] & (1 << b))
      {
        one_cnt++;
        if (one_cnt > 1)
          return 0; // 超过 1 个“开”，直接不是单层
      }
    }
  }

  return (one_cnt == 1) ? 1 : 0;
}

// 二维码信息读取_命令1/2
char *BuildCvjJsonFromValidData12(const char *valid_data)
{
  static char json_out[1200];
  char ids_array[900];
  int len;
  int comma_cnt;
  int pos[3];
  const char *cmd_ptr;
  int cmd_len;
  const char *id_ptr;
  int id_len;
  const char *ids_ptr;
  int ids_len;
  char id_val[15] = {'\0'};
  int first_num;
  int p, q, k, i;
  char cmd_char; /* 保存 '1' 或 '2' */
  int j = 0;
  int t;
  char temp[15] = {'\0'};

  if (valid_data == 0)
    return 0;

  /* 计算长度 */
  len = 0;
  while (valid_data[len] != '\0')
    len++;
  if (len == 0)
    return 0;

  /* 找前三个逗号：cmd,id,ids,sign */
  comma_cnt = 0;
  for (i = 0; i < len && comma_cnt < 3; i++)
  {
    if (valid_data[i] == ',')
      pos[comma_cnt++] = i;
  }
  if (comma_cnt < 3)
    return 0;

  /* 第1段：cmd，允许 '1' 或 '2' */
  cmd_ptr = valid_data;
  cmd_len = pos[0];
  if (cmd_len != 1)
    return 0;
  cmd_char = cmd_ptr[0];
  if (!(cmd_char == '1' || cmd_char == '2'))
    return 0;

  /* 第2段：id（数值） */
  id_ptr = valid_data + pos[0] + 1;
  id_len = pos[1] - (pos[0] + 1);
  if (id_len <= 0)
    return 0;

  /* 第3段：ids（下划线分隔） */
  ids_ptr = valid_data + pos[1] + 1;
  ids_len = pos[2] - (pos[1] + 1);
  if (ids_len <= 0)
    return 0;

  /* id 转 long */
  j = 0;
  if (id_ptr[0] == '-')
  {
    j = 1;
  }

  k = 0;
  for (; j < id_len; j++)
  {
    id_val[k++] = id_ptr[j];
  }
  id_val[k] = '\0';

  /* 组装 body.qrCodeIds：按 '_' 切分，数量不固定 */
  k = 0;
  first_num = 1;
  k += sprintf(ids_array + k, "[");
  p = 0;
  for (q = 0; q <= ids_len; q++)
  {

    if (q == ids_len || ids_ptr[q] == '_')
    {
      int tok_len = q - p;
      if (tok_len > 0)
      {
        t = p;
        if (ids_ptr[t] == '-')
        {
          t++;
        }

        j = 0;
        for (; t < p + tok_len; t++)
        {
          temp[j++] = ids_ptr[t];
        }
        temp[j] = '\0';

        if (!first_num)
        {
          k += sprintf(ids_array + k, ",");
        }
        k += sprintf(ids_array + k, "%s", temp);
        first_num = 0;
      }
      p = q + 1;
    }
  }
  k += sprintf(ids_array + k, "]");
  ids_array[k] = '\0';

  /* 拼最终 JSON，cmd 使用实际的 cmd_char */
  sprintf(json_out,
          "{"
          "\"body\":{"
          "\"qrCodeIds\":%s"
          "},"
          "\"cmd\":\"%c\","
          "\"id\":%s,"
          "\"publicKey\":\"%s\"" /* ★ 这里改 */
          "}",
          ids_array, cmd_char, id_val,
          GetDevicePublicKey() /* ★ 动态取当前公钥 */
  );

  return json_out;
}


/* 从 valid_data 中提取 ID (第二段)，返回 id 字符串指针。
 * 返回的字符串存放在静态缓冲区里，调用者不需要释放。
 * 失败返回 NULL。
 */
const char *ExtractIdFromValidData(const char *valid_data)
{
  static char id_str[16]; /* 足够存放32位整型的字符串 */
  int len, i, comma_cnt, start, end;

  if (!valid_data)
    return NULL;

  /* 找到第一个和第二个逗号位置 */
  len = 0;
  while (valid_data[len] != '\0')
    len++;

  comma_cnt = 0;
  start = -1;
  end = -1;
  for (i = 0; i < len; i++)
  {
    if (valid_data[i] == ',')
    {
      comma_cnt++;
      if (comma_cnt == 1)
        start = i + 1; /* id 起点 */
      else if (comma_cnt == 2)
      {
        end = i;
        break;
      }
    }
  }

  if (start < 0 || end < 0 || end <= start)
    return NULL;

  /* 拷贝子串 */
  if (end - start >= (int)sizeof(id_str))
    return NULL; /* 太长保护 */
  for (i = 0; i < end - start; i++)
  {
    id_str[i] = valid_data[start + i];
  }
  id_str[end - start] = '\0';

  return id_str;
}

/* 将 received (格式: cmd,id,ids_with_underscore,sig) 转成 JSON 字符串
 * 返回: 指向静态缓冲区的 JSON 字符串，失败则返回 NULL
 */
char *BuildGrantVerifyJson(const char *availableTimes,
                           const char *validTimeBegin,
                           const char *validTimeEnd,
                           const char *floors,
                           const char *cmd,
                           long id)
{
  static char json_buf[1024]; /* 静态缓冲区，函数返回后仍然有效 */

  sprintf(json_buf,
          "{"
          "\"body\":{"
          "\"availableTimes\":\"%s\","
          "\"floors\":%s,"
          "\"validTimeBegin\":\"%s\","
          "\"validTimeEnd\":\"%s\""
          "},"
          "\"cmd\":\"%s\","
          "\"id\":%ld,"
          "\"publicKey\":\"%s\"" /* ★ 改这里 */
          "}",
          availableTimes,
          floors,
          validTimeBegin,
          validTimeEnd,
          cmd,
          id,
          GetDevicePublicKey() /* ★ 新增参数 */
  );

  return json_buf; /* 返回指向 static 缓冲区的指针 */
}

/* 解析一条形如：
 *   1,2147483647,2147483647_2147483646_2147483645_2147483644_2147483643,6626605aa1
 * 的数据，提取第3段（以下划线分隔的多个id），逐个以32字节块写入EEPROM：
 *   - 对每个 id：用 ASCII 拷入 buf[0..len-1]，其余补 0x00 到 32 字节
 *   - 调用 add_epromdata(buf)
 *
 * 返回：成功调用 add_epromdata 的次数（添加的数量）
 * 失败（格式不对等）返回 0
 */
int AddBlacklistIds_FromPacket(const char *packet)
{
  int len, i;
  int pos[3]; /* 记录前三个逗号位置：0=命令分隔, 1=id分隔, 2=ids分隔(后面是签名) */
  int comma_cnt;
  int start, end;
  int added;
  const char *ids_start;
  int ids_len;

  uint8_t buf[DATA_SIZE];
  int p, q;

  if (packet == 0)
    return 0;

  /* 计算长度 */
  len = 0;
  while (packet[len] != '\0')
    len++;
  if (len == 0)
    return 0;

  /* 找到至少3个逗号（我们需要定位第3段） */
  comma_cnt = 0;
  for (i = 0; i < len && comma_cnt < 3; i++)
  {
    if (packet[i] == ',')
    {
      pos[comma_cnt++] = i;
    }
  }
  if (comma_cnt < 3)
  {
    /* 格式不足：命令,id,ids,签名 */
    return 0;
  }

  /* 第3段（ids）起点是第二个逗号后，终点在第三个逗号前 */
  start = pos[1] + 1; /* 第二个逗号后 */
  end = pos[2];       /* 第三个逗号前 */
  if (end <= start)
    return 0;

  ids_start = packet + start;
  ids_len = end - start;

  /* 遍历 ids 段，按 '_' 逐个提取 token */
  added = 0;
  p = 0; /* p 指向当前token起点的相对索引 */
  for (q = 0; q <= ids_len; q++)
  {
    /* 到 '_' 或 末尾（q==ids_len）时，处理一个 token */
    if (q == ids_len || ids_start[q] == '_')
    {
      int tok_len = q - p;

      if (tok_len > 0)
      {
        int copy_n = (tok_len > DATA_SIZE) ? DATA_SIZE : tok_len;
        int j;

        /* 清零并拷贝 ASCII id 到10字节缓冲 */
        for (j = 0; j < DATA_SIZE; j++)
          buf[j] = '\0';
        /* 拷贝 token 内容到 buf[0..copy_n-1] */
        for (j = 0; j < copy_n; j++)
        {
          buf[j] = (uint8_t)ids_start[p + j];
        }
        /* 写入一条黑名单记录（固定10字节） */
        addDataToBlacklist(buf);
        added++;
      }

      /* 跳到下一个 token 的起点（跳过 '_'） */
      p = q + 1;
    }
  }

  return added;
}

/* 解析一条形如：
 *   2,2147483646,2147483642_2147483641_2147483640_2147483639_2147483638,bc7b54a5e4
 * 的数据，提取第3段（以下划线分隔的多个id），逐个以32字节块调用 del_epromdata()
 *
 * 返回：成功调用 del_epromdata 的次数（删除的数量）
 * 失败返回 0
 */
int DelBlacklistIds_FromPacket(const char *packet)
{
  int len, i;
  int pos[3]; /* 记录前三个逗号位置：0=命令分隔, 1=id分隔, 2=ids分隔(后面是签名) */
  int comma_cnt;
  int start, end;
  int removed;
  const char *ids_start;
  int ids_len;

  uint8_t buf[DATA_SIZE];
  int p, q;

  if (packet == 0)
    return 0;

  /* 计算长度 */
  len = 0;
  while (packet[len] != '\0')
    len++;
  if (len == 0)
    return 0;

  /* 找到至少3个逗号 */
  comma_cnt = 0;
  for (i = 0; i < len && comma_cnt < 3; i++)
  {
    if (packet[i] == ',')
    {
      pos[comma_cnt++] = i;
    }
  }
  if (comma_cnt < 3)
    return 0;

  /* 第3段 ids 起止位置 */

  start = pos[1] + 1;
  end = pos[2];
  if (end <= start)
    return 0;

  ids_start = packet + start;
  ids_len = end - start;

  /* 遍历 ids 段 */
  removed = 0;
  p = 0;
  for (q = 0; q <= ids_len; q++)
  {
    if (q == ids_len || ids_start[q] == '_')
    {
      int tok_len = q - p;

      if (tok_len > 0)
      {
        int copy_n = (tok_len > DATA_SIZE) ? DATA_SIZE : tok_len;
        int j;

        /* 清零并拷贝 token */
        for (j = 0; j < DATA_SIZE; j++)
          buf[j] = 0x00;
        for (j = 0; j < copy_n; j++)
        {
          buf[j] = (uint8_t)ids_start[p + j];
        }

        /* 调用删除函数 */
        deleteDataFromBlacklist(buf);
        removed++;
      }
      p = q + 1; // 下一个 token 起点
    }
  }
  return removed;
}

void build_frame_A2(uint16_t chardan, uint8_t chartihao,
                    const uint8_t *extra11, uint8_t *outbuf, int *outlen)
{
  int pos = 0;

  /* 前三个字节 */
  outbuf[pos++] = 0x7E;
  outbuf[pos++] = 0xA2;
  outbuf[pos++] = 0x00;

  /* 第4-11字节 */
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;

  /* 拼接 extra11 (必须传入11字节) */
  memcpy(outbuf + pos, extra11, 7);
  pos += 7;

  /* 两备用位 */
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;

  *outlen = pos;
}

void build_frame_A1(uint16_t chardan, uint8_t chartihao,
                    const uint8_t *extra11, uint8_t *outbuf, int *outlen)
{
  int pos = 0;

  /* 前三个字节 */
  outbuf[pos++] = 0x7E;
  outbuf[pos++] = 0xA1;
  outbuf[pos++] = 0x00;

  /* 拼接 extra11 (必须传入11字节) */
  memcpy(outbuf + pos, extra11, 7);
  pos += 7;

  /* 第4-11字节 */
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;

  /* 两备用位 */
  outbuf[pos++] = 0x00;
  outbuf[pos++] = 0x00;

  *outlen = pos;
}


int VerifyDeviceCredentials(const char *user, const char *pass)
{
  int ok1, ok2, i;
  if (!user || !pass)
    return 0;

  /* strcmp 等价实现（避免库差异），完全匹配 */
  i = 0;
  while (g_device_id[i] == user[i] && g_device_id[i] != '\0')
    i++;
  ok1 = (g_device_id[i] == user[i]);

  i = 0;
  while (g_device_password[i] == pass[i] && g_device_password[i] != '\0')
    i++;
  ok2 = (g_device_password[i] == pass[i]);

  return (ok1 && ok2) ? 1 : 0;
}

// 密钥更改
void SetDevicePublicKey(const char *pk)
{
  int n;
  if (!pk)
    return;
  n = 0;
  while (pk[n] != '\0' && n < (int)sizeof(g_device_public_Key) - 1)
  {
    g_device_public_Key[n] = pk[n];
    n++;
  }
  g_device_public_Key[n] = '\0';

  // 密钥存储
  WriteKey(pk,PUBLIC_KEY_ADDR,PUBLIC_KEY_LEN);
}

const char *GetDevicePublicKey(void)
{
  return g_device_public_Key;
}
