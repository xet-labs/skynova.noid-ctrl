/*
 * test_dispatch.c - tests for the CMD dispatcher, the telemetry stream timer and the TLM/EVT/NACK packers.
 * Run on the PC:  pio test -e native -f test_dispatch
 *
 * Three tests build whole ACK, NACK and EVT frames and compare them with rows of protocol-v0 section 8;
 * two tests parse the drive rows of section 8 and check what the dispatcher hands to the base function.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_dispatch.h"
#include "skynoid_wire.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t WIRE_DRIVE_A[] = {0x00, 0x04, 0x01, 0x03, 0x10, 0x02, 0x02, 0x01, 0x01, 0x02, 0x06, 0x02, 0xc8, 0x01, 0x01, 0x05, 0x2c, 0x01, 0x05, 0x81, 0x00};
static const uint8_t WIRE_DRIVE_B[] = {0x00, 0x04, 0x01, 0x03, 0x10, 0x02, 0x03, 0x01, 0x01, 0x02, 0x06, 0x04, 0x9c, 0xff, 0x2d, 0x05, 0x2c, 0x01, 0x22, 0xa0, 0x00};
static const uint8_t WIRE_ACK[]     = {0x00, 0x04, 0x01, 0x04, 0x10, 0x02, 0x07, 0x02, 0x02, 0x01, 0x01, 0x03, 0x0e, 0x0a, 0x00};
static const uint8_t WIRE_NACK[]    = {0x00, 0x04, 0x01, 0x05, 0x10, 0x02, 0x08, 0x02, 0x04, 0x02, 0x03, 0x03, 0x04, 0x03, 0x03, 0x8b, 0xb9, 0x00};
static const uint8_t WIRE_EVT[]     = {0x00, 0x04, 0x01, 0x07, 0x01, 0x02, 0x09, 0x01, 0x01, 0x02, 0x01, 0x04, 0x01, 0x42, 0xf7, 0x00};

/* ---- a fake set of base functions that records what was called ------------ */
typedef struct {
    int calls[8];                 /* arm, disarm, estop, clear, stop, cal, set_param, drive */
    int32_t v, w; uint32_t ttl;
    uint8_t pid; int32_t pval;
    int rc;                       /* what every function returns */
    int set_rc;                   /* what set_param returns */
    uint16_t detail;
} fake_t;

static int f_arm(void *c)   { ((fake_t *)c)->calls[0]++; return ((fake_t *)c)->rc; }
static int f_disarm(void *c){ ((fake_t *)c)->calls[1]++; return ((fake_t *)c)->rc; }
static int f_estop(void *c) { ((fake_t *)c)->calls[2]++; return ((fake_t *)c)->rc; }
static int f_clear(void *c) { ((fake_t *)c)->calls[3]++; return ((fake_t *)c)->rc; }
static int f_stop(void *c)  { ((fake_t *)c)->calls[4]++; return ((fake_t *)c)->rc; }
static int f_cal(void *c)   { ((fake_t *)c)->calls[5]++; return ((fake_t *)c)->rc; }
static int f_param(void *c, uint8_t id, int32_t v)
{
    fake_t *f = c; f->calls[6]++; f->pid = id; f->pval = v; return f->set_rc;
}
static int f_drive(void *c, int32_t v, int32_t w, uint32_t ttl)
{
    fake_t *f = c; f->calls[7]++; f->v = v; f->w = w; f->ttl = ttl; return f->rc;
}
static uint16_t f_detail(void *c) { return ((fake_t *)c)->detail; }

static skd_ops_t make_ops(fake_t *f)
{
    memset(f, 0, sizeof *f);
    skd_ops_t o = { .ctx = f, .arm = f_arm, .disarm = f_disarm, .estop = f_estop, .clear_estop = f_clear,
                    .drive = f_drive, .stop = f_stop, .cal_floor = f_cal, .set_param = f_param,
                    .last_detail = f_detail };
    return o;
}

static skd_reply_t run(const skd_ops_t *o, skd_stream_t *s, uint8_t msg, const uint8_t *p, uint16_t len, uint32_t now)
{
    skd_reply_t r = { .is_ack = true, .code = 99, .detail = 99 };
    skd_handle_cmd(o, s, msg, p, len, now, &r);
    return r;
}

static void feed_frame(const uint8_t *wire, size_t n, skw_frame_t *out)
{
    skw_parser_t p; skw_parser_init(&p);
    skw_event_t ev = SKW_EV_NONE;
    for (size_t i = 0; i < n; i++) { ev = skw_parser_feed(&p, wire[i], out); }
    TEST_ASSERT_EQUAL_INT(SKW_EV_FRAME, ev);
}

/* ---- commands without payload --------------------------------------------- */
static void test_each_simple_command_calls_its_function_once_and_acks(void)
{
    const uint8_t msgs[] = { SKD_CMD_ARM, SKD_CMD_DISARM, SKD_CMD_ESTOP, SKD_CMD_CLEAR_ESTOP, SKD_CMD_STOP, SKD_CMD_CAL_FLOOR };
    for (int i = 0; i < 6; i++) {
        fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
        skd_reply_t r = run(&o, &s, msgs[i], NULL, 0, 0);
        TEST_ASSERT_TRUE(r.is_ack);
        for (int k = 0; k < 8; k++) { TEST_ASSERT_EQUAL_INT(k == i ? 1 : 0, f.calls[k]); }
    }
}

static void test_refusal_codes_pass_through_with_detail_zero(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    f.rc = SKB_NACK_NOT_ARMED; f.detail = 77;                      /* detail is only used for hazard and invalid_arg */
    skd_reply_t r = run(&o, &s, SKD_CMD_STOP, NULL, 0, 0);
    TEST_ASSERT_FALSE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_NOT_ARMED, r.code);
    TEST_ASSERT_EQUAL_INT(0, r.detail);
    f.rc = SKB_NACK_ESTOPPED;
    r = run(&o, &s, SKD_CMD_ARM, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_ESTOPPED, r.code);
}

static void test_a_payload_on_a_no_payload_command_is_bad_len_and_nothing_runs(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t junk[1] = {1};
    skd_reply_t r = run(&o, &s, SKD_CMD_ARM, junk, 1, 0);
    TEST_ASSERT_FALSE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_BAD_LEN, r.code);
    TEST_ASSERT_EQUAL_INT(0, r.detail);
    TEST_ASSERT_EQUAL_INT(0, f.calls[0]);
}

static void test_unknown_messages_and_console_line_are_unknown_msg(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t ids[] = { 0x00, 0x01, SKD_CMD_CONSOLE_LINE, 0x13, 0x30, 0x40, 0xFF };
    for (size_t i = 0; i < sizeof ids; i++) {
        skd_reply_t r = run(&o, &s, ids[i], NULL, 0, 0);
        TEST_ASSERT_FALSE(r.is_ack);
        TEST_ASSERT_EQUAL_INT(SKD_NACK_UNKNOWN_MSG, r.code);
    }
    for (int k = 0; k < 8; k++) { TEST_ASSERT_EQUAL_INT(0, f.calls[k]); }
}

static void test_a_missing_function_is_unknown_msg_not_a_crash(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    o.arm = NULL; o.drive = NULL; o.set_param = NULL;
    const uint8_t d[6] = {0};  const uint8_t sp[5] = {1, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(SKD_NACK_UNKNOWN_MSG, run(&o, &s, SKD_CMD_ARM, NULL, 0, 0).code);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_UNKNOWN_MSG, run(&o, &s, SKD_CMD_DRIVE, d, 6, 0).code);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_UNKNOWN_MSG, run(&o, &s, SKD_CMD_SET_PARAM, sp, 5, 0).code);
}

/* ---- drive ------------------------------------------------------------------ */
static void test_spec_drive_rows_reach_the_base_function_decoded(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    skw_frame_t fr;
    feed_frame(WIRE_DRIVE_A, sizeof WIRE_DRIVE_A, &fr);
    TEST_ASSERT_EQUAL_INT(SKW_T_CMD, fr.type);
    skd_reply_t r = run(&o, &s, fr.msg, fr.payload, fr.len, 0);
    TEST_ASSERT_TRUE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(200, f.v); TEST_ASSERT_EQUAL_INT(0, f.w); TEST_ASSERT_EQUAL_UINT32(300, f.ttl);

    feed_frame(WIRE_DRIVE_B, sizeof WIRE_DRIVE_B, &fr);
    r = run(&o, &s, fr.msg, fr.payload, fr.len, 0);
    TEST_ASSERT_TRUE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(-100, f.v); TEST_ASSERT_EQUAL_INT(45, f.w); TEST_ASSERT_EQUAL_UINT32(300, f.ttl);
}

static void test_drive_length_is_checked_with_the_expected_length_as_detail(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t d[7] = {0};
    skd_reply_t r = run(&o, &s, SKD_CMD_DRIVE, d, 5, 0);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_BAD_LEN, r.code); TEST_ASSERT_EQUAL_INT(6, r.detail);
    r = run(&o, &s, SKD_CMD_DRIVE, d, 7, 0);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_BAD_LEN, r.code); TEST_ASSERT_EQUAL_INT(6, r.detail);
    TEST_ASSERT_EQUAL_INT(0, f.calls[7]);
}

static void test_drive_extremes_decode_signed(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t d[6] = {0x00, 0x80, 0xFF, 0x7F, 0xFF, 0xFF};      /* v=-32768 w=32767 ttl=65535 */
    run(&o, &s, SKD_CMD_DRIVE, d, 6, 0);
    TEST_ASSERT_EQUAL_INT(-32768, f.v); TEST_ASSERT_EQUAL_INT(32767, f.w); TEST_ASSERT_EQUAL_UINT32(65535, f.ttl);
}

static void test_hazard_blocked_carries_the_mask_and_matches_the_spec_nack_row(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    f.rc = SKB_NACK_HAZARD_BLOCKED; f.detail = 3;
    const uint8_t d[6] = {200, 0, 0, 0, 0x2c, 0x01};
    skd_reply_t r = run(&o, &s, SKD_CMD_DRIVE, d, 6, 0);
    TEST_ASSERT_FALSE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(4, r.code); TEST_ASSERT_EQUAL_INT(3, r.detail);

    skw_frame_t fr = { .type = SKW_T_NACK, .msg = SKD_CMD_DRIVE, .flags = 0, .seq = 8, .ack = 4, .len = 3 };
    skd_build_nack(fr.payload, r.code, r.detail);
    uint8_t wire[SKW_MAX_WIRE];
    const size_t n = skw_frame_encode(&fr, wire, sizeof wire);
    TEST_ASSERT_EQUAL_UINT32(sizeof WIRE_NACK, n);
    TEST_ASSERT_EQUAL_MEMORY(WIRE_NACK, wire, n);
}

static void test_invalid_arg_from_drive_carries_the_argument_index(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    f.rc = SKB_NACK_INVALID_ARG; f.detail = 2;
    const uint8_t d[6] = {1, 0, 0, 0, 0, 0};
    skd_reply_t r = run(&o, &s, SKD_CMD_DRIVE, d, 6, 0);
    TEST_ASSERT_EQUAL_INT(6, r.code); TEST_ASSERT_EQUAL_INT(2, r.detail);
}

static void test_ack_matches_the_spec_row(void)
{
    skw_frame_t fr = { .type = SKW_T_ACK, .msg = SKD_CMD_DRIVE, .flags = 0, .seq = 7, .ack = 2, .len = 0 };
    uint8_t wire[SKW_MAX_WIRE];
    const size_t n = skw_frame_encode(&fr, wire, sizeof wire);
    TEST_ASSERT_EQUAL_UINT32(sizeof WIRE_ACK, n);
    TEST_ASSERT_EQUAL_MEMORY(WIRE_ACK, wire, n);
}

/* ---- set_param ---------------------------------------------------------------- */
static void test_set_param_decodes_id_and_signed_value(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t p[5] = {10, 0x38, 0xFF, 0xFF, 0xFF};              /* id 10, value -200 */
    skd_reply_t r = run(&o, &s, SKD_CMD_SET_PARAM, p, 5, 0);
    TEST_ASSERT_TRUE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(10, f.pid); TEST_ASSERT_EQUAL_INT(-200, f.pval);
}

static void test_set_param_failures_become_invalid_arg_with_the_argument_index(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t p[5] = {99, 0, 0, 0, 0};
    f.set_rc = SKP_UNKNOWN_ID;
    skd_reply_t r = run(&o, &s, SKD_CMD_SET_PARAM, p, 5, 0);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, r.code); TEST_ASSERT_EQUAL_INT(0, r.detail);
    f.set_rc = SKP_RANGE;
    r = run(&o, &s, SKD_CMD_SET_PARAM, p, 5, 0);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, r.code); TEST_ASSERT_EQUAL_INT(1, r.detail);
    f.set_rc = SKP_CONSTRAINT;
    r = run(&o, &s, SKD_CMD_SET_PARAM, p, 5, 0);
    TEST_ASSERT_EQUAL_INT(1, r.detail);
    r = run(&o, &s, SKD_CMD_SET_PARAM, p, 4, 0);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_BAD_LEN, r.code); TEST_ASSERT_EQUAL_INT(5, r.detail);
}

/* ---- subscribe and the stream timer ------------------------------------------ */
static void test_subscribe_starts_the_stream_first_tlm_at_once_then_every_period(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    TEST_ASSERT_FALSE(skd_stream_due(&s, 1000));                    /* off until subscribed */
    const uint8_t p[3] = {1, 100, 0};
    TEST_ASSERT_TRUE(run(&o, &s, SKD_CMD_SUBSCRIBE, p, 3, 1000).is_ack);
    TEST_ASSERT_TRUE(skd_stream_due(&s, 1000));
    TEST_ASSERT_FALSE(skd_stream_due(&s, 1099));
    TEST_ASSERT_TRUE(skd_stream_due(&s, 1100));
    TEST_ASSERT_FALSE(skd_stream_due(&s, 1150));
}

static void test_a_late_tick_gives_one_tlm_not_a_burst(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t p[3] = {1, 100, 0};
    run(&o, &s, SKD_CMD_SUBSCRIBE, p, 3, 0);
    TEST_ASSERT_TRUE(skd_stream_due(&s, 0));
    TEST_ASSERT_TRUE(skd_stream_due(&s, 1000));                     /* 900 ms late */
    TEST_ASSERT_FALSE(skd_stream_due(&s, 1001));
    TEST_ASSERT_TRUE(skd_stream_due(&s, 1100));
}

static void test_period_zero_turns_the_stream_off_and_stop_does_too(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t on[3] = {1, 50, 0}, off[3] = {1, 0, 0};
    run(&o, &s, SKD_CMD_SUBSCRIBE, on, 3, 0);
    TEST_ASSERT_TRUE(skd_stream_due(&s, 0));
    TEST_ASSERT_TRUE(run(&o, &s, SKD_CMD_SUBSCRIBE, off, 3, 10).is_ack);
    TEST_ASSERT_FALSE(skd_stream_due(&s, 500));
    run(&o, &s, SKD_CMD_SUBSCRIBE, on, 3, 600);
    skd_stream_stop(&s);                                            /* session ended */
    TEST_ASSERT_FALSE(skd_stream_due(&s, 700));
}

static void test_subscribe_rejects_other_streams_and_bad_length(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t other[3] = {2, 100, 0};
    skd_reply_t r = run(&o, &s, SKD_CMD_SUBSCRIBE, other, 3, 0);
    TEST_ASSERT_FALSE(r.is_ack);
    TEST_ASSERT_EQUAL_INT(SKB_NACK_INVALID_ARG, r.code); TEST_ASSERT_EQUAL_INT(0, r.detail);
    TEST_ASSERT_FALSE(skd_stream_due(&s, 0));
    r = run(&o, &s, SKD_CMD_SUBSCRIBE, other, 2, 0);
    TEST_ASSERT_EQUAL_INT(SKD_NACK_BAD_LEN, r.code); TEST_ASSERT_EQUAL_INT(3, r.detail);
    r = run(&o, NULL, SKD_CMD_SUBSCRIBE, other, 3, 0);              /* no stream object: refuse, no crash */
    TEST_ASSERT_EQUAL_INT(SKD_NACK_UNKNOWN_MSG, r.code);
}

static void test_the_stream_timer_survives_a_clock_wrap(void)
{
    fake_t f; skd_ops_t o = make_ops(&f); skd_stream_t s; skd_stream_init(&s);
    const uint8_t p[3] = {1, 100, 0};
    run(&o, &s, SKD_CMD_SUBSCRIBE, p, 3, 0xFFFFFFC0u);
    TEST_ASSERT_TRUE(skd_stream_due(&s, 0xFFFFFFC0u));
    TEST_ASSERT_FALSE(skd_stream_due(&s, 0x00000010u));             /* 80 ms later, across the wrap */
    TEST_ASSERT_TRUE(skd_stream_due(&s, 0x00000030u));              /* 112 ms later */
}

/* ---- packers ---------------------------------------------------------------- */
static void test_status_packs_17_bytes_in_spec_order(void)
{
    const skd_status_t st = { .hazard = 0x03, .range_mm = 0x0201, .range_status = 2, .batt_mv = 0x2EE0,
                              .ir_raw = {0x0101, 0x0202, 0x0303, 0x0404}, .motor_ma = 0x0BB8, .state = 2 };
    uint8_t out[SKD_STATUS_LEN];
    TEST_ASSERT_EQUAL_UINT32(17, skd_build_status(out, &st));
    const uint8_t want[17] = { 0x03, 0x01, 0x02, 0x02, 0xE0, 0x2E, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0xB8, 0x0B, 0x02 };
    TEST_ASSERT_EQUAL_MEMORY(want, out, 17);
}

static void test_cliff_event_matches_the_spec_row(void)
{
    const skb_event_t ev = { .id = SKB_EV_CLIFF, .a = 1, .b = 0 };
    skw_frame_t fr = { .type = SKW_T_EVT, .flags = 0, .seq = 9, .ack = 0 };
    uint8_t msg = 0;
    const int len = skd_build_event(fr.payload, &msg, &ev);
    TEST_ASSERT_EQUAL_INT(1, len);
    fr.msg = msg; fr.len = (uint16_t)len;
    uint8_t wire[SKW_MAX_WIRE];
    const size_t n = skw_frame_encode(&fr, wire, sizeof wire);
    TEST_ASSERT_EQUAL_UINT32(sizeof WIRE_EVT, n);
    TEST_ASSERT_EQUAL_MEMORY(WIRE_EVT, wire, n);
}

static void test_every_event_has_the_payload_of_the_spec(void)
{
    uint8_t out[2]; uint8_t msg;
    skb_event_t e = { .id = SKB_EV_HAZARD_CLEARED, .a = 0x0F, .b = 0 };
    TEST_ASSERT_EQUAL_INT(1, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(2, msg); TEST_ASSERT_EQUAL_INT(0x0F, out[0]);
    e.id = SKB_EV_BATT_LOW; e.a = 0x2EE0;
    TEST_ASSERT_EQUAL_INT(2, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(3, msg);
    TEST_ASSERT_EQUAL_INT(0xE0, out[0]); TEST_ASSERT_EQUAL_INT(0x2E, out[1]);
    e.id = SKB_EV_BATT_CRIT; e.a = 9000;
    TEST_ASSERT_EQUAL_INT(2, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(4, msg);
    e.id = SKB_EV_DRIVE_EXPIRED;
    TEST_ASSERT_EQUAL_INT(0, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(5, msg);
    e.id = SKB_EV_STALL; e.a = 0x0BB8;
    TEST_ASSERT_EQUAL_INT(2, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(6, msg);
    TEST_ASSERT_EQUAL_INT(0xB8, out[0]); TEST_ASSERT_EQUAL_INT(0x0B, out[1]);
    e.id = SKB_EV_NODE_FAULT; e.a = 2;
    TEST_ASSERT_EQUAL_INT(1, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(8, msg); TEST_ASSERT_EQUAL_INT(2, out[0]);
    e.id = SKB_EV_LINK_LOST;
    TEST_ASSERT_EQUAL_INT(0, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(9, msg);
    e.id = SKB_EV_SENSOR_FAULT; e.a = 3; e.b = 2;
    TEST_ASSERT_EQUAL_INT(2, skd_build_event(out, &msg, &e)); TEST_ASSERT_EQUAL_INT(10, msg);
    TEST_ASSERT_EQUAL_INT(3, out[0]); TEST_ASSERT_EQUAL_INT(2, out[1]);
    e.id = 77;                                                      /* not an EVT message */
    TEST_ASSERT_EQUAL_INT(-1, skd_build_event(out, &msg, &e));
}

static void test_touch_event_is_zone_then_down_flag_msg_7(void)
{
    uint8_t out[2] = {0xAA, 0xAA}; uint8_t msg = 0;
    skb_event_t e = { .id = SKB_EV_TOUCH, .a = 2, .b = 1 };       /* hand_r pressed */
    TEST_ASSERT_EQUAL_INT(2, skd_build_event(out, &msg, &e));
    TEST_ASSERT_EQUAL_INT(7, msg);
    TEST_ASSERT_EQUAL_INT(2, out[0]); TEST_ASSERT_EQUAL_INT(1, out[1]);
    e.a = 0; e.b = 0;                                             /* head released */
    TEST_ASSERT_EQUAL_INT(2, skd_build_event(out, &msg, &e));
    TEST_ASSERT_EQUAL_INT(7, msg);
    TEST_ASSERT_EQUAL_INT(0, out[0]); TEST_ASSERT_EQUAL_INT(0, out[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_simple_command_calls_its_function_once_and_acks);
    RUN_TEST(test_refusal_codes_pass_through_with_detail_zero);
    RUN_TEST(test_a_payload_on_a_no_payload_command_is_bad_len_and_nothing_runs);
    RUN_TEST(test_unknown_messages_and_console_line_are_unknown_msg);
    RUN_TEST(test_a_missing_function_is_unknown_msg_not_a_crash);
    RUN_TEST(test_spec_drive_rows_reach_the_base_function_decoded);
    RUN_TEST(test_drive_length_is_checked_with_the_expected_length_as_detail);
    RUN_TEST(test_drive_extremes_decode_signed);
    RUN_TEST(test_hazard_blocked_carries_the_mask_and_matches_the_spec_nack_row);
    RUN_TEST(test_invalid_arg_from_drive_carries_the_argument_index);
    RUN_TEST(test_ack_matches_the_spec_row);
    RUN_TEST(test_set_param_decodes_id_and_signed_value);
    RUN_TEST(test_set_param_failures_become_invalid_arg_with_the_argument_index);
    RUN_TEST(test_subscribe_starts_the_stream_first_tlm_at_once_then_every_period);
    RUN_TEST(test_a_late_tick_gives_one_tlm_not_a_burst);
    RUN_TEST(test_period_zero_turns_the_stream_off_and_stop_does_too);
    RUN_TEST(test_subscribe_rejects_other_streams_and_bad_length);
    RUN_TEST(test_the_stream_timer_survives_a_clock_wrap);
    RUN_TEST(test_status_packs_17_bytes_in_spec_order);
    RUN_TEST(test_cliff_event_matches_the_spec_row);
    RUN_TEST(test_every_event_has_the_payload_of_the_spec);
    RUN_TEST(test_touch_event_is_zone_then_down_flag_msg_7);
    return UNITY_END();
}
