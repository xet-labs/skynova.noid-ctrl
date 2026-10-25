/*
 * ring.c - a small byte ring that survives a reset (the flight recorder), and
 *          the pattern timing of the buzzer, and a debouncer.
 *
 * Purpose : three tiny helpers with no hardware in them.
 * Called by: core/recorder (ring), hal/buzzer + features/power (buzzer), features/touch (debounce).
 * Test    : test_logic.c, test_ring_*, test_deb_*, test_buzzer_*.
 *
 * The ring header (magic, head, len) is kept apart from the data buffer so the
 * header can sit in RTC noinit RAM next to the buffer. After a reset the header
 * is only trusted if the magic (which includes the capacity) and the indices
 * make sense; otherwise the ring starts empty. Random power-on RAM fails the check.
 */
#include "skynoid_core.h"

#define RING_MAGIC 0x534B5247u      /* "SKRG" */

void skl_ring_attach(skl_ring_t *r, uint32_t cap)
{
    if (r->magic != (RING_MAGIC ^ cap) || r->head >= cap || r->len > cap) {
        r->magic = RING_MAGIC ^ cap;
        r->head = 0;
        r->len = 0;
    }
}

void skl_ring_put(skl_ring_t *r, uint8_t *buf, uint32_t cap, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf[r->head] = data[i];
        r->head = (r->head + 1u) % cap;
        if (r->len < cap) { r->len++; }             /* full: the oldest byte is overwritten */
    }
}

size_t skl_ring_read(const skl_ring_t *r, const uint8_t *buf, uint32_t cap, uint8_t *out, size_t out_cap)
{
    const size_t n = (r->len < out_cap) ? r->len : out_cap;
    /* if the caller's buffer is smaller than the ring, keep the NEWEST n bytes */
    uint32_t idx = (r->head + cap - (uint32_t)n) % cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = buf[idx];
        idx = (idx + 1u) % cap;
    }
    return n;
}

/* ---- debounce ------------------------------------------------------------ */
void skl_deb_init(skl_deb_t *d, bool level)
{
    d->stable = level;
    d->cand = level;
    d->since_ms = 0;
}

bool skl_deb_step(skl_deb_t *d, bool level, uint32_t now_ms, uint32_t hold_ms)
{
    if (level == d->stable) { d->cand = level; return false; }   /* nothing to confirm */
    if (level != d->cand) { d->cand = level; d->since_ms = now_ms; }
    if ((uint32_t)(now_ms - d->since_ms) >= hold_ms) {
        d->stable = level;
        return true;
    }
    return false;
}

/* ---- buzzer patterns (timings from 02-base-node.md, Buzzer table) --------- */
bool skl_buzzer_on(uint8_t pattern, uint32_t t)
{
    switch (pattern) {
    case 1:  return t < 80u;                                          /* chirp */
    case 2:  return t < 80u || (t >= 160u && t < 240u);               /* double chirp */
    case 3:  return t < 600u && ((t / 120u) % 2u) == 0u;              /* three 120 ms beeps */
    case 4:  return (t % 2000u) < 1000u;                              /* critical: 1 s on, 1 s off, forever */
    case 5:  return t < 400u;                                         /* error */
    default: return false;                                            /* 0 = off */
    }
}

bool skl_buzzer_done(uint8_t pattern, uint32_t t)
{
    switch (pattern) {
    case 1:  return t >= 80u;
    case 2:  return t >= 240u;
    case 3:  return t >= 600u;
    case 4:  return false;
    case 5:  return t >= 400u;
    default: return true;
    }
}
