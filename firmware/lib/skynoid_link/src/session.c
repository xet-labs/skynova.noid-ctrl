/*
 * session.c - session timing and PING/PONG/HELLO payload packing (pure C).
 * See include/skynoid_link.h for the rules and test/test_link for the checks.
 */
#include "skynoid_link.h"

void sklk_init(sklk_session_t *s)
{
    s->active = false;
    s->ever_ended = false;
    s->last_ping_ms = 0;
    s->ended_ms = 0;
}

bool sklk_on_ping(sklk_session_t *s, uint32_t now_ms)
{
    const bool starts = !s->active;
    s->active = true;
    s->last_ping_ms = now_ms;
    return starts;
}

sklk_tick_t sklk_tick(sklk_session_t *s, uint32_t now_ms, uint32_t timeout_ms)
{
    if (s->active && (uint32_t)(now_ms - s->last_ping_ms) >= timeout_ms) {
        s->active = false;
        s->ever_ended = true;
        s->ended_ms = now_ms;
        return SKLK_TICK_SESSION_ENDED;
    }
    return SKLK_TICK_NONE;
}

bool sklk_frames_allowed(const sklk_session_t *s, uint32_t now_ms, uint32_t hold_ms)
{
    if (s->active) { return true; }
    return s->ever_ended && (uint32_t)(now_ms - s->ended_ms) < hold_ms;
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

bool sklk_parse_ping(const uint8_t *payload, uint16_t len, uint32_t *host_t_ms)
{
    if (payload == NULL || host_t_ms == NULL || len != SKLK_PING_LEN) { return false; }
    *host_t_ms = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                 ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
    return true;
}

size_t sklk_build_pong(uint8_t out[SKLK_PONG_LEN], uint32_t host_t_ms, uint32_t node_t_ms, uint8_t hazard)
{
    put_u32(out, host_t_ms);
    put_u32(out + 4, node_t_ms);
    out[8] = hazard;
    return SKLK_PONG_LEN;
}

size_t sklk_build_hello(uint8_t out[SKLK_HELLO_LEN], const sklk_hello_t *h)
{
    out[0] = h->role;
    put_u16(out + 1, h->caps);
    out[3] = h->fw_major;
    out[4] = h->fw_minor;
    out[5] = h->proto;
    out[6] = h->reset_reason;
    for (int i = 0; i < 6; i++) { out[7 + i] = h->chip_id[i]; }
    put_u16(out + 13, h->boot_count);
    put_u32(out + 15, h->schema_hash);
    return SKLK_HELLO_LEN;
}
