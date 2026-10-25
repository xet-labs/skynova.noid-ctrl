/*
 * test_link.c - tests for the pure link logic: session timing and PING/PONG/HELLO payloads.
 * Run on the PC:  pio test -e native -f test_link
 *
 * The payload tests build whole frames with the wire library and compare them with rows of
 * protocol-v0 section 8, so the node's PONG and HELLO bytes are the spec's bytes.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_link.h"
#include "skynoid_wire.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t WIRE_PING[] = {0x00, 0x04, 0x01, 0x09, 0x01, 0x02, 0x01, 0x01, 0x01, 0x02, 0x04, 0x03, 0xe8, 0x03, 0x01, 0x03, 0xda, 0xae, 0x00};
static const uint8_t WIRE_PONG[] = {0x00, 0x04, 0x01, 0x0a, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x09, 0x03, 0xe8, 0x03, 0x01, 0x03, 0x88, 0x13, 0x01, 0x01, 0x03, 0x73, 0xeb, 0x00};
static const uint8_t WIRE_HELLO[] = {0x00, 0x04, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x13, 0x03, 0x03, 0xff, 0x02, 0x01, 0x0a, 0x01, 0x01, 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0xa3, 0xe4, 0x00};

/* ---- session ------------------------------------------------------------ */
static void test_first_ping_starts_a_session_the_next_does_not(void)
{
    sklk_session_t s; sklk_init(&s);
    TEST_ASSERT_TRUE(sklk_on_ping(&s, 1000));
    TEST_ASSERT_FALSE(sklk_on_ping(&s, 1100));
    TEST_ASSERT_TRUE(s.active);
}

static void test_session_ends_at_exactly_the_timeout(void)
{
    sklk_session_t s; sklk_init(&s);
    sklk_on_ping(&s, 1000);
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_NONE, sklk_tick(&s, 1499, 500));
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_SESSION_ENDED, sklk_tick(&s, 1500, 500));
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_NONE, sklk_tick(&s, 1600, 500));      /* reported once only */
}

static void test_a_new_ping_after_the_end_starts_a_new_session(void)
{
    sklk_session_t s; sklk_init(&s);
    sklk_on_ping(&s, 0);
    sklk_tick(&s, 600, 500);
    TEST_ASSERT_TRUE(sklk_on_ping(&s, 2000));                              /* HELLO again */
}

static void test_ping_keeps_the_session_alive(void)
{
    sklk_session_t s; sklk_init(&s);
    sklk_on_ping(&s, 0);
    for (uint32_t t = 100; t <= 2000; t += 100) {
        sklk_on_ping(&s, t);
        TEST_ASSERT_EQUAL_INT(SKLK_TICK_NONE, sklk_tick(&s, t + 50, 500));
    }
    TEST_ASSERT_TRUE(s.active);
}

static void test_tick_without_a_session_does_nothing(void)
{
    sklk_session_t s; sklk_init(&s);
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_NONE, sklk_tick(&s, 100000, 500));
}

static void test_frames_allowed_during_session_and_for_the_hold_time(void)
{
    sklk_session_t s; sklk_init(&s);
    TEST_ASSERT_FALSE(sklk_frames_allowed(&s, 0, 3000));                   /* never had a session: plain text */
    sklk_on_ping(&s, 1000);
    TEST_ASSERT_TRUE(sklk_frames_allowed(&s, 1200, 3000));
    sklk_tick(&s, 1500, 500);                                              /* ended at 1500 */
    TEST_ASSERT_TRUE(sklk_frames_allowed(&s, 4499, 3000));
    TEST_ASSERT_FALSE(sklk_frames_allowed(&s, 4500, 3000));
}

static void test_clock_wrap_is_harmless(void)
{
    sklk_session_t s; sklk_init(&s);
    sklk_on_ping(&s, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_NONE, sklk_tick(&s, 0xFFFFFFFFu, 500));
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_NONE, sklk_tick(&s, 0x0000005Fu, 500));   /* 351 ms later across the wrap */
    TEST_ASSERT_EQUAL_INT(SKLK_TICK_SESSION_ENDED, sklk_tick(&s, 0x00000100u, 500));
    TEST_ASSERT_TRUE(sklk_frames_allowed(&s, 0x00000200u, 3000));
}

/* ---- payloads ----------------------------------------------------------- */
static void test_parse_ping_reads_little_endian_and_checks_length(void)
{
    const uint8_t p[] = {0xE8, 0x03, 0x00, 0x00};
    uint32_t t = 0;
    TEST_ASSERT_TRUE(sklk_parse_ping(p, 4, &t));
    TEST_ASSERT_EQUAL_UINT32(1000, t);
    TEST_ASSERT_FALSE(sklk_parse_ping(p, 3, &t));
    TEST_ASSERT_FALSE(sklk_parse_ping(p, 5, &t));
    TEST_ASSERT_FALSE(sklk_parse_ping(NULL, 4, &t));
}

static void test_ping_row_of_the_spec_parses(void)
{
    skw_frame_t f;
    skw_parser_t p; skw_parser_init(&p);
    skw_event_t ev = SKW_EV_NONE;
    for (size_t i = 0; i < sizeof WIRE_PING; i++) { ev = skw_parser_feed(&p, WIRE_PING[i], &f); }
    TEST_ASSERT_EQUAL_INT(SKW_EV_FRAME, ev);
    uint32_t t = 0;
    TEST_ASSERT_TRUE(sklk_parse_ping(f.payload, f.len, &t));
    TEST_ASSERT_EQUAL_UINT32(1000, t);
}

static void test_pong_matches_the_spec_row(void)
{
    skw_frame_t f = { .type = SKW_T_PONG, .msg = 1, .flags = 0, .seq = 1, .ack = 1, .len = SKLK_PONG_LEN };
    sklk_build_pong(f.payload, 1000, 5000, 0);
    uint8_t wire[SKW_MAX_WIRE];
    const size_t n = skw_frame_encode(&f, wire, sizeof wire);
    TEST_ASSERT_EQUAL_UINT32(sizeof WIRE_PONG, n);
    TEST_ASSERT_EQUAL_MEMORY(WIRE_PONG, wire, n);
}

static void test_hello_matches_the_spec_row(void)
{
    const sklk_hello_t h = { .role = 3, .caps = 0x00FF, .fw_major = 1, .fw_minor = 0, .proto = 1,
                             .reset_reason = 1, .chip_id = {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5},
                             .boot_count = 1, .schema_hash = 0 };
    skw_frame_t f = { .type = SKW_T_HELLO, .msg = 1, .flags = 0, .seq = 0, .ack = 0, .len = SKLK_HELLO_LEN };
    sklk_build_hello(f.payload, &h);
    uint8_t wire[SKW_MAX_WIRE];
    const size_t n = skw_frame_encode(&f, wire, sizeof wire);
    TEST_ASSERT_EQUAL_UINT32(sizeof WIRE_HELLO, n);
    TEST_ASSERT_EQUAL_MEMORY(WIRE_HELLO, wire, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_ping_starts_a_session_the_next_does_not);
    RUN_TEST(test_session_ends_at_exactly_the_timeout);
    RUN_TEST(test_a_new_ping_after_the_end_starts_a_new_session);
    RUN_TEST(test_ping_keeps_the_session_alive);
    RUN_TEST(test_tick_without_a_session_does_nothing);
    RUN_TEST(test_frames_allowed_during_session_and_for_the_hold_time);
    RUN_TEST(test_clock_wrap_is_harmless);
    RUN_TEST(test_parse_ping_reads_little_endian_and_checks_length);
    RUN_TEST(test_ping_row_of_the_spec_parses);
    RUN_TEST(test_pong_matches_the_spec_row);
    RUN_TEST(test_hello_matches_the_spec_row);
    return UNITY_END();
}
