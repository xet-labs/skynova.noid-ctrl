/*
 * skynoid_dispatch.h - the framed command dispatcher, the telemetry stream and the payload packers (pure C).
 *
 * WHAT THIS FILE IS
 *   Step 1.5a. When a CMD frame arrives, skd_handle_cmd() checks its length, decodes its payload, calls the
 *   matching base function and says what to answer: ACK, or NACK with a code and a detail. The base
 *   functions are passed in as a small table (skd_ops_t), so this file knows nothing about ESP-IDF and is
 *   tested on the PC (pio test -e native -f test_dispatch). src/link/link.c holds the real table
 *   (the base_cmd_* functions, the SAME ones the console uses) and turns the reply into a frame.
 *
 * COMMANDS HANDLED (protocol-v0 sections 3 and 4)
 *   0x02 arm, 0x03 disarm, 0x05 estop, 0x06 clear_estop, 0x11 stop, 0x12 cal_floor   payload: none
 *   0x04 subscribe   stream_id u8, period_ms u16      stream 1 = BASE_STATUS, period 0 = off
 *   0x07 set_param   param_id u8, value i32
 *   0x10 drive       v_mm_s i16, w_dps i16, ttl_ms u16
 *   Everything else, including 0x08 console_line (not wired yet) and the upper-node commands: NACK unknown_msg.
 *
 * NACK DETAIL (table 7): bad_len = the expected length; hazard_blocked = the hazard mask; invalid_arg = the
 *   index of the bad argument (set_param: 0 = unknown id, 1 = value out of range or breaks a relation);
 *   everything else 0.
 *
 * TIME  uint32 milliseconds from a free-running clock; differences use unsigned subtraction (wrap safe).
 * TESTS firmware/test/test_dispatch
 */
#ifndef SKYNOID_DISPATCH_H
#define SKYNOID_DISPATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "skynoid_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CMD message ids (protocol-v0 sections 3 and 4). */
enum {
    SKD_CMD_ARM = 0x02, SKD_CMD_DISARM = 0x03, SKD_CMD_SUBSCRIBE = 0x04, SKD_CMD_ESTOP = 0x05,
    SKD_CMD_CLEAR_ESTOP = 0x06, SKD_CMD_SET_PARAM = 0x07, SKD_CMD_CONSOLE_LINE = 0x08,
    SKD_CMD_DRIVE = 0x10, SKD_CMD_STOP = 0x11, SKD_CMD_CAL_FLOOR = 0x12
};

/* NACK codes (table 7); 3..7 are the SKB_NACK_* values of skynoid_base.h. */
enum { SKD_NACK_UNKNOWN_MSG = 1, SKD_NACK_BAD_LEN = 2 };

#define SKD_STREAM_BASE_STATUS 1u
#define SKD_STATUS_LEN 17u            /* TLM base_status payload */

/* The base functions the dispatcher may call. Every function returns 0 = accepted, else a NACK code,
 * except set_param, which returns an skp_result_t (SKP_OK = 0). last_detail gives the detail of the most
 * recent refusal (hazard mask or argument index). */
typedef struct {
    void    *ctx;
    int      (*arm)(void *ctx);
    int      (*disarm)(void *ctx);
    int      (*estop)(void *ctx);
    int      (*clear_estop)(void *ctx);
    int      (*drive)(void *ctx, int32_t v_mm_s, int32_t w_dps, uint32_t ttl_ms);
    int      (*stop)(void *ctx);
    int      (*cal_floor)(void *ctx);
    int      (*set_param)(void *ctx, uint8_t id, int32_t value);
    uint16_t (*last_detail)(void *ctx);
} skd_ops_t;

typedef struct {
    bool     is_ack;      /* true: ACK (no payload); false: NACK with code and detail */
    uint8_t  code;        /* NACK code, 0 for ACK */
    uint16_t detail;
} skd_reply_t;

/* ---- the telemetry stream ---- */
typedef struct {
    bool     on;
    uint16_t period_ms;
    uint32_t next_ms;     /* when the next TLM is due */
} skd_stream_t;

void skd_stream_init(skd_stream_t *s);
void skd_stream_stop(skd_stream_t *s);                    /* session end: stop streaming */
/* True once per period while the stream is on; the next one is scheduled from now (no catch-up burst). */
bool skd_stream_due(skd_stream_t *s, uint32_t now_ms);

/* Decodes one CMD and fills the reply. now_ms is used by subscribe (the first TLM is due at once). */
void skd_handle_cmd(const skd_ops_t *ops, skd_stream_t *stream, uint8_t msg,
                    const uint8_t *payload, uint16_t len, uint32_t now_ms, skd_reply_t *reply);

/* ---- payload packers (little endian, fixed width) ---- */
typedef struct {
    uint8_t  hazard;
    uint16_t range_mm;
    uint8_t  range_status;
    uint16_t batt_mv;
    uint16_t ir_raw[4];       /* front-left, front-right, rear-left, rear-right */
    uint16_t motor_ma;
    uint8_t  state;           /* 0 boot, 1 safe, 2 armed, 3 estop, 4 fault, 5 maintenance */
} skd_status_t;

/* Writes SKD_STATUS_LEN bytes. Returns SKD_STATUS_LEN. */
size_t skd_build_status(uint8_t out[SKD_STATUS_LEN], const skd_status_t *s);

/* Packs an EVT payload (at most 2 bytes). Returns the payload length and sets *msg to the EVT msg id, or
 * returns -1 for an event id that has no EVT message. */
int skd_build_event(uint8_t out[2], uint8_t *msg, const skb_event_t *ev);

/* NACK payload: code u8, detail u16. Returns 3. */
size_t skd_build_nack(uint8_t out[3], uint8_t code, uint16_t detail);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_DISPATCH_H */
