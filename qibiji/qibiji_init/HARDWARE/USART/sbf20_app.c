#include "sbf20_app.h"
#include "sbf20.h"
#include "usart.h"
#include "reg.h"
#include "24cxx.h"
#include "boot.h"
#include "wdg.h"
#include "timer.h"
#include "cJSON.h"
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define SBF20_APP_JSON_MAX 512u

/*
 * read() config groups ("configs" array): gate / ota / mqtt.
 * The mqtt group keeps productKey/deviceKey/deviceSecret/mqtt_addr in the
 * EEPROM (slots 64/96/128/160, same layout as the application firmware). The
 * PC tool reads/writes them through this firmware and can additionally push
 * them to the 4G module with AT commands.
 */
#define SBF20_APP_GRP_GATE 0x01u
#define SBF20_APP_GRP_OTA  0x02u
#define SBF20_APP_GRP_MQTT 0x04u
#define SBF20_APP_GRP_ALL  (SBF20_APP_GRP_GATE | SBF20_APP_GRP_OTA | SBF20_APP_GRP_MQTT)

/* gate parameter area: 10 u16 words starting at byte 0x0000 (0-19 bytes) */
#define SBF20_APP_GATE_WORDS 10u

/* MQTT slots (byte addresses) and their JSON keys, in read/write order */
static const u16 s_mqtt_addrs[4] = {
  MQTT_PRODUCTKEY_BYTE_ADDR,
  MQTT_DEVICEKEY_BYTE_ADDR,
  MQTT_DEVICESECRET_BYTE_ADDR,
  MQTT_MQTTADDR_BYTE_ADDR
};

static const char *const s_mqtt_keys[4] = {
  "productKey", "deviceKey", "deviceSecret", "mqtt_addr"
};

/* Scratch for the four MQTT strings (read response / write request). Kept at
 * file scope so the small main stack is not blown by a 4 x 33 byte array. */
static char s_mqtt_scratch[4][MQTT_KEY_STR_LEN];

/* AT console tuning */
#define SBF20_APP_AT_CMD_MAX    128u
#define SBF20_APP_AT_WAIT_MS    1500u
#define SBF20_APP_AT_SILENCE_MS 40u
#define SBF20_APP_AT_DATA_MAX   192u

typedef enum
{
  SBF20_APP_CODE_OK = 0,
  SBF20_APP_CODE_PARAM = 1,
  SBF20_APP_CODE_BUSY = 2,
  SBF20_APP_CODE_UNKNOWN = 3,
  SBF20_APP_CODE_JSON = 4,
  SBF20_APP_CODE_TIMEOUT = 5,
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
  u8 req_done;
  u8 biz_done;
} sbf20_app_ctx_t;

static sbf20_app_ctx_t s_app;

static void sbf20_app_reset_rx(void)
{
  s_app.rx_len = 0u;
}

static void sbf20_app_reset_tx(void)
{
  s_app.tx_len = 0u;
  s_app.tx_off = 0u;
  s_app.req_done = 0u;
  s_app.biz_done = 0u;
}

static u8 sbf20_app_append_rx(const u8 *pld, u8 plen)
{
  if (plen == 0u)
    return 1u;
  if ((u32)s_app.rx_len + (u32)plen > SBF20_APP_JSON_MAX)
    return 0u;
  memcpy(&s_app.rx_buf[s_app.rx_len], pld, plen);
  s_app.rx_len = (u16)(s_app.rx_len + plen);
  return 1u;
}

/* brace depth back to zero -> the whole request JSON has arrived */
static u8 sbf20_app_json_ready(void)
{
  int depth = 0;
  u8 started = 0u;
  u16 i;

  if (s_app.rx_len == 0u)
    return 0u;

  for (i = 0u; i < s_app.rx_len; i++)
  {
    if (s_app.rx_buf[i] == '{')
    {
      depth++;
      started = 1u;
    }
    else if (s_app.rx_buf[i] == '}')
    {
      depth--;
      if (started && depth == 0)
        return 1u;
    }
  }
  return 0u;
}

static void sbf20_app_log_rx_req(void)
{
  printf("[SBF20 RX %u] ", (unsigned)s_app.rx_len);
  if (s_app.rx_len > 0u)
    Usart1_Send((char *)s_app.rx_buf, s_app.rx_len);
  printf("\r\n");
}

static void sbf20_app_build_rsp(sbf20_app_code_t code, const char *message)
{
  int n;

  if (code == SBF20_APP_CODE_OK && (message == NULL || message[0] == '\0'))
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
}

/* ------------------------------------------------------- incremental builder */

static u16 s_json_off;

static void sbf20_app_json_begin(void)
{
  s_json_off = 0u;
  s_app.tx_buf[0] = '\0';
}

static void sbf20_app_json_add(const char *fmt, ...)
{
  va_list ap;
  int n;
  size_t room;

  if (s_json_off >= SBF20_APP_JSON_MAX)
    return;

  room = (size_t)(SBF20_APP_JSON_MAX - s_json_off);
  va_start(ap, fmt);
  n = vsnprintf((char *)s_app.tx_buf + s_json_off, room, fmt, ap);
  va_end(ap);

  if (n <= 0)
    return;
  if ((size_t)n >= room)
    s_json_off = SBF20_APP_JSON_MAX; /* truncated, keep the buffer terminated */
  else
    s_json_off = (u16)(s_json_off + (u16)n);
}

static void sbf20_app_json_end(void)
{
  s_app.tx_len = (s_json_off < SBF20_APP_JSON_MAX) ? s_json_off : SBF20_APP_JSON_MAX;
  s_app.tx_off = 0u;
}

/* Keep the response valid JSON even if the EEPROM holds junk bytes. */
static void sbf20_app_sanitize(char *s)
{
  u16 i;

  for (i = 0u; s[i] != '\0'; i++)
  {
    u8 c = (u8)s[i];
    if (c < 0x20u || c > 0x7Eu || c == '"' || c == '\\')
      s[i] = '.';
  }
}

/* Read one 32-byte MQTT slot into a MQTT_KEY_STR_LEN buffer. A blank chip
 * reads back as 0xFF, which means "never configured" and becomes empty. */
static void sbf20_app_read_mqtt_str(u16 addr, char *out)
{
  memset(out, 0, MQTT_KEY_STR_LEN);
  AT24CXX_Read(addr, (u8 *)out, MQTT_KEY_EEPROM_LEN);
  out[MQTT_KEY_EEPROM_LEN] = '\0';
  if ((u8)out[0] == 0xFFu)
    out[0] = '\0';
  sbf20_app_sanitize(out);
}

/* printable ASCII, no quote/backslash; the length is checked by the caller */
static u8 sbf20_app_param_ok(const char *s, size_t len)
{
  size_t i;
  u8 c;

  for (i = 0u; i < len; i++)
  {
    c = (u8)s[i];
    if (c < 0x20u || c > 0x7Eu)
      return 0u;
    if (c == '"' || c == '\\')
      return 0u;
  }
  return 1u;
}

static void sbf20_app_str_save(char *dst, const char *src, size_t len)
{
  memset(dst, 0, OTA_VER_STR_LEN);
  if (len > 0u)
    memcpy(dst, src, len);
}

/* ----------------------------------------------------------------- helpers */

static void sbf20_app_json_escape(const char *src, u16 src_len,
                                  char *out, u16 out_cap, u16 *out_len)
{
  u16 i;
  u16 n = 0u;
  u8 c;
  const char *rep;
  size_t rl;

  out[0] = '\0';
  *out_len = 0u;

  for (i = 0u; i < src_len; i++)
  {
    c = (u8)src[i];
    rep = NULL;
    switch (c)
    {
    case '\\':
      rep = "\\\\";
      break;
    case '"':
      rep = "\\\"";
      break;
    case '\r':
      rep = "\\r";
      break;
    case '\n':
      rep = "\\n";
      break;
    case '\t':
      rep = "\\t";
      break;
    default:
      if (c < 0x20u || c > 0x7Eu)
        rep = ".";
      break;
    }

    rl = (rep != NULL) ? strlen(rep) : 1u;
    if ((u32)n + (u32)rl + 1u > (u32)out_cap)
      break;

    if (rep != NULL)
      memcpy(&out[n], rep, rl);
    else
      out[n] = (char)c;
    n = (u16)(n + rl);
  }

  out[n] = '\0';
  *out_len = n;
}

/*
 * Send one AT command to the module and collect the raw echo.
 * Blocking (bounded by SBF20_APP_AT_WAIT_MS) with a watchdog feed inside.
 * @return 1 = response collected, 0 = timeout
 */
typedef u16 (*sbf20_acc_len_fn)(void);
typedef const char *(*sbf20_acc_data_fn)(void);

/*
 * Wait for a module reply on the given accumulator: return as soon as a final
 * result is visible (OK / ERROR) instead of waiting for the line to go quiet,
 * because the PC tool drives the module command by command. Falls back to
 * "40 ms of silence" and finally to the 1500 ms timeout.
 */
static u8 sbf20_app_at_wait(sbf20_acc_len_fn acc_len, sbf20_acc_data_fn acc_data)
{
  u32 t0;
  u32 now;
  u32 seen_ms;
  u16 seen = 0u;
  u16 got;
  const char *acc;

  t0 = g_ms_tick;
  seen_ms = t0;

  for (;;)
  {
    IWDG_Feed();
    now = g_ms_tick;
    got = acc_len();

    if (got != seen)
    {
      seen = got;
      seen_ms = now;
    }

    if (got > 0u)
    {
      acc = acc_data();

      if (strstr(acc, "OK\r\n") != NULL ||
          strstr(acc, "OK\n") != NULL ||
          strstr(acc, "ERROR") != NULL)
        return 1u;

      if ((u32)(now - seen_ms) >= SBF20_APP_AT_SILENCE_MS)
        return 1u;
    }

    if ((u32)(now - t0) >= SBF20_APP_AT_WAIT_MS)
      return 0u;
  }
}

/* USART2 = 4G module */
static u8 sbf20_app_at_exchange(const char *cmd, u16 cmd_len)
{
  uart2_rx_acc_reset();
  Usart2_Send((char *)cmd, cmd_len);
  return sbf20_app_at_wait(uart2_rx_acc_len, uart2_rx_acc_data);
}

/* USART3 = BLE module (DX-BT24) */
static u8 sbf20_app_at3_exchange(const char *cmd, u16 cmd_len)
{
  uart3_rx_acc_reset();
  Usart3_Send((char *)cmd, cmd_len);
  return sbf20_app_at_wait(uart3_rx_acc_len, uart3_rx_acc_data);
}

/* Build "<cmd>\r\n" from {"cmd":...}; 0 = invalid (response already sent) */
static u8 sbf20_app_at_prepare(cJSON *root, char *cmd, u16 cmd_cap, u16 *out_len)
{
  cJSON *item;
  u16 len;
  u16 i;

  item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
  if (!cJSON_IsString(item) || item->valuestring == NULL)
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "cmd_missing");
    return 0u;
  }

  len = (u16)strlen(item->valuestring);
  if (len == 0u || len > SBF20_APP_AT_CMD_MAX)
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "cmd_length");
    return 0u;
  }

  memset(cmd, 0, cmd_cap);
  for (i = 0u; i < len; i++)
  {
    if ((u8)item->valuestring[i] < 0x20u || (u8)item->valuestring[i] > 0x7Eu)
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "cmd_char");
      return 0u;
    }
  }

  /* drop a trailing CR/LF, the MCU always appends exactly one "\r\n" */
  while (len > 0u &&
         (item->valuestring[len - 1u] == '\r' || item->valuestring[len - 1u] == '\n'))
    len--;

  if (len == 0u)
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "cmd_length");
    return 0u;
  }

  memcpy(cmd, item->valuestring, len);
  cmd[len] = '\r';
  cmd[len + 1u] = '\n';
  cmd[len + 2u] = '\0';
  *out_len = (u16)(len + 2u);
  return 1u;
}

/* --------------------------------------------------------------- operations */

/* gate parameters: JSON key -> EEPROM byte address of the u16 word */
typedef struct
{
  const char *name;
  u16 addr;
} sbf20_app_word_t;

static const sbf20_app_word_t s_gate_words[5] = {
  { "motor_warning_curr_upper", 0x0004u },
  { "gate_maxheight",           0x0006u },
  { "gate_maxturns",            0x0008u },
  { "gate_kaidu",               0x000Au },
  { "gate_kaidu_count",         0x000Cu }
};

/* @return 1 ok, 0 = EEPROM write failed (low byte at addr, high at addr+1) */
static u8 eeprom_write_word(u16 addr, u16 value)
{
  if (AT24CXX_WriteOneByte(addr, (u8)(value & 0x00FFu)) != AT24CXX_OK)
    return 0u;
  if (AT24CXX_WriteOneByte((u16)(addr + 1u), (u8)(value >> 8)) != AT24CXX_OK)
    return 0u;

  IWDG_Feed();
  return 1u;
}

/* Write one 32-byte MQTT slot; an empty string clears it to zeroes. */
static u8 eeprom_write_mqtt_str(u16 addr, const char *s)
{
  u8 buf[MQTT_KEY_EEPROM_LEN];
  size_t n = strlen(s);

  memset(buf, 0, sizeof(buf));
  if (n > (size_t)MQTT_KEY_EEPROM_LEN)
    n = (size_t)MQTT_KEY_EEPROM_LEN;
  if (n > 0u)
    memcpy(buf, s, n);

  if (AT24CXX_Write(addr, buf, MQTT_KEY_EEPROM_LEN) != AT24CXX_OK)
    return 0u;

  IWDG_Feed();
  return 1u;
}

/*
 * JSON number -> unsigned integer that fits into `max`.
 * Only the storage width is enforced here; business ranges are not checked.
 */
static u8 sbf20_app_json_u32(const cJSON *item, u32 max, u32 *out)
{
  double d;

  if (item == NULL || !cJSON_IsNumber(item))
    return 0u;

  d = cJSON_GetNumberValue(item);
  if (d < 0.0 || d > (double)max)
    return 0u;
  if (d != (double)(u32)d)
    return 0u;

  *out = (u32)d;
  return 1u;
}

/* "configs" array -> group mask; missing/empty = all, unknown names ignored */
static u8 sbf20_app_parse_groups(cJSON *root)
{
  cJSON *configs = cJSON_GetObjectItemCaseSensitive(root, "configs");
  cJSON *item;
  u8 groups = 0u;
  int i;
  int n;

  if (!cJSON_IsArray(configs))
    return SBF20_APP_GRP_ALL;

  n = cJSON_GetArraySize(configs);
  if (n <= 0)
    return SBF20_APP_GRP_ALL;

  for (i = 0; i < n; i++)
  {
    item = cJSON_GetArrayItem(configs, i);
    if (!cJSON_IsString(item) || item->valuestring == NULL)
      continue;
    if (strcmp(item->valuestring, "gate") == 0)
      groups |= SBF20_APP_GRP_GATE;
    else if (strcmp(item->valuestring, "ota") == 0)
      groups |= SBF20_APP_GRP_OTA;
    else if (strcmp(item->valuestring, "mqtt") == 0)
      groups |= SBF20_APP_GRP_MQTT;
    /* unknown group names are ignored on purpose */
  }
  return groups;
}

static void sbf20_app_build_read_rsp(u8 groups)
{
  char ver[OTA_VER_STR_LEN];
  u16 gate[SBF20_APP_GATE_WORDS];
  OTA_InfoCB ota;
  u8 first = 1u;

  sbf20_app_json_begin();
  sbf20_app_json_add("{\"code\":0,\"params\":{");

  if (groups & SBF20_APP_GRP_GATE)
  {
    memset(gate, 0, sizeof(gate));
    AT24CXX_Readdatas(0x0000u, gate, SBF20_APP_GATE_WORDS);

    if (!first)
      sbf20_app_json_add(",");
    first = 0u;
    sbf20_app_json_add("\"motor_warning_curr_upper\":%u", (unsigned)gate[2]);
    sbf20_app_json_add(",\"gate_maxheight\":%u", (unsigned)gate[3]);
    sbf20_app_json_add(",\"gate_maxturns\":%u", (unsigned)gate[4]);
    sbf20_app_json_add(",\"gate_kaidu\":%u", (unsigned)gate[5]);
    sbf20_app_json_add(",\"gate_kaidu_count\":%u", (unsigned)gate[6]);
  }

  if (groups & SBF20_APP_GRP_OTA)
  {
    memset(&ota, 0, sizeof(ota));
    AT24CXX_Read(OTA_BYTE_ADDR, (u8 *)&ota, OTA_INFOCB_SIZE);
    memset(ver, 0, sizeof(ver));
    memcpy(ver, ota.OTA_ver, sizeof(ota.OTA_ver));
    sbf20_app_sanitize(ver);

    if (!first)
      sbf20_app_json_add(",");
    first = 0u;
    sbf20_app_json_add("\"OTA_flag\":%lu", (unsigned long)ota.OTA_flag);
    sbf20_app_json_add(",\"OTA_ver\":\"%s\"", ver);
    sbf20_app_json_add(",\"OTA_size\":%lu", (unsigned long)ota.OTA_size);
    sbf20_app_json_add(",\"UPDATE_flag\":%u", (unsigned)ota.UPDATE_flag);
  }

  if (groups & SBF20_APP_GRP_MQTT)
  {
    u8 k;

    for (k = 0u; k < 4u; k++)
      sbf20_app_read_mqtt_str(s_mqtt_addrs[k], s_mqtt_scratch[k]);

    if (!first)
      sbf20_app_json_add(",");
    first = 0u;
    sbf20_app_json_add("\"productKey\":\"%s\"", s_mqtt_scratch[0]);
    sbf20_app_json_add(",\"deviceKey\":\"%s\"", s_mqtt_scratch[1]);
    sbf20_app_json_add(",\"deviceSecret\":\"%s\"", s_mqtt_scratch[2]);
    sbf20_app_json_add(",\"mqtt_addr\":\"%s\"", s_mqtt_scratch[3]);
  }

  sbf20_app_json_add("}}");
  sbf20_app_json_end();
}

static void sbf20_app_process_read(cJSON *root)
{
  sbf20_app_build_read_rsp(sbf20_app_parse_groups(root));
}

static void sbf20_app_process_write(cJSON *root)
{
  cJSON *params;
  cJSON *item;
  char ver[OTA_VER_STR_LEN];
  u8 setver = 0u;
  u8 gate_set[5] = { 0u, 0u, 0u, 0u, 0u };
  u16 gate_val[5] = { 0u, 0u, 0u, 0u, 0u };
  u8 ota_flag_set = 0u, ota_upd_set = 0u;
  u8 mqtt_set[4] = { 0u, 0u, 0u, 0u };
  u32 ota_flag = 0u;
  u32 tmp = 0u;
  u8 ota_upd = 0u;
  u8 any = 0u;
  size_t len;
  int i;

  memset(ver, 0, sizeof(ver));

  params = cJSON_GetObjectItemCaseSensitive(root, "params");
  if (!cJSON_IsObject(params))
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "params_missing");
    return;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "OTA_ver");
  if (item != NULL)
  {
    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "OTA_ver");
      return;
    }
    len = strlen(item->valuestring);
    /* the slot is 32 bytes; keep one byte for the terminating '\0' */
    if (len > (size_t)(OTA_VER_LEN - 1u) ||
        !sbf20_app_param_ok(item->valuestring, len))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "OTA_ver");
      return;
    }
    sbf20_app_str_save(ver, item->valuestring, len);
    setver = 1u;
    any = 1u;
  }

  for (i = 0; i < 5; i++)
  {
    item = cJSON_GetObjectItemCaseSensitive(params, s_gate_words[i].name);
    if (item == NULL)
      continue;
    if (!sbf20_app_json_u32(item, 0xFFFFu, &tmp))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, s_gate_words[i].name);
      return;
    }
    gate_val[i] = (u16)tmp;
    gate_set[i] = 1u;
    any = 1u;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "OTA_flag");
  if (item != NULL)
  {
    if (!sbf20_app_json_u32(item, 0xFFFFFFFFu, &ota_flag))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "OTA_flag");
      return;
    }
    ota_flag_set = 1u;
    any = 1u;
  }

  item = cJSON_GetObjectItemCaseSensitive(params, "UPDATE_flag");
  if (item != NULL)
  {
    if (!sbf20_app_json_u32(item, 0xFFu, &tmp))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, "UPDATE_flag");
      return;
    }
    ota_upd = (u8)tmp;
    ota_upd_set = 1u;
    any = 1u;
  }

  /* OTA_size is read only and is silently ignored when present */

  /* MQTT strings: validate all of them first, write them only after that */
  for (i = 0; i < 4; i++)
  {
    item = cJSON_GetObjectItemCaseSensitive(params, s_mqtt_keys[i]);
    if (item == NULL)
      continue;
    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, s_mqtt_keys[i]);
      return;
    }
    len = strlen(item->valuestring);
    if (len > (size_t)MQTT_KEY_EEPROM_LEN ||
        !sbf20_app_param_ok(item->valuestring, len))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_PARAM, s_mqtt_keys[i]);
      return;
    }
    memset(s_mqtt_scratch[i], 0, MQTT_KEY_STR_LEN);
    if (len > 0u)
      memcpy(s_mqtt_scratch[i], item->valuestring, len);
    mqtt_set[i] = 1u;
    any = 1u;
  }

  if (!any)
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_UNKNOWN, "no_param");
    return;
  }

  /* gate words are written one by one so the unused words stay untouched */
  for (i = 0; i < 5; i++)
  {
    if (gate_set[i] && !eeprom_write_word(s_gate_words[i].addr, gate_val[i]))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_BUSY, "eeprom_fail");
      return;
    }
  }

  /* the OTA block is a struct: read it back, patch the fields, write the block */
  if (ota_flag_set || setver || ota_upd_set)
  {
    OTA_InfoCB ota;

    memset(&ota, 0, sizeof(ota));
    AT24CXX_Read(OTA_BYTE_ADDR, (u8 *)&ota, OTA_INFOCB_SIZE);

    if (setver)
    {
      memset(ota.OTA_ver, 0, sizeof(ota.OTA_ver));
      memcpy(ota.OTA_ver, ver, sizeof(ota.OTA_ver));
    }
    if (ota_flag_set)
      ota.OTA_flag = ota_flag;
    if (ota_upd_set)
      ota.UPDATE_flag = ota_upd;

    IWDG_Feed();
    if (AT24CXX_Write(OTA_BYTE_ADDR, (u8 *)&ota, OTA_INFOCB_SIZE) != AT24CXX_OK)
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_BUSY, "eeprom_fail");
      return;
    }
    IWDG_Feed();
  }

  /* MQTT slots are plain 32-byte strings, written one slot at a time */
  for (i = 0; i < 4; i++)
  {
    if (mqtt_set[i] && !eeprom_write_mqtt_str(s_mqtt_addrs[i], s_mqtt_scratch[i]))
    {
      sbf20_app_build_rsp(SBF20_APP_CODE_BUSY, "eeprom_fail");
      return;
    }
  }

  sbf20_app_build_rsp(SBF20_APP_CODE_OK, NULL);
}

/* static: keep the 1KB stack free (same rationale as the sibling project's
 * stack-overflow fix) */
static char s_at_cmd[SBF20_APP_AT_CMD_MAX + 4u];
static char s_at_data[SBF20_APP_AT_DATA_MAX * 2u + 8u];

static void sbf20_app_build_at_rsp(const char *text, u16 text_len)
{
  u16 data_len = 0u;
  int n;

  sbf20_app_json_escape(text, text_len, s_at_data, (u16)sizeof(s_at_data), &data_len);

  n = snprintf((char *)s_app.tx_buf, SBF20_APP_JSON_MAX,
               "{\"code\":0,\"data\":\"%s\"}", s_at_data);
  if (n <= 0)
    s_app.tx_len = 0u;
  else
    s_app.tx_len = (u16)((n > (int)SBF20_APP_JSON_MAX) ? SBF20_APP_JSON_MAX : n);
  s_app.tx_off = 0u;
}

/* USART2 = 4G module */
static void sbf20_app_process_at(cJSON *root)
{
  u16 len;

  if (!sbf20_app_at_prepare(root, s_at_cmd, (u16)sizeof(s_at_cmd), &len))
    return;

  if (!sbf20_app_at_exchange(s_at_cmd, len))
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_TIMEOUT, "at_timeout");
    return;
  }

  sbf20_app_build_at_rsp(uart2_rx_acc_data(), uart2_rx_acc_len());
}

/* USART3 = BLE module (DX-BT24): AT passthrough, same shape as "at" */
static void sbf20_app_process_at3(cJSON *root)
{
  u16 len;

  if (!sbf20_app_at_prepare(root, s_at_cmd, (u16)sizeof(s_at_cmd), &len))
    return;

  if (!sbf20_app_at3_exchange(s_at_cmd, len))
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_TIMEOUT, "at3_timeout");
    return;
  }

  sbf20_app_build_at_rsp(uart3_rx_acc_data(), uart3_rx_acc_len());
}

static void sbf20_app_process(void)
{
  cJSON *root;
  cJSON *op;

  root = cJSON_ParseWithLengthOpts((char *)s_app.rx_buf, (size_t)s_app.rx_len,
                                   NULL, 0);
  if (root == NULL)
  {
    sbf20_app_build_rsp(SBF20_APP_CODE_JSON, "json_error");
    return;
  }

  op = cJSON_GetObjectItemCaseSensitive(root, "op");
  if (!cJSON_IsString(op) || op->valuestring == NULL)
  {
    cJSON_Delete(root);
    sbf20_app_build_rsp(SBF20_APP_CODE_UNKNOWN, "op_missing");
    return;
  }

  if (strcmp(op->valuestring, "read") == 0)
    sbf20_app_process_read(root);
  else if (strcmp(op->valuestring, "write") == 0)
    sbf20_app_process_write(root);
  else if (strcmp(op->valuestring, "at") == 0)
    sbf20_app_process_at(root);
  else if (strcmp(op->valuestring, "at3") == 0)
    sbf20_app_process_at3(root);
  else
    sbf20_app_build_rsp(SBF20_APP_CODE_UNKNOWN, "op_unknown");

  cJSON_Delete(root);
}

/* ------------------------------------------------------------- transport glue */

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

  chunk = (remain > (u16)rsp_max) ? rsp_max : (u8)remain;
  memcpy(rsp_pld, &s_app.tx_buf[s_app.tx_off], chunk);
  s_app.tx_off = (u16)(s_app.tx_off + chunk);
  *rsp_len = chunk;
  return (u8)((s_app.tx_off < s_app.tx_len) ? 1u : 0u);
}

static u8 sbf20_app_overflow_rsp(u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  static const char err[] = "{\"code\":1,\"message\":\"overflow\"}";
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

  if (!s_app.req_done)
  {
    if (req_more != 0u && !sbf20_app_json_ready())
    {
      *rsp_len = 0u;
      return 1u; /* still collecting the request */
    }

    s_app.req_done = 1u;
    sbf20_app_log_rx_req();
    s_app.biz_done = 1u;
    sbf20_app_process();
  }

  return sbf20_app_copy_result_chunk(rsp_pld, rsp_len, rsp_max);
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
  sbf20_init(sbf20_app_pair_handler);
  sbf20_set_session_done_cb(sbf20_app_session_done);
}

const u8 *sbf20_app_last_rx_json(void)
{
  return s_app.last_rx;
}

u16 sbf20_app_last_rx_json_len(void)
{
  return s_app.last_rx_len;
}
