/*
 * skynoid_logic.h - the robot's decision maths, as pure C (no ESP-IDF).
 *
 * WHAT THIS FILE IS
 *   Everything the base node must DECIDE, separated from everything it must
 *   DO. Tasks on the board read sensors, call these functions, and drive the
 *   pins with the answer. Because nothing here touches hardware, every rule
 *   is tested on the PC (pio test -e native) before it can hurt a robot.
 *
 * WHAT IS IN HERE (spec: 04-firmware/02-base-node.md, 02-system/04-safety-and-reflexes.md)
 *   hazard   which motions a hazard mask allows (R1 direction-aware rule)
 *   drive    cap, mix to wheel speeds, ramp, duty from speed (feed-forward)
 *   mailbox  latest drive command with a time to live (R2 deadman)
 *   motion   per-cycle wheel output: reversal dead cycle, brake then coast
 *   cliff    one IR sensor: debounce, hysteresis, rail faults (R1)
 *   floor    arming check: is the floor usable at all (dark floor case)
 *   battery  LOW / CRIT with persistence and hysteresis (R6)
 *
 * CONVENTIONS
 *   Times are uint32_t milliseconds and compared wrap-safe. A parameter that
 *   is "unknown until measured" is SKL_UNSET; functions refuse to use it
 *   instead of guessing (config-defaults.yaml).
 *   Speeds are mm/s, turn rates deg/s (positive = turn left), duty is
 *   permille (1000 = 100 percent), voltages mV, ADC counts are 12 bit.
 *
 * TESTS: firmware/test/test_logic
 */
#ifndef SKYNOID_LOGIC_H
#define SKYNOID_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKL_UNSET INT32_MIN

/* ---------------------------------------------------------------- helpers */
int32_t skl_clamp(int32_t x, int32_t lo, int32_t hi);
int32_t skl_median3(int32_t a, int32_t b, int32_t c);
int32_t skl_median_n(int32_t *v, size_t n);        /* sorts v in place; n >= 1 */
bool    skl_time_reached(uint32_t now, uint32_t deadline);   /* wrap-safe now >= deadline */

/* ----------------------------------------------------------------- hazard */
/* Hazard mask bits, same as the HAZARD field in PONG and TLM (protocol-v0). */
#define SKL_HZ_FRONT_LEFT   0x01u
#define SKL_HZ_FRONT_RIGHT  0x02u
#define SKL_HZ_REAR_LEFT    0x04u
#define SKL_HZ_REAR_RIGHT   0x08u
#define SKL_HZ_SENSOR_FAULT 0x10u
#define SKL_HZ_UNRELIABLE   0x20u
#define SKL_HZ_FRONT_ANY    (SKL_HZ_FRONT_LEFT | SKL_HZ_FRONT_RIGHT)
#define SKL_HZ_REAR_ANY     (SKL_HZ_REAR_LEFT | SKL_HZ_REAR_RIGHT)
#define SKL_HZ_ALL          0x3Fu

/* May the robot execute (v, w) with this hazard mask?
 *   no bit set            : yes
 *   v == 0 and w == 0     : always yes (stopping is never refused)
 *   any rotation          : no while any bit is set
 *   front and rear        : no (STUCK_HAZARD, a human must move the robot)
 *   front only            : only straight reverse   (v <= 0)
 *   rear only             : only straight forward   (v >= 0)
 *   fault / unreliable    : no motion */
bool skl_hazard_allows(uint8_t mask, int32_t v, int32_t w);

/* ------------------------------------------------------------------ drive */
/* Clamp a command to the soft caps (magnitudes). A cap of SKL_UNSET or < 0 is treated as 0. */
void skl_cap_command(int32_t *v, int32_t *w, int32_t speed_cap, int32_t turn_cap);

/* Wheel speeds from (v, w). Needs the measured wheel base; returns false if it is unset or <= 0.
 * Positive w = left turn: the right wheel runs faster. */
bool skl_mix(int32_t v, int32_t w_dps, int32_t wheel_base_mm, int32_t *left, int32_t *right);

/* Move cur towards target by at most accel * dt. Always moves at least 1 so it cannot stall. */
int32_t skl_ramp(int32_t cur, int32_t target, int32_t accel_mm_s2, uint32_t dt_ms);

/* Signed duty (permille) for a wheel speed. Open loop, scaled by battery voltage:
 *   calibrated   duty = |speed| * k / 1000 percent at 12 V, times 12000 / battery, raised to the
 *                minimum starting duty (stiction) when it is set, capped at 100 percent
 *   uncalibrated (k unset) duty grows linearly to uncal_cap_pct at speed_cap
 * Returns true when the calibrated path was used so the caller can WARN at arm time. */
bool skl_duty_permille(int32_t speed, int32_t k_x1000, int32_t batt_mv, int32_t min_start_pct,
                       int32_t speed_cap, int32_t uncal_cap_pct, int32_t *duty_out);

/* ---------------------------------------------------------------- mailbox */
typedef struct {
    int32_t  v, w;
    uint32_t expires_ms;
    bool     active;
} skl_mailbox_t;

typedef enum { SKL_MB_IDLE = 0, SKL_MB_ACTIVE, SKL_MB_EXPIRED } skl_mb_state_t;

void           skl_mb_put(skl_mailbox_t *mb, int32_t v, int32_t w, uint32_t ttl_ms, uint32_t now_ms);
void           skl_mb_clear(skl_mailbox_t *mb);
/* ACTIVE: *v and *w hold the command. EXPIRED is returned exactly once (emit DRIVE_EXPIRED),
 * then IDLE. IDLE: nothing to do (v and w set to 0). */
skl_mb_state_t skl_mb_get(skl_mailbox_t *mb, uint32_t now_ms, int32_t *v, int32_t *w);

/* ----------------------------------------------------------------- motion */
typedef struct {
    int8_t   last_sign[2];
    uint32_t hold_until_ms;
    bool     en;
} skl_motion_t;

typedef struct {
    int32_t duty_permille[2];   /* signed, left then right, to apply this cycle */
    bool    en;                 /* driver enable pin (shared by both modules) */
    bool    brake;              /* en with both PWM inputs low: motors shorted */
} skl_motion_out_t;

void skl_motion_reset(skl_motion_t *m);
/* One motion cycle. A wheel that reverses outputs 0 for one cycle first (dead time).
 * When both targets are 0 the driver stays enabled and braking for brake_hold_ms after the
 * last motion, then EN drops (coast) so a held motor does not heat the driver. */
void skl_motion_step(skl_motion_t *m, const int32_t target_permille[2], uint32_t now_ms,
                     uint32_t brake_hold_ms, skl_motion_out_t *out);

/* ------------------------------------------------------------------ cliff */
typedef enum {
    SKL_POL_FLOOR_HIGH = 0,   /* floor reflects: reading is high on floor, FALLS at an edge */
    SKL_POL_FLOOR_LOW  = 1    /* reading is low on floor, RISES at an edge */
} skl_polarity_t;

typedef struct {
    int32_t        ratio_pct;        /* edge threshold, percent of baseline (default 50) */
    int32_t        clear_pct;        /* clear threshold (default 65); must be on the floor side of ratio */
    int32_t        debounce_samples; /* consecutive samples beyond the threshold (default 2) */
    uint32_t       clear_hold_ms;    /* must stay clear this long (default 200) */
    uint32_t       rail_ms;          /* stuck at a rail this long is a sensor fault (default 1000) */
    int32_t        sat_low, sat_high;/* rail levels in ADC counts (defaults 40, 4050) */
    skl_polarity_t pol;
} skl_cliff_cfg_t;

typedef struct {
    int32_t  baseline;
    bool     have_baseline;
    int32_t  beyond;
    bool     active, clearing, fault, at_rail;
    uint32_t clear_start_ms, rail_start_ms;
} skl_cliff_t;

typedef enum {
    SKL_CLIFF_NONE = 0, SKL_CLIFF_EDGE, SKL_CLIFF_CLEARED, SKL_CLIFF_FAULT, SKL_CLIFF_FAULT_CLEARED
} skl_cliff_event_t;

void skl_cliff_default_cfg(skl_cliff_cfg_t *cfg);
bool skl_cliff_cfg_valid(const skl_cliff_cfg_t *cfg);      /* hysteresis on the right side, sane ranges */
void skl_cliff_init(skl_cliff_t *c);
void skl_cliff_set_baseline(skl_cliff_t *c, int32_t baseline);
/* Feed one (already filtered) reading. Edge and cleared events take precedence over fault events
 * when both happen in the same sample. */
skl_cliff_event_t skl_cliff_step(skl_cliff_t *c, const skl_cliff_cfg_t *cfg, int32_t reading,
                                 uint32_t now_ms);
/* True while this sensor must block motion: edge, fault, or no baseline yet (fail towards stopped). */
bool skl_cliff_blocking(const skl_cliff_t *c);

typedef enum { SKL_FLOOR_OK = 0, SKL_FLOOR_UNRELIABLE, SKL_FLOOR_UNKNOWN } skl_floor_t;
/* Arming check. contrast = |baseline - no_floor| as percent of full scale (4095).
 * UNKNOWN when min_contrast_pct or no_floor_adc is unset (refuse to arm and say why);
 * UNRELIABLE when the contrast is too small or the baseline sits at a rail. */
skl_floor_t skl_floor_check(int32_t baseline, int32_t no_floor_adc, int32_t min_contrast_pct);

/* ---------------------------------------------------------------- battery */
typedef enum { SKL_BATT_OK = 0, SKL_BATT_LOW, SKL_BATT_CRIT } skl_batt_state_t;
typedef enum { SKL_BATT_EV_NONE = 0, SKL_BATT_EV_LOW, SKL_BATT_EV_CRIT } skl_batt_event_t;

typedef struct {
    int32_t  low_mv, crit_mv;    /* defaults 10650 and 10050 */
    uint32_t persist_ms;         /* default 5000 */
    int32_t  hyst_mv;            /* default 150 */
} skl_batt_cfg_t;

typedef struct {
    skl_batt_state_t state;
    bool     low_timing, crit_timing;
    uint32_t low_start_ms, crit_start_ms;
} skl_batt_t;

void skl_batt_default_cfg(skl_batt_cfg_t *cfg);
void skl_batt_init(skl_batt_t *b);
/* Compensated voltage: measured + current * internal resistance (milliohm). */
int32_t skl_vcomp_mv(int32_t mv, int32_t ma, int32_t r_int_mohm);
/* One sample. LOW and CRIT need the voltage below the threshold for persist_ms. LOW returns to OK
 * only above low_mv + hyst_mv. CRIT is latched until power cycle (the node reboots after charging).
 * Each event is returned once, on entering the state. A reading of 0 (sensor missing) is ignored. */
skl_batt_event_t skl_batt_step(skl_batt_t *b, const skl_batt_cfg_t *cfg, int32_t mv, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_LOGIC_H */
