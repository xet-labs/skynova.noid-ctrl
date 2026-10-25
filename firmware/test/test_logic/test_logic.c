/*
 * test_logic.c - tests for skynoid_logic (hazard, drive, mailbox, motion, cliff, floor, battery).
 *
 * Run on the PC:  pio test -e native -f test_logic
 *
 * Expected values are worked out by hand from the spec numbers (02-base-node.md,
 * config-defaults.yaml); the comment next to each says how.
 */
#include <unity.h>
#include "skynoid_logic.h"
#include "skynoid_params.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- helpers ------------------------------------------------------------ */
static void test_clamp_and_median(void)
{
    TEST_ASSERT_EQUAL_INT32(5, skl_clamp(9, -5, 5));
    TEST_ASSERT_EQUAL_INT32(-5, skl_clamp(-9, -5, 5));
    TEST_ASSERT_EQUAL_INT32(2, skl_clamp(2, -5, 5));
    const int32_t p[6][3] = {{1,2,3},{1,3,2},{2,1,3},{2,3,1},{3,1,2},{3,2,1}};
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_INT32(2, skl_median3(p[i][0], p[i][1], p[i][2]));
    }
    TEST_ASSERT_EQUAL_INT32(7, skl_median3(7, 7, 100));            /* spike rejected */
    int32_t v[5] = {50, 10, 40, 20, 30};
    TEST_ASSERT_EQUAL_INT32(30, skl_median_n(v, 5));
    int32_t one[1] = {9};
    TEST_ASSERT_EQUAL_INT32(9, skl_median_n(one, 1));
}

static void test_time_reached_is_wrap_safe(void)
{
    TEST_ASSERT_TRUE(skl_time_reached(100, 100));
    TEST_ASSERT_FALSE(skl_time_reached(99, 100));
    TEST_ASSERT_TRUE(skl_time_reached(0x00000010u, 0xFFFFFFF0u));   /* 32 ms later, across the wrap */
    TEST_ASSERT_FALSE(skl_time_reached(0xFFFFFFF0u, 0x00000010u));
}

/* ---- hazard ------------------------------------------------------------- */
static void test_hazard_none_allows_everything(void)
{
    TEST_ASSERT_TRUE(skl_hazard_allows(0, 250, 90));
    TEST_ASSERT_TRUE(skl_hazard_allows(0, -250, -90));
}

static void test_hazard_front_only_reverse(void)
{
    TEST_ASSERT_FALSE(skl_hazard_allows(SKL_HZ_FRONT_LEFT, 100, 0));
    TEST_ASSERT_TRUE(skl_hazard_allows(SKL_HZ_FRONT_LEFT, -100, 0));
    TEST_ASSERT_FALSE(skl_hazard_allows(SKL_HZ_FRONT_RIGHT, 0, 30));      /* no rotation */
    TEST_ASSERT_FALSE(skl_hazard_allows(SKL_HZ_FRONT_ANY, -100, 30));     /* reverse with a turn: no */
}

static void test_hazard_rear_only_forward(void)
{
    TEST_ASSERT_FALSE(skl_hazard_allows(SKL_HZ_REAR_LEFT, -100, 0));
    TEST_ASSERT_TRUE(skl_hazard_allows(SKL_HZ_REAR_RIGHT, 100, 0));
}

static void test_hazard_front_and_rear_block_all_motion_but_stop_is_allowed(void)
{
    const uint8_t both = SKL_HZ_FRONT_LEFT | SKL_HZ_REAR_RIGHT;
    TEST_ASSERT_FALSE(skl_hazard_allows(both, 100, 0));
    TEST_ASSERT_FALSE(skl_hazard_allows(both, -100, 0));
    TEST_ASSERT_TRUE(skl_hazard_allows(both, 0, 0));                      /* stopping is never refused */
}

static void test_hazard_fault_and_unreliable_block_motion(void)
{
    TEST_ASSERT_FALSE(skl_hazard_allows(SKL_HZ_SENSOR_FAULT, 100, 0));
    TEST_ASSERT_FALSE(skl_hazard_allows(SKL_HZ_UNRELIABLE, -100, 0));
    TEST_ASSERT_TRUE(skl_hazard_allows(SKL_HZ_UNRELIABLE, 0, 0));
    TEST_ASSERT_TRUE(skl_hazard_allows(0x80, 100, 0));                    /* unknown high bits are ignored */
}

/* ---- drive -------------------------------------------------------------- */
static void test_cap_command(void)
{
    int32_t v = 400, w = -200;
    skl_cap_command(&v, &w, 250, 90);
    TEST_ASSERT_EQUAL_INT32(250, v);
    TEST_ASSERT_EQUAL_INT32(-90, w);
    v = -10; w = 5;
    skl_cap_command(&v, &w, 250, 90);
    TEST_ASSERT_EQUAL_INT32(-10, v);
    TEST_ASSERT_EQUAL_INT32(5, w);
    v = 100; w = 100;
    skl_cap_command(&v, &w, SKL_UNSET, 90);                               /* unknown cap: no motion */
    TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_EQUAL_INT32(90, w);
}

static void test_mix(void)
{
    int32_t l, r;
    TEST_ASSERT_TRUE(skl_mix(100, 0, 180, &l, &r));
    TEST_ASSERT_EQUAL_INT32(100, l);
    TEST_ASSERT_EQUAL_INT32(100, r);
    /* w = 90 deg/s, base 180 mm: diff = 90 * 180 * 0.0174533 = 282.7 -> 282, half = 141 */
    TEST_ASSERT_TRUE(skl_mix(100, 90, 180, &l, &r));
    TEST_ASSERT_EQUAL_INT32(-41, l);
    TEST_ASSERT_EQUAL_INT32(241, r);                                      /* left turn: right wheel faster */
    TEST_ASSERT_TRUE(skl_mix(0, -90, 180, &l, &r));
    TEST_ASSERT_EQUAL_INT32(141, l);
    TEST_ASSERT_EQUAL_INT32(-141, r);                                     /* spin right on the spot */
}

static void test_mix_refuses_unknown_wheel_base(void)
{
    int32_t l = 7, r = 7;
    TEST_ASSERT_FALSE(skl_mix(100, 10, SKL_UNSET, &l, &r));
    TEST_ASSERT_FALSE(skl_mix(100, 10, 0, &l, &r));
    TEST_ASSERT_EQUAL_INT32(7, l);                                        /* outputs untouched */
}

static void test_ramp(void)
{
    /* accel 500 mm/s2 * 10 ms = 5 mm/s per step */
    TEST_ASSERT_EQUAL_INT32(5, skl_ramp(0, 100, 500, 10));
    TEST_ASSERT_EQUAL_INT32(100, skl_ramp(98, 100, 500, 10));             /* does not overshoot */
    TEST_ASSERT_EQUAL_INT32(95, skl_ramp(100, 0, 500, 10));
    TEST_ASSERT_EQUAL_INT32(-5, skl_ramp(0, -100, 500, 10));
    TEST_ASSERT_EQUAL_INT32(1, skl_ramp(0, 100, 50, 10));                 /* 0.5 rounds up to 1: never stalls */
    int32_t s = 0, steps = 0;                                             /* 0 to 250 takes 50 steps = 500 ms */
    while (s != 250 && steps < 1000) { s = skl_ramp(s, 250, 500, 10); steps++; }
    TEST_ASSERT_EQUAL_INT32(50, steps);
}

static void test_duty_calibrated(void)
{
    int32_t d = 0;
    /* k = 300 (0.3 percent per mm/s): 100 mm/s = 30 percent at 12 V = 300 permille */
    TEST_ASSERT_TRUE(skl_duty_permille(100, 300, 12000, SKL_UNSET, 250, 40, &d));
    TEST_ASSERT_EQUAL_INT32(300, d);
    skl_duty_permille(100, 300, 10000, SKL_UNSET, 250, 40, &d);           /* sagging pack: 12/10 more duty */
    TEST_ASSERT_EQUAL_INT32(360, d);
    skl_duty_permille(-100, 300, 12000, SKL_UNSET, 250, 40, &d);
    TEST_ASSERT_EQUAL_INT32(-300, d);
    skl_duty_permille(0, 300, 12000, 25, 250, 40, &d);
    TEST_ASSERT_EQUAL_INT32(0, d);                                        /* standing still is 0 even with min start */
}

static void test_duty_min_start_and_cap(void)
{
    int32_t d = 0;
    skl_duty_permille(10, 300, 12000, 25, 250, 40, &d);                   /* 30 permille, below 25 percent */
    TEST_ASSERT_EQUAL_INT32(250, d);
    skl_duty_permille(-10, 300, 12000, 25, 250, 40, &d);
    TEST_ASSERT_EQUAL_INT32(-250, d);
    skl_duty_permille(500, 3000, 8000, SKL_UNSET, 500, 40, &d);           /* 22500 permille: capped at 100 percent */
    TEST_ASSERT_EQUAL_INT32(1000, d);
    skl_duty_permille(100, 300, 0, SKL_UNSET, 250, 40, &d);               /* battery 0 (no reading): clamped, no divide by zero */
    TEST_ASSERT_EQUAL_INT32(450, d);                                      /* 100*300*120/8000 */
}

static void test_duty_uncalibrated_is_capped_at_40_percent(void)
{
    int32_t d = 0;
    TEST_ASSERT_FALSE(skl_duty_permille(250, SKL_UNSET, 12000, SKL_UNSET, 250, 40, &d));
    TEST_ASSERT_EQUAL_INT32(400, d);                                      /* speed cap = 40 percent */
    skl_duty_permille(125, SKL_UNSET, 12000, SKL_UNSET, 250, 40, &d);
    TEST_ASSERT_EQUAL_INT32(200, d);
    skl_duty_permille(-500, SKL_UNSET, 12000, SKL_UNSET, 250, 40, &d);    /* above the cap: still 40 percent */
    TEST_ASSERT_EQUAL_INT32(-400, d);
    skl_duty_permille(100, SKL_UNSET, 12000, SKL_UNSET, 0, 40, &d);       /* no speed cap known: no motion */
    TEST_ASSERT_EQUAL_INT32(0, d);
}

/* ---- mailbox ------------------------------------------------------------ */
static void test_mailbox_ttl_expires_once(void)
{
    skl_mailbox_t mb = {0};
    int32_t v = 0, w = 0;
    TEST_ASSERT_EQUAL_INT(SKL_MB_IDLE, skl_mb_get(&mb, 1000, &v, &w));
    skl_mb_put(&mb, 120, -30, 300, 1000);
    TEST_ASSERT_EQUAL_INT(SKL_MB_ACTIVE, skl_mb_get(&mb, 1299, &v, &w));
    TEST_ASSERT_EQUAL_INT32(120, v);
    TEST_ASSERT_EQUAL_INT32(-30, w);
    TEST_ASSERT_EQUAL_INT(SKL_MB_EXPIRED, skl_mb_get(&mb, 1300, &v, &w));
    TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_EQUAL_INT(SKL_MB_IDLE, skl_mb_get(&mb, 1310, &v, &w));    /* reported only once */
}

static void test_mailbox_renewal_and_overwrite(void)
{
    skl_mailbox_t mb = {0};
    int32_t v, w;
    skl_mb_put(&mb, 100, 0, 300, 0);
    skl_mb_put(&mb, 50, 10, 300, 200);                                    /* newer command replaces and extends */
    TEST_ASSERT_EQUAL_INT(SKL_MB_ACTIVE, skl_mb_get(&mb, 450, &v, &w));
    TEST_ASSERT_EQUAL_INT32(50, v);
    skl_mb_clear(&mb);                                                    /* stop command or hazard */
    TEST_ASSERT_EQUAL_INT(SKL_MB_IDLE, skl_mb_get(&mb, 460, &v, &w));
}

static void test_mailbox_across_clock_wrap(void)
{
    skl_mailbox_t mb = {0};
    int32_t v, w;
    skl_mb_put(&mb, 10, 0, 0x200, 0xFFFFFF00u);                           /* expires at 0x00000100 after the wrap */
    TEST_ASSERT_EQUAL_INT(SKL_MB_ACTIVE, skl_mb_get(&mb, 0xFFFFFFF0u, &v, &w));
    TEST_ASSERT_EQUAL_INT(SKL_MB_EXPIRED, skl_mb_get(&mb, 0x00000100u, &v, &w));
}

/* ---- motion ------------------------------------------------------------- */
static void test_motion_brake_then_coast(void)
{
    skl_motion_t m; skl_motion_reset(&m);
    skl_motion_out_t o;
    int32_t go[2] = {300, 300}, stop[2] = {0, 0};
    skl_motion_step(&m, go, 0, 200, &o);
    TEST_ASSERT_TRUE(o.en); TEST_ASSERT_FALSE(o.brake);
    skl_motion_step(&m, stop, 10, 200, &o);
    TEST_ASSERT_TRUE(o.en); TEST_ASSERT_TRUE(o.brake);                    /* braking, driver still enabled */
    TEST_ASSERT_EQUAL_INT32(0, o.duty_permille[0]);
    skl_motion_step(&m, stop, 199, 200, &o);
    TEST_ASSERT_TRUE(o.en); TEST_ASSERT_TRUE(o.brake);
    skl_motion_step(&m, stop, 200, 200, &o);
    TEST_ASSERT_FALSE(o.en); TEST_ASSERT_FALSE(o.brake);                  /* coast: EN dropped */
}

static void test_motion_idle_robot_never_enables(void)
{
    skl_motion_t m; skl_motion_reset(&m);
    skl_motion_out_t o;
    int32_t stop[2] = {0, 0};
    skl_motion_step(&m, stop, 5, 200, &o);
    TEST_ASSERT_FALSE(o.en); TEST_ASSERT_FALSE(o.brake);
}

static void test_motion_reversal_inserts_one_dead_cycle_per_wheel(void)
{
    skl_motion_t m; skl_motion_reset(&m);
    skl_motion_out_t o;
    int32_t fwd[2] = {300, 300}, mix[2] = {-300, 300};
    skl_motion_step(&m, fwd, 0, 200, &o);
    skl_motion_step(&m, mix, 10, 200, &o);
    TEST_ASSERT_EQUAL_INT32(0, o.duty_permille[0]);                       /* left reverses: dead cycle */
    TEST_ASSERT_EQUAL_INT32(300, o.duty_permille[1]);                     /* right unaffected */
    TEST_ASSERT_TRUE(o.en); TEST_ASSERT_FALSE(o.brake);
    skl_motion_step(&m, mix, 20, 200, &o);
    TEST_ASSERT_EQUAL_INT32(-300, o.duty_permille[0]);
}

static void test_motion_reverse_after_a_stop_needs_no_extra_dead_cycle(void)
{
    skl_motion_t m; skl_motion_reset(&m);
    skl_motion_out_t o;
    int32_t fwd[2] = {300, 300}, stop[2] = {0, 0}, rev[2] = {-300, -300};
    skl_motion_step(&m, fwd, 0, 200, &o);
    skl_motion_step(&m, stop, 10, 200, &o);
    skl_motion_step(&m, rev, 20, 200, &o);
    TEST_ASSERT_EQUAL_INT32(-300, o.duty_permille[0]);
}

/* ---- cliff -------------------------------------------------------------- */
static skl_cliff_cfg_t cfg_default(void)
{
    skl_cliff_cfg_t c; skl_cliff_default_cfg(&c); return c;
}

static void test_cliff_default_config_is_valid_and_bad_ones_are_not(void)
{
    skl_cliff_cfg_t c = cfg_default();
    TEST_ASSERT_TRUE(skl_cliff_cfg_valid(&c));
    c.clear_pct = c.ratio_pct;                                            /* no hysteresis */
    TEST_ASSERT_FALSE(skl_cliff_cfg_valid(&c));
    c = cfg_default(); c.ratio_pct = 5;
    TEST_ASSERT_FALSE(skl_cliff_cfg_valid(&c));
    c = cfg_default(); c.debounce_samples = 0;
    TEST_ASSERT_FALSE(skl_cliff_cfg_valid(&c));
}

static void test_cliff_without_baseline_blocks_and_stays_quiet(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    TEST_ASSERT_TRUE(skl_cliff_blocking(&c));                             /* fail towards stopped */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 100, 0));
    skl_cliff_set_baseline(&c, 3000);
    TEST_ASSERT_FALSE(skl_cliff_blocking(&c));
}

static void test_cliff_edge_needs_two_consecutive_samples(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);                                     /* edge below 1500 */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 1400, 0));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 3000, 5));     /* spike gone: counter resets */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 1400, 10));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_EDGE, skl_cliff_step(&c, &cfg, 1400, 15));
    TEST_ASSERT_TRUE(skl_cliff_blocking(&c));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 1400, 20));    /* reported once */
}

static void test_cliff_hysteresis_band(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);                                     /* edge < 1500, clear >= 1950 */
    for (uint32_t t = 0; t < 100; t += 5) {                               /* 1700 is no edge from the floor side */
        TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 1700, t));
    }
    skl_cliff_step(&c, &cfg, 1400, 100);
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_EDGE, skl_cliff_step(&c, &cfg, 1400, 105));
    for (uint32_t t = 110; t < 1000; t += 5) {                            /* and 1700 does not clear it */
        TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 1700, t));
    }
    TEST_ASSERT_TRUE(skl_cliff_blocking(&c));
}

static void test_cliff_clear_needs_200_ms_on_the_floor(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);
    skl_cliff_step(&c, &cfg, 1000, 0);
    skl_cliff_step(&c, &cfg, 1000, 5);
    TEST_ASSERT_TRUE(c.active);
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 2000, 100));   /* hold timer starts */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 2000, 299));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_CLEARED, skl_cliff_step(&c, &cfg, 2000, 300));
    TEST_ASSERT_FALSE(skl_cliff_blocking(&c));
}

static void test_cliff_bounce_restarts_the_clear_hold(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);
    skl_cliff_step(&c, &cfg, 1000, 0);
    skl_cliff_step(&c, &cfg, 1000, 5);
    skl_cliff_step(&c, &cfg, 2000, 100);                                  /* clearing starts */
    skl_cliff_step(&c, &cfg, 1000, 150);                                  /* back over the edge */
    skl_cliff_step(&c, &cfg, 2000, 200);                                  /* clearing restarts here */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 2000, 399));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_CLEARED, skl_cliff_step(&c, &cfg, 2000, 400));
}

static void test_cliff_stuck_at_rail_is_a_fault_and_blocks(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);
    skl_cliff_step(&c, &cfg, 4095, 0);                                    /* high rail: above 1500, so no edge */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 4095, 999));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_FAULT, skl_cliff_step(&c, &cfg, 4095, 1000));
    TEST_ASSERT_TRUE(skl_cliff_blocking(&c));
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_FAULT_CLEARED, skl_cliff_step(&c, &cfg, 3000, 1100));
    TEST_ASSERT_FALSE(skl_cliff_blocking(&c));
}

static void test_cliff_low_rail_gives_edge_first_then_fault(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);
    skl_cliff_step(&c, &cfg, 10, 0);
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_EDGE, skl_cliff_step(&c, &cfg, 10, 5));       /* an open edge reads very low */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_FAULT, skl_cliff_step(&c, &cfg, 10, 1000));   /* stuck there for 1 s */
}

static void test_cliff_floor_low_polarity_is_mirrored(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); cfg.pol = SKL_POL_FLOOR_LOW;
    skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 800);                                      /* edge above 1200, clear at or below 1080 */
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_NONE, skl_cliff_step(&c, &cfg, 1100, 0));
    skl_cliff_step(&c, &cfg, 1300, 5);
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_EDGE, skl_cliff_step(&c, &cfg, 1300, 10));
    skl_cliff_step(&c, &cfg, 1100, 20);                                   /* between 1080 and 1200: stays */
    TEST_ASSERT_TRUE(c.active);
    skl_cliff_step(&c, &cfg, 900, 30);
    TEST_ASSERT_EQUAL_INT(SKL_CLIFF_CLEARED, skl_cliff_step(&c, &cfg, 900, 230));
}

static void test_cliff_new_baseline_resets_an_active_edge(void)
{
    skl_cliff_cfg_t cfg = cfg_default(); skl_cliff_t c; skl_cliff_init(&c);
    skl_cliff_set_baseline(&c, 3000);
    skl_cliff_step(&c, &cfg, 1000, 0);
    skl_cliff_step(&c, &cfg, 1000, 5);
    skl_cliff_set_baseline(&c, 2000);                                     /* re-calibrated on the floor */
    TEST_ASSERT_FALSE(skl_cliff_blocking(&c));
}

static void test_floor_check(void)
{
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_UNKNOWN, skl_floor_check(3000, 200, SKL_UNSET));   /* not measured yet: refuse */
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_UNKNOWN, skl_floor_check(3000, SKL_UNSET, 20));
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_OK, skl_floor_check(3000, 200, 20));               /* 2800 counts = 68 percent of scale */
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_UNRELIABLE, skl_floor_check(500, 400, 20));        /* dark floor: 2.4 percent */
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_UNRELIABLE, skl_floor_check(4090, 200, 20));       /* saturated */
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_UNRELIABLE, skl_floor_check(30, 3000, 20));
    TEST_ASSERT_EQUAL_INT(SKL_FLOOR_OK, skl_floor_check(300, 3000, 20));               /* other polarity works too */
}

/* ---- battery ------------------------------------------------------------ */
static void run_batt(skl_batt_t *b, const skl_batt_cfg_t *cfg, int32_t mv, uint32_t from, uint32_t to,
                     skl_batt_event_t *first_event, uint32_t *event_at)
{
    for (uint32_t t = from; t <= to; t += 100) {
        skl_batt_event_t ev = skl_batt_step(b, cfg, mv, t);
        if (ev != SKL_BATT_EV_NONE && first_event && *first_event == SKL_BATT_EV_NONE) {
            *first_event = ev;
            if (event_at) { *event_at = t; }
        }
    }
}

static void test_batt_low_needs_5_seconds(void)
{
    skl_batt_cfg_t cfg; skl_batt_default_cfg(&cfg);
    skl_batt_t b; skl_batt_init(&b);
    skl_batt_event_t ev = SKL_BATT_EV_NONE; uint32_t at = 0;
    run_batt(&b, &cfg, 10500, 0, 4900, &ev, &at);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_NONE, ev);
    run_batt(&b, &cfg, 10500, 5000, 5000, &ev, &at);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_LOW, ev);
    TEST_ASSERT_EQUAL_UINT32(5000, at);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_LOW, b.state);
    ev = SKL_BATT_EV_NONE;
    run_batt(&b, &cfg, 10500, 5100, 9000, &ev, &at);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_NONE, ev);                          /* reported once */
}

static void test_batt_short_dip_is_ignored_and_timer_restarts(void)
{
    skl_batt_cfg_t cfg; skl_batt_default_cfg(&cfg);
    skl_batt_t b; skl_batt_init(&b);
    skl_batt_event_t ev = SKL_BATT_EV_NONE;
    run_batt(&b, &cfg, 10500, 0, 4000, &ev, NULL);                        /* motor start sag */
    run_batt(&b, &cfg, 11500, 4100, 4100, &ev, NULL);                     /* recovered: timer cleared */
    run_batt(&b, &cfg, 10500, 4200, 9100, &ev, NULL);                     /* 4.9 s below again: not yet */
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_NONE, ev);
    run_batt(&b, &cfg, 10500, 9200, 9200, &ev, NULL);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_LOW, ev);
}

static void test_batt_hysteresis_on_recovery(void)
{
    skl_batt_cfg_t cfg; skl_batt_default_cfg(&cfg);
    skl_batt_t b; skl_batt_init(&b);
    skl_batt_event_t ev = SKL_BATT_EV_NONE;
    run_batt(&b, &cfg, 10500, 0, 5000, &ev, NULL);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_LOW, b.state);
    skl_batt_step(&b, &cfg, 10790, 5100);                                 /* below 10650 + 150 */
    TEST_ASSERT_EQUAL_INT(SKL_BATT_LOW, b.state);
    skl_batt_step(&b, &cfg, 10800, 5200);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_OK, b.state);
}

static void test_batt_critical_is_latched_until_reboot(void)
{
    skl_batt_cfg_t cfg; skl_batt_default_cfg(&cfg);
    skl_batt_t b; skl_batt_init(&b);
    skl_batt_event_t ev = SKL_BATT_EV_NONE; uint32_t at = 0;
    run_batt(&b, &cfg, 9900, 0, 5000, &ev, &at);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_CRIT, ev);                          /* CRIT wins over LOW at the same moment */
    TEST_ASSERT_EQUAL_INT(SKL_BATT_CRIT, b.state);
    skl_batt_step(&b, &cfg, 12500, 6000);                                 /* charged on the bench: stays latched */
    TEST_ASSERT_EQUAL_INT(SKL_BATT_CRIT, b.state);
}

static void test_batt_zero_reading_is_ignored_and_vcomp(void)
{
    skl_batt_cfg_t cfg; skl_batt_default_cfg(&cfg);
    skl_batt_t b; skl_batt_init(&b);
    skl_batt_event_t ev = SKL_BATT_EV_NONE;
    run_batt(&b, &cfg, 0, 0, 10000, &ev, NULL);                           /* sensor missing: no false alarm */
    TEST_ASSERT_EQUAL_INT(SKL_BATT_EV_NONE, ev);
    TEST_ASSERT_EQUAL_INT(SKL_BATT_OK, b.state);
    TEST_ASSERT_EQUAL_INT32(11300, skl_vcomp_mv(11000, 3000, 100));       /* 3 A through 100 mohm */
    TEST_ASSERT_EQUAL_INT32(11000, skl_vcomp_mv(11000, 0, 100));
}

/* ---- params ------------------------------------------------------------- */
static void test_params_defaults_match_the_spec(void)
{
    skp_store_t s; skp_reset(&s);
    TEST_ASSERT_EQUAL_INT32(250, skp_get(&s, SKP_SPEED_CAP_MM_S));
    TEST_ASSERT_EQUAL_INT32(90, skp_get(&s, SKP_TURN_CAP_DPS));
    TEST_ASSERT_EQUAL_INT32(500, skp_get(&s, SKP_ACCEL_MM_S2));
    TEST_ASSERT_EQUAL_INT32(10650, skp_get(&s, SKP_BATT_LOW_MV));
    TEST_ASSERT_EQUAL_INT32(10050, skp_get(&s, SKP_BATT_CRIT_MV));
    TEST_ASSERT_EQUAL_INT32(SKL_UNSET, skp_get(&s, SKP_WHEEL_BASE_MM));      /* null in the yaml */
    TEST_ASSERT_EQUAL_INT32(SKL_UNSET, skp_get(&s, SKP_DUTY_PER_MM_S_X1000));
    TEST_ASSERT_EQUAL_INT32(SKL_UNSET, skp_get(&s, 99));                     /* unknown id */
}

static void test_params_ids_and_names_agree_with_the_python_side(void)
{
    /* the same table as skynoid/wire/messages.py PARAM_IDS */
    const char *names[SKP_COUNT] = {"speed_cap_mm_s", "turn_cap_dps", "accel_mm_s2", "cliff_ratio_pct",
        "clear_ratio_pct", "batt_low_mv", "batt_crit_mv", "stall_soft_ma", "stall_hard_ma",
        "wheel_base_mm", "duty_per_mm_s_x1000", "min_start_duty_pct"};
    for (int i = 0; i < SKP_COUNT; i++) {
        const skp_def_t *d = skp_find_id((uint8_t)(i + 1));
        TEST_ASSERT_NOT_NULL(d);
        TEST_ASSERT_EQUAL_STRING(names[i], d->name);
        TEST_ASSERT_TRUE(skp_find_name(names[i]) == d);
    }
    TEST_ASSERT_NULL(skp_find_id(0));
    TEST_ASSERT_NULL(skp_find_id(13));
    TEST_ASSERT_NULL(skp_find_name("nope"));
}

static void test_params_set_checks_range_and_leaves_store_untouched(void)
{
    skp_store_t s; skp_reset(&s);
    TEST_ASSERT_EQUAL_INT(SKP_OK, skp_set(&s, SKP_SPEED_CAP_MM_S, 300));
    TEST_ASSERT_EQUAL_INT32(300, skp_get(&s, SKP_SPEED_CAP_MM_S));
    TEST_ASSERT_EQUAL_INT(SKP_RANGE, skp_set(&s, SKP_SPEED_CAP_MM_S, 501));
    TEST_ASSERT_EQUAL_INT(SKP_RANGE, skp_set(&s, SKP_SPEED_CAP_MM_S, -1));
    TEST_ASSERT_EQUAL_INT32(300, skp_get(&s, SKP_SPEED_CAP_MM_S));
    TEST_ASSERT_EQUAL_INT(SKP_UNKNOWN_ID, skp_set(&s, 42, 1));
    TEST_ASSERT_EQUAL_INT(SKP_OK, skp_set(&s, SKP_WHEEL_BASE_MM, 180));
    TEST_ASSERT_EQUAL_INT(SKP_RANGE, skp_set(&s, SKP_WHEEL_BASE_MM, 10));
}

static void test_params_relations_are_enforced(void)
{
    skp_store_t s; skp_reset(&s);
    TEST_ASSERT_EQUAL_INT(SKP_CONSTRAINT, skp_set(&s, SKP_CLIFF_RATIO_PCT, 70));   /* clear is 65: no hysteresis */
    TEST_ASSERT_EQUAL_INT32(50, skp_get(&s, SKP_CLIFF_RATIO_PCT));
    TEST_ASSERT_EQUAL_INT(SKP_OK, skp_set(&s, SKP_CLEAR_RATIO_PCT, 85));           /* raise clear first ... */
    TEST_ASSERT_EQUAL_INT(SKP_OK, skp_set(&s, SKP_CLIFF_RATIO_PCT, 70));           /* ... then ratio */
    TEST_ASSERT_EQUAL_INT(SKP_CONSTRAINT, skp_set(&s, SKP_BATT_CRIT_MV, 10700));   /* crit above low */
    TEST_ASSERT_EQUAL_INT(SKP_OK, skp_set(&s, SKP_STALL_SOFT_MA, 3000));
    TEST_ASSERT_EQUAL_INT(SKP_CONSTRAINT, skp_set(&s, SKP_STALL_HARD_MA, 2000));   /* hard below soft */
    TEST_ASSERT_EQUAL_INT(SKP_OK, skp_set(&s, SKP_STALL_HARD_MA, 5000));
}

static void test_params_fill_the_logic_configs(void)
{
    skp_store_t s; skp_reset(&s);
    skp_set(&s, SKP_CLEAR_RATIO_PCT, 80);
    skl_cliff_cfg_t c; skp_cliff_cfg(&s, &c, SKL_POL_FLOOR_LOW);
    TEST_ASSERT_EQUAL_INT32(50, c.ratio_pct);
    TEST_ASSERT_EQUAL_INT32(80, c.clear_pct);
    TEST_ASSERT_EQUAL_INT(SKL_POL_FLOOR_LOW, c.pol);
    TEST_ASSERT_TRUE(skl_cliff_cfg_valid(&c));
    skl_batt_cfg_t b; skp_batt_cfg(&s, &b);
    TEST_ASSERT_EQUAL_INT32(10650, b.low_mv);
    TEST_ASSERT_EQUAL_INT32(5000, b.persist_ms);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clamp_and_median);
    RUN_TEST(test_time_reached_is_wrap_safe);
    RUN_TEST(test_hazard_none_allows_everything);
    RUN_TEST(test_hazard_front_only_reverse);
    RUN_TEST(test_hazard_rear_only_forward);
    RUN_TEST(test_hazard_front_and_rear_block_all_motion_but_stop_is_allowed);
    RUN_TEST(test_hazard_fault_and_unreliable_block_motion);
    RUN_TEST(test_cap_command);
    RUN_TEST(test_mix);
    RUN_TEST(test_mix_refuses_unknown_wheel_base);
    RUN_TEST(test_ramp);
    RUN_TEST(test_duty_calibrated);
    RUN_TEST(test_duty_min_start_and_cap);
    RUN_TEST(test_duty_uncalibrated_is_capped_at_40_percent);
    RUN_TEST(test_mailbox_ttl_expires_once);
    RUN_TEST(test_mailbox_renewal_and_overwrite);
    RUN_TEST(test_mailbox_across_clock_wrap);
    RUN_TEST(test_motion_brake_then_coast);
    RUN_TEST(test_motion_idle_robot_never_enables);
    RUN_TEST(test_motion_reversal_inserts_one_dead_cycle_per_wheel);
    RUN_TEST(test_motion_reverse_after_a_stop_needs_no_extra_dead_cycle);
    RUN_TEST(test_cliff_default_config_is_valid_and_bad_ones_are_not);
    RUN_TEST(test_cliff_without_baseline_blocks_and_stays_quiet);
    RUN_TEST(test_cliff_edge_needs_two_consecutive_samples);
    RUN_TEST(test_cliff_hysteresis_band);
    RUN_TEST(test_cliff_clear_needs_200_ms_on_the_floor);
    RUN_TEST(test_cliff_bounce_restarts_the_clear_hold);
    RUN_TEST(test_cliff_stuck_at_rail_is_a_fault_and_blocks);
    RUN_TEST(test_cliff_low_rail_gives_edge_first_then_fault);
    RUN_TEST(test_cliff_floor_low_polarity_is_mirrored);
    RUN_TEST(test_cliff_new_baseline_resets_an_active_edge);
    RUN_TEST(test_floor_check);
    RUN_TEST(test_batt_low_needs_5_seconds);
    RUN_TEST(test_batt_short_dip_is_ignored_and_timer_restarts);
    RUN_TEST(test_batt_hysteresis_on_recovery);
    RUN_TEST(test_batt_critical_is_latched_until_reboot);
    RUN_TEST(test_batt_zero_reading_is_ignored_and_vcomp);
    RUN_TEST(test_params_defaults_match_the_spec);
    RUN_TEST(test_params_ids_and_names_agree_with_the_python_side);
    RUN_TEST(test_params_set_checks_range_and_leaves_store_untouched);
    RUN_TEST(test_params_relations_are_enforced);
    RUN_TEST(test_params_fill_the_logic_configs);
    return UNITY_END();
}
