/* maint.c - maintenance timer, mute flags, loop statistics and printout helpers. Pure logic; see
 * skynoid_maint.h for the contract. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "skynoid_maint.h"

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;      /* correct across the 49 day wrap */
}

void skm_init(skm_t *m)
{
    m->on = false;
    m->deadline_ms = 0;
    m->mute = 0;
}

skm_result_t skm_enter(skm_t *m, uint32_t now_ms, int32_t minutes, bool *newly)
{
    if (minutes < 0 || minutes > SKM_MAX_MIN) {
        return SKM_BAD_MINUTES;                 /* refuse, do not guess what a typo meant */
    }
    if (minutes == 0) {
        minutes = SKM_DEFAULT_MIN;
    }
    const bool was_off = !m->on;
    if (was_off) {
        m->mute = 0;                            /* a fresh session starts with nothing muted */
    }
    m->on = true;
    m->deadline_ms = now_ms + (uint32_t)minutes * 60000u;   /* renewing moves the deadline to now + minutes */
    if (newly) {
        *newly = was_off;
    }
    return SKM_OK;
}

bool skm_exit(skm_t *m)
{
    const bool was_on = m->on;
    m->on = false;
    m->deadline_ms = 0;
    m->mute = 0;
    return was_on;
}

bool skm_expired(const skm_t *m, uint32_t now_ms)
{
    return m->on && time_reached(now_ms, m->deadline_ms);
}

uint32_t skm_remaining_s(const skm_t *m, uint32_t now_ms)
{
    if (!m->on || time_reached(now_ms, m->deadline_ms)) {
        return 0;
    }
    const uint32_t left_ms = m->deadline_ms - now_ms;
    return (left_ms + 999u) / 1000u;            /* round up so "1 s left" never shows as 0 */
}

static bool word_is(const char *word, const char *expect)
{
    while (*word && *expect) {
        if (tolower((unsigned char)*word) != *expect) {
            return false;
        }
        word++;
        expect++;
    }
    return *word == '\0' && *expect == '\0';
}

bool skm_mute_apply(skm_t *m, const char *word)
{
    if (word_is(word, "tlm"))       { m->mute |= SKM_MUTE_TLM; }
    else if (word_is(word, "evt"))  { m->mute |= SKM_MUTE_EVT; }
    else if (word_is(word, "log"))  { m->mute |= SKM_MUTE_LOG; }
    else if (word_is(word, "all"))  { m->mute = SKM_MUTE_ALL; }
    else if (word_is(word, "none")) { m->mute = 0; }
    else { return false; }
    return true;
}

bool skm_muted(const skm_t *m, uint8_t stream)
{
    return m->on && (m->mute & stream) != 0;
}

/* ---- loop statistics ---- */
void sks_loop_init(sks_loop_t *s)
{
    memset(s, 0, sizeof *s);
}

void sks_loop_record(sks_loop_t *s, uint32_t duration_us, uint32_t period_us)
{
    s->count++;
    s->last_us = duration_us;
    if (duration_us > s->max_us) {
        s->max_us = duration_us;
    }
    s->sum_us += duration_us;
    if (duration_us > period_us) {
        s->late++;
    }
}

uint32_t sks_loop_avg_us(const sks_loop_t *s)
{
    return s->count ? (uint32_t)(s->sum_us / s->count) : 0u;
}

int skm_format_loop(char *out, size_t cap, const char *name, const sks_loop_t *s)
{
    return snprintf(out, cap, "%-8s n=%lu avg=%luus max=%luus late=%lu", name,
                    (unsigned long)s->count, (unsigned long)sks_loop_avg_us(s),
                    (unsigned long)s->max_us, (unsigned long)s->late);
}

/* ---- printout helpers ---- */
const char *skm_reset_name(uint8_t reason)
{
    static const char *const NAMES[] = { "unknown", "power-on", "brownout", "watchdog", "software", "panic" };
    return reason < sizeof NAMES / sizeof NAMES[0] ? NAMES[reason] : "unknown";
}

int skm_format_hms(char *out, size_t cap, uint32_t seconds)
{
    const unsigned long d = seconds / 86400u;
    const unsigned long h = (seconds % 86400u) / 3600u;
    const unsigned long m = (seconds % 3600u) / 60u;
    const unsigned long s = seconds % 60u;
    if (d > 0) {
        return snprintf(out, cap, "%lud %02lu:%02lu:%02lu", d, h, m, s);
    }
    return snprintf(out, cap, "%02lu:%02lu:%02lu", h, m, s);
}
