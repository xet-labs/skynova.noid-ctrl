/* frame.c - build and read whole frames, and the byte-by-byte parser.
 *
 * Body layout before COBS (little endian):
 *   [0] ver  [1] type  [2] msg  [3] flags  [4..5] seq  [6..7] ack  [8..9] len
 *   [10 .. 10+len-1] payload   [last 2] crc16 of everything before it
 */
#include <string.h>
#include "skynoid_wire.h"

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

size_t skw_frame_encode(const skw_frame_t *f, uint8_t *out, size_t cap)
{
    uint8_t body[SKW_MAX_BODY];

    if (f->len > SKW_MAX_PAYLOAD || cap < SKW_MAX_WIRE) {
        return 0;   /* we ask for the worst-case size so the caller cannot get it wrong */
    }
    body[0] = (uint8_t)SKW_VERSION;
    body[1] = f->type;
    body[2] = f->msg;
    body[3] = f->flags;
    put_u16(&body[4], f->seq);
    put_u16(&body[6], f->ack);
    put_u16(&body[8], f->len);
    memcpy(&body[SKW_HEADER_LEN], f->payload, f->len);

    size_t body_len = SKW_HEADER_LEN + f->len;
    put_u16(&body[body_len], skw_crc16(body, body_len));
    body_len += SKW_CRC_LEN;

    out[0] = 0x00;                                     /* opening delimiter */
    size_t n = skw_cobs_encode(body, body_len, &out[1]);
    out[1 + n] = 0x00;                                 /* closing delimiter */
    return n + 2;
}

skw_status_t skw_frame_decode(const uint8_t *seg, size_t len, skw_frame_t *out)
{
    uint8_t body[SKW_MAX_BODY];

    int n = skw_cobs_decode(seg, len, body, sizeof body);
    if (n < 0) {
        return SKW_ERR_COBS;
    }
    if ((size_t)n < SKW_HEADER_LEN + SKW_CRC_LEN) {
        return SKW_ERR_SHORT;
    }
    size_t body_len = (size_t)n - SKW_CRC_LEN;
    if (get_u16(&body[body_len]) != skw_crc16(body, body_len)) {
        return SKW_ERR_CRC;
    }
    if (body[0] != SKW_VERSION) {
        return SKW_ERR_VERSION;
    }
    uint16_t plen = get_u16(&body[8]);
    if (plen != body_len - SKW_HEADER_LEN || plen > SKW_MAX_PAYLOAD) {
        return SKW_ERR_LENGTH;
    }
    out->type  = body[1];
    out->msg   = body[2];
    out->flags = body[3];
    out->seq   = get_u16(&body[4]);
    out->ack   = get_u16(&body[6]);
    out->len   = plen;
    memcpy(out->payload, &body[SKW_HEADER_LEN], plen);
    return SKW_OK;
}

/* ---- parser ------------------------------------------------------------- */

void skw_parser_init(skw_parser_t *p)
{
    memset(p, 0, sizeof *p);
}

skw_event_t skw_parser_feed(skw_parser_t *p, uint8_t byte, skw_frame_t *out)
{
    if (byte == 0x00) {
        if (!p->in_frame) {            /* opening delimiter: start collecting */
            p->in_frame = true;
            p->n = 0;
            p->overflow = false;
            return SKW_EV_NONE;
        }
        if (p->n == 0 && !p->overflow) {
            return SKW_EV_NONE;        /* two delimiters in a row: still waiting */
        }
        /* closing delimiter: check what we collected */
        skw_event_t ev;
        if (p->overflow) {
            p->last_error = SKW_ERR_LENGTH;
            ev = SKW_EV_BAD;
        } else {
            p->last_error = skw_frame_decode(p->buf, p->n, out);
            ev = (p->last_error == SKW_OK) ? SKW_EV_FRAME : SKW_EV_BAD;
        }
        if (ev == SKW_EV_FRAME) { p->frames_ok++; } else { p->frames_bad++; }
        p->in_frame = false;
        p->n = 0;
        p->overflow = false;
        return ev;
    }

    if (!p->in_frame) {                /* outside a frame: this is text */
        p->text_byte = byte;
        return SKW_EV_TEXT;
    }
    if (p->overflow) {
        return SKW_EV_NONE;            /* too long: ignore until the next 0x00 */
    }
    if (p->n >= sizeof p->buf) {
        p->overflow = true;
        return SKW_EV_NONE;
    }
    p->buf[p->n++] = byte;
    return SKW_EV_NONE;
}
