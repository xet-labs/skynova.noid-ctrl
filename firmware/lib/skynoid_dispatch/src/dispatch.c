/*
 * dispatch.c - CMD frame decoding, ACK/NACK choice, telemetry timing, TLM and EVT payload packing (pure C).
 * See include/skynoid_dispatch.h for the rules and test/test_dispatch for the checks.
 */
#include "skynoid_dispatch.h"

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int32_t  rd_i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static void wr_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void reply_ack(skd_reply_t *r) { r->is_ack = true; r->code = 0; r->detail = 0; }
static void reply_nack(skd_reply_t *r, uint8_t code, uint16_t detail)
{
    r->is_ack = false; r->code = code; r->detail = detail;
}

/* A base function result: 0 = accepted. hazard_blocked and invalid_arg carry the detail of the refusal. */
static void reply_from(const skd_ops_t *ops, int rc, skd_reply_t *r)
{
    if (rc == 0) { reply_ack(r); return; }
    uint16_t detail = 0;
    if ((rc == SKB_NACK_HAZARD_BLOCKED || rc == SKB_NACK_INVALID_ARG) && ops->last_detail != NULL) {
        detail = ops->last_detail(ops->ctx);
    }
    reply_nack(r, (uint8_t)rc, detail);
}

/* ---- stream ---- */
void skd_stream_init(skd_stream_t *s) { s->on = false; s->period_ms = 0; s->next_ms = 0; }
void skd_stream_stop(skd_stream_t *s) { s->on = false; }

bool skd_stream_due(skd_stream_t *s, uint32_t now_ms)
{
    if (!s->on || (int32_t)(now_ms - s->next_ms) < 0) { return false; }
    s->next_ms = now_ms + s->period_ms;
    return true;
}

/* ---- commands ---- */
static size_t expected_len(uint8_t msg)
{
    switch (msg) {
    case SKD_CMD_SUBSCRIBE: return 3;
    case SKD_CMD_SET_PARAM: return 5;
    case SKD_CMD_DRIVE:     return 6;
    default:                return 0;     /* arm, disarm, estop, clear_estop, stop, cal_floor */
    }
}

void skd_handle_cmd(const skd_ops_t *ops, skd_stream_t *stream, uint8_t msg,
                    const uint8_t *p, uint16_t len, uint32_t now_ms, skd_reply_t *r)
{
    int (*simple)(void *) = NULL;
    switch (msg) {
    case SKD_CMD_ARM:         simple = ops->arm; break;
    case SKD_CMD_DISARM:      simple = ops->disarm; break;
    case SKD_CMD_ESTOP:       simple = ops->estop; break;
    case SKD_CMD_CLEAR_ESTOP: simple = ops->clear_estop; break;
    case SKD_CMD_STOP:        simple = ops->stop; break;
    case SKD_CMD_CAL_FLOOR:   simple = ops->cal_floor; break;
    case SKD_CMD_SUBSCRIBE:
    case SKD_CMD_SET_PARAM:
    case SKD_CMD_DRIVE:       break;
    default:                  reply_nack(r, SKD_NACK_UNKNOWN_MSG, 0); return;   /* includes console_line for now */
    }

    if (len != expected_len(msg) || (len > 0 && p == NULL)) {
        reply_nack(r, SKD_NACK_BAD_LEN, (uint16_t)expected_len(msg));
        return;
    }

    if (msg == SKD_CMD_SUBSCRIBE) {
        if (stream == NULL) { reply_nack(r, SKD_NACK_UNKNOWN_MSG, 0); return; }
        if (p[0] != SKD_STREAM_BASE_STATUS) { reply_nack(r, SKB_NACK_INVALID_ARG, 0); return; }
        const uint16_t period = rd_u16(p + 1);
        if (period == 0) {
            stream->on = false;
        } else {
            stream->on = true;
            stream->period_ms = period;
            stream->next_ms = now_ms;                       /* the first TLM is due at once */
        }
        reply_ack(r);
        return;
    }
    if (msg == SKD_CMD_SET_PARAM) {
        if (ops->set_param == NULL) { reply_nack(r, SKD_NACK_UNKNOWN_MSG, 0); return; }
        const int rc = ops->set_param(ops->ctx, p[0], rd_i32(p + 1));
        if (rc == 0) { reply_ack(r); }
        else { reply_nack(r, SKB_NACK_INVALID_ARG, (uint16_t)(rc == SKP_UNKNOWN_ID ? 0 : 1)); }
        return;
    }
    if (msg == SKD_CMD_DRIVE) {
        if (ops->drive == NULL) { reply_nack(r, SKD_NACK_UNKNOWN_MSG, 0); return; }
        const int16_t v = (int16_t)rd_u16(p);
        const int16_t w = (int16_t)rd_u16(p + 2);
        const uint16_t ttl = rd_u16(p + 4);
        reply_from(ops, ops->drive(ops->ctx, v, w, ttl), r);
        return;
    }
    if (simple == NULL) { reply_nack(r, SKD_NACK_UNKNOWN_MSG, 0); return; }
    reply_from(ops, simple(ops->ctx), r);
}

/* ---- packers ---- */
size_t skd_build_status(uint8_t out[SKD_STATUS_LEN], const skd_status_t *s)
{
    out[0] = s->hazard;
    wr_u16(out + 1, s->range_mm);
    out[3] = s->range_status;
    wr_u16(out + 4, s->batt_mv);
    for (int i = 0; i < 4; i++) { wr_u16(out + 6 + 2 * i, s->ir_raw[i]); }
    wr_u16(out + 14, s->motor_ma);
    out[16] = s->state;
    return SKD_STATUS_LEN;
}

int skd_build_event(uint8_t out[2], uint8_t *msg, const skb_event_t *ev)
{
    *msg = ev->id;
    switch (ev->id) {
    case SKB_EV_CLIFF:
    case SKB_EV_HAZARD_CLEARED:
    case SKB_EV_NODE_FAULT:
        out[0] = (uint8_t)ev->a;
        return 1;
    case SKB_EV_BATT_LOW:
    case SKB_EV_BATT_CRIT:
    case SKB_EV_STALL:
        wr_u16(out, ev->a);
        return 2;
    case SKB_EV_DRIVE_EXPIRED:
    case SKB_EV_LINK_LOST:
        return 0;
    case SKB_EV_SENSOR_FAULT:
        out[0] = (uint8_t)ev->a;
        out[1] = ev->b;
        return 2;
    default:
        *msg = 0;
        return -1;
    }
}

size_t skd_build_nack(uint8_t out[3], uint8_t code, uint16_t detail)
{
    out[0] = code;
    wr_u16(out + 1, detail);
    return 3;
}
