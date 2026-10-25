/*
 * motion.c - wheel mixing, ramping and speed-to-duty conversion (integer maths only).
 *
 * Purpose : the numbers behind "drive(v, w)": split into two wheel speeds, limit
 *           acceleration, and turn a speed into a PWM duty without encoders.
 * Called by: firmware/src/features/drive.c (10 ms motion task).
 * Test    : test_logic.c, test_motion_*.
 * Spec    : 04-firmware/02-base-node.md "Drive control", config-defaults.yaml.
 */
#include "skynoid_logic.h"

int32_t skl_clamp(int32_t x, int32_t lo, int32_t hi)
{
    if (x < lo) { return lo; }
    if (x > hi) { return hi; }
    return x;
}

/* v_left = v - w_rad * base / 2, v_right = v + w_rad * base / 2, with
 * w_rad = w * pi / 180. So delta = w * base * pi / 360. pi is 31416/10000. */
skl_wheels_t skl_mix(int32_t v_mm_s, int32_t w_dps, int32_t wheel_base_mm)
{
    const int64_t num = (int64_t)w_dps * wheel_base_mm * 31416;
    const int64_t half = (num >= 0) ? 1800000 : -1800000;          /* round to nearest */
    const int32_t delta = (int32_t)((num + half) / 3600000);
    skl_wheels_t out = { v_mm_s - delta, v_mm_s + delta };
    return out;
}

int32_t skl_ramp_step(int32_t accel_mm_s2, int32_t dt_ms)
{
    int32_t step = (int32_t)(((int64_t)accel_mm_s2 * dt_ms) / 1000);
    return (step < 1) ? 1 : step;
}

int32_t skl_ramp(int32_t cur, int32_t target, int32_t max_step)
{
    if (target > cur + max_step) { return cur + max_step; }
    if (target < cur - max_step) { return cur - max_step; }
    return target;
}

int32_t skl_duty_uncal(int32_t speed_mm_s, int32_t speed_cap_mm_s, int32_t cap_pct)
{
    if (speed_cap_mm_s <= 0 || cap_pct <= 0) { return 0; }
    const int64_t num = (int64_t)speed_mm_s * cap_pct;
    const int64_t half = (num >= 0) ? speed_cap_mm_s / 2 : -(speed_cap_mm_s / 2);
    return skl_clamp((int32_t)((num + half) / speed_cap_mm_s), -cap_pct, cap_pct);
}

int32_t skl_duty_cal(int32_t speed_mm_s, int32_t duty_per_mm_s_x1000, int32_t batt_mv,
                     int32_t min_start_pct, int32_t cap_pct)
{
    if (speed_mm_s == 0 || cap_pct <= 0) { return 0; }
    if (batt_mv <= 0) { batt_mv = 12000; }                        /* no reading: assume nominal */
    /* duty = speed * k * (12 V / V_batt): a sagging pack gets proportionally more duty */
    const int64_t num = (int64_t)speed_mm_s * duty_per_mm_s_x1000 * 12000;
    const int64_t den = (int64_t)1000 * batt_mv;
    int32_t duty = (int32_t)(num / den);
    duty += (speed_mm_s > 0) ? min_start_pct : -min_start_pct;    /* beat stiction */
    return skl_clamp(duty, -cap_pct, cap_pct);
}
