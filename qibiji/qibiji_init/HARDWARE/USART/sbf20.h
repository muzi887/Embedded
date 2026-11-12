#ifndef SBF20_H
#define SBF20_H

#include "sys.h"

/*
 * SBF-20 (Serial Binary Frame, <=20B) transport layer, v2.x.
 *
 *   offset 0 : SOF   fixed 0x55
 *   offset 1 : LEN   DATA bytes + 1 (CRC8 included), 2..17
 *   offset 2 : DATA  FLAGS(1B) + PAYLOAD(0..15B)
 *   offset 2+n: CRC8  CRC8/Maxim over SOF,LEN,DATA (result is 0 on a good frame)
 *
 * FLAGS = (MORE << 7) | (INDEX & 0x7F)
 *
 * On this product the PC/Qt application is always the initiator and the MCU
 * is always the responder.
 */

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

/* Responder: fill one response payload per pair, return the response FLAGS.MORE */
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

/* Initiator (kept for MCU side probing / future push): empty opaque pair */
u8 sbf20_initiator_probe(void);
u8 sbf20_initiator_send(const u8 *req, u16 req_len);
void sbf20_initiator_abort(void);

#endif
