/*
 * skynoid_servo.h - the pure maths of the servo engine (no ESP-IDF, no FreeRTOS, no I2C).
 *
 * WHAT THIS FILE IS
 *   Step G. The rules from vault 04-firmware/03-upper-node.md "Servo engine" and "Servo power budget" that can
 *   be tested on the PC: angle to pulse width to PCA9685 counts, soft limits, the minimum-jerk move profile
 *   and the speed limit, the power-budget rules. src/hal_servo.c is the only place that talks to the chip.
 *
 * ASSUMES POSITIONAL SERVOS. The BOM servos are probably continuous-rotation (decision D20): a continuous servo
 * has no angle, so nothing here can aim it. Until the two-minute pulse test confirms a positional servo, the
 * firmware keeps these functions away from the outputs (SKYNOID_SERVO_TYPE_CONFIRMED, see app_config.h).
 *
 * UNITS  angles in tenths of a degree (deg_x10, 0..1800 for a 180 degree servo), pulses in microseconds,
 *        time in milliseconds from a free-running uint32 clock (differences are wrap safe), speeds in
 *        degrees per second. Integer maths only.
 *
 * UNMEASURED NUMBERS (#verify on the bench, spec U1 to U3): the default pulse range 700..2300 us, the
 *   absolute pulse bounds 600..2400 us, the PCA9685 oscillator 25 MHz (nominal) and the default power limits.
 *
 * TESTS: firmware/test/test_servo
 */
#ifndef SKYNOID_SERVO_H
#define SKYNOID_SERVO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Last line of defence: no pulse outside this range is ever produced, whatever the calibration says. */
#define SKS_PULSE_ABS_MIN_US 600
#define SKS_PULSE_ABS_MAX_US 2400

#define SKS_PCA_COUNTS 4096          /* PCA9685 resolution: counts per PWM period */
#define SKS_PCA_OSC_HZ 25000000u     /* nominal internal oscillator */
#define SKS_PWM_HZ 50u
#define SKS_PERIOD_US 20000          /* 1 / 50 Hz */

/* Joints of V1 (spec 03-upper-node). The channel each joint uses on the PCA9685 is not decided here. */
enum { SKS_HEAD_YAW = 0, SKS_HEAD_PITCH, SKS_ARM_L_SHOULDER_PITCH, SKS_ARM_L_SHOULDER_ROLL, SKS_ARM_L_ELBOW,
       SKS_ARM_R_SHOULDER_PITCH, SKS_ARM_R_SHOULDER_ROLL, SKS_ARM_R_ELBOW, SKS_N_JOINTS };

typedef struct {
    int32_t pulse_min_us;            /* pulse at 0 degrees */
    int32_t pulse_max_us;            /* pulse at 180 degrees */
    int32_t min_deg_x10;             /* soft limits: targets are clamped into [min, max] */
    int32_t max_deg_x10;
    int32_t rest_deg_x10;            /* where the joint is assumed to be at power-up */
    int32_t offset_deg_x10;          /* added to the target before the pulse is computed (mounting correction) */
    bool    invert;                  /* mirror: 0 degrees gives pulse_max */
} sks_joint_cal_t;

/* 700..2300 us, limits 0..180 degrees, rest 90 degrees, no offset, not inverted. */
void sks_cal_default(sks_joint_cal_t *c);

/* Clamp a target into the joint's soft limits. *clamped (may be NULL) is set when the value had to change. */
int32_t sks_clamp_deg_x10(const sks_joint_cal_t *c, int32_t deg_x10, bool *clamped);

/* Target angle to pulse width: clamp to the soft limits, add the offset, keep inside 0..1800, mirror if
 * inverted, map linearly onto [pulse_min, pulse_max], then force into the absolute bounds. */
int32_t sks_deg_to_us(const sks_joint_cal_t *c, int32_t deg_x10, bool *clamped);

/* Pulse width to PCA9685 "off" count for a period of period_us (20000 for 50 Hz): rounded, 0..4095. */
uint16_t sks_us_to_counts(int32_t us, int32_t period_us);

/* PRESCALE register value for freq_hz with an oscillator of osc_hz: round(osc / (4096 * f)) - 1, kept in 3..255.
 * 25 MHz and 50 Hz give 121. A real chip's oscillator is off by a few percent: measure the period (spec U1). */
uint8_t sks_prescale(uint32_t osc_hz, uint32_t freq_hz);

/* ---- move profile: minimum jerk, s(t) = 10 t^3 - 15 t^4 + 6 t^5 ---------------------------------------------- */

/* s for time t_ms into a move of dur_ms, in Q16 (0 .. 65536). dur_ms 0 or t_ms >= dur_ms gives 65536. */
uint32_t sks_minjerk_q16(uint32_t t_ms, uint32_t dur_ms);

/* Shortest duration in which |delta| tenths of a degree can be moved without exceeding vmax_dps (the peak speed
 * of the curve is 1.875 times the average). vmax_dps <= 0 means no speed limit and gives 0. Rounded up. */
uint32_t sks_min_duration_ms(int32_t delta_deg_x10, int32_t vmax_dps);

typedef struct {
    int32_t  from_x10, to_x10;
    uint32_t start_ms, dur_ms;
    bool     active;
} sks_move_t;

/* Start a move. The duration is the larger of req_dur_ms and what vmax_dps allows. A zero-length move (same
 * target, or duration 0 after the limits) is not active and sks_move_pos returns the target at once. */
void    sks_move_start(sks_move_t *m, int32_t from_deg_x10, int32_t to_deg_x10, uint32_t req_dur_ms,
                       int32_t vmax_dps, uint32_t now_ms);
int32_t sks_move_pos(const sks_move_t *m, uint32_t now_ms);     /* position now, in tenths of a degree */
bool    sks_move_done(const sks_move_t *m, uint32_t now_ms);

/* ---- power budget (spec "Servo power budget"; thresholds come from measurement, not from here) ------------------- */

/* Weight of a joint: shoulders count 2, everything else 1. Unknown joints count 2 (the safe side). */
int32_t sks_joint_weight(int joint);

/* May a joint with new_weight start while moving_weight_sum is already moving? Default budget in the spec is 6. */
bool sks_budget_admit(int32_t moving_weight_sum, int32_t new_weight, int32_t budget);

/* Motion starts must be at least gap_ms apart (spec: 40 ms). */
bool sks_stagger_ok(uint32_t last_start_ms, uint32_t now_ms, uint32_t gap_ms);

/* Speed scale in percent from the servo-rail current: 100 up to soft_ma, falling linearly to 50 at hard_ma, and 0
 * at or above hard_ma (the caller then disables the outputs; the spec asks for the hard limit to hold 50 ms first).
 * soft_ma <= 0 turns the soft scaling off; hard_ma <= 0 turns the hard limit off. */
int32_t sks_current_scale_pct(int32_t ma, int32_t soft_ma, int32_t hard_ma);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_SERVO_H */
