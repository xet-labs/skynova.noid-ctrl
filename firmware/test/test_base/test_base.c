/*
 * test_base.c - tests for skynoid_base.h: the base node state machine (arm, estop, drive, cliff
 * reflex, link loss, battery, stall guard) driven by synthetic time and sensor traces.
 *
 * Run on the PC:  pio test -e native -f test_base
 * Hand-worked numbers (accel 500 mm/s2, motion step 10 ms => 5 mm/s per step; uncalibrated duty
 * ceiling 40 percent at the 250 mm/s speed cap => duty = speed * 400 / 250 permille):
 *   100 mm/s straight  -> 160 permille on both wheels
 *   turn 60 deg/s, base 150 mm: diff = 60 * 150 * 0.017453 = 157 mm/s; left -78, right +79
 *   -> duties -124 and +126 permille
 */
#include <string.h>
#include <unity.h>
#include "skynoid_base.h"

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    skp_store_t p;
    skb_t       b;
    uint32_t    t;
    int32_t     ir[SKB_N_IR];
    int32_t     batt_mv, ma;
    skl_motion_out_t out;
    int         reflexes;
    skb_event_t ev[64];
    int         n_ev;
} rig_t;

static void rig_init(rig_t *r, bool cliff_required)
{
    skb_cfg_t cfg;
    memset(r, 0, sizeof *r);
    skp_reset(&r->p);
    skb_cfg_default(&cfg);
    cfg.cliff_required = cliff_required;
    cfg.no_floor_adc = 300;
    cfg.min_contrast_pct = 20;
    skb_init(&r->b, &r->p, &cfg);
    for (int i = 0; i < SKB_N_IR; i++) { r->ir[i] = 3000; }
    r->batt_mv = 12000;
    r->t = 1000;
}

/* Advance time by ms (multiples of 5): safety every 5 ms, motion every 10 ms. */
static void run(rig_t *r, uint32_t ms)
{
    for (uint32_t done = 0; done < ms; done += 5) {
        r->t += 5;
        skb_safety_step(&r->b, r->ir, r->batt_mv, r->ma, r->t);
        if (skb_take_reflex(&r->b)) { r->reflexes++; }
        if (r->t % 10 == 0) { skb_motion_step(&r->b, r->t, &r->out); }
        skb_event_t e;
        while (skb_pop_event(&r->b, &e)) { if (r->n_ev < 64) { r->ev[r->n_ev++] = e; } }
    }
}

static int count_ev(const rig_t *r, uint8_t id)
{
    int n = 0;
    for (int i = 0; i < r->n_ev; i++) { if (r->ev[i].id == id) { n++; } }
    return n;
}

static const skb_event_t *find_ev(const rig_t *r, uint8_t id)
{
    for (int i = 0; i < r->n_ev; i++) { if (r->ev[i].id == id) { return &r->ev[i]; } }
    return NULL;
}

/* A bench rig that is armed and has a floor baseline (cliff protection live). */
static void armed_on_floor(rig_t *r)
{
    rig_init(r, true);
    run(r, 20);
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_arm(&r->b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_cal_floor(&r->b, r->t));
    run(r, 300);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r->b));
    r->n_ev = 0;
}

/* ---- state machine ------------------------------------------------------- */
static void test_boot_state_is_safe_and_blocked_until_a_floor_is_learned(void)
{
    rig_t r; rig_init(&r, true);
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_UNRELIABLE, skb_hazard(&r.b));     /* no baseline: fail towards stopped */
    run(&r, 50);
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_CLIFF));            /* not reported as a "new" hazard */

    rig_t bench; rig_init(&bench, false);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&bench.b));                 /* bench build: guard off until cal_floor */
}

static void test_commands_that_move_need_arm_and_say_which_nack(void)
{
    rig_t r; rig_init(&r, false);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_NOT_ARMED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_NOT_ARMED, skb_cmd_stop(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_NOT_ARMED, skb_cmd_cal_floor(&r.b, r.t));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_arm(&r.b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_arm(&r.b));                     /* arming twice is harmless */
    TEST_ASSERT_EQUAL_INT(SKB_ARMED, skb_state(&r.b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_disarm(&r.b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_disarm(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));
}

static void test_estop_latches_survives_disarm_and_needs_clear(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_estop(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_ESTOP, skb_state(&r.b));
    TEST_ASSERT_TRUE(skb_take_reflex(&r.b));
    skb_cmd_disarm(&r.b);
    TEST_ASSERT_EQUAL_INT(SKB_ESTOP, skb_state(&r.b));               /* disarm must not clear it */
    TEST_ASSERT_EQUAL_INT(SKB_NACK_ESTOPPED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_ESTOPPED, skb_cmd_arm(&r.b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_clear_estop(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));                /* cleared goes to SAFE, not ARMED */
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_clear_estop(&r.b));             /* idempotent */
}

static void test_estop_works_even_when_the_node_is_in_fault(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    r.batt_mv = 10000;
    run(&r, 5200);
    TEST_ASSERT_EQUAL_INT(SKB_FAULT, skb_state(&r.b));
    skb_cmd_estop(&r.b);
    TEST_ASSERT_EQUAL_INT(SKB_FAULT, skb_state(&r.b));               /* the more severe state stays */
}

/* ---- drive ---------------------------------------------------------------- */
static void test_drive_is_refused_with_the_hazard_mask_while_no_floor_is_known(void)
{
    rig_t r; rig_init(&r, true);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
    TEST_ASSERT_EQUAL_UINT16(SKL_HZ_UNRELIABLE, skb_last_detail(&r.b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 0, 0, 300, r.t));   /* stopping is never refused */
}

static void test_drive_argument_checks(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, skb_cmd_drive(&r.b, 100, 0, 0, r.t));
    TEST_ASSERT_EQUAL_UINT16(2, skb_last_detail(&r.b));              /* argument index 2 = ttl */
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, skb_cmd_drive(&r.b, 100, 0, 2001, r.t));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 100, 0, 2000, r.t));
    /* wheel base is unset by default: straight is fine, any turn is refused and says why */
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, skb_cmd_drive(&r.b, 100, 45, 300, r.t));
    TEST_ASSERT_NOT_NULL(strstr(skb_last_reason(&r.b), "wheel_base_mm"));
    TEST_ASSERT_EQUAL_INT(0, skp_set(&r.p, SKP_WHEEL_BASE_MM, 150));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 100, 45, 300, r.t));
}

static void test_straight_drive_ramps_then_gives_the_uncalibrated_duty(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 100, 0, 2000, r.t));
    run(&r, 50);                                                     /* 5 motion steps: 25 mm/s so far */
    TEST_ASSERT_TRUE(r.out.duty_permille[0] > 0 && r.out.duty_permille[0] < 160);
    run(&r, 400);
    TEST_ASSERT_EQUAL_INT32(160, r.out.duty_permille[0]);
    TEST_ASSERT_EQUAL_INT32(160, r.out.duty_permille[1]);
    TEST_ASSERT_TRUE(r.out.en);
    TEST_ASSERT_FALSE(r.out.brake);
}

static void test_speed_is_capped_before_ramping(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 400, 0, 2000, r.t));   /* cap is 250 */
    run(&r, 700);
    TEST_ASSERT_EQUAL_INT32(400, r.out.duty_permille[0]);               /* 250 * 400 / 250 */
}

static void test_turn_in_place_runs_the_wheels_opposite(void)
{
    rig_t r; rig_init(&r, false);
    skp_set(&r.p, SKP_WHEEL_BASE_MM, 150);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 0, 60, 2000, r.t));
    run(&r, 400);
    TEST_ASSERT_EQUAL_INT32(-124, r.out.duty_permille[0]);
    TEST_ASSERT_EQUAL_INT32(126, r.out.duty_permille[1]);
}

static void test_wheel_invert_flips_one_side_only(void)
{
    rig_t r; rig_init(&r, false);
    r.b.cfg.invert[0] = true;
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 100, 0, 2000, r.t);
    run(&r, 400);
    TEST_ASSERT_EQUAL_INT32(-160, r.out.duty_permille[0]);
    TEST_ASSERT_EQUAL_INT32(160, r.out.duty_permille[1]);
}

static void test_ttl_expiry_brakes_at_once_and_reports_once(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 100, 0, 300, r.t);
    run(&r, 250);
    TEST_ASSERT_TRUE(r.out.duty_permille[0] > 0);
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_DRIVE_EXPIRED));
    run(&r, 70);                                                     /* TTL passed: 320 ms after the command */
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_DRIVE_EXPIRED));
    TEST_ASSERT_EQUAL_INT32(0, r.out.duty_permille[0]);
    TEST_ASSERT_TRUE(r.out.en && r.out.brake);                       /* shorted motor, not coasting */
    run(&r, 500);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_DRIVE_EXPIRED));    /* never twice */
    TEST_ASSERT_FALSE(r.out.en);                                     /* brake window over: coast */
}

static void test_renewing_the_command_keeps_the_robot_moving(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    for (int i = 0; i < 10; i++) { skb_cmd_drive(&r.b, 100, 0, 300, r.t); run(&r, 100); }
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_DRIVE_EXPIRED));
    TEST_ASSERT_EQUAL_INT32(160, r.out.duty_permille[0]);
}

static void test_stop_brakes_now_and_bypasses_the_ramp(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 200, 0, 2000, r.t);
    run(&r, 500);
    TEST_ASSERT_TRUE(r.out.duty_permille[0] > 0);
    r.reflexes = 0;
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_stop(&r.b));
    TEST_ASSERT_TRUE(skb_take_reflex(&r.b));
    run(&r, 10);
    TEST_ASSERT_EQUAL_INT32(0, r.out.duty_permille[0]);
    TEST_ASSERT_TRUE(r.out.brake);
}

static void test_a_reversal_gets_one_dead_cycle(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 25, 0, 2000, r.t);
    run(&r, 100);
    skb_cmd_drive(&r.b, -25, 0, 2000, r.t);
    int zero_cycles = 0;
    int32_t prev = r.out.duty_permille[0];
    for (int i = 0; i < 40; i++) {
        run(&r, 10);
        if (prev > 0 && r.out.duty_permille[0] == 0) { zero_cycles++; }
        prev = r.out.duty_permille[0];
    }
    TEST_ASSERT_EQUAL_INT(1, zero_cycles);
    TEST_ASSERT_TRUE(r.out.duty_permille[0] < 0);
}

/* ---- link loss --------------------------------------------------------------- */
static void test_link_loss_while_armed_brakes_goes_safe_and_queues_the_event(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 100, 0, 2000, r.t);
    run(&r, 300);
    r.reflexes = 0;
    skb_link_lost(&r.b);
    run(&r, 10);
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));
    TEST_ASSERT_EQUAL_INT(1, r.reflexes);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_LINK_LOST));
    TEST_ASSERT_EQUAL_INT32(0, r.out.duty_permille[0]);
    skb_link_lost(&r.b);                                             /* already safe: no second event */
    run(&r, 10);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_LINK_LOST));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_NOT_ARMED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
}

/* ---- floor calibration and cliffs ---------------------------------------------- */
static void test_cal_floor_refuses_while_measurements_are_unset(void)
{
    rig_t r; rig_init(&r, true);
    r.b.cfg.no_floor_adc = SKL_UNSET;
    run(&r, 20);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, skb_cmd_cal_floor(&r.b, r.t));
    TEST_ASSERT_NOT_NULL(strstr(skb_last_reason(&r.b), "unset"));
}

static void test_cal_floor_needs_a_standing_robot(void)
{
    rig_t r; armed_on_floor(&r);
    skb_cmd_drive(&r.b, 100, 0, 2000, r.t);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_BUSY, skb_cmd_cal_floor(&r.b, r.t));
}

static void test_cal_floor_with_good_contrast_clears_the_block(void)
{
    rig_t r; rig_init(&r, true);
    run(&r, 20);
    skb_cmd_arm(&r.b);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_UNRELIABLE, skb_hazard(&r.b));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_cal_floor(&r.b, r.t));
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_UNRELIABLE, skb_hazard(&r.b));     /* still collecting */
    run(&r, 300);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r.b));
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_HAZARD_CLEARED));
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
}

static void test_cal_floor_with_a_dark_floor_stays_blocked(void)
{
    rig_t r; rig_init(&r, true);
    for (int i = 0; i < SKB_N_IR; i++) { r.ir[i] = 400; }            /* barely above the no-floor 300 */
    run(&r, 20);
    skb_cmd_arm(&r.b);
    skb_cmd_cal_floor(&r.b, r.t);
    run(&r, 300);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_UNRELIABLE, skb_hazard(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
}

static void test_cliff_ahead_brakes_by_reflex_and_allows_only_reverse(void)
{
    rig_t r; armed_on_floor(&r);
    skb_cmd_drive(&r.b, 200, 0, 2000, r.t);
    run(&r, 300);
    TEST_ASSERT_TRUE(r.out.duty_permille[0] > 0);
    r.reflexes = 0;
    r.ir[0] = 1000;                                                  /* front-left sees the edge */
    run(&r, 5);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r.b));                     /* one sample is not enough (debounce 2) */
    run(&r, 5);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_FRONT_LEFT, skb_hazard(&r.b));
    TEST_ASSERT_EQUAL_INT(1, r.reflexes);                            /* the safety step itself asked for the brake */
    run(&r, 10);
    TEST_ASSERT_EQUAL_INT32(0, r.out.duty_permille[0]);
    const skb_event_t *e = find_ev(&r, SKB_EV_CLIFF);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(SKL_HZ_FRONT_LEFT, e->a);

    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
    TEST_ASSERT_EQUAL_UINT16(SKL_HZ_FRONT_LEFT, skb_last_detail(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, 0, 40, 300, r.t));   /* no rotation either */
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_drive(&r.b, -100, 0, 300, r.t));                        /* straight reverse */
}

static void test_cliff_clears_only_after_the_hold_time(void)
{
    rig_t r; armed_on_floor(&r);
    r.ir[1] = 1000;
    run(&r, 20);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_FRONT_RIGHT, skb_hazard(&r.b));
    r.ir[1] = 3000;
    run(&r, 150);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_FRONT_RIGHT, skb_hazard(&r.b));    /* hysteresis: not yet */
    run(&r, 100);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r.b));
    const skb_event_t *e = find_ev(&r, SKB_EV_HAZARD_CLEARED);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(0, e->a);
}

static void test_front_and_rear_edges_together_block_everything(void)
{
    rig_t r; armed_on_floor(&r);
    r.ir[0] = 1000;
    r.ir[3] = 1000;
    run(&r, 20);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_FRONT_LEFT | SKL_HZ_REAR_RIGHT, skb_hazard(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, -100, 0, 300, r.t));
}

static void test_one_noisy_sample_does_not_trigger(void)
{
    rig_t r; armed_on_floor(&r);
    r.ir[2] = 1000;
    run(&r, 5);
    r.ir[2] = 3000;
    run(&r, 100);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r.b));
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_CLIFF));
}

static void test_sensor_stuck_at_the_rail_is_a_fault_and_blocks_motion(void)
{
    rig_t r; armed_on_floor(&r);
    r.ir[2] = 4095;
    run(&r, 900);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r.b));                     /* 1 s rule not reached yet */
    run(&r, 200);
    TEST_ASSERT_EQUAL_HEX8(SKL_HZ_SENSOR_FAULT, skb_hazard(&r.b));
    const skb_event_t *e = find_ev(&r, SKB_EV_SENSOR_FAULT);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(2, e->a);
    TEST_ASSERT_EQUAL_UINT8(2, e->b);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_HAZARD_BLOCKED, skb_cmd_drive(&r.b, 100, 0, 300, r.t));
    r.ir[2] = 3000;
    run(&r, 20);
    TEST_ASSERT_EQUAL_HEX8(0, skb_hazard(&r.b));
}

static void test_a_blocked_command_is_dropped_if_a_hazard_shows_up_before_it_runs(void)
{
    rig_t r; armed_on_floor(&r);
    skb_cmd_drive(&r.b, 100, 0, 2000, r.t);                          /* accepted: no hazard yet */
    r.ir[0] = 1000;
    run(&r, 10);                                                     /* hazard appears before the first motion step */
    run(&r, 100);
    TEST_ASSERT_EQUAL_INT32(0, r.out.duty_permille[0]);
}

/* ---- battery ---------------------------------------------------------------------- */
static void test_battery_low_reports_once_after_the_persistence_time(void)
{
    rig_t r; rig_init(&r, false);
    r.batt_mv = 10500;                                               /* below 10650, above 10050 */
    run(&r, 4500);
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_BATT_LOW));
    run(&r, 1000);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_BATT_LOW));
    TEST_ASSERT_EQUAL_UINT16(10500, find_ev(&r, SKB_EV_BATT_LOW)->a);
    run(&r, 3000);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_BATT_LOW));
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));                /* LOW alone does not stop the robot */
}

static void test_battery_critical_faults_the_node_and_latches(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 100, 0, 2000, r.t);
    r.batt_mv = 10000;
    run(&r, 5500);
    TEST_ASSERT_EQUAL_INT(SKB_FAULT, skb_state(&r.b));
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_BATT_CRIT));
    const skb_event_t *f = find_ev(&r, SKB_EV_NODE_FAULT);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_UINT16(SKB_FAULT_BATT_CRIT, f->a);
    TEST_ASSERT_FALSE(r.out.en);                                     /* driver off: coast, not brake */
    TEST_ASSERT_EQUAL_INT32(0, r.out.duty_permille[0]);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_BUSY, skb_cmd_arm(&r.b));
    skb_cmd_clear_estop(&r.b);
    TEST_ASSERT_EQUAL_INT(SKB_FAULT, skb_state(&r.b));               /* only a power cycle clears it */
    r.batt_mv = 12000;
    run(&r, 2000);
    TEST_ASSERT_EQUAL_INT(SKB_FAULT, skb_state(&r.b));
}

static void test_a_missing_battery_reading_is_ignored(void)
{
    rig_t r; rig_init(&r, false);
    r.batt_mv = 0;
    run(&r, 8000);
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_BATT_LOW));
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));
}

/* ---- stall guard --------------------------------------------------------------------- */
static void test_stall_guard_is_off_while_the_limits_are_unset(void)
{
    rig_t r; rig_init(&r, false);
    skb_cmd_arm(&r.b);
    r.ma = 20000;
    run(&r, 1000);
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_STALL));
    TEST_ASSERT_EQUAL_INT(SKB_ARMED, skb_state(&r.b));
}

static void test_soft_stall_brakes_once_and_a_hard_stall_cuts_the_driver(void)
{
    rig_t r; rig_init(&r, false);
    TEST_ASSERT_EQUAL_INT(0, skp_set(&r.p, SKP_STALL_HARD_MA, 6000));
    TEST_ASSERT_EQUAL_INT(0, skp_set(&r.p, SKP_STALL_SOFT_MA, 3000));
    skb_cmd_arm(&r.b);
    skb_cmd_drive(&r.b, 100, 0, 2000, r.t);
    run(&r, 200);
    r.reflexes = 0;
    r.ma = 4000;
    run(&r, 250);
    TEST_ASSERT_EQUAL_INT(0, count_ev(&r, SKB_EV_STALL));            /* 300 ms not reached */
    run(&r, 100);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_STALL));
    TEST_ASSERT_EQUAL_UINT16(4000, find_ev(&r, SKB_EV_STALL)->a);
    TEST_ASSERT_EQUAL_INT(1, r.reflexes);
    run(&r, 500);
    TEST_ASSERT_EQUAL_INT(1, count_ev(&r, SKB_EV_STALL));            /* reported once per stall */
    TEST_ASSERT_EQUAL_INT(SKB_ARMED, skb_state(&r.b));

    r.ma = 7000;
    run(&r, 100);
    TEST_ASSERT_EQUAL_INT(SKB_FAULT, skb_state(&r.b));
    TEST_ASSERT_FALSE(r.out.en);
    TEST_ASSERT_EQUAL_UINT16(SKB_FAULT_STALL_HARD, find_ev(&r, SKB_EV_NODE_FAULT)->a);
    r.ma = 0;
    TEST_ASSERT_EQUAL_INT(0, skb_cmd_clear_estop(&r.b));
    TEST_ASSERT_EQUAL_INT(SKB_SAFE, skb_state(&r.b));
}

/* ---- events -------------------------------------------------------------------------- */
static void test_event_queue_drops_new_events_when_full_and_counts_them(void)
{
    rig_t r; rig_init(&r, false);
    for (int i = 0; i < 20; i++) { skb_cmd_arm(&r.b); skb_link_lost(&r.b); }
    TEST_ASSERT_EQUAL_UINT8(SKB_EVQ_LEN, r.b.ev_len);
    TEST_ASSERT_EQUAL_UINT16(4, r.b.ev_dropped);
    skb_event_t e;
    int n = 0;
    while (skb_pop_event(&r.b, &e)) { TEST_ASSERT_EQUAL_UINT8(SKB_EV_LINK_LOST, e.id); n++; }
    TEST_ASSERT_EQUAL_INT(SKB_EVQ_LEN, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_state_is_safe_and_blocked_until_a_floor_is_learned);
    RUN_TEST(test_commands_that_move_need_arm_and_say_which_nack);
    RUN_TEST(test_estop_latches_survives_disarm_and_needs_clear);
    RUN_TEST(test_estop_works_even_when_the_node_is_in_fault);
    RUN_TEST(test_drive_is_refused_with_the_hazard_mask_while_no_floor_is_known);
    RUN_TEST(test_drive_argument_checks);
    RUN_TEST(test_straight_drive_ramps_then_gives_the_uncalibrated_duty);
    RUN_TEST(test_speed_is_capped_before_ramping);
    RUN_TEST(test_turn_in_place_runs_the_wheels_opposite);
    RUN_TEST(test_wheel_invert_flips_one_side_only);
    RUN_TEST(test_ttl_expiry_brakes_at_once_and_reports_once);
    RUN_TEST(test_renewing_the_command_keeps_the_robot_moving);
    RUN_TEST(test_stop_brakes_now_and_bypasses_the_ramp);
    RUN_TEST(test_a_reversal_gets_one_dead_cycle);
    RUN_TEST(test_link_loss_while_armed_brakes_goes_safe_and_queues_the_event);
    RUN_TEST(test_cal_floor_refuses_while_measurements_are_unset);
    RUN_TEST(test_cal_floor_needs_a_standing_robot);
    RUN_TEST(test_cal_floor_with_good_contrast_clears_the_block);
    RUN_TEST(test_cal_floor_with_a_dark_floor_stays_blocked);
    RUN_TEST(test_cliff_ahead_brakes_by_reflex_and_allows_only_reverse);
    RUN_TEST(test_cliff_clears_only_after_the_hold_time);
    RUN_TEST(test_front_and_rear_edges_together_block_everything);
    RUN_TEST(test_one_noisy_sample_does_not_trigger);
    RUN_TEST(test_sensor_stuck_at_the_rail_is_a_fault_and_blocks_motion);
    RUN_TEST(test_a_blocked_command_is_dropped_if_a_hazard_shows_up_before_it_runs);
    RUN_TEST(test_battery_low_reports_once_after_the_persistence_time);
    RUN_TEST(test_battery_critical_faults_the_node_and_latches);
    RUN_TEST(test_a_missing_battery_reading_is_ignored);
    RUN_TEST(test_stall_guard_is_off_while_the_limits_are_unset);
    RUN_TEST(test_soft_stall_brakes_once_and_a_hard_stall_cuts_the_driver);
    RUN_TEST(test_event_queue_drops_new_events_when_full_and_counts_them);
    return UNITY_END();
}
