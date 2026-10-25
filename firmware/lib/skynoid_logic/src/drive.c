/* drive.c - helpers, hazard rule, drive maths, mailbox (TTL) and motion step.
 * Pure logic, see skynoid_logic.h for the contract and units. */
#include "skynoid_logic.h"

/* ---- helpers ---------------------------------------------------------- */
int32_t skl_clamp(int32_t x, int32_t lo, int32_t hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

int32_t skl_median3(int32_t a, int32_t b, int32_t c)
{
    if (a > b) { int32_t t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return a > b ? a : b;           /* after the swaps: max(a, min(b, c)) */
}

int32_t skl_median_n(int32_t *v, size_t n)
{
    for (size_t i = 1; i < n; i++) {        /* insertion sort: n is tiny (about 40 samples) */
        int32_t key = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; j--; }
        v[j] = key;
    }
    return v[n / 2];
}

bool skl_time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;  /* correct across the 49 day wrap */
}

/* ---- hazard ----------------------------------------------------------- */
bool skl_hazard_allows(uint8_t mask, int32_t v, int32_t w)
{
    mask &= SKL_HZ_ALL;
    if (mask == 0 || (v == 0 && w == 0)) { return true; }
    if (w != 0) { return false; }                       /* no rotation near an edge */
    const bool front = (mask & SKL_HZ_FRONT_ANY) != 0;
    const bool rear  = (mask & SKL_HZ_REAR_ANY) != 0;
    if (front && rear) { return false; }
    if (front) { return v <= 0; }
    if (rear)  { return v >= 0; }
    return false;                                       /* fault or unreliable floor only */
}

/* ---- drive ------------------------------------------------------------ */
static int32_t cap_mag(int32_t x, int32_t cap)
{
    if (cap == SKL_UNSET || cap < 0) { cap = 0; }
    return skl_clamp(x, -cap, cap);
}

void skl_cap_command(int32_t *v, int32_t *w, int32_t speed_cap, int32_t turn_cap)
{
    *v = cap_mag(*v, speed_cap);
    *w = cap_mag(*w, turn_cap);
}

bool skl_mix(int32_t v, int32_t w_dps, int32_t wheel_base_mm, int32_t *left, int32_t *right)
{
    if (wheel_base_mm == SKL_UNSET || wheel_base_mm <= 0) { return false; }
    /* wheel speed difference = w[rad/s] * base; pi/180 = 17453 / 1000000 */
    const int64_t diff = ((int64_t)w_dps * wheel_base_mm * 17453) / 1000000;
    *left  = v - (int32_t)(diff / 2);
    *right = v + (int32_t)(diff - diff / 2);
    return true;
}

int32_t skl_ramp(int32_t cur, int32_t target, int32_t accel_mm_s2, uint32_t dt_ms)
{
    int64_t step = ((int64_t)accel_mm_s2 * (int64_t)dt_ms) / 1000;
    if (step < 1) { step = 1; }
    if (target > cur) { return (target - cur > step) ? cur + (int32_t)step : target; }
    return (cur - target > step) ? cur - (int32_t)step : target;
}

bool skl_duty_permille(int32_t speed, int32_t k_x1000, int32_t batt_mv, int32_t min_start_pct,
                       int32_t speed_cap, int32_t uncal_cap_pct, int32_t *duty_out)
{
    const int32_t sign = speed < 0 ? -1 : 1;
    const int64_t mag = speed < 0 ? -(int64_t)speed : speed;
    int64_t duty;
    bool calibrated = (k_x1000 != SKL_UNSET && k_x1000 > 0);

    if (mag == 0) { *duty_out = 0; return calibrated; }
    if (calibrated) {
        const int32_t batt = skl_clamp(batt_mv, 8000, 13000);    /* a missing reading must not divide by 0 */
        duty = (mag * k_x1000 * 120) / batt;                     /* permille; see header for the derivation */
        if (min_start_pct != SKL_UNSET && min_start_pct > 0 && duty < (int64_t)min_start_pct * 10) {
            duty = (int64_t)min_start_pct * 10;
        }
        if (duty > 1000) { duty = 1000; }
    } else {
        const int64_t cap = (int64_t)uncal_cap_pct * 10;
        duty = (speed_cap > 0) ? (mag * cap) / speed_cap : 0;
        if (duty > cap) { duty = cap; }
    }
    *duty_out = sign * (int32_t)duty;
    return calibrated;
}

/* ---- mailbox (R2 deadman) -------------------------------------------- */
void skl_mb_put(skl_mailbox_t *mb, int32_t v, int32_t w, uint32_t ttl_ms, uint32_t now_ms)
{
    mb->v = v;
    mb->w = w;
    mb->expires_ms = now_ms + ttl_ms;
    mb->active = true;
}

void skl_mb_clear(skl_mailbox_t *mb)
{
    mb->active = false;
    mb->v = mb->w = 0;
}

skl_mb_state_t skl_mb_get(skl_mailbox_t *mb, uint32_t now_ms, int32_t *v, int32_t *w)
{
    if (!mb->active) { *v = *w = 0; return SKL_MB_IDLE; }
    if (skl_time_reached(now_ms, mb->expires_ms)) {
        skl_mb_clear(mb);
        *v = *w = 0;
        return SKL_MB_EXPIRED;                          /* reported once */
    }
    *v = mb->v;
    *w = mb->w;
    return SKL_MB_ACTIVE;
}

/* ---- motion ----------------------------------------------------------- */
void skl_motion_reset(skl_motion_t *m)
{
    m->last_sign[0] = m->last_sign[1] = 0;
    m->hold_until_ms = 0;
    m->en = false;
}

void skl_motion_step(skl_motion_t *m, const int32_t target_permille[2], uint32_t now_ms,
                     uint32_t brake_hold_ms, skl_motion_out_t *out)
{
    const bool moving = (target_permille[0] != 0) || (target_permille[1] != 0);

    for (int i = 0; i < 2; i++) {
        const int8_t sign = target_permille[i] > 0 ? 1 : (target_permille[i] < 0 ? -1 : 0);
        if (sign != 0 && m->last_sign[i] != 0 && sign != m->last_sign[i]) {
            out->duty_permille[i] = 0;                  /* reversal: one dead cycle with both inputs low */
            m->last_sign[i] = 0;
        } else {
            out->duty_permille[i] = target_permille[i];
            m->last_sign[i] = sign;
        }
    }

    if (moving) {
        m->en = true;
        m->hold_until_ms = now_ms + brake_hold_ms;
    } else if (m->en && skl_time_reached(now_ms, m->hold_until_ms)) {
        m->en = false;                                  /* brake window over: coast */
    }
    out->en = m->en;
    out->brake = m->en && !moving;
}
