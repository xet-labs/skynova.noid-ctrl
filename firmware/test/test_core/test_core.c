/*
 * test_core.c - tests for skynoid_core.h: flight-recorder ring, debouncer, buzzer timing, I2C status.
 *
 * Run on the PC:  pio test -e native -f test_core
 * Expected values are worked out by hand from the spec (02-base-node.md Buzzer table,
 * console-commands.md "devices" status order); the comment next to each says how.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_core.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- flight recorder ring ------------------------------------------------ */
static void test_ring_garbage_header_is_reset_but_a_valid_one_survives(void)
{
    uint8_t buf[8];
    skl_ring_t r;
    memset(&r, 0xA5, sizeof r);                    /* what random power-on RAM looks like */
    skl_ring_attach(&r, sizeof buf);
    TEST_ASSERT_EQUAL_UINT32(0, r.len);
    TEST_ASSERT_EQUAL_UINT32(0, r.head);

    skl_ring_put(&r, buf, sizeof buf, (const uint8_t *)"abc", 3);
    skl_ring_attach(&r, sizeof buf);               /* a soft reset runs attach again: data must stay */
    TEST_ASSERT_EQUAL_UINT32(3, r.len);

    skl_ring_attach(&r, 16);                       /* different capacity: the magic no longer matches */
    TEST_ASSERT_EQUAL_UINT32(0, r.len);
}

static void test_ring_attach_rejects_out_of_range_indices(void)
{
    skl_ring_t r;
    skl_ring_attach(&r, 8);                        /* make a valid header first */
    r.head = 8;                                    /* head must be < cap */
    skl_ring_attach(&r, 8);
    TEST_ASSERT_EQUAL_UINT32(0, r.head);
    r.len = 9;                                     /* len must be <= cap */
    skl_ring_attach(&r, 8);
    TEST_ASSERT_EQUAL_UINT32(0, r.len);
}

static void test_ring_read_returns_oldest_first(void)
{
    uint8_t buf[8], out[8];
    skl_ring_t r;
    memset(&r, 0, sizeof r);
    skl_ring_attach(&r, sizeof buf);
    skl_ring_put(&r, buf, sizeof buf, (const uint8_t *)"abcde", 5);
    TEST_ASSERT_EQUAL_UINT32(5, skl_ring_read(&r, buf, sizeof buf, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("abcde", out, 5);
}

static void test_ring_overwrites_the_oldest_when_full(void)
{
    uint8_t buf[4], out[8];
    skl_ring_t r;
    memset(&r, 0, sizeof r);
    skl_ring_attach(&r, sizeof buf);
    skl_ring_put(&r, buf, sizeof buf, (const uint8_t *)"abcdef", 6);   /* 6 into 4: keeps the last 4 */
    TEST_ASSERT_EQUAL_UINT32(4, r.len);
    TEST_ASSERT_EQUAL_UINT32(4, skl_ring_read(&r, buf, sizeof buf, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("cdef", out, 4);
    skl_ring_put(&r, buf, sizeof buf, (const uint8_t *)"gh", 2);       /* wraps the write index */
    TEST_ASSERT_EQUAL_UINT32(4, skl_ring_read(&r, buf, sizeof buf, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("efgh", out, 4);
}

static void test_ring_small_output_buffer_gets_the_newest_bytes(void)
{
    uint8_t buf[8], out[3];
    skl_ring_t r;
    memset(&r, 0, sizeof r);
    skl_ring_attach(&r, sizeof buf);
    skl_ring_put(&r, buf, sizeof buf, (const uint8_t *)"abcdef", 6);
    TEST_ASSERT_EQUAL_UINT32(3, skl_ring_read(&r, buf, sizeof buf, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY("def", out, 3);
}

static void test_ring_empty_read_is_zero(void)
{
    uint8_t buf[4], out[4];
    skl_ring_t r;
    memset(&r, 0, sizeof r);
    skl_ring_attach(&r, sizeof buf);
    TEST_ASSERT_EQUAL_UINT32(0, skl_ring_read(&r, buf, sizeof buf, out, sizeof out));
}

/* ---- debouncer ------------------------------------------------------------ */
static void test_deb_accepts_a_level_only_after_the_hold_time(void)
{
    skl_deb_t d;
    skl_deb_init(&d, false);
    TEST_ASSERT_FALSE(skl_deb_step(&d, true, 1000, 30));    /* first sight of the new level */
    TEST_ASSERT_FALSE(skl_deb_step(&d, true, 1029, 30));    /* 29 ms: not yet */
    TEST_ASSERT_TRUE(skl_deb_step(&d, true, 1030, 30));     /* 30 ms: accepted */
    TEST_ASSERT_TRUE(d.stable);
    TEST_ASSERT_FALSE(skl_deb_step(&d, true, 1100, 30));    /* no further change event */
}

static void test_deb_a_glitch_restarts_the_timer(void)
{
    skl_deb_t d;
    skl_deb_init(&d, false);
    skl_deb_step(&d, true, 0, 30);
    skl_deb_step(&d, false, 10, 30);                        /* back to the accepted level: forget it */
    skl_deb_step(&d, true, 20, 30);                         /* candidate starts again at 20 */
    TEST_ASSERT_FALSE(skl_deb_step(&d, true, 49, 30));
    TEST_ASSERT_TRUE(skl_deb_step(&d, true, 50, 30));
}

static void test_deb_works_across_the_clock_wrap(void)
{
    skl_deb_t d;
    skl_deb_init(&d, false);
    skl_deb_step(&d, true, 0xFFFFFFF0u, 30);
    TEST_ASSERT_FALSE(skl_deb_step(&d, true, 0x0000000Au, 30));   /* 26 ms later */
    TEST_ASSERT_TRUE(skl_deb_step(&d, true, 0x00000010u, 30));    /* 32 ms later */
}

static void test_deb_falling_edge_is_debounced_too(void)
{
    skl_deb_t d;
    skl_deb_init(&d, true);
    skl_deb_step(&d, false, 0, 30);
    TEST_ASSERT_TRUE(skl_deb_step(&d, false, 30, 30));
    TEST_ASSERT_FALSE(d.stable);
}

/* ---- buzzer patterns (02-base-node.md Buzzer table) ------------------------- */
static int on_edges(uint8_t pattern, uint32_t until_ms)
{
    int edges = 0;
    bool prev = false;
    for (uint32_t t = 0; t < until_ms; t++) {
        const bool now = skl_buzzer_on(pattern, t);
        if (now && !prev) { edges++; }
        prev = now;
    }
    return edges;
}

static void test_buzzer_chirp_is_80_ms(void)
{
    TEST_ASSERT_TRUE(skl_buzzer_on(1, 0));
    TEST_ASSERT_TRUE(skl_buzzer_on(1, 79));
    TEST_ASSERT_FALSE(skl_buzzer_on(1, 80));
    TEST_ASSERT_FALSE(skl_buzzer_done(1, 79));
    TEST_ASSERT_TRUE(skl_buzzer_done(1, 80));
}

static void test_buzzer_double_chirp_is_80_on_80_off_80_on(void)
{
    TEST_ASSERT_TRUE(skl_buzzer_on(2, 79));
    TEST_ASSERT_FALSE(skl_buzzer_on(2, 80));
    TEST_ASSERT_FALSE(skl_buzzer_on(2, 159));
    TEST_ASSERT_TRUE(skl_buzzer_on(2, 160));
    TEST_ASSERT_TRUE(skl_buzzer_on(2, 239));
    TEST_ASSERT_FALSE(skl_buzzer_on(2, 240));
    TEST_ASSERT_EQUAL_INT(2, on_edges(2, 1000));
    TEST_ASSERT_TRUE(skl_buzzer_done(2, 240));
}

static void test_buzzer_low_battery_is_three_120_ms_beeps(void)
{
    TEST_ASSERT_TRUE(skl_buzzer_on(3, 0));
    TEST_ASSERT_TRUE(skl_buzzer_on(3, 119));
    TEST_ASSERT_FALSE(skl_buzzer_on(3, 120));
    TEST_ASSERT_TRUE(skl_buzzer_on(3, 240));
    TEST_ASSERT_TRUE(skl_buzzer_on(3, 480));
    TEST_ASSERT_FALSE(skl_buzzer_on(3, 600));
    TEST_ASSERT_EQUAL_INT(3, on_edges(3, 2000));
    TEST_ASSERT_FALSE(skl_buzzer_done(3, 599));
    TEST_ASSERT_TRUE(skl_buzzer_done(3, 600));
}

static void test_buzzer_critical_is_1_s_on_1_s_off_forever(void)
{
    TEST_ASSERT_TRUE(skl_buzzer_on(4, 0));
    TEST_ASSERT_TRUE(skl_buzzer_on(4, 999));
    TEST_ASSERT_FALSE(skl_buzzer_on(4, 1000));
    TEST_ASSERT_FALSE(skl_buzzer_on(4, 1999));
    TEST_ASSERT_TRUE(skl_buzzer_on(4, 2000));
    TEST_ASSERT_FALSE(skl_buzzer_done(4, 3600000u));          /* never ends by itself */
}

static void test_buzzer_error_is_400_ms_and_off_is_silent(void)
{
    TEST_ASSERT_TRUE(skl_buzzer_on(5, 399));
    TEST_ASSERT_FALSE(skl_buzzer_on(5, 400));
    TEST_ASSERT_TRUE(skl_buzzer_done(5, 400));
    TEST_ASSERT_FALSE(skl_buzzer_on(0, 0));
    TEST_ASSERT_TRUE(skl_buzzer_done(0, 0));
    TEST_ASSERT_FALSE(skl_buzzer_on(99, 0));                  /* unknown id: silent, and done */
    TEST_ASSERT_TRUE(skl_buzzer_done(99, 0));
}

/* ---- I2C device status ------------------------------------------------------ */
static void scan(bool present[128], const uint8_t *addrs, size_t n)
{
    memset(present, 0, 128);
    for (size_t i = 0; i < n; i++) { present[addrs[i]] = true; }
}

static const skl_i2c_dev_t INA = { 0x41, { 0x40, 0x44, 0x45, 0 }, 3, 2 };   /* required, strapped to 0x41 */

static void test_i2c_status_ok_and_fault_follow_the_identity_check(void)
{
    bool p[128]; uint8_t found;
    const uint8_t a[] = { 0x41 };
    scan(p, a, 1);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, skl_i2c_status(&INA, p, 1, &found));
    TEST_ASSERT_EQUAL_UINT8(0x41, found);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, skl_i2c_status(&INA, p, 0, &found));   /* ACK but wrong identity */
    TEST_ASSERT_EQUAL_UINT8(0x41, found);
}

static void test_i2c_status_moved_names_where_it_was_found(void)
{
    bool p[128]; uint8_t found;
    const uint8_t a[] = { 0x40 };                                   /* A0 pad not closed: INA219 at 0x40 */
    scan(p, a, 1);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MOVED, skl_i2c_status(&INA, p, 1, &found));
    TEST_ASSERT_EQUAL_UINT8(0x40, found);
}

static void test_i2c_status_missing_when_required_absent_when_optional(void)
{
    bool p[128]; uint8_t found = 0x55;
    scan(p, NULL, 0);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MISSING, skl_i2c_status(&INA, p, 1, &found));
    TEST_ASSERT_EQUAL_UINT8(0, found);
    const skl_i2c_dev_t opt = { 0x40, { 0 }, 0, 1 };                /* optional (servo driver not fitted) */
    TEST_ASSERT_EQUAL_INT(SKL_DEV_ABSENT, skl_i2c_status(&opt, p, 1, &found));
}

static void test_i2c_status_address_owned_by_another_device_is_not_trusted(void)
{
    /* PCA9685 answers at 0x40. The INA219 is expected at 0x41 with 0x40 as an alternative, but the caller
     * knows 0x40 belongs to the PCA9685, so it hides 0x40 by passing check = -1 for the PCA's own entry and
     * removing 0x40 from the INA's view. Here: a device whose main address is claimed and nothing else answers. */
    bool p[128]; uint8_t found;
    const uint8_t a[] = { 0x40 };
    scan(p, a, 1);
    const skl_i2c_dev_t pca_rival = { 0x40, { 0 }, 0, 2 };
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MISSING, skl_i2c_status(&pca_rival, p, -1, &found));
    TEST_ASSERT_EQUAL_UINT8(0, found);
}

static void test_i2c_status_ignores_bad_alternative_addresses(void)
{
    bool p[128]; uint8_t found;
    scan(p, NULL, 0);
    const skl_i2c_dev_t d = { 0x41, { 0xFF, 0x80, 0, 0 }, 9, 2 };    /* n_alt too big, addresses out of range */
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MISSING, skl_i2c_status(&d, p, 1, &found));
}

static void test_i2c_unexpected_lists_unclaimed_addresses_only(void)
{
    bool p[128]; uint8_t out[8];
    const uint8_t seen[] = { 0x03, 0x29, 0x3C, 0x50, 0x7F };         /* 0x03 and 0x7F are outside 0x08..0x77 */
    scan(p, seen, sizeof seen);
    const uint8_t claimed[] = { 0x29, 0x3C };
    TEST_ASSERT_EQUAL_UINT32(1, skl_i2c_unexpected(p, claimed, sizeof claimed, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8(0x50, out[0]);
}

static void test_i2c_unexpected_respects_the_output_capacity(void)
{
    bool p[128]; uint8_t out[2];
    const uint8_t seen[] = { 0x10, 0x20, 0x30 };
    scan(p, seen, sizeof seen);
    TEST_ASSERT_EQUAL_UINT32(2, skl_i2c_unexpected(p, NULL, 0, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8(0x10, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x20, out[1]);
}

static void test_dev_status_names_match_the_console_table(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", skl_dev_status_name(SKL_DEV_OK));
    TEST_ASSERT_EQUAL_STRING("moved", skl_dev_status_name(SKL_DEV_MOVED));
    TEST_ASSERT_EQUAL_STRING("MISSING", skl_dev_status_name(SKL_DEV_MISSING));
    TEST_ASSERT_EQUAL_STRING("absent", skl_dev_status_name(SKL_DEV_ABSENT));
    TEST_ASSERT_EQUAL_STRING("FAULT", skl_dev_status_name(SKL_DEV_FAULT));
    TEST_ASSERT_EQUAL_STRING("unverified", skl_dev_status_name(SKL_DEV_UNVERIFIED));
    TEST_ASSERT_EQUAL_STRING("unexpected", skl_dev_status_name(SKL_DEV_UNEXPECTED));
    TEST_ASSERT_EQUAL_STRING("disabled", skl_dev_status_name(SKL_DEV_DISABLED));
}

/* ---- status LED patterns ------------------------------------------------------- */
static void test_led_slow_is_500_on_500_off(void)
{
    TEST_ASSERT_TRUE(skl_led_on(1, 0));
    TEST_ASSERT_TRUE(skl_led_on(1, 499));
    TEST_ASSERT_FALSE(skl_led_on(1, 500));
    TEST_ASSERT_FALSE(skl_led_on(1, 999));
    TEST_ASSERT_TRUE(skl_led_on(1, 1000));
}

static void test_led_fast_is_100_on_100_off(void)
{
    TEST_ASSERT_TRUE(skl_led_on(3, 99));
    TEST_ASSERT_FALSE(skl_led_on(3, 100));
    TEST_ASSERT_TRUE(skl_led_on(3, 200));
}

static void test_led_double_is_two_blinks_in_two_seconds(void)
{
    int on_samples = 0, edges = 0;
    bool prev = false;
    for (uint32_t t = 0; t < 2000; t += 10) {
        const bool on = skl_led_on(4, t);
        on_samples += on;
        if (on && !prev) { edges++; }
        prev = on;
    }
    TEST_ASSERT_EQUAL_INT(2, edges);                    /* two blinks */
    TEST_ASSERT_EQUAL_INT(20, on_samples);              /* 2 x 100 ms at 10 ms steps */
    TEST_ASSERT_FALSE(skl_led_on(4, 1000));
}

static void test_led_off_solid_and_unknown(void)
{
    TEST_ASSERT_FALSE(skl_led_on(0, 0));
    TEST_ASSERT_TRUE(skl_led_on(2, 12345));
    TEST_ASSERT_FALSE(skl_led_on(99, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ring_garbage_header_is_reset_but_a_valid_one_survives);
    RUN_TEST(test_ring_attach_rejects_out_of_range_indices);
    RUN_TEST(test_ring_read_returns_oldest_first);
    RUN_TEST(test_ring_overwrites_the_oldest_when_full);
    RUN_TEST(test_ring_small_output_buffer_gets_the_newest_bytes);
    RUN_TEST(test_ring_empty_read_is_zero);
    RUN_TEST(test_deb_accepts_a_level_only_after_the_hold_time);
    RUN_TEST(test_deb_a_glitch_restarts_the_timer);
    RUN_TEST(test_deb_works_across_the_clock_wrap);
    RUN_TEST(test_deb_falling_edge_is_debounced_too);
    RUN_TEST(test_buzzer_chirp_is_80_ms);
    RUN_TEST(test_buzzer_double_chirp_is_80_on_80_off_80_on);
    RUN_TEST(test_buzzer_low_battery_is_three_120_ms_beeps);
    RUN_TEST(test_buzzer_critical_is_1_s_on_1_s_off_forever);
    RUN_TEST(test_buzzer_error_is_400_ms_and_off_is_silent);
    RUN_TEST(test_i2c_status_ok_and_fault_follow_the_identity_check);
    RUN_TEST(test_i2c_status_moved_names_where_it_was_found);
    RUN_TEST(test_i2c_status_missing_when_required_absent_when_optional);
    RUN_TEST(test_i2c_status_address_owned_by_another_device_is_not_trusted);
    RUN_TEST(test_i2c_status_ignores_bad_alternative_addresses);
    RUN_TEST(test_i2c_unexpected_lists_unclaimed_addresses_only);
    RUN_TEST(test_i2c_unexpected_respects_the_output_capacity);
    RUN_TEST(test_dev_status_names_match_the_console_table);
    RUN_TEST(test_led_slow_is_500_on_500_off);
    RUN_TEST(test_led_fast_is_100_on_100_off);
    RUN_TEST(test_led_double_is_two_blinks_in_two_seconds);
    RUN_TEST(test_led_off_solid_and_unknown);
    return UNITY_END();
}
