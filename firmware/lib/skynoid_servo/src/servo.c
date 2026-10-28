/*
 * servo.c - the pure maths of the servo engine. See include/skynoid_servo.h for the rules and
 * test/test_servo for the checks.
 */
#include <stddef.h>

#include "skynoid_servo.h"

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

void sks_cal_default(sks_joint_cal_t *c)
{
    c->pulse_min_us = 700;
    c->pulse_max_us = 2300;
    c->min_deg_x10 = 0;
    c->max_deg_x10 = 1800;
    c->rest_deg_x10 = 900;
    c->offset_deg_x10 = 0;
    c->invert = false;
}

int32_t sks_clamp_deg_x10(const sks_joint_cal_t *c, int32_t deg_x10, bool *clamped)
{
    const int32_t v = clamp32(deg_x10, c->min_deg_x10, c->max_deg_x10);
    if (clamped != NULL) { *clamped = (v != deg_x10); }
    return v;
}

int32_t sks_deg_to_us(const sks_joint_cal_t *c, int32_t deg_x10, bool *clamped)
{
    int32_t a = sks_clamp_deg_x10(c, deg_x10, clamped);
    a = clamp32(a + c->offset_deg_x10, 0, 1800);
    if (c->invert) { a = 1800 - a; }
    const int64_t span = (int64_t)c->pulse_max_us - c->pulse_min_us;
    const int64_t us = (int64_t)c->pulse_min_us + (span * a + (span >= 0 ? 900 : -900)) / 1800;
    return clamp32((int32_t)us, SKS_PULSE_ABS_MIN_US, SKS_PULSE_ABS_MAX_US);
}

uint16_t sks_us_to_counts(int32_t us, int32_t period_us)
{
    if (us <= 0 || period_us <= 0) { return 0; }
    const int64_t n = ((int64_t)us * SKS_PCA_COUNTS + period_us / 2) / period_us;
    return (uint16_t)clamp32((int32_t)(n > 4095 ? 4095 : n), 0, 4095);
}

uint8_t sks_prescale(uint32_t osc_hz, uint32_t freq_hz)
{
    if (freq_hz == 0) { return 255; }
    const uint64_t div = (uint64_t)SKS_PCA_COUNTS * freq_hz;
    const int64_t p = (int64_t)((osc_hz + div / 2) / div) - 1;
    return (uint8_t)clamp32((int32_t)(p > 255 ? 255 : p), 3, 255);
}

uint32_t sks_minjerk_q16(uint32_t t_ms, uint32_t dur_ms)
{
    if (dur_ms == 0 || t_ms >= dur_ms) { return 65536u; }
    /* s = u^3 (10 - 15 u + 6 u^2), worked in Q30 with rounding and only rounded to Q16 at the end. The first version
     * used Q16 all the way: the cancellation between the three terms cost up to 12 counts and made the curve step
     * backwards at about 12 percent of the move (found by test_servo on the PC). Products stay below 2^64. */
    const uint64_t ONE = 1ull << 30;
    const uint64_t u = (((uint64_t)t_ms << 30) + dur_ms / 2u) / dur_ms;            /* Q30, 0 .. 2^30 */
    const uint64_t u2 = (u * u + ONE / 2u) >> 30;
    const uint64_t u3 = (u2 * u + ONE / 2u) >> 30;
    const uint64_t poly = 10u * ONE + 6u * u2 - 15u * u;                            /* 10 - 15u + 6u^2, always > 0 */
    const uint64_t s30 = (u3 * poly + ONE / 2u) >> 30;
    uint64_t s16 = (s30 + (1u << 13)) >> 14;
    if (s16 > 65536u) { s16 = 65536u; }
    return (uint32_t)s16;
}

uint32_t sks_min_duration_ms(int32_t delta_deg_x10, int32_t vmax_dps)
{
    if (vmax_dps <= 0) { return 0; }
    const int64_t d = delta_deg_x10 < 0 ? -(int64_t)delta_deg_x10 : delta_deg_x10;
    const int64_t den = 10LL * vmax_dps;
    return (uint32_t)((1875 * d + den - 1) / den);               /* ceil(1.875 * |delta deg| / vmax s) in ms */
}

void sks_move_start(sks_move_t *m, int32_t from_deg_x10, int32_t to_deg_x10, uint32_t req_dur_ms,
                    int32_t vmax_dps, uint32_t now_ms)
{
    const uint32_t need = sks_min_duration_ms(to_deg_x10 - from_deg_x10, vmax_dps);
    m->from_x10 = from_deg_x10;
    m->to_x10 = to_deg_x10;
    m->start_ms = now_ms;
    m->dur_ms = req_dur_ms > need ? req_dur_ms : need;
    m->active = (from_deg_x10 != to_deg_x10) && (m->dur_ms > 0);
}

int32_t sks_move_pos(const sks_move_t *m, uint32_t now_ms)
{
    if (!m->active) { return m->to_x10; }
    const uint32_t el = (uint32_t)(now_ms - m->start_ms);
    if (el >= m->dur_ms) { return m->to_x10; }
    const int64_t s = sks_minjerk_q16(el, m->dur_ms);
    const int64_t delta = (int64_t)m->to_x10 - m->from_x10;
    return (int32_t)(m->from_x10 + (delta * s) / 65536);
}

bool sks_move_done(const sks_move_t *m, uint32_t now_ms)
{
    return !m->active || (uint32_t)(now_ms - m->start_ms) >= m->dur_ms;
}

int32_t sks_joint_weight(int joint)
{
    switch (joint) {
    case SKS_HEAD_YAW:
    case SKS_HEAD_PITCH:
    case SKS_ARM_L_ELBOW:
    case SKS_ARM_R_ELBOW:
        return 1;
    default:
        return 2;                                                /* shoulders, and any unknown joint: the safe side */
    }
}

bool sks_budget_admit(int32_t moving_weight_sum, int32_t new_weight, int32_t budget)
{
    return moving_weight_sum + new_weight <= budget;
}

bool sks_stagger_ok(uint32_t last_start_ms, uint32_t now_ms, uint32_t gap_ms)
{
    return (uint32_t)(now_ms - last_start_ms) >= gap_ms;
}

int32_t sks_current_scale_pct(int32_t ma, int32_t soft_ma, int32_t hard_ma)
{
    if (hard_ma > 0 && ma >= hard_ma) { return 0; }
    if (soft_ma <= 0 || ma <= soft_ma) { return 100; }
    if (hard_ma <= soft_ma) { return 50; }                       /* no room to interpolate: go to the floor */
    return 100 - (50 * (ma - soft_ma)) / (hard_ma - soft_ma);
}
