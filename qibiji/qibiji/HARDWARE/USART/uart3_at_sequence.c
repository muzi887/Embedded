#include "uart3_at_sequence.h"
#include "usart.h"
#include "4G.h"
#include "timer.h"
#include "24cxx.h"
#include "reg.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

volatile u8 g_uart3_at_sequence_request = 0;
char g_mqtt_productKey[MQTT_KEY_STR_LEN];
char g_mqtt_deviceKey[MQTT_KEY_STR_LEN];
char g_mqtt_deviceSecret[MQTT_KEY_STR_LEN];
char g_mqtt_addr[MQTT_KEY_STR_LEN];

void uart3_at_sequence_request_set(u8 en)
{
  g_uart3_at_sequence_request = en ? 1u : 0u;
  if (en)
    printf("[AT_SEQ] request set (will run when main polls)\r\n");
}

u8 uart3_at_sequence_should_run(void)
{
  return (g_uart3_at_sequence_request != 0) ? 1u : 0u;
}

/* 与 uart3_at_build_cmd_table() 中 BC(0)..BC(22) 条数一致（含探活 CSQ） */
#define AT_CMD_COUNT 23u
/** 主题路径含 productKey/deviceKey 各最长 64，留足 snprintf 余量 */
#define AT_CMD_LINE_MAX 320u

static char s_cmd_bufs[AT_CMD_COUNT][AT_CMD_LINE_MAX];
/* 1 = 该条"尽力而为"：模块不支持/报错只告警跳过，不中断整条配置序列 */
static u8 s_cmd_optional[AT_CMD_COUNT];

#define AT_WAIT_OK_MS      2000u
#define AT_CMD_GAP_MS      50u
#define AT_RX_SILENCE_MS   40u

typedef enum
{
  AT_ST_IDLE = 0,
  AT_ST_SEND,
  AT_ST_WAIT,
  AT_ST_GAP,
} at_seq_state_t;

static at_seq_state_t s_st = AT_ST_IDLE;
static u8 s_idx = 0;
static uint32_t s_wait_t0 = 0;
static u16 s_rx_last_got = 0;
static uint32_t s_rx_silence_t0 = 0;

/**
 * g_mqtt_addr → AT+MQTTSVR 需要的 host,port
 * - 已为「地址,端口」则原样拷贝
 * - 「地址:端口」则将最后一个 ':' 换为 ','（例 61.182.29.18:1883 → 61.182.29.18,1883）
 */
static int format_mqtt_svr(char *out, size_t out_sz, const char *in)
{
  const char *last_colon;
  size_t host_len;
  size_t port_len;

  if (!in || !in[0] || out_sz < 4u)
    return -1;

  if (strchr(in, ',') != NULL)
  {
    if (strlen(in) >= out_sz)
      return -1;
    memcpy(out, in, strlen(in) + 1u);
    return 0;
  }

  last_colon = strrchr(in, ':');
  if (last_colon == NULL || last_colon == in || last_colon[1] == '\0')
    return -1;

  host_len = (size_t)(last_colon - in);
  port_len = strlen(last_colon + 1);
  if (host_len + 1u + port_len + 1u > out_sz)
    return -1;

  memcpy(out, in, host_len);
  out[host_len] = ',';
  memcpy(out + host_len + 1u, last_colon + 1, port_len + 1u);
  return 0;
}

static int snprintf_chk(char *buf, size_t cap, const char *fmt, ...)
{
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= cap)
    return -1;
  return 0;
}

/**
 * @brief 将 g_mqtt_productKey / g_mqtt_deviceKey 写入 EEPROM
 *        各最多 MQTT_KEY_EEPROM_LEN(32) 有效字符
 */
void mqtt_keys_save_to_eeprom(void)
{
  char pk_buf[MQTT_KEY_EEPROM_LEN];
  char dk_buf[MQTT_KEY_EEPROM_LEN];

  memset(pk_buf, 0, sizeof(pk_buf));
  memset(dk_buf, 0, sizeof(dk_buf));
  memcpy(pk_buf, g_mqtt_productKey, MQTT_KEY_EEPROM_LEN);
  memcpy(dk_buf, g_mqtt_deviceKey, MQTT_KEY_EEPROM_LEN);

  AT24CXX_Write(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)pk_buf, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Write(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)dk_buf, MQTT_KEY_EEPROM_LEN);
  printf("[AT_SEQ] mqtt pk/dk saved to eeprom @%u/%u\r\n",
         (unsigned)MQTT_PRODUCTKEY_BYTE_ADDR, (unsigned)MQTT_DEVICEKEY_BYTE_ADDR);
}

static int uart3_at_build_cmd_table(void)
{
  char pk[MQTT_KEY_STR_LEN];
  char dk[MQTT_KEY_STR_LEN];
  char sec[MQTT_KEY_STR_LEN];
  char adr[MQTT_KEY_STR_LEN];
  char svr[80];

  memset(pk, 0, sizeof(pk));
  memset(dk, 0, sizeof(dk));
  memset(sec, 0, sizeof(sec));
  memset(adr, 0, sizeof(adr));
  memset(s_cmd_optional, 0, sizeof(s_cmd_optional));

  AT24CXX_Read(MQTT_PRODUCTKEY_BYTE_ADDR, (u8 *)pk, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Read(MQTT_DEVICEKEY_BYTE_ADDR, (u8 *)dk, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Read(MQTT_DEVICESECRET_BYTE_ADDR, (u8 *)sec, MQTT_KEY_EEPROM_LEN);
  AT24CXX_Read(MQTT_MQTTADDR_BYTE_ADDR, (u8 *)adr, MQTT_KEY_EEPROM_LEN);
  /* '\0' 放在第 33 字节，不覆盖第 32 个字符 */
  pk[MQTT_KEY_EEPROM_LEN] = '\0';
  dk[MQTT_KEY_EEPROM_LEN] = '\0';
  sec[MQTT_KEY_EEPROM_LEN] = '\0';
  adr[MQTT_KEY_EEPROM_LEN] = '\0';

  if ((u8)pk[0] == 0xFFu)
    pk[0] = '\0';
  if ((u8)dk[0] == 0xFFu)
    dk[0] = '\0';
  if ((u8)sec[0] == 0xFFu)
    sec[0] = '\0';
  if ((u8)adr[0] == 0xFFu)
    adr[0] = '\0';

  /* 同步到全局，供后续业务使用 */
  memset(g_mqtt_productKey, 0, sizeof(g_mqtt_productKey));
  memset(g_mqtt_deviceKey, 0, sizeof(g_mqtt_deviceKey));
  memset(g_mqtt_deviceSecret, 0, sizeof(g_mqtt_deviceSecret));
  memset(g_mqtt_addr, 0, sizeof(g_mqtt_addr));
  memcpy(g_mqtt_productKey, pk, MQTT_KEY_EEPROM_LEN);
  memcpy(g_mqtt_deviceKey, dk, MQTT_KEY_EEPROM_LEN);
  memcpy(g_mqtt_deviceSecret, sec, MQTT_KEY_EEPROM_LEN);
  memcpy(g_mqtt_addr, adr, MQTT_KEY_EEPROM_LEN);
  g_mqtt_productKey[MQTT_KEY_EEPROM_LEN] = '\0';
  g_mqtt_deviceKey[MQTT_KEY_EEPROM_LEN] = '\0';
  g_mqtt_deviceSecret[MQTT_KEY_EEPROM_LEN] = '\0';
  g_mqtt_addr[MQTT_KEY_EEPROM_LEN] = '\0';

  if (pk[0] == '\0' || dk[0] == '\0' || sec[0] == '\0' || adr[0] == '\0')
  {
    printf("[AT_SEQ] missing mqtt pk/dk/secret/addr\r\n");
    return -1;
  }
  if (format_mqtt_svr(svr, sizeof(svr), adr) != 0)
  {
    printf("[AT_SEQ] bad mqtt addr (need \"地址,端口\" or \"地址:端口\")\r\n");
    return -1;
  }

#define BC(I, ...)                                                                               \
  do                                                                                             \
  {                                                                                              \
    if (snprintf_chk(s_cmd_bufs[(I)], AT_CMD_LINE_MAX, __VA_ARGS__) != 0)                       \
      return -1;                                                                                 \
  } while (0)
/* 尽力而为步骤：失败只告警跳过（见 seq_step_failed） */
#define BC_OPT(I, ...)                                                                           \
  do                                                                                             \
  {                                                                                              \
    BC(I, __VA_ARGS__);                                                                          \
    s_cmd_optional[(I)] = 1u;                                                                    \
  } while (0)

  /* 探活：确认 USART2 链路与模块可应答，再建 MQTT 参数 */
  BC(0, "usr.cn#AT+CSQ?\r\n");
  BC(1, "usr.cn#AT+WKMOD=MQTT,NOR\r\n");
  BC(2, "usr.cn#AT+MQTTSVR=%s\r\n", svr);
  BC(3, "usr.cn#AT+MQTTUSER=%s\r\n", dk);
  BC(4, "usr.cn#AT+MQTTPSW=%s\r\n", sec);
  BC(5, "usr.cn#AT+MQTTCID=%s\r\n", dk);
  BC(6, "usr.cn#AT+MQTTMOD=1\r\n");
  BC(7, "usr.cn#AT+HEARTEN=OFF\r\n");
  /* MQTTPUBTP=<num>,<en>,<topic>,<qos>,<retain>,<flag> 共 6 参（缺参会 +CME ERROR:53） */
  BC(8, "usr.cn#AT+MQTTPUBTP=1,1,iot/v1/%s/%s/up/sys/online,0,0,0\r\n", pk, dk);
  BC(9, "usr.cn#AT+MQTTPUBTP=2,1,iot/v1/%s/%s/up/sys/heartbeat,0,0,0\r\n", pk, dk);
  BC(10, "usr.cn#AT+MQTTPUBTP=3,1,iot/v1/%s/%s/up/property/post,0,0,0\r\n", pk, dk);
  BC(11, "usr.cn#AT+MQTTPUBTP=4,1,iot/v1/%s/%s/up/event/post,0,0,0\r\n", pk, dk);
  BC(12, "usr.cn#AT+MQTTPUBTP=5,1,iot/v1/%s/%s/up/alarm/post,0,0,0\r\n", pk, dk);
  BC(13, "usr.cn#AT+MQTTPUBTP=6,1,iot/v1/%s/%s/up/service/reply,0,0,0\r\n", pk, dk);
  BC(14, "usr.cn#AT+MQTTPUBTP=7,1,iot/v1/%s/%s/up/ota/request,0,0,0\r\n", pk, dk);
  BC(15, "usr.cn#AT+MQTTPUBTP=8,1,iot/v1/%s/%s/up/request/invoke,0,0,0\r\n", pk, dk);
  /* MQTTSUBTP=<num>,<en>,<topic>,<qos> 共 4 参 */
  BC(16, "usr.cn#AT+MQTTSUBTP=1,1,iot/v1/%s/%s/down/service/invoke,0\r\n", pk, dk);
  BC(17, "usr.cn#AT+MQTTSUBTP=2,1,iot/v1/%s/%s/down/config/push,0\r\n", pk, dk);
  BC(18, "usr.cn#AT+MQTTSUBTP=3,1,iot/v1/%s/%s/down/property/get,0\r\n", pk, dk);
  BC(19, "usr.cn#AT+MQTTSUBTP=4,1,iot/v1/%s/%s/down/ota/reply,0\r\n", pk, dk);
  BC(20, "usr.cn#AT+MQTTSUBTP=5,1,iot/v1/%s/%s/down/request/reply,0\r\n", pk, dk);
  /* 关掉模组"串口数据缓存"（CACHEN）：服务器不可用期间的上行否则会被模组缓存，
     链路恢复后成批重放（现场表现：一次收到 20+ 条旧对时回执）。
     该条为尽力而为：模块若不支持/报错只告警跳过，不影响其余 MQTT 配置继续下发。 */
  BC_OPT(21, "usr.cn#AT+CACHEN=OFF\r\n");
  BC(22, "usr.cn#AT+S\r\n");

#undef BC
#undef BC_OPT
  return 0;
}

/** 发 AT 前：重开 USART2 RX DMA */
static void seq_rx_arm(void)
{
  UART2_RX_Complete = 0;
  Usart2_Rec_Cnt = 0;
  s_rx_last_got = 0;
  s_rx_silence_t0 = g_tim4_ms_tick;
  MYDMA_Enable2(DMA1_Channel6);
}

static u16 seq_dma_got(void)
{
  u16 dma_left = (u16)DMA_GetCurrDataCounter(DMA1_Channel6);
  if (dma_left > DMA_Rec2_Len)
    return 0u;
  return (u16)(DMA_Rec2_Len - dma_left);
}

/** IDLE 中断，或 DMA 有数据且静默 AT_RX_SILENCE_MS */
static u8 seq_rx_frame_ready(void)
{
  u16 got = seq_dma_got();

  if (got != s_rx_last_got)
  {
    s_rx_last_got = got;
    s_rx_silence_t0 = g_tim4_ms_tick;
  }

  if (UART2_RX_Complete)
  {
    if (Usart2_Rec_Cnt == 0 && got > 0)
      Usart2_Rec_Cnt = got;
    return 1u;
  }

  if (got > 0u &&
      (uint32_t)(g_tim4_ms_tick - s_rx_silence_t0) >= AT_RX_SILENCE_MS)
  {
    Usart2_Rec_Cnt = got;
    UART2_RX_Complete = 1;
    return 1u;
  }
  return 0u;
}

static void seq_nul_terminate_rx(void)
{
  if (Usart2_Rec_Cnt < DMA_Rec2_Len)
    DMA_Rece_Buf2[Usart2_Rec_Cnt] = '\0';
  else
    DMA_Rece_Buf2[DMA_Rec2_Len - 1] = '\0';
}

static void seq_abort(void)
{
  g_uart3_at_sequence_request = 0;
  s_st = AT_ST_IDLE;
  s_idx = 0;
  seq_rx_arm();
}

static u8 resp_has_error(const char *p)
{
  if (strstr(p, "\r\nERROR") != NULL)
    return 1u;
  if (strstr(p, "ERROR\r\n") != NULL)
    return 1u;
  if (strstr(p, "+CME ERROR") != NULL)
    return 1u;
  return 0u;
}

/** 推进到下一条指令；返回 UART3_AT_SEQ_BUSY，全部完成则返回 UART3_AT_SEQ_DONE_ALL_OK */
static u8 seq_advance_or_done(void)
{
  s_idx++;
  if (s_idx >= AT_CMD_COUNT)
  {
    mqtt_keys_save_to_eeprom();
    printf("[AT_SEQ] all commands OK -> system reset\r\n");
    g_uart3_at_sequence_request = 0;
    s_st = AT_ST_IDLE;
    s_idx = 0;
    return UART3_AT_SEQ_DONE_ALL_OK;
  }

  s_wait_t0 = g_tim4_ms_tick;
  s_st = AT_ST_GAP;
  return UART3_AT_SEQ_BUSY;
}

/** 当前步骤失败：可选步骤（如 AT+CACHEN=OFF）只告警跳过，其余中断整条序列（保持原语义） */
static u8 seq_step_failed(const char *why)
{
  printf("[AT_SEQ] FAIL(%s): step %u/%u rx_done=%u rec=%u dma_got=%u\r\n", why,
         (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT,
         (unsigned)UART2_RX_Complete, (unsigned)Usart2_Rec_Cnt, (unsigned)seq_dma_got());
  printf("[AT_SEQ] FAIL cmd: %s", s_cmd_bufs[s_idx]);
  printf("[AT_SEQ] FAIL rx : %s\r\n", (char *)DMA_Rece_Buf2);

  if (s_cmd_optional[s_idx])
  {
    printf("[AT_SEQ] WARN: optional step failed, skip and continue\r\n");
    return seq_advance_or_done();
  }

  seq_abort();
  return UART3_AT_SEQ_ABORTED;
}

u8 uart3_at_sequence_poll(void)
{
  const char *cmd;
  u16 len;

  if (g_uart3_at_sequence_request == 0)
  {
    s_st = AT_ST_IDLE;
    s_idx = 0;
    return UART3_AT_SEQ_BUSY;
  }

  switch (s_st)
  {
  case AT_ST_IDLE:
    if (uart3_at_build_cmd_table() != 0)
    {
      seq_abort();
      return UART3_AT_SEQ_ABORTED;
    }
    s_idx = 0;
    printf("[AT_SEQ] start: %u cmd(s), LINKA=%u, OK timeout %ums/step\r\n",
           (unsigned)AT_CMD_COUNT, (unsigned)GPRS_LINKA, (unsigned)AT_WAIT_OK_MS);
    /* usr.cn# 可在透传/MQTT 下直接发 AT，不必先 RST 等 boot banner */
    s_st = AT_ST_SEND;
    return UART3_AT_SEQ_BUSY;

  case AT_ST_SEND:
    cmd = s_cmd_bufs[s_idx];
    len = (u16)strlen(cmd);
    seq_rx_arm();
    printf("[AT_SEQ] TX %u/%u len=%u cmd=%s",
           (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT, (unsigned)len, cmd);
    Usart2_Send((char *)cmd, len);
    s_wait_t0 = g_tim4_ms_tick;
    s_st = AT_ST_WAIT;
    return UART3_AT_SEQ_BUSY;

  case AT_ST_WAIT:
    if ((uint32_t)(g_tim4_ms_tick - s_wait_t0) >= AT_WAIT_OK_MS)
    {
      u16 got = seq_dma_got();
      if (Usart2_Rec_Cnt == 0 && got > 0)
        Usart2_Rec_Cnt = got;
      seq_nul_terminate_rx();
      return seq_step_failed("timeout");
    }
    if (!seq_rx_frame_ready())
      return UART3_AT_SEQ_BUSY;

    seq_nul_terminate_rx();

    if (resp_has_error((char *)DMA_Rece_Buf2))
      return seq_step_failed("modem ERROR");

    if (strstr((char *)DMA_Rece_Buf2, "OK") == NULL)
      return seq_step_failed("no OK");

    printf("[AT_SEQ] OK step %u/%u\r\n",
           (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT);
    return seq_advance_or_done();

  case AT_ST_GAP:
    if ((uint32_t)(g_tim4_ms_tick - s_wait_t0) >= AT_CMD_GAP_MS)
      s_st = AT_ST_SEND;
    return UART3_AT_SEQ_BUSY;

  default:
    printf("[AT_SEQ] FAIL: internal state\r\n");
    seq_abort();
    return UART3_AT_SEQ_ABORTED;
  }
}

u8 uart3_at_sequence_main_loop_tick(void)
{
  u8 rc;

  if (!uart3_at_sequence_should_run())
    return 0;

  rc = uart3_at_sequence_poll();
  switch (rc)
  {
  case UART3_AT_SEQ_DONE_ALL_OK:
    // Cmd_Setting_OnAtSequenceDone();
    GM4G_Reset();       /* 工程内无 GM4G_Restart，用 4G.h 的硬件复位 */
    NVIC_SystemReset();
    break;
  case UART3_AT_SEQ_ABORTED:
    /* 请求标志已在 seq_abort 清零；返回 0 让 main 立刻恢复常规任务 */
    printf("[AT_SEQ] aborted, resume normal main loop\r\n");
    return 0;
  default:
    break;
  }
  return 1;
}
