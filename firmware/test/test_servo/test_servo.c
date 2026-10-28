/*
 * test_servo.c - tests for the pure servo maths (angle to pulse to counts, limits, minimum-jerk move, budget).
 * Run on the PC:  pio test -e native -f test_servo
 */
#include <unity.h>
#include "skynoid_servo.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- angle to pulse -------------------------------------------------------------------- */
static void test_default_calibration_maps_0_90_180_degrees(void)
{
    sks_joint_cal_t c; sks_cal_default(&c);
    TEST_ASSERT_EQUAL_INT(700, sks_deg_to_us(&c, 0, NULL));
    TEST_ASSERT_EQUAL_INT(1500, sks_deg_to_us(&c, 900, NULL));
    TEST_ASSERT_EQUAL_INT(2300, sks_deg_to_us(&c, 1800, NULL));
}

static void test_invert_mirrors_the_range(void)
{
    sks_joint_cal_t c; sks_cal_default(&c); c.invert = true;
    TEST_ASSERT_EQUAL_INT(2300, sks_deg_to_us(&c, 0, NULL));
    TEST_ASSERT_EQUAL_INT(1500, sks_deg_to_us(&c, 900, NULL));
    TEST_ASSERT_EQUAL_INT(700, sks_deg_to_us(&c, 1800, NULL));
}

static void test_offset_shifts_the_target_before_the_pulse(void)
{
    sks_joint_cal_t c; sks_cal_default(&c); c.offset_deg_x10 = 100;
    TEST_ASSERT_EQUAL_INT(1589, sks_deg_to_us(&c, 900, NULL));              /* 100 degrees */
    TEST_ASSERT_EQUAL_INT(2300, sks_deg_to_us(&c, 1800, NULL));             /* 190 degrees is held at 180 */
}

static void test_soft_limits_clamp_and_report(void)
{
    sks_joint_cal_t c; sks_cal_default(&c); c.min_deg_x10 = 300; c.max_deg_x10 = 1500;
    bool clamped = false;
    TEST_ASSERT_EQUAL_INT(1500, sks_clamp_deg_x10(&c, 1800, &clamped)); TEST_ASSERT_TRUE(clamped);
    TEST_ASSERT_EQUAL_INT(300, sks_clamp_deg_x10(&c, -50, &clamped));   TEST_ASSERT_TRUE(clamped);
    TEST_ASSERT_EQUAL_INT(900, sks_clamp_deg_x10(&c, 900, &clamped));   TEST_ASSERT_FALSE(clamped);
    const int32_t at_limit = sks_deg_to_us(&c, 1500, NULL);
    TEST_ASSERT_EQUAL_INT(at_limit, sks_deg_to_us(&c, 1800, &clamped)); TEST_ASSERT_TRUE(clamped);
}

static void test_absolute_pulse_bounds_beat_a_wild_calibration(void)
{
    sks_joint_cal_t c; sks_cal_default(&c); c.pulse_min_us = 100; c.pulse_max_us = 5000;
    TEST_ASSERT_EQUAL_INT(SKS_PULSE_ABS_MIN_US, sks_deg_to_us(&c, 0, NULL));
    TEST_ASSERT_EQUAL_INT(SKS_PULSE_ABS_MAX_US, sks_deg_to_us(&c, 1800, NULL));
}

static void test_the_pulse_never_leaves_the_bounds_for_any_target(void)
{
    sks_joint_cal_t c; sks_cal_default(&c); c.pulse_min_us = 0; c.pulse_max_us = 9000; c.offset_deg_x10 = -500;
    for (int32_t a = -3000; a <= 5000; a += 37) {
        const int32_t us = sks_deg_to_us(&c, a, NULL);
        TEST_ASSERT_TRUE(us >= SKS_PULSE_ABS_MIN_US && us <= SKS_PULSE_ABS_MAX_US);
    }
}

/* ---- pulse to counts, prescale ------------------------------------------------------------- */
static void test_counts_at_50_hz(void)
{
    TEST_ASSERT_EQUAL_UINT16(205, sks_us_to_counts(1000, SKS_PERIOD_US));   /* about 205 counts per ms */
    TEST_ASSERT_EQUAL_UINT16(307, sks_us_to_counts(1500, SKS_PERIOD_US));
    TEST_ASSERT_EQUAL_UINT16(410, sks_us_to_counts(2000, SKS_PERIOD_US));
    TEST_ASSERT_EQUAL_UINT16(0, sks_us_to_counts(0, SKS_PERIOD_US));
    TEST_ASSERT_EQUAL_UINT16(0, sks_us_to_counts(-5, SKS_PERIOD_US));
    TEST_ASSERT_EQUAL_UINT16(4095, sks_us_to_counts(40000, SKS_PERIOD_US)); /* longer than the period */
    TEST_ASSERT_EQUAL_UINT16(0, sks_us_to_counts(1500, 0));                 /* bad period: no output */
}

static void test_prescale_for_50_hz(void)
{
    TEST_ASSERT_EQUAL_UINT8(121, sks_prescale(SKS_PCA_OSC_HZ, SKS_PWM_HZ));
    TEST_ASSERT_EQUAL_UINT8(255, sks_prescale(SKS_PCA_OSC_HZ, 0));
    TEST_ASSERT_EQUAL_UINT8(3, sks_prescale(100000u, 50u));                 /* below the chip's minimum: held at 3 */
}

/* ---- minimum jerk --------------------------------------------------------------------------- */
static void test_minjerk_endpoints_and_middle(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, sks_minjerk_q16(0, 1000));
    TEST_ASSERT_EQUAL_UINT32(32768, sks_minjerk_q16(500, 1000));
    TEST_ASSERT_EQUAL_UINT32(65536, sks_minjerk_q16(1000, 1000));
    TEST_ASSERT_EQUAL_UINT32(65536, sks_minjerk_q16(5000, 1000));
    TEST_ASSERT_EQUAL_UINT32(65536, sks_minjerk_q16(0, 0));
}

static void test_minjerk_rises_steadily_and_is_symmetric(void)
{
    uint32_t prev = 0;
    for (uint32_t t = 0; t <= 1000; t += 10) {
        const uint32_t s = sks_minjerk_q16(t, 1000);
        TEST_ASSERT_TRUE(s >= prev);
        prev = s;
        const uint32_t mirror = sks_minjerk_q16(1000 - t, 1000);
        const int32_t sum = (int32_t)(s + mirror);
        TEST_ASSERT_TRUE(sum >= 65536 - 4 && sum <= 65536 + 4);
    }
}

static void test_slow_start_no_jump_at_the_beginning(void)
{
    TEST_ASSERT_TRUE(sks_minjerk_q16(10, 1000) < 10);                       /* 1 percent of the time, a tiny fraction of the way */
}

/* ---- speed limit and moves -------------------------------------------------------------------- */
static void test_min_duration_follows_the_peak_speed_of_the_curve(void)
{
    TEST_ASSERT_EQUAL_UINT32(1875, sks_min_duration_ms(900, 90));           /* 90 degrees at 90 deg/s */
    TEST_ASSERT_EQUAL_UINT32(1875, sks_min_duration_ms(-900, 90));
    TEST_ASSERT_EQUAL_UINT32(0, sks_min_duration_ms(900, 0));               /* no limit */
    TEST_ASSERT_EQUAL_UINT32(1, sks_min_duration_ms(1, 1000));              /* rounded up, never 0 for a real move */
}

static void test_move_follows_the_profile(void)
{
    sks_move_t m; sks_move_start(&m, 0, 900, 1000, 0, 5000);
    TEST_ASSERT_TRUE(m.active);
    TEST_ASSERT_EQUAL_INT(0, sks_move_pos(&m, 5000));
    TEST_ASSERT_EQUAL_INT(450, sks_move_pos(&m, 5500));
    TEST_ASSERT_EQUAL_INT(900, sks_move_pos(&m, 6000));
    TEST_ASSERT_FALSE(sks_move_done(&m, 5999));
    TEST_ASSERT_TRUE(sks_move_done(&m, 6000));
}

static void test_move_goes_downwards_too(void)
{
    sks_move_t m; sks_move_start(&m, 900, 0, 1000, 0, 0);
    TEST_ASSERT_EQUAL_INT(450, sks_move_pos(&m, 500));
    TEST_ASSERT_EQUAL_INT(0, sks_move_pos(&m, 1000));
}

static void test_speed_limit_stretches_a_move_and_is_never_exceeded(void)
{
    sks_move_t m; sks_move_start(&m, 0, 900, 100, 90, 0);                   /* asked for 100 ms, 90 deg/s allows 1875 */
    TEST_ASSERT_EQUAL_UINT32(1875, m.dur_ms);
    int32_t prev = sks_move_pos(&m, 0);
    for (uint32_t t = 10; t <= 1900; t += 10) {
        const int32_t p = sks_move_pos(&m, t);
        TEST_ASSERT_TRUE(p - prev <= 10);                                   /* 90 deg/s is 9 tenths per 10 ms; 1 for rounding */
        TEST_ASSERT_TRUE(p >= prev);
        prev = p;
    }
    TEST_ASSERT_EQUAL_INT(900, prev);
}

static void test_a_zero_length_move_is_inactive(void)
{
    sks_move_t m; sks_move_start(&m, 500, 500, 1000, 90, 0);
    TEST_ASSERT_FALSE(m.active);
    TEST_ASSERT_EQUAL_INT(500, sks_move_pos(&m, 10));
    TEST_ASSERT_TRUE(sks_move_done(&m, 0));
    sks_move_start(&m, 0, 100, 0, 0, 0);                                    /* duration 0, no speed limit: jump to the target */
    TEST_ASSERT_FALSE(m.active);
    TEST_ASSERT_EQUAL_INT(100, sks_move_pos(&m, 0));
}

static void test_a_move_survives_the_clock_wrap(void)
{
    sks_move_t m; sks_move_start(&m, 0, 900, 1000, 0, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL_INT(450, sks_move_pos(&m, 0xFFFFFF00u + 500u));       /* crosses 2^32 */
    TEST_ASSERT_TRUE(sks_move_done(&m, 0xFFFFFF00u + 1000u));
}

/* ---- power budget ---------------------------------------------------------------------------- */
static void test_joint_weights(void)
{
    TEST_ASSERT_EQUAL_INT(1, sks_joint_weight(SKS_HEAD_YAW));
    TEST_ASSERT_EQUAL_INT(1, sks_joint_weight(SKS_HEAD_PITCH));
    TEST_ASSERT_EQUAL_INT(2, sks_joint_weight(SKS_ARM_L_SHOULDER_PITCH));
    TEST_ASSERT_EQUAL_INT(2, sks_joint_weight(SKS_ARM_R_SHOULDER_ROLL));
    TEST_ASSERT_EQUAL_INT(1, sks_joint_weight(SKS_ARM_L_ELBOW));
    TEST_ASSERT_EQUAL_INT(2, sks_joint_weight(99));                         /* unknown: the safe side */
    TEST_ASSERT_EQUAL_INT(2, sks_joint_weight(-1));
}

static void test_budget_admits_up_to_the_limit(void)
{
    TEST_ASSERT_TRUE(sks_budget_admit(4, 2, 6));
    TEST_ASSERT_FALSE(sks_budget_admit(5, 2, 6));
    TEST_ASSERT_TRUE(sks_budget_admit(0, 2, 6));
    TEST_ASSERT_FALSE(sks_budget_admit(0, 2, 1));
}

static void test_starts_are_staggered_across_the_wrap(void)
{
    TEST_ASSERT_FALSE(sks_stagger_ok(1000, 1039, 40));
    TEST_ASSERT_TRUE(sks_stagger_ok(1000, 1040, 40));
    TEST_ASSERT_FALSE(sks_stagger_ok(0xFFFFFFF0u, 0x00000010u, 40));        /* 32 ms later */
    TEST_ASSERT_TRUE(sks_stagger_ok(0xFFFFFFF0u, 0x00000020u, 40));         /* 48 ms later */
}

static void test_current_scale_soft_then_hard(void)
{
    TEST_ASSERT_EQUAL_INT(100, sks_current_scale_pct(3000, 4500, 6500));
    TEST_ASSERT_EQUAL_INT(100, sks_current_scale_pct(4500, 4500, 6500));
    TEST_ASSERT_EQUAL_INT(75, sks_current_scale_pct(5500, 4500, 6500));
    TEST_ASSERT_EQUAL_INT(51, sks_current_scale_pct(6480, 4500, 6500));      /* just under the hard limit: close to the floor */
    TEST_ASSERT_EQUAL_INT(0, sks_current_scale_pct(6500, 4500, 6500));
    TEST_ASSERT_EQUAL_INT(0, sks_current_scale_pct(9000, 4500, 6500));
    TEST_ASSERT_EQUAL_INT(100, sks_current_scale_pct(9000, 0, 0));          /* both checks off */
    TEST_ASSERT_EQUAL_INT(50, sks_current_scale_pct(9000, 4500, 0));        /* soft only: floor of 50 percent, no cut-off */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_calibration_maps_0_90_180_degrees);
    RUN_TEST(test_invert_mirrors_the_range);
    RUN_TEST(test_offset_shifts_the_target_before_the_pulse);
    RUN_TEST(test_soft_limits_clamp_and_report);
    RUN_TEST(test_absolute_pulse_bounds_beat_a_wild_calibration);
    RUN_TEST(test_the_pulse_never_leaves_the_bounds_for_any_target);
    RUN_TEST(test_counts_at_50_hz);
    RUN_TEST(test_prescale_for_50_hz);
    RUN_TEST(test_minjerk_endpoints_and_middle);
    RUN_TEST(test_minjerk_rises_steadily_and_is_symmetric);
    RUN_TEST(test_slow_start_no_jump_at_the_beginning);
    RUN_TEST(test_min_duration_follows_the_peak_speed_of_the_curve);
    RUN_TEST(test_move_follows_the_profile);
    RUN_TEST(test_move_goes_downwards_too);
    RUN_TEST(test_speed_limit_stretches_a_move_and_is_never_exceeded);
    RUN_TEST(test_a_zero_length_move_is_inactive);
    RUN_TEST(test_a_move_survives_the_clock_wrap);
    RUN_TEST(test_joint_weights);
    RUN_TEST(test_budget_admits_up_to_the_limit);
    RUN_TEST(test_starts_are_staggered_across_the_wrap);
    RUN_TEST(test_current_scale_soft_then_hard);
    return UNITY_END();
}
