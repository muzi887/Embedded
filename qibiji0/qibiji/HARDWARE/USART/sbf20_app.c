#include "sbf20_app.h"
#include "sbf20.h"
#include "usart.h"
#include "reg.h"
#include "24cxx.h"
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

static u8 sbf20_app_persist_intervals(void)
{
  if (AT24CXX_Check() != 0u)
    return 0u;

  AT24CXX_WriteWord(SBF20_EEPROM_SAMPLE_ADDR, s_app.sample_interval);
  AT24CXX_WriteWord(SBF20_EEPROM_UPLOAD_ADDR, s_app.upload_period);
  eeprom_buf[SBF20_EEPROM_SAMPLE_ADDR] = s_app.sample_interval;
  eeprom_buf[SBF20_EEPROM_UPLOAD_ADDR] = s_app.upload_period;
  armReg.reportT = s_app.sample_interval;
  return 1u;
}

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
}

static u8 sbf20_app_json_number_ok(const cJSON *item, int *out)
{
  double v;

  if (!cJSON_IsNumber(item))
    return 0u;

  v = cJSON_GetNumberValue(item);
  if (v < (double)SBF20_SAMPLE_MIN || v > (double)SBF20_SAMPLE_MAX)
    return 0u;

  *out = (int)v;
  return 1u;
}

static void sbf20_app_process_write(void)
{
  cJSON *root;
  cJSON *op;
  cJSON *params;
  cJSON *item;
  u8 has_known = 0u;
  int sample = 0;
  int upload = 0;
  u8 sample_set = 0u;
  u8 upload_set = 0u;

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

  item = cJSON_GetObjectItemCaseSensitive(params, "sampleInterval");
  if (item != NULL)
  {
    if (!sbf20_app_json_number_ok(item, &sample))
    {
      cJSON_Delete(root);
      sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
      return;
    }
    has_known = 1u;
    sample_set = 1u;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "uploadPeriod");
  if (item != NULL)
  {
    if (!sbf20_app_json_number_ok(item, &upload))
    {
      cJSON_Delete(root);
      sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
      return;
    }
    has_known = 1u;
    upload_set = 1u;
  }

  if (!has_known)
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_UNKNOWN, NULL);
    return;
  }

  if (sample_set)
    s_app.sample_interval = (u16)sample;
  if (upload_set)
    s_app.upload_period = (u16)upload;
  else if (sample_set)
    s_app.upload_period = (u16)(s_app.sample_interval * 2u);

  cJSON_Delete(root);

  if (!sbf20_app_persist_intervals())
  {
    sbf20_app_build_write_rsp(SBF20_APP_CODE_FLASH, "flash_busy");
    return;
  }

  sbf20_app_build_write_rsp(SBF20_APP_CODE_OK, NULL);
}

static u8 sbf20_app_config_wants_sampling(const cJSON *configs)
{
  cJSON *item;
  u8 i;
  u8 size;

  if (!cJSON_IsArray(configs))
    return 0u;

  size = (u8)cJSON_GetArraySize(configs);
  if (size == 0u)
    return 1u;

  for (i = 0u; i < size; i++)
  {
    item = cJSON_GetArrayItem(configs, i);
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        strcmp(item->valuestring, "samplingConfig") == 0)
      return 1u;
  }
  return 0u;
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
    if (strcmp(item->valuestring, "samplingConfig") != 0)
      return 1u;
  }
  return 0u;
}

static void sbf20_app_process_read(void)
{
  cJSON *root;
  cJSON *op;
  cJSON *configs;
  int n;

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

  configs = cJSON_GetObjectItemCaseSensitive(root, "configs");
  if (configs != NULL && !cJSON_IsArray(configs))
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_PARAM, NULL);
    return;
  }

  if (configs != NULL && sbf20_app_config_has_unknown(configs))
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_UNKNOWN, NULL);
    return;
  }

  if (configs != NULL && !sbf20_app_config_wants_sampling(configs))
  {
    cJSON_Delete(root);
    sbf20_app_build_write_rsp(SBF20_APP_CODE_UNKNOWN, NULL);
    return;
  }

  cJSON_Delete(root);

  n = snprintf((char *)s_app.tx_buf, SBF20_APP_JSON_MAX,
               "{\"code\":0,\"params\":{\"sampleInterval\":%u,\"uploadPeriod\":%u}}",
               (unsigned)s_app.sample_interval, (unsigned)s_app.upload_period);
  if (n <= 0)
    s_app.tx_len = 0u;
  else
    s_app.tx_len = (u16)((n > (int)SBF20_APP_JSON_MAX) ? SBF20_APP_JSON_MAX : n);
  s_app.tx_off = 0u;
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
