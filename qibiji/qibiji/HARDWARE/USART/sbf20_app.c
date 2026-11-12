#include "sbf20_app.h"
#include "sbf20.h"
#include "usart.h"
#include "reg.h"
#include "24cxx.h"
#include "uart3_at_sequence.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define SBF20_APP_JSON_MAX       512u
#define SBF20_EEPROM_SAMPLE_ADDR   7u
#define SBF20_EEPROM_UPLOAD_ADDR   8u
#define SBF20_SAMPLE_MIN           1u
#define SBF20_SAMPLE_MAX           86400u

typedef enum
{
  SBF20_APP_CODE_OK = 0,
  SBF20_APP_CODE_PARAM = 1,
  SBF20_APP_CODE_FLASH = 2,
  SBF20_APP_CODE_UNKNOWN = 3,
  SBF20_APP_CODE_JSON = 4,
} sbf20_app_code_t;

typedef struct
{
  u8 rx_buf[SBF20_APP_JSON_MAX];
  u16 rx_len;
  u8 last_rx[SBF20_APP_JSON_MAX + 1u];
  u16 last_rx_len;
  u8 tx_buf[SBF20_APP_JSON_MAX];
  u16 tx_len;
  u16 tx_off;
  u8 is_read;
  u8 req_done;
  u8 biz_done;
  u16 sample_interval;
  u16 upload_period;
} sbf20_app_ctx_t;

static sbf20_app_ctx_t s_app;

extern u16 eeprom_buf[eepromLength];

static void sbf20_app_reset_rx(void)
{
  s_app.rx_len = 0u;
}

static void sbf20_app_reset_tx(void)
{
  s_app.tx_len = 0u;
  s_app.tx_off = 0u;
  s_app.is_read = 0u;
  s_app.req_done = 0u;
  s_app.biz_done = 0u;
}

static u8 sbf20_app_append_rx(const u8 *pld, u8 plen)
{
  if (plen == 0u)
    return 1u;
  if ((u32)s_app.rx_len + plen > SBF20_APP_JSON_MAX)
    return 0u;
  memcpy(&s_app.rx_buf[s_app.rx_len], pld, plen);
  s_app.rx_len = (u16)(s_app.rx_len + plen);
  return 1u;
}

static u8 sbf20_app_has_op(const u8 *buf, u16 len, const char *tag, u16 tag_len)
{
  u16 i;

  if (len < tag_len)
    return 0u;

  for (i = 0u; i <= len - tag_len; i++)
  {
    if (memcmp(&buf[i], tag, tag_len) == 0)
      return 1u;
  }
  return 0u;
}

static u8 sbf20_app_is_read_req(const u8 *buf, u16 len)
{
  static const char tag[] = "\"op\":\"read\"";
  return sbf20_app_has_op(buf, len, tag, (u16)(sizeof(tag) - 1u));
}

static u8 sbf20_app_is_write_req(const u8 *buf, u16 len)
{
  static const char tag[] = "\"op\":\"write\"";
  return sbf20_app_has_op(buf, len, tag, (u16)(sizeof(tag) - 1u));
}

static u8 sbf20_app_json_complete(const u8 *buf, u16 len, const char *tag, u16 tag_len)
{
  u16 depth = 0u;
  u16 i;

  if (len == 0u)
    return 0u;
  if (!sbf20_app_has_op(buf, len, tag, tag_len))
    return 0u;

  for (i = 0u; i < len; i++)
  {
    if (buf[i] == '{')
      depth++;
    else if (buf[i] == '}')
    {
      if (depth > 0u)
        depth--;
      if (depth == 0u)
        return 1u;
    }
  }
  return 0u;
}

static void sbf20_app_load_from_eeprom(void)
{
  if (eeprom_buf[SBF20_EEPROM_SAMPLE_ADDR] >= SBF20_SAMPLE_MIN &&
      eeprom_buf[SBF20_EEPROM_SAMPLE_ADDR] <= SBF20_SAMPLE_MAX)
  {
    s_app.sample_interval = eeprom_buf[SBF20_EEPROM_SAMPLE_ADDR];
    armReg.reportT = s_app.sample_interval;
  }
  else if (armReg.reportT >= SBF20_SAMPLE_MIN && armReg.reportT <= SBF20_SAMPLE_MAX)
  {
    s_app.sample_interval = armReg.reportT;
  }
  else
  {
    s_app.sample_interval = 30u;
    armReg.reportT = 30u;
  }

  if (eeprom_buf[SBF20_EEPROM_UPLOAD_ADDR] >= SBF20_SAMPLE_MIN &&
      eeprom_buf[SBF20_EEPROM_UPLOAD_ADDR] <= SBF20_SAMPLE_MAX)
    s_app.upload_period = eeprom_buf[SBF20_EEPROM_UPLOAD_ADDR];
  else
    s_app.upload_period = (u16)(s_app.sample_interval * 2u);
}

static void sbf20_app_log_tx_rsp(void);

static void sbf20_app_build_write_rsp(sbf20_app_code_t code, const char *message)
{
  int n;

  if (code == SBF20_APP_CODE_OK)
    n = snprintf((char *)s_app.tx_buf, SBF20_APP_JSON_MAX, "{\"code\":0}");
  else if (message != NULL && message[0] != '\0')
    n = snprintf((char *)s_app.tx_buf, SBF20_APP_JSON_MAX,
                 "{\"code\":%u,\"message\":\"%s\"}", (unsigned)code, message);
  else
    n = snprintf((char *)s_app.tx_buf, SBF20_APP_JSON_MAX,
                 "{\"code\":%u}", (unsigned)code);

  if (n <= 0)
    s_app.tx_len = 0u;
  else
    s_app.tx_len = (u16)((n > (int)SBF20_APP_JSON_MAX) ? SBF20_APP_JSON_MAX : n);
  s_app.tx_off = 0u;
  sbf20_app_log_tx_rsp();
}

/* 解析 JSON 字符串：最多 MQTT_KEY_EEPROM_LEN(32) 字符；out 容量须 >= MQTT_KEY_STR_LEN */
static u8 sbf20_app_json_str_ok(const cJSON *item, char *out, u16 out_cap)
{
  size_t n;

  if (!cJSON_IsString(item) || item->valuestring == NULL || out == NULL ||
      out_cap < MQTT_KEY_STR_LEN)
    return 0u;

  n = strlen(item->valuestring);
  if (n > (size_t)MQTT_KEY_EEPROM_LEN)
    return 0u;

  memset(out, 0, out_cap);
  if (n > 0u)
    memcpy(out, item->valuestring, n);
  return 1u;
}

/* 将 MQTT 字符串写入 EEPROM（满 32 字节），并同步全局变量 */
static u8 sbf20_app_persist_mqtt_strs(const char *v1, const char *v2,
                                      const char *v3, const char *v4,
                                      u8 set1, u8 set2, u8 set3, u8 set4)
{
  char buf[MQTT_KEY_EEPROM_LEN];

  if (AT24CXX_Check() != 0u)
    return 0u;

  if (set1)
  {
    memset(buf, 0, sizeof(buf));
    memcpy(buf, v1, MQTT_KEY_EEPROM_LEN);
    AT24CXX_Write(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)buf, MQTT_KEY_EEPROM_LEN);
    memset(g_mqtt_productKey, 0, sizeof(g_mqtt_productKey));
    memcpy(g_mqtt_productKey, buf, MQTT_KEY_EEPROM_LEN);
    g_mqtt_productKey[MQTT_KEY_EEPROM_LEN] = '\0';
  }
  if (set2)
  {
    memset(buf, 0, sizeof(buf));
    memcpy(buf, v2, MQTT_KEY_EEPROM_LEN);
    AT24CXX_Write(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)buf, MQTT_KEY_EEPROM_LEN);
    memset(g_mqtt_deviceKey, 0, sizeof(g_mqtt_deviceKey));
    memcpy(g_mqtt_deviceKey, buf, MQTT_KEY_EEPROM_LEN);
    g_mqtt_deviceKey[MQTT_KEY_EEPROM_LEN] = '\0';
  }
  if (set3)
  {
    memset(buf, 0, sizeof(buf));
    memcpy(buf, v3, MQTT_KEY_EEPROM_LEN);
    AT24CXX_Write(MQTT_DEVICESECRET_BYTE_ADDR, (u8 *)buf, MQTT_KEY_EEPROM_LEN);
    memset(g_mqtt_deviceSecret, 0, sizeof(g_mqtt_deviceSecret));
    memcpy(g_mqtt_deviceSecret, buf, MQTT_KEY_EEPROM_LEN);
    g_mqtt_deviceSecret[MQTT_KEY_EEPROM_LEN] = '\0';
  }
  if (set4)
  {
    memset(buf, 0, sizeof(buf));
    memcpy(buf, v4, MQTT_KEY_EEPROM_LEN);
    AT24CXX_Write(MQTT_MQTTADDR_BYTE_ADDR, (u8 *)buf, MQTT_KEY_EEPROM_LEN);
    memset(g_mqtt_addr, 0, sizeof(g_mqtt_addr));
    memcpy(g_mqtt_addr, buf, MQTT_KEY_EEPROM_LEN);
    g_mqtt_addr[MQTT_KEY_EEPROM_LEN] = '\0';
  }
  return 1u;
}

/* 蓝牙写参数：解析 productKey/deviceKey/deviceSecret/mqtt_addr 字符串并写入 EEPROM */
static void sbf20_app_process_write(void)
{
  cJSON *root;
  cJSON *op;
  cJSON *params;
  cJSON *item;
  char v1[MQTT_KEY_STR_LEN];
  char v2[MQTT_KEY_STR_LEN];
  char v3[MQTT_KEY_STR_LEN];
  char v4[MQTT_KEY_STR_LEN];
  u8 set1 = 0u, set2 = 0u, set3 = 0u, set4 = 0u;

  memset(v1, 0, sizeof(v1));
  memset(v2, 0, sizeof(v2));
  memset(v3, 0, sizeof(v3));
  memset(v4, 0, sizeof(v4));

  root = cJSON_ParseWithLengthOpts((char *)s_app.rx_buf, s_app.rx_len, NULL, 0);
  if (root == NULL)
  {
    sbf20_app_build_write_rsp(SBF20_APP_CODE_JSON, NULL);
    return;
  }

  op = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (!cJSON_IsString(op) || op->valuestring == NULL ||
      strcmp(op->valuestring, "write") != 0)
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_UNKNOWN, NULL);
    return;
  }

  params = cJSON_GetObjectItemCaseSensitive(root, "params");
  if (!cJSON_IsObject(params))
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
    return;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "productKey");
  if (item != NULL)
  {
    if (!sbf20_app_json_str_ok(item, v1, MQTT_KEY_STR_LEN))
    {
      cJSON_Delete(root);
      sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
      return;
    }
    set1 = 1u;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "deviceKey");
  if (item != NULL)
  {
    if (!sbf20_app_json_str_ok(item, v2, MQTT_KEY_STR_LEN))
    {
      cJSON_Delete(root);
      sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
      return;
    }
    set2 = 1u;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "deviceSecret");
  if (item != NULL)
  {
    if (!sbf20_app_json_str_ok(item, v3, MQTT_KEY_STR_LEN))
    {
      cJSON_Delete(root);
      sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
      return;
    }
    set3 = 1u;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "mqtt_addr");
  if (item != NULL)
  {
    if (!sbf20_app_json_str_ok(item, v4, MQTT_KEY_STR_LEN))
    {
      cJSON_Delete(root);
      sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
      return;
    }
    set4 = 1u;
  }

  if ((set1 | set2 | set3 | set4) == 0u)
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_UNKNOWN, NULL);
    return;
  }

  cJSON_Delete(root);

  if (!sbf20_app_persist_mqtt_strs(v1, v2, v3, v4, set1, set2, set3, set4))
  {
    sbf20_app_build_write_rsp(SBF20_APP_CODE_FLASH, "flash_busy");
    return;
  }

  sbf20_app_build_write_rsp(SBF20_APP_CODE_OK, NULL);
  uart3_at_sequence_request_set(1);
}

static u8 sbf20_app_config_has_unknown(const cJSON *configs)
{
  cJSON *item;
  u8 i;
  u8 size;

  if (!cJSON_IsArray(configs))
    return 0u;

  size = (u8)cJSON_GetArraySize(configs);
  if (size == 0u)
    return 0u;

  for (i = 0u; i < size; i++)
  {
    item = cJSON_GetArrayItem(configs, i);
    if (!cJSON_IsString(item) || item->valuestring == NULL)
      return 1u;
    if (strcmp(item->valuestring, "groupA") != 0 &&
        strcmp(item->valuestring, "groupB") != 0)
      return 1u;
  }
  return 0u;
}

/* 解析 configs：置位 need_a / need_b；缺省或空数组视为两组都要 */
// static void sbf20_app_config_parse_groups(const cJSON *configs, u8 *need_a, u8 *need_b)
// {
//   cJSON *item;
//   u8 i;
//   u8 size;

//   *need_a = 0u;
//   *need_b = 0u;

//   if (configs == NULL || !cJSON_IsArray(configs))
//   {
//     *need_a = 1u;
//     *need_b = 1u;
//     return;
//   }

//   size = (u8)cJSON_GetArraySize(configs);
//   if (size == 0u)
//   {
//     *need_a = 1u;
//     *need_b = 1u;
//     return;
//   }

//   for (i = 0u; i < size; i++)
//   {
//     item = cJSON_GetArrayItem(configs, i);
//     if (!cJSON_IsString(item) || item->valuestring == NULL)
//       continue;
//     if (strcmp(item->valuestring, "groupA") == 0)
//       *need_a = 1u;
//     else if (strcmp(item->valuestring, "groupB") == 0)
//       *need_b = 1u;
//   }
// }

/* 组装读应答：v1~v4 为 MQTT 字符串，最多 32 字符 */
static void sbf20_app_build_read_rsp(void)
{
  char v1[MQTT_KEY_STR_LEN];
  char v2[MQTT_KEY_STR_LEN];
  char v3[MQTT_KEY_STR_LEN];
  char v4[MQTT_KEY_STR_LEN];
  int n;

  memset(v1, 0, sizeof(v1));
  memset(v2, 0, sizeof(v2));
  memset(v3, 0, sizeof(v3));
  memset(v4, 0, sizeof(v4));

  AT24CXX_Read(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)v1, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Read(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)v2, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Read(MQTT_DEVICESECRET_BYTE_ADDR, (u8 *)v3, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Read(MQTT_MQTTADDR_BYTE_ADDR, (u8 *)v4, MQTT_KEY_EEPROM_LEN);
  /* '\0' 放在第 33 字节，勿覆盖第 32 个有效字符 */
  v1[MQTT_KEY_EEPROM_LEN] = '\0';
  v2[MQTT_KEY_EEPROM_LEN] = '\0';
  v3[MQTT_KEY_EEPROM_LEN] = '\0';
  v4[MQTT_KEY_EEPROM_LEN] = '\0';

  /* 未写入的 EEPROM 常为 0xFF，当作空串 */
  if ((u8)v1[0] == 0xFFu)
    v1[0] = '\0';
  if ((u8)v2[0] == 0xFFu)
    v2[0] = '\0';
  if ((u8)v3[0] == 0xFFu)
    v3[0] = '\0';
  if ((u8)v4[0] == 0xFFu)
    v4[0] = '\0';

  n = snprintf((char *)s_app.tx_buf, SBF20_APP_JSON_MAX,
               "{\"code\":0,\"params\":{"
               "\"productKey\":\"%s\","
               "\"deviceKey\":\"%s\","
               "\"deviceSecret\":\"%s\","
               "\"mqtt_addr\":\"%s\"}}",
               v1, v2, v3, v4);

  if (n <= 0)
    s_app.tx_len = 0u;
  else
    s_app.tx_len = (u16)((n > (int)SBF20_APP_JSON_MAX) ? SBF20_APP_JSON_MAX : n);
  s_app.tx_off = 0u;
  sbf20_app_log_tx_rsp();
}

static void sbf20_app_process_read(void)
{
  cJSON *root;
  cJSON *op;

  root = cJSON_ParseWithLengthOpts((char *)s_app.rx_buf, s_app.rx_len, NULL, 0);
  if (root == NULL)
  {
    sbf20_app_build_write_rsp(SBF20_APP_CODE_JSON, NULL);
    return;
  }

  op = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (!cJSON_IsString(op) || op->valuestring == NULL ||
      strcmp(op->valuestring, "read") != 0)
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_UNKNOWN, NULL);
    return;
  }

  cJSON_Delete(root);
  sbf20_app_build_read_rsp();
}

static u8 sbf20_app_read_req_complete(void)
{
  static const char tag[] = "\"op\":\"read\"";
  return sbf20_app_json_complete(s_app.rx_buf, s_app.rx_len, tag, (u16)(sizeof(tag) - 1u));
}

static u8 sbf20_app_write_req_complete(void)
{
  static const char tag[] = "\"op\":\"write\"";
  return sbf20_app_json_complete(s_app.rx_buf, s_app.rx_len, tag, (u16)(sizeof(tag) - 1u));
}

static void sbf20_app_log_rx_req(void)
{
  printf("[SBF20 RX %u] ", (unsigned)s_app.rx_len);
  if (s_app.rx_len > 0u)
    Usart1_Send((char *)s_app.rx_buf, s_app.rx_len);
  printf("\r\n");
}

/* 蓝牙回执 JSON 经串口1输出，供上位机联调 */
static void sbf20_app_log_tx_rsp(void)
{
  printf("[SBF20 TX %u] ", (unsigned)s_app.tx_len);
  if (s_app.tx_len > 0u)
    Usart1_Send((char *)s_app.tx_buf, s_app.tx_len);
  printf("\r\n");
}

static u8 sbf20_app_read_req_ready(u8 req_more)
{
  return (u8)(req_more == 0u || sbf20_app_read_req_complete());
}

static u8 sbf20_app_write_req_ready(u8 req_more)
{
  return (u8)(req_more == 0u || sbf20_app_write_req_complete());
}

static u8 sbf20_app_copy_result_chunk(u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  u16 remain;
  u8 chunk;

  remain = (u16)(s_app.tx_len - s_app.tx_off);
  if (remain == 0u)
  {
    *rsp_len = 0u;
    return 0u;
  }

  chunk = (remain > rsp_max) ? rsp_max : (u8)remain;
  memcpy(rsp_pld, &s_app.tx_buf[s_app.tx_off], chunk);
  s_app.tx_off = (u16)(s_app.tx_off + chunk);
  *rsp_len = chunk;
  return (s_app.tx_off < s_app.tx_len) ? 1u : 0u;
}

static u8 sbf20_app_read_rsp_chunk(u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  return sbf20_app_copy_result_chunk(rsp_pld, rsp_len, rsp_max);
}

static u8 sbf20_app_read_begin_rsp(u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  if (!s_app.biz_done)
  {
    s_app.biz_done = 1u;
    sbf20_app_log_rx_req();
    sbf20_app_process_read();
  }
  return sbf20_app_read_rsp_chunk(rsp_pld, rsp_len, rsp_max);
}

static u8 sbf20_app_read_pair(
    u8 req_more, u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  if (!s_app.req_done)
  {
    if (!sbf20_app_read_req_ready(req_more))
    {
      *rsp_len = 0u;
      return 1u;
    }
    s_app.req_done = 1u;
    return sbf20_app_read_begin_rsp(rsp_pld, rsp_len, rsp_max);
  }

  if (s_app.tx_off < s_app.tx_len)
    return sbf20_app_read_rsp_chunk(rsp_pld, rsp_len, rsp_max);

  *rsp_len = 0u;
  return 0u;
}

static u8 sbf20_app_write_rsp(
    u8 req_more, u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  if (!sbf20_app_write_req_ready(req_more))
  {
    *rsp_len = 0u;
    return 1u;
  }

  if (req_more != 0u)
  {
    *rsp_len = 0u;
    return 1u;
  }

  if (!s_app.biz_done)
  {
    s_app.biz_done = 1u;
    sbf20_app_log_rx_req();
    sbf20_app_process_write();
  }

  if (s_app.tx_len == 0u || rsp_max == 0u)
  {
    *rsp_len = 0u;
    return 0u;
  }

  return sbf20_app_copy_result_chunk(rsp_pld, rsp_len, rsp_max);
}

static u8 sbf20_app_overflow_rsp(u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  static const char err[] = "{\"code\":1}";
  u8 len = (u8)(sizeof(err) - 1u);

  if (rsp_max < len)
  {
    *rsp_len = 0u;
    return 0u;
  }
  memcpy(rsp_pld, err, len);
  *rsp_len = len;
  return 0u;
}

static u8 sbf20_app_pair_handler(
    u8 index, u8 req_more,
    const u8 *req_pld, u8 req_len,
    u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  if (index == 0u)
  {
    sbf20_app_reset_rx();
    sbf20_app_reset_tx();
  }

  if (!sbf20_app_append_rx(req_pld, req_len))
  {
    sbf20_app_reset_rx();
    sbf20_app_reset_tx();
    return sbf20_app_overflow_rsp(rsp_pld, rsp_len, rsp_max);
  }

  if (sbf20_app_is_read_req(s_app.rx_buf, s_app.rx_len))
    s_app.is_read = 1u;

  if (s_app.is_read && !sbf20_app_is_write_req(s_app.rx_buf, s_app.rx_len))
    return sbf20_app_read_pair(req_more, rsp_pld, rsp_len, rsp_max);

  return sbf20_app_write_rsp(req_more, rsp_pld, rsp_len, rsp_max);
}

static void sbf20_app_session_done(const u8 *req, u16 len)
{
  u16 copy_len = len;

  if (copy_len > SBF20_APP_JSON_MAX)
    copy_len = SBF20_APP_JSON_MAX;

  s_app.last_rx_len = copy_len;
  if (copy_len > 0u)
    memcpy(s_app.last_rx, req, copy_len);
  s_app.last_rx[copy_len] = '\0';
}

void sbf20_app_init(void)
{
  memset(&s_app, 0, sizeof(s_app));
  sbf20_app_load_from_eeprom();
  sbf20_init(sbf20_app_pair_handler);
  sbf20_set_session_done_cb(sbf20_app_session_done);
  printf("[SBF20] app v2.0 sample=%u upload=%u\r\n",
         (unsigned)s_app.sample_interval, (unsigned)s_app.upload_period);
}

const u8 *sbf20_app_last_rx_json(void)
{
  return s_app.last_rx;
}

u16 sbf20_app_last_rx_json_len(void)
{
  return s_app.last_rx_len;
}
