/*
 * test_touch.c - tests for the touch pad logic (debounce, boot ignore, stuck pad).
 * Run on the PC:  pio test -e native -f test_touch
 */
#include <unity.h>
#include "skynoid_touch.h"

void setUp(void) {}
void tearDown(void) {}

#define HOLD   30u
#define BOOT   500u
#define STUCK  20000u

typedef struct { int down, up, stuck; } counts_t;

/* Feed one zone with a constant level every 10 ms from t0 (inclusive) to t1 (exclusive). */
static void run(skt_t *t, uint8_t zone, bool level, uint32_t t0, uint32_t t1, counts_t *c)
{
    for (uint32_t now = t0; (uint32_t)(now - t0) < (uint32_t)(t1 - t0); now += 10u) {
        const uint8_t f = skt_step(t, zone, level, now);
        if (f & SKT_F_DOWN)  { c->down++; }
        if (f & SKT_F_UP)    { c->up++; }
        if (f & SKT_F_STUCK) { c->stuck++; }
    }
}

static void test_a_short_press_is_not_reported(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK); counts_t c = {0};
    run(&t, 0, true, 1000, 1020, &c);                 /* 20 ms, shorter than the hold */
    run(&t, 0, false, 1020, 1200, &c);
    TEST_ASSERT_EQUAL_INT(0, c.down); TEST_ASSERT_EQUAL_INT(0, c.up);
}

static void test_a_held_press_reports_down_once_then_up_once(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK); counts_t c = {0};
    run(&t, 1, true, 1000, 1500, &c);
    TEST_ASSERT_EQUAL_INT(1, c.down); TEST_ASSERT_EQUAL_INT(0, c.up);
    TEST_ASSERT_TRUE(skt_is_down(&t, 1));
    run(&t, 1, false, 1500, 2000, &c);
    TEST_ASSERT_EQUAL_INT(1, c.down); TEST_ASSERT_EQUAL_INT(1, c.up);
    TEST_ASSERT_FALSE(skt_is_down(&t, 1));
}

static void test_down_arrives_after_the_hold_not_before(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK);
    TEST_ASSERT_EQUAL_UINT8(0, skt_step(&t, 0, true, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, skt_step(&t, 0, true, 1020));
    TEST_ASSERT_EQUAL_UINT8(SKT_F_DOWN, skt_step(&t, 0, true, 1030));
}

static void test_contact_bounce_gives_no_event(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK); counts_t c = {0};
    for (uint32_t now = 1000; now < 1400; now += 10) { (void)c; if (skt_step(&t, 0, ((now / 10) % 2) == 0, now)) { c.down++; } }
    TEST_ASSERT_EQUAL_INT(0, c.down);
}

static void test_nothing_is_reported_in_the_boot_window(void)
{
    skt_t t; skt_init(&t, 1000, BOOT, HOLD, STUCK); counts_t c = {0};
    run(&t, 0, true, 1000, 1200, &c);                 /* module output during its own calibration */
    run(&t, 0, false, 1200, 1500, &c);                /* released before the window ends */
    TEST_ASSERT_EQUAL_INT(0, c.down); TEST_ASSERT_EQUAL_INT(0, c.up);
}

static void test_a_pad_still_pressed_when_the_window_ends_reports_down(void)
{
    skt_t t; skt_init(&t, 1000, BOOT, HOLD, STUCK); counts_t c = {0};
    run(&t, 0, true, 1000, 1800, &c);
    TEST_ASSERT_EQUAL_INT(1, c.down); TEST_ASSERT_EQUAL_INT(0, c.up);
}

static void test_a_stuck_pad_reports_up_and_stuck_once_then_stays_silent(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK); counts_t c = {0};
    run(&t, 2, true, 1000, 1000 + STUCK - 100, &c);
    TEST_ASSERT_EQUAL_INT(1, c.down); TEST_ASSERT_EQUAL_INT(0, c.stuck);
    run(&t, 2, true, 1000 + STUCK - 100, 1000 + STUCK + 5000, &c);
    TEST_ASSERT_EQUAL_INT(1, c.down); TEST_ASSERT_EQUAL_INT(1, c.up); TEST_ASSERT_EQUAL_INT(1, c.stuck);
    TEST_ASSERT_TRUE(skt_is_stuck(&t, 2)); TEST_ASSERT_FALSE(skt_is_down(&t, 2));
}

static void test_a_stuck_pad_works_again_after_it_is_released(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK); counts_t c = {0};
    run(&t, 0, true, 1000, 1000 + STUCK + 1000, &c);
    run(&t, 0, false, 1000 + STUCK + 1000, 1000 + STUCK + 1500, &c);
    TEST_ASSERT_FALSE(skt_is_stuck(&t, 0));
    counts_t d = {0};
    run(&t, 0, true, 40000, 40500, &d);
    TEST_ASSERT_EQUAL_INT(1, d.down);
}

static void test_stuck_check_off_when_stuck_ms_is_zero(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, 0); counts_t c = {0};
    run(&t, 0, true, 1000, 100000, &c);
    TEST_ASSERT_EQUAL_INT(1, c.down); TEST_ASSERT_EQUAL_INT(0, c.stuck); TEST_ASSERT_EQUAL_INT(0, c.up);
}

static void test_zones_do_not_disturb_each_other(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK); counts_t a = {0}, b = {0};
    run(&t, 0, true, 1000, 1200, &a);
    run(&t, 3, false, 1000, 1200, &b);
    TEST_ASSERT_EQUAL_INT(1, a.down); TEST_ASSERT_EQUAL_INT(0, b.down);
    TEST_ASSERT_TRUE(skt_is_down(&t, 0)); TEST_ASSERT_FALSE(skt_is_down(&t, 3));
}

static void test_a_bad_zone_is_ignored(void)
{
    skt_t t; skt_init(&t, 0, 0, HOLD, STUCK);
    TEST_ASSERT_EQUAL_UINT8(0, skt_step(&t, SKT_ZONES, true, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, skt_step(&t, 255, true, 2000));
    TEST_ASSERT_FALSE(skt_is_down(&t, 9)); TEST_ASSERT_FALSE(skt_is_stuck(&t, 9));
}

static void test_the_clock_may_wrap_during_a_press(void)
{
    skt_t t; skt_init(&t, 0xFFFFFF00u, 0, HOLD, STUCK); counts_t c = {0};
    run(&t, 0, true, 0xFFFFFF00u, 0x00000200u, &c);   /* crosses 2^32 */
    TEST_ASSERT_EQUAL_INT(1, c.down);
    run(&t, 0, false, 0x00000200u, 0x00000400u, &c);
    TEST_ASSERT_EQUAL_INT(1, c.up);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_short_press_is_not_reported);
    RUN_TEST(test_a_held_press_reports_down_once_then_up_once);
    RUN_TEST(test_down_arrives_after_the_hold_not_before);
    RUN_TEST(test_contact_bounce_gives_no_event);
    RUN_TEST(test_nothing_is_reported_in_the_boot_window);
    RUN_TEST(test_a_pad_still_pressed_when_the_window_ends_reports_down);
    RUN_TEST(test_a_stuck_pad_reports_up_and_stuck_once_then_stays_silent);
    RUN_TEST(test_a_stuck_pad_works_again_after_it_is_released);
    RUN_TEST(test_stuck_check_off_when_stuck_ms_is_zero);
    RUN_TEST(test_zones_do_not_disturb_each_other);
    RUN_TEST(test_a_bad_zone_is_ignored);
    RUN_TEST(test_the_clock_may_wrap_during_a_press);
    return UNITY_END();
}
