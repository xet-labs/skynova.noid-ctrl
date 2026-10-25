/*
 * skynoid_wire.h - the link format between the ESP32 and the Raspberry Pi.
 *
 * WHAT THIS FILE IS
 *   The public interface of the "wire" library: CRC, COBS, frame build and
 *   a byte-by-byte parser. It is plain C with NO ESP-IDF includes, so the same
 *   code is tested on the PC (pio test -e native) and runs on the ESP32.
 *
 * WHAT A FRAME LOOKS LIKE ON THE WIRE (spec: vault note 10-specs/protocol-v0)
 *     0x00 | COBS( header(10) | payload(len) | crc16(2) ) | 0x00
 *   - 0x00 only ever marks the edge of a frame (COBS removes every other zero).
 *   - header = ver, type, msg, flags, seq(u16), ack(u16), len(u16), little endian.
 *   - crc16  = CRC-16/CCITT-FALSE over header + payload, stored little endian.
 *
 * HOW IT IS USED
 *   Sending : fill a skw_frame_t, call skw_frame_encode(), write the bytes.
 *   Receiving: call skw_parser_feed() once per received byte (see the parser).
 *
 * TESTS: firmware/test/test_wire  (every row of spec/vectors.json is in there)
 */
#ifndef SKYNOID_WIRE_H
#define SKYNOID_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- sizes (all in bytes) ---------------------------------------------- */
#define SKW_VERSION      1u
#define SKW_HEADER_LEN   10u
#define SKW_CRC_LEN      2u
#define SKW_MAX_PAYLOAD  192u
#define SKW_MAX_BODY     (SKW_HEADER_LEN + SKW_MAX_PAYLOAD + SKW_CRC_LEN) /* 204 */
/* COBS adds at most 1 byte per 254, plus 1; the two frame delimiters add 2. */
#define SKW_MAX_WIRE     (SKW_MAX_BODY + (SKW_MAX_BODY / 254u) + 1u + 2u)

/* ---- message types (header field "type") ------------------------------- */
typedef enum {
    SKW_T_HELLO  = 1,
    SKW_T_CONFIG = 2,
    SKW_T_CMD    = 3,
    SKW_T_ACK    = 4,
    SKW_T_NACK   = 5,
    SKW_T_TLM    = 6,
    SKW_T_EVT    = 7,
    SKW_T_LOG    = 8,
    SKW_T_PING   = 9,
    SKW_T_PONG   = 10
} skw_type_t;

/* One decoded frame. "ver" is not stored: only version 1 is accepted. */
typedef struct {
    uint8_t  type;                    /* skw_type_t */
    uint8_t  msg;                     /* message id inside the type */
    uint8_t  flags;                   /* reserved: send 0, ignore on receive */
    uint16_t seq;                     /* sender's own frame counter */
    uint16_t ack;                     /* seq of the frame being answered, else 0 */
    uint16_t len;                     /* payload length, at most SKW_MAX_PAYLOAD */
    uint8_t  payload[SKW_MAX_PAYLOAD];
} skw_frame_t;

/* Result of decoding one frame (the bytes between two 0x00 delimiters). */
typedef enum {
    SKW_OK = 0,
    SKW_ERR_COBS,     /* not valid COBS */
    SKW_ERR_SHORT,    /* fewer than header + crc bytes */
    SKW_ERR_CRC,      /* checksum does not match */
    SKW_ERR_VERSION,  /* header version is not 1 */
    SKW_ERR_LENGTH    /* len field disagrees with the real payload size */
} skw_status_t;

/* ---- building blocks ---------------------------------------------------- */

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection).
 * Check value: crc of the ASCII text "123456789" is 0x29B1. */
uint16_t skw_crc16(const uint8_t *data, size_t len);

/* COBS encode. out must hold at least len + len/254 + 1 bytes.
 * Returns the number of bytes written. */
size_t skw_cobs_encode(const uint8_t *in, size_t len, uint8_t *out);

/* COBS decode. Returns the decoded length, or -1 if the input is invalid
 * or does not fit in cap bytes. */
int skw_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

/* ---- whole frames ------------------------------------------------------- */

/* Build 0x00 + COBS(body) + 0x00 into out. Returns bytes written, or 0 if
 * the payload is too long or cap is too small (cap SKW_MAX_WIRE is always enough). */
size_t skw_frame_encode(const skw_frame_t *f, uint8_t *out, size_t cap);

/* Decode the bytes BETWEEN two delimiters (no 0x00 bytes) into a frame. */
skw_status_t skw_frame_decode(const uint8_t *seg, size_t len, skw_frame_t *out);

/* ---- byte-by-byte parser ----------------------------------------------- */
/*
 * The parser separates two kinds of traffic on one UART:
 *   - frames:  start with 0x00, so they are easy to recognise;
 *   - text:    typed console input or log text, which never contains 0x00.
 * Rule: a byte that arrives OUTSIDE a frame is text. This lets one UART carry
 * both a binary link and a human console (see vault note 06-link-and-debug-options).
 */
typedef enum {
    SKW_EV_NONE = 0,   /* byte consumed, nothing to report yet */
    SKW_EV_FRAME,      /* a valid frame is in *out */
    SKW_EV_BAD,        /* a frame arrived but failed a check (see parser->last_error) */
    SKW_EV_TEXT        /* byte was outside a frame; it is in parser->text_byte */
} skw_event_t;

typedef struct {
    uint8_t      buf[SKW_MAX_WIRE]; /* bytes of the frame being received */
    size_t       n;                 /* how many bytes are in buf */
    bool         in_frame;          /* true after an opening 0x00 */
    bool         overflow;          /* too long: ignore bytes until the next 0x00 */
    uint8_t      text_byte;         /* valid after SKW_EV_TEXT */
    skw_status_t last_error;        /* valid after SKW_EV_BAD */
    uint32_t     frames_ok;         /* counters, for the "stats" command */
    uint32_t     frames_bad;
} skw_parser_t;

void        skw_parser_init(skw_parser_t *p);
skw_event_t skw_parser_feed(skw_parser_t *p, uint8_t byte, skw_frame_t *out);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_WIRE_H */
