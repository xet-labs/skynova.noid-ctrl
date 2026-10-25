/*
 * test_logtext.c - tests for the logger's pure text helpers.
 * Run on the PC:  pio test -e native -f test_logtext
 */
#include <string.h>
#include <unity.h>
#include "skynoid_logtext.h"

void setUp(void) {}
void tearDown(void) {}

static void test_level_from_esp_log_lines(void)
{
    TEST_ASSERT_EQUAL_INT(0, lt_level_from_text("E (12) tag: boom"));
    TEST_ASSERT_EQUAL_INT(1, lt_level_from_text("W (12) tag: hmm"));
    TEST_ASSERT_EQUAL_INT(2, lt_level_from_text("I (5) tag: hi"));
    TEST_ASSERT_EQUAL_INT(3, lt_level_from_text("D (5) tag: dbg"));
    TEST_ASSERT_EQUAL_INT(4, lt_level_from_text("V (5) tag: verbose"));
}

static void test_level_skips_a_colour_prefix(void)
{
    TEST_ASSERT_EQUAL_INT(2, lt_level_from_text("\033[0;32mI (5) tag: hi\033[0m"));
    TEST_ASSERT_EQUAL_INT(0, lt_level_from_text("\033[0;31mE (5) tag: bad"));
}

static void test_level_rejects_things_that_are_not_log_lines(void)
{
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text(NULL));
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text(""));
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text("I"));
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text("Info: not a log line"));
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text("X (5) tag"));
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text("\033[0;32m"));        /* colour code and nothing else */
    TEST_ASSERT_EQUAL_INT(-1, lt_level_from_text("\033[0;32"));         /* unfinished colour code */
}

static void test_format_builds_the_standard_line(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_UINT32(17, lt_format(buf, sizeof buf, 2, 12345, "boot ok"));
    TEST_ASSERT_EQUAL_STRING("I (12345) boot ok", buf);
    lt_format(buf, sizeof buf, 0, 7, "x");
    TEST_ASSERT_EQUAL_STRING("E (7) x", buf);
    lt_format(buf, sizeof buf, 99, 7, "x");                             /* unknown level falls back to info */
    TEST_ASSERT_EQUAL_STRING("I (7) x", buf);
}

static void test_format_cuts_instead_of_overflowing(void)
{
    char buf[10];
    TEST_ASSERT_EQUAL_UINT32(9, lt_format(buf, sizeof buf, 2, 1, "a long message"));
    TEST_ASSERT_EQUAL_UINT32(9, strlen(buf));
    TEST_ASSERT_EQUAL_UINT32(0, lt_format(buf, 0, 2, 1, "x"));
}

static void test_format_output_is_recognised_by_level_detection(void)
{
    char buf[64];
    for (int lvl = 0; lvl <= 4; lvl++) {
        lt_format(buf, sizeof buf, lvl, 42, "msg");
        TEST_ASSERT_EQUAL_INT(lvl, lt_level_from_text(buf));
    }
}

static void test_trim_eol(void)
{
    TEST_ASSERT_EQUAL_UINT32(3, lt_trim_eol("abc\r\n", 5));
    TEST_ASSERT_EQUAL_UINT32(3, lt_trim_eol("abc", 3));
    TEST_ASSERT_EQUAL_UINT32(0, lt_trim_eol("\n\n", 2));
    TEST_ASSERT_EQUAL_UINT32(0, lt_trim_eol("", 0));
}

static void test_crlf_conversion(void)
{
    char out[32]; char prev = 0;
    size_t n = lt_to_crlf(out, "a\nb\n", 4, &prev);
    TEST_ASSERT_EQUAL_UINT32(6, n);
    TEST_ASSERT_EQUAL_MEMORY("a\r\nb\r\n", out, 6);
}

static void test_crlf_leaves_existing_pairs_alone_across_calls(void)
{
    char out[32]; char prev = 0;
    size_t n = lt_to_crlf(out, "x\r\ny", 4, &prev);
    TEST_ASSERT_EQUAL_UINT32(4, n);                                     /* already CRLF: unchanged */
    prev = 0;
    n = lt_to_crlf(out, "x\r", 2, &prev);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    n = lt_to_crlf(out, "\n", 1, &prev);                                /* the pair is split over two calls */
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_UINT8('\n', (uint8_t)out[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_level_from_esp_log_lines);
    RUN_TEST(test_level_skips_a_colour_prefix);
    RUN_TEST(test_level_rejects_things_that_are_not_log_lines);
    RUN_TEST(test_format_builds_the_standard_line);
    RUN_TEST(test_format_cuts_instead_of_overflowing);
    RUN_TEST(test_format_output_is_recognised_by_level_detection);
    RUN_TEST(test_trim_eol);
    RUN_TEST(test_crlf_conversion);
    RUN_TEST(test_crlf_leaves_existing_pairs_alone_across_calls);
    return UNITY_END();
}
