#ifndef SBF20_H
#define SBF20_H

#include "sys.h"

#define SBF20_SOF           0x55u
#define SBF20_LEN_MIN       2u
#define SBF20_LEN_MAX       17u
#define SBF20_DATA_MAX      16u
#define SBF20_PAYLOAD_MAX   15u
#define SBF20_FRAME_MAX     19u
#define SBF20_HALF_PKT_MS   100u
#define SBF20_PAIR_TIMEOUT_MS  500u
#define SBF20_SESSION_BUF   512u

typedef enum
{
  SBF20_ROLE_IDLE = 0,
  SBF20_ROLE_RESPONDER,
  SBF20_ROLE_INITIATOR,
} sbf20_role_t;

/* 应答方：每完成一对时填充本片响应 PAYLOAD，返回响应 FLAGS.MORE */
typedef u8 (*sbf20_pair_handler_t)(
    u8 index, u8 req_more,
    const u8 *req_pld, u8 req_len,
    u8 *rsp_pld, u8 *rsp_len, u8 rsp_max);

typedef void (*sbf20_session_done_t)(const u8 *req, u16 len);

typedef void (*sbf20_initiator_done_t)(const u8 *rsp, u16 len, u8 ok);

void sbf20_init(sbf20_pair_handler_t handler);
void sbf20_set_session_done_cb(sbf20_session_done_t cb);
void sbf20_set_initiator_done_cb(sbf20_initiator_done_t cb);
void sbf20_poll(void);

sbf20_role_t sbf20_role(void);
u8 sbf20_initiator_busy(void);

/* 发起方：探活（空 opaque 一对） */
u8 sbf20_initiator_probe(void);
/* 发起方：发送请求 opaque，异步收响应 opaque（写会话形态） */
u8 sbf20_initiator_send(const u8 *req, u16 req_len);
void sbf20_initiator_abort(void);

#endif
