#include "uart3_at_sequence.h"
#include "usart.h"
#include "timer.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

volatile u8 g_uart3_at_sequence_request = 0;
char g_mqtt_productKey[128] = "pk001";
char g_mqtt_deviceKey[128] = "dev001";
char g_mqtt_deviceSecret[128] = "7ad6fc5410284f7dadd3cc9295eecb81";
char g_mqtt_addr[80] = "82.157.234.123:1883";

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

#define AT_CMD_COUNT 12u
/** 主题路径含 productKey/deviceKey 各最长 64，留足 snprintf 余量 */
#define AT_CMD_LINE_MAX 320u

static char s_cmd_bufs[AT_CMD_COUNT][AT_CMD_LINE_MAX];

#define AT_WAIT_OK_MS   2000u
#define AT_CMD_GAP_MS   50u

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

static int uart3_at_build_cmd_table(void)
{
  const char *pk = g_mqtt_productKey;
  const char *dk = g_mqtt_deviceKey;
  const char *sec = g_mqtt_deviceSecret;
  char svr[80];

  if (!pk || !pk[0] || !dk || !dk[0] || !sec || !sec[0])
  {
    printf("[AT_SEQ] missing mqtt pk/dk/secret\r\n");
    return -1;
  }
  if (format_mqtt_svr(svr, sizeof(svr), g_mqtt_addr[0] ? g_mqtt_addr : "") != 0)
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

  BC(0, "usr.cn#AT+WKMOD=MQTT,NOR\r\n");
  BC(1, "usr.cn#AT+MQTTSVR=%s\r\n", svr);
  BC(2, "usr.cn#AT+MQTTUSER=%s\r\n", dk);
  BC(3, "usr.cn#AT+MQTTPSW=%s\r\n", sec);
  BC(4, "usr.cn#AT+MQTTCID=%s|%s\r\n", pk, dk);
  BC(5, "usr.cn#AT+MQTTMOD=1\r\n");
  BC(6, "usr.cn#AT+HEARTEN=OFF\r\n");
  BC(7, "usr.cn#AT+MQTTSUBTP=1,1,iot/v1/%s/%s/up/property/post,0\r\n", pk, dk);
  BC(8, "usr.cn#AT+MQTTSUBTP=2,1,iot/v1/%s/%s/up/event/post,0\r\n", pk, dk);
  BC(9, "usr.cn#AT+MQTTSUBTP=3,1,iot/v1/%s/%s/up/service/reply,0\r\n", pk, dk);
  BC(10, "usr.cn#AT+MQTTPUBTP=1,1,iot/v1/%s/%s/down/service/invoke,0,0\r\n", pk, dk);
  BC(11, "usr.cn#AT+S\r\n");

#undef BC
  return 0;
}

static void seq_abort(void)
{
  g_uart3_at_sequence_request = 0;
  s_st = AT_ST_IDLE;
  s_idx = 0;
  UART2_RX_Complete = 0;
  Usart2_Rec_Cnt = 0;
  MYDMA_Enable2(DMA1_Channel6);
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
    UART2_RX_Complete = 0;
    Usart2_Rec_Cnt = 0;
    MYDMA_Enable2(DMA1_Channel6);
    printf("[AT_SEQ] start: %u dynamic cmd(s), OK timeout %ums/step\r\n",
           (unsigned)AT_CMD_COUNT, (unsigned)AT_WAIT_OK_MS);
    s_st = AT_ST_SEND;
    /* fall-through */
  case AT_ST_SEND:
    cmd = s_cmd_bufs[s_idx];
    len = (u16)strlen(cmd);
    UART2_RX_Complete = 0;
    Usart2_Rec_Cnt = 0;
    printf("[AT_SEQ] TX %u/%u len=%u\r\n",
           (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT, (unsigned)len);
    Usart2_Send((u8 *)cmd, len);
    s_wait_t0 = g_tim4_ms_tick;
    s_st = AT_ST_WAIT;
    return UART3_AT_SEQ_BUSY;

  case AT_ST_WAIT:
    if ((uint32_t)(g_tim4_ms_tick - s_wait_t0) >= AT_WAIT_OK_MS)
    {
      printf("[AT_SEQ] FAIL: timeout step %u/%u (>%ums)\r\n",
             (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT, (unsigned)AT_WAIT_OK_MS);
      seq_abort();
      return UART3_AT_SEQ_ABORTED;
    }
    if (!UART2_RX_Complete)
      return UART3_AT_SEQ_BUSY;

    if (Usart2_Rec_Cnt < DMA_Rec2_Len)
      DMA_Rece_Buf2[Usart2_Rec_Cnt] = '\0';
    else
      DMA_Rece_Buf2[DMA_Rec2_Len - 1] = '\0';

    if (resp_has_error((char *)DMA_Rece_Buf2))
    {
      printf("[AT_SEQ] FAIL: modem ERROR step %u/%u rx_len=%u\r\n",
             (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT, (unsigned)Usart2_Rec_Cnt);
      UART2_RX_Complete = 0;
      Usart2_Rec_Cnt = 0;
      seq_abort();
      return UART3_AT_SEQ_ABORTED;
    }

    if (strstr((char *)DMA_Rece_Buf2, "OK") == NULL)
    {
      printf("[AT_SEQ] FAIL: no OK step %u/%u rx_len=%u\r\n",
             (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT, (unsigned)Usart2_Rec_Cnt);
      UART2_RX_Complete = 0;
      Usart2_Rec_Cnt = 0;
      seq_abort();
      return UART3_AT_SEQ_ABORTED;
    }

    printf("[AT_SEQ] OK step %u/%u\r\n",
           (unsigned)(s_idx + 1u), (unsigned)AT_CMD_COUNT);

    UART2_RX_Complete = 0;
    Usart2_Rec_Cnt = 0;

    s_idx++;
    if (s_idx >= AT_CMD_COUNT)
    {
      printf("[AT_SEQ] all commands OK -> system reset\r\n");
      g_uart3_at_sequence_request = 0;
      s_st = AT_ST_IDLE;
      s_idx = 0;
      return UART3_AT_SEQ_DONE_ALL_OK;
    }

    s_wait_t0 = g_tim4_ms_tick;
    s_st = AT_ST_GAP;
    return UART3_AT_SEQ_BUSY;

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
    // GM4G_Restart();
    NVIC_SystemReset();
    break;
  case UART3_AT_SEQ_ABORTED:
    // Cmd_Setting_OnAtSequenceAbort();
    break;
  default:
    break;
  }
  return 1;
}
