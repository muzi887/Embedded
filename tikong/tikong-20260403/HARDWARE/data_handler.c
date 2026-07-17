#include "data_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <time.h>
char g_device_user[32] = "admin";
char g_device_pass[32] = "12345678";
char g_device_publicKey[PUBLIC_KEY_LEN + 1] = "12345678"; // 密钥长度
/* ====== 辅助：HEX 转数值 ====== */
static int hex2val(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
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

// 二维码信息上报_命令1/2
char *BuildCvjJsonFromValidData12_1(const char *valid_data, char *haxi)
{
  static char json_out[1400]; /* 返回缓冲 */
  const char *f[7];
  int flen[7];
  int pos[6];
  int len, i, comma_cnt, last;

  /* 变量提前声明 */
  len = 0;
  i = 0;
  comma_cnt = 0;
  last = -1;

  if (valid_data == 0)
    return 0;

  /* 计算长度（valid_data 已是干净文本，且以 '\0' 结束） */
  while (valid_data[len] != '\0')
    len++;
  if (len <= 0)
    return 0;

  /* 统计逗号并记录位置 —— 需要恰好 3 个逗号（4 段） */
  for (i = 0; i < len && comma_cnt < 3; i++)
  {
    if (valid_data[i] == ',')
    {
      pos[comma_cnt++] = i;
    }
  }
  if (comma_cnt != 3)
    return 0;

  /* 切分出 4 段：f[0..3], flen[] 保存长度 */
  last = -1;
  for (i = 0; i < 3; i++)
  {
    f[i] = valid_data + last + 1;
    flen[i] = pos[i] - (last + 1);
    last = pos[i];
  }
  f[3] = valid_data + last + 1;
  flen[3] = len - (last + 1);

  /* 必须是 cmd == "3" */
  if (!((flen[0] == 1) && ((f[0][0] == '1') || (f[0][0] == '2'))))
    return 0;

  /* 拼装最终 JSON：
     cmd 用 f[0]（字符串）
     id  用 f[1]（数值打印）
     publicKey 固定 "12345678"
   */
  /* 注意：为安全起见，字段拷贝到临时缓冲再使用（保证以 '\0' 结束） */
  {
    char cmd[4];    /* "1/2" */
    char idbuf[24]; /* id 字符串转印到 %ld 要注意范围，这里直接放到 JSON 用 %s 或者先 atol */
    char ExecutionID[200];
    int n = 0;

    /* 拷贝并补 '\0' */
    n = (flen[0] < (int)sizeof(cmd) - 1) ? flen[0] : (int)sizeof(cmd) - 1;
    memcpy(cmd, f[0], n);
    cmd[n] = '\0';

    n = (flen[1] < (int)sizeof(idbuf) - 1) ? flen[1] : (int)sizeof(idbuf) - 1;
    memcpy(idbuf, f[1], n);
    idbuf[n] = '\0';

    n = (flen[2] < (int)sizeof(ExecutionID) - 1) ? flen[2] : (int)sizeof(ExecutionID) - 1;
    memcpy(ExecutionID, f[2], n);
    ExecutionID[n] = '\0';

    /* 组装 JSON 到 static 缓冲 */
    sprintf(json_out,
            "{"
            "\\\"body\\\":{"
            "\\\"cmd\\\":\\\"%s\\\","
            "\\\"id\\\":%s,"
            "\\\"ExecutionID\\\":%s,"
            "\\\"signature\\\":\\\"%s\\\""
            "}"
            "}",
            cmd, idbuf, ExecutionID, haxi);
  }
  return json_out;
}

// 制作加签数据_命令3
char *BuildCvjJsonFromValidData3(const char *valid_data, char *haxi)
{
  static char json_out[1400]; /* 返回缓冲 */
  char floors_json[700];      /* 楼层数组缓冲 */
  const char *f[7];
  int flen[7];
  int pos[6];
  int len, i, comma_cnt, last;
  int j = 0;

  /* 变量提前声明 */
  len = 0;
  i = 0;
  comma_cnt = 0;
  last = -1;

  if (valid_data == 0)
    return 0;

  /* 计算长度（valid_data 已是干净文本，且以 '\0' 结束） */
  while (valid_data[len] != '\0')
    len++;
  if (len <= 0)
    return 0;

  /* 统计逗号并记录位置 —— 需要恰好 6 个逗号（7 段） */
  for (i = 0; i < len && comma_cnt < 6; i++)
  {
    if (valid_data[i] == ',')
    {
      pos[comma_cnt++] = i;
    }
  }
  if (comma_cnt != 6)
    return 0;

  /* 切分出 7 段：f[0..6], flen[] 保存长度 */
  last = -1;
  for (i = 0; i < 6; i++)
  {
    f[i] = valid_data + last + 1;
    flen[i] = pos[i] - (last + 1);
    last = pos[i];
  }
  f[6] = valid_data + last + 1;
  flen[6] = len - (last + 1);

  /* 必须是 cmd == "3" */
  if (!(flen[0] == 1 && f[0][0] == '3'))
    return 0;

  /* 生成 floors_json：由 f[5] (floorsHex) 转换 */
  if (!FloorsHexToJsonArray_FromField(f[5], flen[5], floors_json, sizeof(floors_json)))
  {
    return 0;
  }

  /* 拼装最终 JSON：
     body.availableTimes 用 f[2]
     body.validTimeBegin 用 f[3]
     body.validTimeEnd   用 f[4]
     cmd 用 f[0]（字符串）
     id  用 f[1]（数值打印）
     publicKey 固定 "12345678"
   */
  /* 注意：为安全起见，字段拷贝到临时缓冲再使用（保证以 '\0' 结束） */
  {
    char at[24];    /* availableTimes 可能短 */
    char vtb[20];   /* 14位 */
    char vte[20];   /* 14位 */
    char cmd[4];    /* "3" */
    char idbuf[24]; /* id 字符串转印到 %ld 要注意范围，这里直接放到 JSON 用 %s 或者先 atol */

    char id_val[15] = {'\0'};
    int n, k = 0;

    /* 拷贝并补 '\0' */
    n = (flen[2] < (int)sizeof(at) - 1) ? flen[2] : (int)sizeof(at) - 1;
    memcpy(at, f[2], n);
    at[n] = '\0';

    n = (flen[3] < (int)sizeof(vtb) - 1) ? flen[3] : (int)sizeof(vtb) - 1;
    memcpy(vtb, f[3], n);
    vtb[n] = '\0';

    n = (flen[4] < (int)sizeof(vte) - 1) ? flen[4] : (int)sizeof(vte) - 1;
    memcpy(vte, f[4], n);
    vte[n] = '\0';

    n = (flen[0] < (int)sizeof(cmd) - 1) ? flen[0] : (int)sizeof(cmd) - 1;
    memcpy(cmd, f[0], n);
    cmd[n] = '\0';

    n = (flen[1] < (int)sizeof(idbuf) - 1) ? flen[1] : (int)sizeof(idbuf) - 1;
    memcpy(idbuf, f[1], n);
    idbuf[n] = '\0';

    /* id 转 long（也可以直接以字符串形式输出到 JSON：%s） */
    j = 0;
    k = 0;
    if (idbuf[0] == '-')
    {
      j = 1;
    }

    for (; j < n; j++)
    {
      id_val[k++] = idbuf[j];
    }

    id_val[k] = '\0';

    /* 组装 JSON 到 static 缓冲 */
    sprintf(json_out,
            "{"
            "\"body\":{"
            "\"availableTimes\":\"%s\","
            "\"floors\":%s,"
            "\"validTimeBegin\":\"%s\","
            "\"validTimeEnd\":\"%s\""
            "},"
            "\"cmd\":\"%s\","
            "\"id\":%s,"
            "\"publicKey\":\"%s\"" /* ★ 改这里 */
            "}",
            at, floors_json, vtb, vte, cmd, id_val,
            GetDevicePublicKey() /* ★ 取动态公钥 */
    );
  }

  return json_out;
}

// 二维码信息上报_命令3
char *BuildCvjJsonFromValidData3_1(const char *valid_data, char *haxi, const char *strid)
{
  static char json_out[1400]; /* 返回缓冲 */
  char floors_json[700];      /* 楼层数组缓冲 */
  const char *f[7];
  int flen[7];
  int pos[6];
  int len, i, comma_cnt, last;

  /* 变量提前声明 */
  len = 0;
  i = 0;
  comma_cnt = 0;
  last = -1;

  if (valid_data == 0)
    return 0;

  /* 计算长度（valid_data 已是干净文本，且以 '\0' 结束） */
  while (valid_data[len] != '\0')
    len++;
  if (len <= 0)
    return 0;

  /* 统计逗号并记录位置 —— 需要恰好 6 个逗号（7 段） */
  for (i = 0; i < len && comma_cnt < 6; i++)
  {
    if (valid_data[i] == ',')
    {
      pos[comma_cnt++] = i;
    }
  }
  if (comma_cnt != 6)
    return 0;

  /* 切分出 7 段：f[0..6], flen[] 保存长度 */
  last = -1;
  for (i = 0; i < 6; i++)
  {
    f[i] = valid_data + last + 1;
    flen[i] = pos[i] - (last + 1);
    last = pos[i];
  }
  f[6] = valid_data + last + 1;
  flen[6] = len - (last + 1);

  /* 必须是 cmd == "3" */
  if (!(flen[0] == 1 && f[0][0] == '3'))
    return 0;

  /* 生成 floors_json：由 f[5] (floorsHex) 转换 */
  if (!FloorsHexToJsonArray_FromField(f[5], flen[5], floors_json, sizeof(floors_json)))
  {
    return 0;
  }

  /* 拼装最终 JSON：
     body.availableTimes 用 f[2]
     body.validTimeBegin 用 f[3]
     body.validTimeEnd   用 f[4]
     cmd 用 f[0]（字符串）
     id  用 f[1]（数值打印）
     publicKey 固定 "12345678"
   */
  /* 注意：为安全起见，字段拷贝到临时缓冲再使用（保证以 '\0' 结束） */
  {
    char at[24];  /* availableTimes 可能短 */
    char vtb[20]; /* 14位 */
    char vte[20]; /* 14位 */
    char cmd[4];  /* "3" */

    int n = 0;

    /* 拷贝并补 '\0' */
    n = (flen[2] < (int)sizeof(at) - 1) ? flen[2] : (int)sizeof(at) - 1;
    memcpy(at, f[2], n);
    at[n] = '\0';

    n = (flen[3] < (int)sizeof(vtb) - 1) ? flen[3] : (int)sizeof(vtb) - 1;
    memcpy(vtb, f[3], n);
    vtb[n] = '\0';

    n = (flen[4] < (int)sizeof(vte) - 1) ? flen[4] : (int)sizeof(vte) - 1;
    memcpy(vte, f[4], n);
    vte[n] = '\0';

    n = (flen[0] < (int)sizeof(cmd) - 1) ? flen[0] : (int)sizeof(cmd) - 1;
    memcpy(cmd, f[0], n);
    cmd[n] = '\0';

    /* 组装 JSON 到 static 缓冲 */
    sprintf(json_out,
            "{"
            "\\\"body\\\":{"
            "\\\"availableTimes\\\":\\\"%s\\\","
            "\\\"floors\\\":%s,"
            "\\\"validTimeBegin\\\":\\\"%s\\\","
            "\\\"validTimeEnd\\\":\\\"%s\\\","
            "\\\"cmd\\\":\\\"%s\\\","
            "\\\"id\\\":%s,"
            "\\\"signature\\\":\\\"%s\\\""
            "}"
            "}",
            at, floors_json, vtb, vte, cmd, strid, haxi);
  }

  return json_out;
}

// 二维码信息读取_命令4
char *BuildCvjJsonFromValidData4(const char *valid_data)
{
  static char json_out[512];

  /* 切段用的变量（C90 块首声明） */
  int len;
  int i, k, j;
  int comma_cnt;
  int pos[4]; /* 我们需要前4个逗号位置：cmd,id,user,pass,pk */
  const char *f_cmd;
  int len_cmd;
  const char *f_id;
  int len_id;
  const char *f_user;
  int len_user;
  const char *f_pass;
  int len_pass;
  const char *f_pk;
  int len_pk;
  char id_val[15] = {'\0'};

  if (valid_data == 0)
    return 0;

  /* 计算长度 */
  len = 0;

  /* 找到前4个逗号（形成5段） */
  comma_cnt = 0;
  for (i = 0; i < len && comma_cnt < 4; i++)
  {
    if (valid_data[i] == ',')
      pos[comma_cnt++] = i;
  }

  if (comma_cnt < 4)
    return 0;

  /* 取 5 段：cmd, id, user, pass, pk */
  f_cmd = valid_data;
  len_cmd = pos[0] - 0;

  f_id = valid_data + pos[0] + 1;
  len_id = pos[1] - (pos[0] + 1);

  f_user = valid_data + pos[1] + 1;
  len_user = pos[2] - (pos[1] + 1);

  f_pass = valid_data + pos[2] + 1;
  len_pass = pos[3] - (pos[2] + 1);

  f_pk = valid_data + pos[3] + 1;
  len_pk = len - (pos[3] + 1);

  /* 校验 cmd== '4' */
  if (!(len_cmd == 1 && f_cmd[0] == '4'))
    return 0;

  /* id*/
  j = 0;
  k = 0;

  if (len_id > 0 && f_id[0] == '-')
  {
    j = 1;
  }
  for (; j < len_id; j++)
  {
    id_val[k++] = f_id[j];
  }

  id_val[k] = '\0';

  /* 拷贝字符串字段到临时缓冲（确保 '\0' 结尾） */
  {
    char user[40], pass[40], pk[40];
    int n;

    n = (len_user < (int)sizeof(user) - 1) ? len_user : (int)sizeof(user) - 1;
    for (i = 0; i < n; i++)
      user[i] = f_user[i];
    user[n] = '\0';

    n = (len_pass < (int)sizeof(pass) - 1) ? len_pass : (int)sizeof(pass) - 1;
    for (i = 0; i < n; i++)
      pass[i] = f_pass[i];
    pass[n] = '\0';

    n = (len_pk < (int)sizeof(pk) - 1) ? len_pk : (int)sizeof(pk) - 1;
    for (i = 0; i < n; i++)
      pk[i] = f_pk[i];
    pk[n] = '\0';

    /* 组装 JSON 文本（与你现有 BuildCvjJsonFromValidData3 一致的风格） */
    sprintf(json_out,
            "{"
            "\"body\":{"
            "\"password\":\"%s\","
            "\"publicKey\":\"%s\","
            "\"userName\":\"%s\""
            "},"
            "\"cmd\":\"4\","
            "\"id\":%s"
            "}",
            pass, pk, user, id_val);
  }

  return json_out;
}

// 二维码信息读取_命令6
char *BuildCvjJsonFromValidData6(const char *valid_data)
{
  static char json_out[1200];
  char ids_array[900];
  int len;
  int i;
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
  int p;
  int q;
  int k, j, t;
  char cmd_char; /* 保存 '1' 或 '2' */
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
          "\"id\":%ld,"
          "\"publicKey\":\"%s\"" /* ★ 这里改 */
          "}",
          ids_array, cmd_char, id_val,
          GetDevicePublicKey() /* ★ 动态取当前公钥 */
  );

  return json_out;
}

/* 去表尾 1 字节 + 去表头杂字节；中间原样保留，不分割 */
char *ExtractValidData_Bytes(const unsigned char *data, int len)
{
  static char out[512];
  int work_len;
  int start;
  int end;
  int i;
  int k;
  int first_digit;
  int tk_pos;
  unsigned char c;

  /* 调试：可打开观察 */
  /* printf("DBG len=%d, last=0x%02X\r\n", len, (len>0)?data[len-1]:0); */

  work_len = len;
  start = 0;
  end = 0;
  i = 0;
  k = 0;
  first_digit = -1;
  tk_pos = -1;

  if (data == NULL || work_len <= 1)
    return NULL;

  /* === 1) 无条件去掉表尾 1 字节（类型不定） === */
  // work_len -= 1; /* 现在有效范围是 [0 .. work_len-1] */

  /* === 2) 预扫描：找 “TK” 与 “第一个数字” === */
  for (i = 0; i < work_len; i++)
  {
    c = data[i];
    if (first_digit < 0 && c >= '0' && c <= '9')
      first_digit = i;
    if (i + 1 < work_len && data[i] == 'T' && data[i + 1] == 'K')
      tk_pos = i;
  }

  /* === 3) 选择起点 === */
  if (tk_pos >= 0)
  {
    start = tk_pos; /* 固件：从 'T' 开始 */
  }
  else if (first_digit >= 0)
  {
    start = first_digit; /* 二维码 CSV：从数字开始 */
  }
  else
  {
    /* 兜底：第一个可打印 ASCII (32..126) */
    start = 0;
    while (start < work_len)
    {
      unsigned char c = data[start];
      if (c >= 32 && c <= 126)
        break;
      start++;
    }
    if (start >= work_len)
      return NULL;
  }

  /* === 4) 末尾回退到最后一个可打印 ASCII (32..126) === */
  end = work_len - 1;
  while (end > start)
  {
    unsigned char c = data[end];
    if (c >= 32 && c <= 126)
      break;
    end--;
  }
  if (end < start)
    return NULL;

  /* === 5) 拷贝 [start..end] 到 out === */
  for (i = start, k = 0; i <= end && k < (int)sizeof(out) - 1; i++, k++)
  {
    out[k] = (char)data[i];
  }
  out[k] = '\0';

  /* === 6) 保险兜底：再强制去掉最后 1 个可见字符 ===
     若你传入的 len 没包含表尾多余字节，这里会把末尾 J/K 仍然去掉。*/
  /* === 6) 智能删除特定末尾字符 === */
  if (k > 0)
  {
    // 只删除特定的控制字符或不需要的字符
    char last_char = out[k - 1];
    if (last_char == 'J' || last_char == 'K' || last_char == '\a' || last_char < 32)
    {
      out[k - 1] = '\0';
      k--;
    }
  }

  return (k > 1) ? out : NULL;
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

/* 判断是否是 CVJ 格式的新指令
 * 规则：
 *   - 第一个字段必须是数字（命令号）
 *   - 后面必须至少有 2 个逗号（因为必须要有 cmd,id,其它...）
 *   - 不再要求逗号总数固定
 */
int IsCvjFormat(const char *s)
{
  int len, i, comma_cnt;

  if (s == NULL)
    return 0;

  /* 手动计算长度 */
  len = 0;
  while (s[len] != '\0')
    len++;
  if (len <= 0)
    return 0;

  /* 第一个字符必须是数字（命令号） */
  if (s[0] < '0' || s[0] > '9')
  {
    return 0;
  }
  if (s[1] != ',')
  {
    return 0;
  }

  /* 统计逗号数量 */
  comma_cnt = 0;
  for (i = 0; i < len; i++)
  {
    if (s[i] == ',')
    {
      comma_cnt++;
    }
  }

  /* 至少 2 个逗号：cmd,id,后续数据 */
  if (comma_cnt < 2)
  {
    return 0;
  }

  return 1; /* 满足条件就是 CVJ 格式的新指令 */
}

char IsCmd(const char *valid_data)
{
  int i;
  if (valid_data == 0)
    return 0;

  /* 第一个字段在第一个逗号之前 */
  for (i = 0; valid_data[i] != '\0'; i++)
  {
    if (valid_data[i] == ',')
      break; /* 遇到逗号，结束命令字段 */
  }

  return valid_data[0];
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

/* 大小写不敏感的 tolower，仅处理 A-F */
static char toLowerHex(char c)
{
  if (c >= 'A' && c <= 'F')
    return (char)(c - 'A' + 'a');
  return c;
}

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
  printf("sig_field = %s\n", sig_field);
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

/* ===== 将 DS3231_Time 组装为 14 位时间戳 YYYYMMDDhhmmss ===== */
static void MakeTimestamp14FromDS3231(const DS3231_Time *t, char out14[15])
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

/* ===== 从 valid_data 中提取第4/5段（validBegin/validEnd），并做区间判断 =====
   期望 valid_data 形如：
   3,2147483645,-1,20250101000000,20251231235959,81FFFFFFFFFFFFFFFFFFFF,59a474ecf7
   返回：1= begin<=now<=end；0=不在区间或解析失败
*/
int CheckValidPeriod_WithNow(const char *valid_data, const DS3231_Time *now)
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
  MakeTimestamp14FromDS3231(now, now14);

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

// 按字节反转十六进制字符串（每2个字符为一个单位）
void reverse_hex_by_bytes(char *hex_str)
{
  int len = strlen(hex_str);
  int i;
  char temp;

  if (len % 2 != 0)
    return; // 必须是偶数长度

  // 交换字节位置
  for (i = 0; i < len / 2; i += 2)
  {
    // 交换第i字节和第(len-2-i)字节
    temp = hex_str[i];
    hex_str[i] = hex_str[len - 2 - i];
    hex_str[len - 2 - i] = temp;

    temp = hex_str[i + 1];
    hex_str[i + 1] = hex_str[len - 1 - i];
    hex_str[len - 1 - i] = temp;
  }
}

// 十六进制字符转换为数值
unsigned char hex_char_to_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return 0xFF; // 无效字符
}

int hex_string_to_bytes(const char *hex_str, unsigned char *bytes, int max_bytes)
{
  int len = strlen(hex_str);
  int i;

  if (len % 2 != 0 || len / 2 > max_bytes)
  {
    printf("错误: 十六进制字符串长度无效\n");
    return 0;
  }

  for (i = 0; i < len; i += 2)
  {
    // 直接计算十六进制值，避免复杂的转换
    unsigned char hi, lo;

    // 处理高4位
    if (hex_str[i] >= '0' && hex_str[i] <= '9')
    {
      hi = hex_str[i] - '0';
    }
    else if (hex_str[i] >= 'A' && hex_str[i] <= 'F')
    {
      hi = hex_str[i] - 'A' + 10;
    }
    else if (hex_str[i] >= 'a' && hex_str[i] <= 'f')
    {
      hi = hex_str[i] - 'a' + 10;
    }
    else
    {
      printf("错误: 无效十六进制字符 '%c'\n", hex_str[i]);
      return 0;
    }

    // 处理低4位
    if (hex_str[i + 1] >= '0' && hex_str[i + 1] <= '9')
    {
      lo = hex_str[i + 1] - '0';
    }
    else if (hex_str[i + 1] >= 'A' && hex_str[i + 1] <= 'F')
    {
      lo = hex_str[i + 1] - 'A' + 10;
    }
    else if (hex_str[i + 1] >= 'a' && hex_str[i + 1] <= 'f')
    {
      lo = hex_str[i + 1] - 'a' + 10;
    }
    else
    {
      // printf("错误: 无效十六进制字符 '%c'\n", hex_str[i + 1]);
      return 0;
    }

    bytes[i / 2] = (hi << 4) | lo;
  }

  return len / 2;
}

void build_26byte_packet(const char *hex_data, const char *hex_chardan, unsigned char device_id, unsigned char *output)
{
  int pos = 0;
  unsigned char fixed_data[] = {0x0A, 0xCC, 0x01, 0x0F, 0x02, 0x01, 0x00, 0x00, 0x00, 0x05};
  unsigned char data_bytes[11];
  unsigned char chardan_bytes[2];
  int data_len;
  int chardan_len;
  int i;
  char hex_data_temp[50] = {0};
  // 1. 7E 1A - 固定包头
  output[pos++] = 0x7E;
  output[pos++] = 0x1A;

  // 2. 两个字节的单元地址
  chardan_len = hex_string_to_bytes(hex_chardan, chardan_bytes, 2);
  if (chardan_len == 2)
  {
    output[pos++] = chardan_bytes[0];
    output[pos++] = chardan_bytes[1];
    // printf("单元地址: %02X %02X\n", chardan_bytes[0], chardan_bytes[1]);
  }
  else
  {
    output[pos++] = 0xFF;
    output[pos++] = 0xFF;
    // printf("使用默认单元地址: FF FF\n");
  }

  // 3. 一个字节的目标设备号
  output[pos++] = device_id;
  // printf("设备号: %02X\n", device_id);

  // 4. 固定数据
  memcpy(output + pos, fixed_data, sizeof(fixed_data));
  pos += sizeof(fixed_data);
  // printf("固定数据: ");
  for (i = 0; i < sizeof(fixed_data); i++)
  {
    printf("%02X ", fixed_data[i]);
  }
  printf("\n");

  // 5. hex_data的11个字节
  printf("原始hex_data: %s\n", hex_data);
  // 创建副本以避免修改原始数据

  if (hex_data)
  {
    strncpy(hex_data_temp, hex_data, sizeof(hex_data_temp) - 1);
  }

  data_len = hex_string_to_bytes(hex_data_temp, data_bytes, 11);
  //    data_len = hex_string_to_bytes(hex_data, data_bytes, 11);

  if (data_len == 11)
  {
    printf("转换后的数据字节: ");
    for (i = 0; i < 11; i++)
    {
      printf("%02X ", data_bytes[i]);
    }
    printf("\n");

    memcpy(output + pos, data_bytes, 11);
    pos += 11;
  }
  else
  {
    printf("hex_data转换失败，使用默认值FF\n");
    memset(output + pos, 0xFF, 11);
    pos += 11;
  }
}

// 打印字节数组
void print_hex_array(const unsigned char *data, int length)
{
  int i;
  for (i = 0; i < length; i++)
  {
    printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0)
      printf("\n");
  }
  printf("\n");
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

/* HEX字符 -> 数值 */
int hex2val_local(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

/* 22个HEX字符 -> 7字节数组 */
int hex14_to_bytes7(const char *hex14, uint8_t out7[7])
{
  int i, hi, lo;

  if (hex14 == 0 || out7 == 0)
    return 0;

  for (i = 0; i < 7; i++)
  {
    hi = hex2val_local(hex14[i * 2]);
    lo = hex2val_local(hex14[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return 0;
    out7[i] = (uint8_t)((hi << 4) | lo);
  }

  return 1;
}

/* 按字节反转 */
void reverse_bytes(char *hex, int hex_len)
{
    int i;
    char t0, t1;

    for (i = 0; i < hex_len / 4; i++)
    {
        t0 = hex[i*2];
        t1 = hex[i*2 + 1];

        hex[i*2]               = hex[hex_len - 2 - i*2];
        hex[i*2 + 1]           = hex[hex_len - 1 - i*2];

        hex[hex_len - 2 - i*2] = t0;
        hex[hex_len - 1 - i*2] = t1;
    }
}


/* 对 buf[0..len-1] 全部字节做和，返回结果 */
uint8_t sum_checksum(const uint8_t *data, uint16_t len)
{
  uint16_t i;
  uint16_t sum = 0;

  for (i = 0; i < len; i++)
  {
    sum += data[i];
  }

  return (uint8_t)(sum & 0xFF); // 取低 8 位作为校验值
}

/* ===== 命令4：设备侧账号/密码/公钥（RAM中保存字符串） ===== */

/* 设置/获取/校验 */
void SetDeviceCredentials(const char *user, const char *pass)
{
  int n;
  if (user)
  {
    n = 0;
    while (user[n] != '\0' && n < (int)sizeof(g_device_user) - 1)
    {
      g_device_user[n] = user[n];
      n++;
    }
    g_device_user[n] = '\0';
  }
  if (pass)
  {
    n = 0;
    while (pass[n] != '\0' && n < (int)sizeof(g_device_pass) - 1)
    {
      g_device_pass[n] = pass[n];
      n++;
    }
    g_device_pass[n] = '\0';
  }
}

int VerifyDeviceCredentials(const char *user, const char *pass)
{
  int ok1, ok2, i;
  if (!user || !pass)
    return 0;

  /* strcmp 等价实现（避免库差异），完全匹配 */
  i = 0;
  while (g_device_user[i] == user[i] && g_device_user[i] != '\0')
    i++;
  ok1 = (g_device_user[i] == user[i]);

  i = 0;
  while (g_device_pass[i] == pass[i] && g_device_pass[i] != '\0')
    i++;
  ok2 = (g_device_pass[i] == pass[i]);

  return (ok1 && ok2) ? 1 : 0;
}

// 密钥更改
void SetDevicePublicKey(const char *pk)
{
  int n;
  if (!pk)
    return;
  n = 0;
  while (pk[n] != '\0' && n < (int)sizeof(g_device_publicKey) - 1)
  {
    g_device_publicKey[n] = pk[n];
    n++;
  }
  g_device_publicKey[n] = '\0';

  // 密钥存储
  WriteKey(pk);
}

const char *GetDevicePublicKey(void)
{
  return g_device_publicKey;
}
