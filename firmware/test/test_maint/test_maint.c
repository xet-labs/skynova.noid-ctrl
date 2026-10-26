/*
 * test_maint.c - tests for skynoid_maint (maintenance timer, mute flags, loop statistics, printout helpers).
 *
 * Run on the PC:  pio test -e native -f test_maint
 * Expected values are worked out by hand from the rules in skynoid_maint.h; the comment next to each says how.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_maint.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- timer -------------------------------------------------------------- */
static void test_enter_with_zero_minutes_uses_the_default(void)
{
    skm_t m; skm_init(&m);
    bool newly = false;
    TEST_ASSERT_EQUAL_INT(SKM_OK, skm_enter(&m, 5000, 0, &newly));
    TEST_ASSERT_TRUE(m.on);
    TEST_ASSERT_TRUE(newly);
    TEST_ASSERT_EQUAL_UINT32(5000u + 10u * 60000u, m.deadline_ms);      /* default 10 minutes */
}

static void test_enter_limits(void)
{
    skm_t m; skm_init(&m);
    TEST_ASSERT_EQUAL_INT(SKM_OK, skm_enter(&m, 0, 1, NULL));
    TEST_ASSERT_EQUAL_UINT32(60000u, m.deadline_ms);
    TEST_ASSERT_EQUAL_INT(SKM_OK, skm_enter(&m, 0, 60, NULL));
    TEST_ASSERT_EQUAL_UINT32(3600000u, m.deadline_ms);                  /* maximum 60 minutes */
}

static void test_bad_minutes_are_refused_and_change_nothing(void)
{
    skm_t m; skm_init(&m);
    TEST_ASSERT_EQUAL_INT(SKM_BAD_MINUTES, skm_enter(&m, 0, 61, NULL));
    TEST_ASSERT_EQUAL_INT(SKM_BAD_MINUTES, skm_enter(&m, 0, -1, NULL));
    TEST_ASSERT_EQUAL_INT(SKM_BAD_MINUTES, skm_enter(&m, 0, 90, NULL));
    TEST_ASSERT_FALSE(m.on);                                            /* still off */
    skm_enter(&m, 1000, 5, NULL);
    const uint32_t deadline = m.deadline_ms;
    TEST_ASSERT_EQUAL_INT(SKM_BAD_MINUTES, skm_enter(&m, 2000, 99, NULL));
    TEST_ASSERT_EQUAL_UINT32(deadline, m.deadline_ms);                  /* a refused renew keeps the old deadline */
    TEST_ASSERT_TRUE(m.on);
}

static void test_renew_moves_the_deadline_and_is_not_new(void)
{
    skm_t m; skm_init(&m);
    bool newly = false;
    skm_enter(&m, 0, 10, &newly);
    skm_mute_apply(&m, "log");
    TEST_ASSERT_EQUAL_INT(SKM_OK, skm_enter(&m, 300000, 10, &newly));
    TEST_ASSERT_FALSE(newly);                                           /* nothing to redo: already in maintenance */
    TEST_ASSERT_EQUAL_UINT32(300000u + 600000u, m.deadline_ms);         /* renewed from now */
    TEST_ASSERT_TRUE(skm_muted(&m, SKM_MUTE_LOG));                      /* a renew keeps the mute flags */
}

static void test_expiry_boundary(void)
{
    skm_t m; skm_init(&m);
    TEST_ASSERT_FALSE(skm_expired(&m, 1000000));                        /* off never expires */
    skm_enter(&m, 1000, 1, NULL);                                       /* deadline 61000 */
    TEST_ASSERT_FALSE(skm_expired(&m, 60999));
    TEST_ASSERT_TRUE(skm_expired(&m, 61000));
    TEST_ASSERT_TRUE(skm_expired(&m, 70000));
}

static void test_expiry_across_the_clock_wrap(void)
{
    skm_t m; skm_init(&m);
    skm_enter(&m, 0xFFFFFF00u, 1, NULL);                                /* deadline wraps past zero */
    TEST_ASSERT_FALSE(skm_expired(&m, 0xFFFFFF10u));
    TEST_ASSERT_FALSE(skm_expired(&m, 0xFFFFFF00u + 59999u));
    TEST_ASSERT_TRUE(skm_expired(&m, 0xFFFFFF00u + 60000u));
}

static void test_remaining_seconds_round_up(void)
{
    skm_t m; skm_init(&m);
    TEST_ASSERT_EQUAL_UINT32(0u, skm_remaining_s(&m, 0));               /* off */
    skm_enter(&m, 0, 10, NULL);                                         /* deadline 600000 ms */
    TEST_ASSERT_EQUAL_UINT32(600u, skm_remaining_s(&m, 0));
    TEST_ASSERT_EQUAL_UINT32(600u, skm_remaining_s(&m, 1));             /* 599999 ms left rounds up to 600 */
    TEST_ASSERT_EQUAL_UINT32(1u, skm_remaining_s(&m, 599001));          /* 999 ms left shows 1, not 0 */
    TEST_ASSERT_EQUAL_UINT32(0u, skm_remaining_s(&m, 600000));          /* at the deadline */
    TEST_ASSERT_EQUAL_UINT32(0u, skm_remaining_s(&m, 700000));
}

static void test_exit_reports_whether_it_was_on_and_clears_mute(void)
{
    skm_t m; skm_init(&m);
    TEST_ASSERT_FALSE(skm_exit(&m));                                    /* nothing to leave */
    skm_enter(&m, 0, 5, NULL);
    skm_mute_apply(&m, "all");
    TEST_ASSERT_TRUE(skm_exit(&m));                                     /* the caller now disarms and resets params */
    TEST_ASSERT_FALSE(m.on);
    TEST_ASSERT_EQUAL_UINT8(0, m.mute);
    TEST_ASSERT_FALSE(skm_exit(&m));
}

/* ---- mute --------------------------------------------------------------- */
static void test_mute_words(void)
{
    skm_t m; skm_init(&m);
    skm_enter(&m, 0, 5, NULL);
    TEST_ASSERT_TRUE(skm_mute_apply(&m, "tlm"));
    TEST_ASSERT_TRUE(skm_muted(&m, SKM_MUTE_TLM));
    TEST_ASSERT_FALSE(skm_muted(&m, SKM_MUTE_EVT));
    TEST_ASSERT_TRUE(skm_mute_apply(&m, "EVT"));                        /* case-insensitive */
    TEST_ASSERT_TRUE(skm_muted(&m, SKM_MUTE_EVT));
    TEST_ASSERT_TRUE(skm_muted(&m, SKM_MUTE_TLM));                      /* adds, does not replace */
    TEST_ASSERT_FALSE(skm_muted(&m, SKM_MUTE_LOG));
    TEST_ASSERT_TRUE(skm_mute_apply(&m, "all"));
    TEST_ASSERT_TRUE(skm_muted(&m, SKM_MUTE_LOG));
    TEST_ASSERT_TRUE(skm_mute_apply(&m, "none"));
    TEST_ASSERT_FALSE(skm_muted(&m, SKM_MUTE_TLM));
}

static void test_mute_rejects_other_words_and_changes_nothing(void)
{
    skm_t m; skm_init(&m);
    skm_enter(&m, 0, 5, NULL);
    skm_mute_apply(&m, "log");
    TEST_ASSERT_FALSE(skm_mute_apply(&m, "tlmx"));
    TEST_ASSERT_FALSE(skm_mute_apply(&m, "tl"));
    TEST_ASSERT_FALSE(skm_mute_apply(&m, ""));
    TEST_ASSERT_FALSE(skm_mute_apply(&m, "everything"));
    TEST_ASSERT_EQUAL_UINT8(SKM_MUTE_LOG, m.mute);
}

static void test_mute_counts_only_while_maintenance_is_on(void)
{
    skm_t m; skm_init(&m);
    skm_mute_apply(&m, "log");                                          /* set while off */
    TEST_ASSERT_FALSE(skm_muted(&m, SKM_MUTE_LOG));                     /* has no effect outside maintenance */
    bool newly = false;
    skm_enter(&m, 0, 5, &newly);
    TEST_ASSERT_TRUE(newly);
    TEST_ASSERT_FALSE(skm_muted(&m, SKM_MUTE_LOG));                     /* a fresh session starts with nothing muted */
}

/* ---- loop statistics ---------------------------------------------------- */
static void test_loop_stats(void)
{
    sks_loop_t s; sks_loop_init(&s);
    TEST_ASSERT_EQUAL_UINT32(0u, sks_loop_avg_us(&s));                  /* nothing recorded: no divide by zero */
    sks_loop_record(&s, 200, 5000);
    sks_loop_record(&s, 910, 5000);
    sks_loop_record(&s, 6000, 5000);                                    /* longer than the 5 ms period: late */
    TEST_ASSERT_EQUAL_UINT32(3u, s.count);
    TEST_ASSERT_EQUAL_UINT32(6000u, s.max_us);
    TEST_ASSERT_EQUAL_UINT32(6000u, s.last_us);
    TEST_ASSERT_EQUAL_UINT32(1u, s.late);
    TEST_ASSERT_EQUAL_UINT32(2370u, sks_loop_avg_us(&s));               /* (200 + 910 + 6000) / 3 */
}

static void test_loop_exactly_on_period_is_not_late(void)
{
    sks_loop_t s; sks_loop_init(&s);
    sks_loop_record(&s, 5000, 5000);
    TEST_ASSERT_EQUAL_UINT32(0u, s.late);
}

static void test_loop_sum_does_not_overflow_32_bits(void)
{
    sks_loop_t s; sks_loop_init(&s);
    for (int i = 0; i < 3; i++) { sks_loop_record(&s, 4000000000u, 5000); }   /* sum 12e9 > 2^32 */
    TEST_ASSERT_EQUAL_UINT32(4000000000u, sks_loop_avg_us(&s));
}

static void test_format_loop_line(void)
{
    sks_loop_t s; sks_loop_init(&s);
    s.count = 1200; s.sum_us = 288000; s.max_us = 910; s.late = 0;      /* average 240 */
    char buf[80];
    const int n = skm_format_loop(buf, sizeof buf, "safety", &s);
    TEST_ASSERT_EQUAL_STRING("safety   n=1200 avg=240us max=910us late=0", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

static void test_format_loop_truncates_but_stays_terminated(void)
{
    sks_loop_t s; sks_loop_init(&s);
    sks_loop_record(&s, 100, 5000);
    char buf[10];
    memset(buf, 'x', sizeof buf);
    const int n = skm_format_loop(buf, sizeof buf, "motion", &s);
    TEST_ASSERT_TRUE(n >= (int)sizeof buf);                             /* snprintf rules: the full length is returned */
    TEST_ASSERT_EQUAL_UINT8(0, buf[sizeof buf - 1]);
}

/* ---- printout helpers --------------------------------------------------- */
static void test_reset_names(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", skm_reset_name(0));
    TEST_ASSERT_EQUAL_STRING("power-on", skm_reset_name(1));
    TEST_ASSERT_EQUAL_STRING("brownout", skm_reset_name(2));
    TEST_ASSERT_EQUAL_STRING("watchdog", skm_reset_name(3));
    TEST_ASSERT_EQUAL_STRING("software", skm_reset_name(4));
    TEST_ASSERT_EQUAL_STRING("panic", skm_reset_name(5));
    TEST_ASSERT_EQUAL_STRING("unknown", skm_reset_name(6));
    TEST_ASSERT_EQUAL_STRING("unknown", skm_reset_name(255));
}

static void test_format_hms(void)
{
    char b[24];
    skm_format_hms(b, sizeof b, 0);        TEST_ASSERT_EQUAL_STRING("00:00:00", b);
    skm_format_hms(b, sizeof b, 3661);     TEST_ASSERT_EQUAL_STRING("01:01:01", b);     /* 1 h 1 min 1 s */
    skm_format_hms(b, sizeof b, 86399);    TEST_ASSERT_EQUAL_STRING("23:59:59", b);
    skm_format_hms(b, sizeof b, 86400);    TEST_ASSERT_EQUAL_STRING("1d 00:00:00", b);
    skm_format_hms(b, sizeof b, 90061);    TEST_ASSERT_EQUAL_STRING("1d 01:01:01", b);  /* 86400 + 3661 */
    skm_format_hms(b, sizeof b, 4294967295u); TEST_ASSERT_EQUAL_STRING("49710d 06:28:15", b);   /* largest value */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enter_with_zero_minutes_uses_the_default);
    RUN_TEST(test_enter_limits);
    RUN_TEST(test_bad_minutes_are_refused_and_change_nothing);
    RUN_TEST(test_renew_moves_the_deadline_and_is_not_new);
    RUN_TEST(test_expiry_boundary);
    RUN_TEST(test_expiry_across_the_clock_wrap);
    RUN_TEST(test_remaining_seconds_round_up);
    RUN_TEST(test_exit_reports_whether_it_was_on_and_clears_mute);
    RUN_TEST(test_mute_words);
    RUN_TEST(test_mute_rejects_other_words_and_changes_nothing);
    RUN_TEST(test_mute_counts_only_while_maintenance_is_on);
    RUN_TEST(test_loop_stats);
    RUN_TEST(test_loop_exactly_on_period_is_not_late);
    RUN_TEST(test_loop_sum_does_not_overflow_32_bits);
    RUN_TEST(test_format_loop_line);
    RUN_TEST(test_format_loop_truncates_but_stays_terminated);
    RUN_TEST(test_reset_names);
    RUN_TEST(test_format_hms);
    return UNITY_END();
}
