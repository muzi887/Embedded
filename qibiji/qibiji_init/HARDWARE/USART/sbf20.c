#include "sbf20.h"
#include "usart.h"
#include "timer.h"
#include <string.h>
#include <stdio.h>

typedef enum
{
  SBF_RX_WAIT_SOF = 0,
  SBF_RX_WAIT_LEN,
  SBF_RX_WAIT_BODY,
} sbf_rx_state_t;

typedef struct
{
  u8 active;
  u8 expected_index;
  u8 done_index;
  u16 assembled_len;
  u8 assembled[SBF20_SESSION_BUF];
  u8 last_rsp_frame[SBF20_FRAME_MAX];
  u16 last_rsp_len;
} sbf20_resp_sess_t;

typedef struct
{
  u8 active;
  u8 probe;
  u8 index;
  u8 last_rsp_more;
  u16 tx_len;
  u16 tx_off;
  u16 rx_len;
  u8 tx_buf[SBF20_SESSION_BUF];
  u8 rx_buf[SBF20_SESSION_BUF];
  u32 pair_deadline_ms;
} sbf20_init_sess_t;

static sbf_rx_state_t s_rx_st = SBF_RX_WAIT_SOF;
static u8 s_rx_frame[SBF20_FRAME_MAX];
static u8 s_rx_body_need;
static u8 s_rx_body_cnt;
static u32 s_last_rx_ms;

/*
 * The USART1 idle ISR re-arms the DMA immediately, so DMA_Rece_Buf1 is written
 * again as soon as the next frame starts arriving. Parse from this snapshot
 * instead of the live DMA buffer, and clear the "frame ready" flag before
 * leaving the critical section so a frame that arrives during parsing is not
 * swallowed.
 */
static u8 s_rx_snapshot[DMA_Rec1_Len];

#define SBF20_SESSION_IDLE_MS  500u

/* set to 1 to dump every received frame on USART1 while bringing up the link */
#define SBF20_DEBUG_RX 0u

static sbf20_role_t s_role = SBF20_ROLE_IDLE;
static sbf20_resp_sess_t s_resp;
static sbf20_init_sess_t s_init;
static u32 s_sess_last_ms;
static sbf20_pair_handler_t s_pair_handler;
static sbf20_session_done_t s_session_done_cb;
static sbf20_initiator_done_t s_initiator_done_cb;

static u8 sbf20_crc8_maxim(const u8 *data, u16 len)
{
  u8 crc = 0x00u;
  u16 i;
  u8 b;

  for (i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (b = 0; b < 8u; b++)
      crc = (crc & 1u) ? (u8)((crc >> 1) ^ 0x8Cu) : (u8)(crc >> 1);
  }
  return crc;
}

static void sbf20_rx_reset(void)
{
  s_rx_st = SBF_RX_WAIT_SOF;
  s_rx_body_need = 0;
  s_rx_body_cnt = 0;
}

static void sbf20_resp_reset(void)
{
  memset(&s_resp, 0, sizeof(s_resp));
}

static void sbf20_init_reset(void)
{
  memset(&s_init, 0, sizeof(s_init));
}

static void sbf20_session_touch(void)
{
  s_sess_last_ms = g_ms_tick;
}

static void sbf20_role_idle(void)
{
  s_role = SBF20_ROLE_IDLE;
  sbf20_resp_reset();
  sbf20_init_reset();
}

static void sbf20_check_session_idle(void)
{
  if (s_role == SBF20_ROLE_IDLE)
    return;
  if ((u32)(g_ms_tick - s_sess_last_ms) >= SBF20_SESSION_IDLE_MS)
  {
    if (s_role == SBF20_ROLE_INITIATOR && s_initiator_done_cb != NULL)
      s_initiator_done_cb(NULL, 0u, 0u);
    sbf20_role_idle();
  }
}

static void sbf20_check_initiator_timeout(void)
{
  if (s_role != SBF20_ROLE_INITIATOR || !s_init.active)
    return;
  if ((u32)(g_ms_tick - s_init.pair_deadline_ms) >= SBF20_PAIR_TIMEOUT_MS)
  {
    if (s_initiator_done_cb != NULL)
      s_initiator_done_cb(s_init.rx_buf, s_init.rx_len, 0u);
    sbf20_role_idle();
  }
}

static u8 sbf20_send_raw(const u8 *buf, u16 len)
{
  Usart1_Send((char *)buf, len);
  return 1u;
}

static u8 sbf20_send_frame(const u8 *data, u8 data_len)
{
  u8 frame[SBF20_FRAME_MAX];

  if (data_len > SBF20_DATA_MAX)
    return 0u;

  frame[0] = SBF20_SOF;
  frame[1] = (u8)(data_len + 1u);
  if (data_len > 0u)
    memcpy(&frame[2], data, data_len);
  frame[2u + data_len] = sbf20_crc8_maxim(frame, (u16)(2u + data_len));
  return sbf20_send_raw(frame, (u16)(3u + data_len));
}

static u8 sbf20_send_line(u8 flags, const u8 *payload, u8 plen)
{
  u8 data[SBF20_DATA_MAX];

  if (plen > SBF20_PAYLOAD_MAX)
    return 0u;

  data[0] = flags;
  if (plen > 0u)
    memcpy(&data[1], payload, plen);
  return sbf20_send_frame(data, (u8)(1u + plen));
}

static u8 sbf20_default_pair_handler(
    u8 index, u8 req_more,
    const u8 *req_pld, u8 req_len,
    u8 *rsp_pld, u8 *rsp_len, u8 rsp_max)
{
  (void)index;
  (void)req_pld;
  (void)req_len;
  (void)rsp_pld;
  (void)rsp_max;
  *rsp_len = 0u;
  return req_more;
}

static u8 sbf20_flags_make(u8 index, u8 more)
{
  return (u8)((more ? 0x80u : 0x00u) | (index & 0x7Fu));
}

static void sbf20_flags_parse(u8 flags, u8 *index, u8 *more)
{
  *more = (flags & 0x80u) ? 1u : 0u;
  *index = flags & 0x7Fu;
}

static u8 sbf20_cache_response_frame(u8 flags, const u8 *payload, u8 plen)
{
  u8 data[SBF20_DATA_MAX];
  u8 data_len;

  if (plen > SBF20_PAYLOAD_MAX)
    return 0u;

  data[0] = flags;
  if (plen > 0u)
    memcpy(&data[1], payload, plen);
  data_len = (u8)(1u + plen);

  s_resp.last_rsp_frame[0] = SBF20_SOF;
  s_resp.last_rsp_frame[1] = (u8)(data_len + 1u);
  memcpy(&s_resp.last_rsp_frame[2], data, data_len);
  s_resp.last_rsp_frame[2u + data_len] =
      sbf20_crc8_maxim(s_resp.last_rsp_frame, (u16)(2u + data_len));
  s_resp.last_rsp_len = (u16)(3u + data_len);
  return 1u;
}

static u8 sbf20_append_resp_req(const u8 *pld, u8 plen)
{
  if (plen == 0u)
    return 1u;
  if ((u16)s_resp.assembled_len + plen > SBF20_SESSION_BUF)
    return 0u;
  memcpy(&s_resp.assembled[s_resp.assembled_len], pld, plen);
  s_resp.assembled_len = (u16)(s_resp.assembled_len + plen);
  return 1u;
}

static u8 sbf20_append_init_rsp(const u8 *pld, u8 plen)
{
  if (plen == 0u)
    return 1u;
  if ((u16)s_init.rx_len + plen > SBF20_SESSION_BUF)
    return 0u;
  memcpy(&s_init.rx_buf[s_init.rx_len], pld, plen);
  s_init.rx_len = (u16)(s_init.rx_len + plen);
  return 1u;
}

static void sbf20_resp_begin(void)
{
  sbf20_resp_reset();
  s_resp.active = 1u;
  s_resp.expected_index = 0u;
  s_role = SBF20_ROLE_RESPONDER;
  sbf20_session_touch();
}

static u8 sbf20_is_probe(u8 index, u8 more, u8 data_len, const u8 *data)
{
  return (u8)(index == 0u && more == 0u && data_len == 1u && data[0] == 0x00u);
}

static u8 sbf20_initiator_send_current(void)
{
  u16 remain;
  u8 chunk;
  u8 more;
  u8 flags;
  const u8 *pld;

  if (s_init.probe)
  {
    return sbf20_send_line(0x00u, NULL, 0u);
  }

  remain = (u16)(s_init.tx_len - s_init.tx_off);
  if (remain > 0u)
  {
    chunk = (remain > SBF20_PAYLOAD_MAX) ? SBF20_PAYLOAD_MAX : (u8)remain;
    more = (u8)((s_init.tx_off + chunk) < s_init.tx_len);
    pld = &s_init.tx_buf[s_init.tx_off];
    flags = sbf20_flags_make(s_init.index, more);
    if (!sbf20_send_line(flags, pld, chunk))
      return 0u;
    s_init.tx_off = (u16)(s_init.tx_off + chunk);
    return 1u;
  }

  /* keep collecting the response with an empty request payload */
  more = s_init.last_rsp_more ? 1u : 0u;
  flags = sbf20_flags_make(s_init.index, more);
  return sbf20_send_line(flags, NULL, 0u);
}

static void sbf20_initiator_finish(u8 ok)
{
  if (s_initiator_done_cb != NULL)
    s_initiator_done_cb(s_init.rx_buf, s_init.rx_len, ok);
  sbf20_role_idle();
}

static u8 sbf20_initiator_begin(const u8 *req, u16 req_len, u8 probe)
{
  if (s_role != SBF20_ROLE_IDLE)
    return 0u;

  sbf20_init_reset();
  s_init.active = 1u;
  s_init.probe = probe;
  s_init.index = 0u;
  s_role = SBF20_ROLE_INITIATOR;

  if (!probe)
  {
    if (req == NULL || req_len == 0u || req_len > SBF20_SESSION_BUF)
    {
      sbf20_role_idle();
      return 0u;
    }
    memcpy(s_init.tx_buf, req, req_len);
    s_init.tx_len = req_len;
  }

  if (!sbf20_initiator_send_current())
  {
    sbf20_role_idle();
    return 0u;
  }

  s_init.pair_deadline_ms = g_ms_tick;
  sbf20_session_touch();
  return 1u;
}

static u8 sbf20_handle_response(const u8 *data, u8 data_len)
{
  u8 flags;
  u8 index;
  u8 more;
  u8 rsp_pld_len;
  const u8 *rsp_pld;

  if (!s_init.active || data_len < 1u)
    return 0u;

  flags = data[0];
  sbf20_flags_parse(flags, &index, &more);
  rsp_pld = &data[1];
  rsp_pld_len = (u8)(data_len - 1u);

  if (index != s_init.index)
    return 0u;

  if (s_init.probe)
  {
    if (!sbf20_is_probe(index, more, data_len, data))
      sbf20_initiator_finish(0u);
    else
      sbf20_initiator_finish(1u);
    return 1u;
  }

  if (!sbf20_append_init_rsp(rsp_pld, rsp_pld_len))
  {
    sbf20_initiator_finish(0u);
    return 0u;
  }

  s_init.last_rsp_more = more;
  sbf20_session_touch();

  if (more == 0u)
  {
    sbf20_initiator_finish(1u);
    return 1u;
  }

  s_init.index = (u8)(index + 1u);
  s_init.pair_deadline_ms = g_ms_tick;
  return sbf20_initiator_send_current();
}

static u8 sbf20_handle_request(const u8 *data, u8 data_len)
{
  u8 flags;
  u8 index;
  u8 more;
  u8 req_pld_len;
  const u8 *req_pld;
  u8 rsp_pld[SBF20_PAYLOAD_MAX];
  u8 rsp_len = 0u;
  u8 rsp_more;
  u8 rsp_flags;
  sbf20_pair_handler_t handler;

  if (s_role == SBF20_ROLE_INITIATOR)
    return 0u;

  if (data_len < 1u)
    return 0u;

  flags = data[0];
  sbf20_flags_parse(flags, &index, &more);
  req_pld = &data[1];
  req_pld_len = (u8)(data_len - 1u);

  if (!s_resp.active)
    sbf20_resp_begin();
  else if (index == 0u && s_resp.expected_index > 0u)
    sbf20_resp_begin();

  handler = s_pair_handler ? s_pair_handler : sbf20_default_pair_handler;

  /* duplicate pair: replay the cached response instead of re-running the business */
  if (index < s_resp.done_index)
  {
    if (s_resp.last_rsp_len == 0u)
      return 0u;
    return sbf20_send_raw(s_resp.last_rsp_frame, s_resp.last_rsp_len);
  }

  if (index != s_resp.expected_index)
    return 0u;

  if (!sbf20_append_resp_req(req_pld, req_pld_len))
  {
    sbf20_role_idle();
    return 0u;
  }

  rsp_more = handler(index, more, req_pld, req_pld_len,
                     rsp_pld, &rsp_len, SBF20_PAYLOAD_MAX);
  rsp_flags = sbf20_flags_make(index, rsp_more);

  if (!sbf20_cache_response_frame(rsp_flags, rsp_pld, rsp_len))
  {
    sbf20_role_idle();
    return 0u;
  }
  if (!sbf20_send_line(rsp_flags, rsp_pld, rsp_len))
    return 0u;

  s_resp.done_index = (u8)(index + 1u);
  s_resp.expected_index = (u8)(index + 1u);
  sbf20_session_touch();

  if (more == 0u && rsp_more == 0u)
  {
    if (s_session_done_cb != NULL)
      s_session_done_cb(s_resp.assembled, s_resp.assembled_len);
    sbf20_role_idle();
  }

  return 1u;
}

static void sbf20_dispatch_frame(void)
{
  u8 len_field;
  u16 frame_len;
  u8 data_len;
  const u8 *data;
  u8 index;
  u8 more;

  len_field = s_rx_frame[1];
  frame_len = (u16)(len_field + 2u);
  data_len = (u8)(len_field - 1u);
  data = &s_rx_frame[2];

  if (data_len < 1u)
    return;

  sbf20_flags_parse(data[0], &index, &more);

  if (s_role == SBF20_ROLE_INITIATOR && s_init.active)
  {
    sbf20_handle_response(data, data_len);
    return;
  }

  if (sbf20_is_probe(index, more, data_len, data))
  {
    sbf20_send_raw(s_rx_frame, frame_len);
    sbf20_role_idle();
    return;
  }

  sbf20_handle_request(data, data_len);
}

static void sbf20_feed_byte(u8 byte)
{
  s_last_rx_ms = g_ms_tick;

  switch (s_rx_st)
  {
  case SBF_RX_WAIT_SOF:
    if (byte == SBF20_SOF)
    {
      s_rx_frame[0] = byte;
      s_rx_st = SBF_RX_WAIT_LEN;
    }
    break;

  case SBF_RX_WAIT_LEN:
    if (byte < SBF20_LEN_MIN || byte > SBF20_LEN_MAX)
    {
      if (byte == SBF20_SOF)
      {
        s_rx_frame[0] = byte;
        s_rx_st = SBF_RX_WAIT_LEN;
      }
      else
      {
        sbf20_rx_reset();
      }
      break;
    }
    s_rx_frame[1] = byte;
    s_rx_body_need = byte;
    s_rx_body_cnt = 0u;
    s_rx_st = SBF_RX_WAIT_BODY;
    break;

  case SBF_RX_WAIT_BODY:
    s_rx_frame[2u + s_rx_body_cnt] = byte;
    s_rx_body_cnt++;
    if (s_rx_body_cnt >= s_rx_body_need)
    {
      if (sbf20_crc8_maxim(s_rx_frame, (u16)(2u + s_rx_body_need)) == 0u)
        sbf20_dispatch_frame();
      sbf20_rx_reset();
    }
    break;

  default:
    sbf20_rx_reset();
    break;
  }
}

static void sbf20_check_half_packet(void)
{
  if (s_rx_st == SBF_RX_WAIT_SOF)
    return;
  if ((u32)(g_ms_tick - s_last_rx_ms) >= SBF20_HALF_PKT_MS)
    sbf20_rx_reset();
}

void sbf20_init(sbf20_pair_handler_t handler)
{
  s_pair_handler = handler;
  s_session_done_cb = NULL;
  s_initiator_done_cb = NULL;
  s_last_rx_ms = g_ms_tick;
  sbf20_rx_reset();
  sbf20_role_idle();
}

void sbf20_set_session_done_cb(sbf20_session_done_t cb)
{
  s_session_done_cb = cb;
}

void sbf20_set_initiator_done_cb(sbf20_initiator_done_t cb)
{
  s_initiator_done_cb = cb;
}

sbf20_role_t sbf20_role(void)
{
  return s_role;
}

u8 sbf20_initiator_busy(void)
{
  return (u8)(s_role == SBF20_ROLE_INITIATOR);
}

u8 sbf20_initiator_probe(void)
{
  return sbf20_initiator_begin(NULL, 0u, 1u);
}

u8 sbf20_initiator_send(const u8 *req, u16 req_len)
{
  return sbf20_initiator_begin(req, req_len, 0u);
}

void sbf20_initiator_abort(void)
{
  if (s_role == SBF20_ROLE_INITIATOR)
    sbf20_initiator_finish(0u);
}

void sbf20_poll(void)
{
  u16 len;
  u16 i;

  sbf20_check_half_packet();
  sbf20_check_session_idle();
  sbf20_check_initiator_timeout();

  if (!UART1_RX_Complete)
    return;

  __disable_irq();
  len = Usart1_Rec_Cnt;
  if (len > DMA_Rec1_Len)
    len = DMA_Rec1_Len;
  UART1_RX_Complete = 0u;
  Usart1_Rec_Cnt = 0u;
  for (i = 0; i < len; i++)
    s_rx_snapshot[i] = (u8)DMA_Rece_Buf1[i];
  __enable_irq();

#if SBF20_DEBUG_RX
  printf("[RX %u] ", (unsigned)len);
  for (i = 0; i < len; i++)
    printf("%02X ", s_rx_snapshot[i]);
  printf("\r\n");
#endif

  for (i = 0; i < len; i++)
    sbf20_feed_byte(s_rx_snapshot[i]);
}
