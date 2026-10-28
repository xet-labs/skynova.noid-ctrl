/*
 * test_text.c - tests for the console text core (line editor, tokenizer, numbers).
 *
 * Run on the PC:  pio test -e native            (both suites)
 *                 pio test -e native -f test_text   (only this one)
 *
 * The line editor is tested the way the console will use it: by feeding it
 * byte strings exactly as a terminal sends them (including CR+LF and arrow keys)
 * and checking which events come out and what line is delivered.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_text.h"

void setUp(void) {}
void tearDown(void) {}

typedef struct { int echo, erase, line, too_long; } counts_t;

/* Feed a C string byte by byte; count events; return the last event that was not NONE. */
static skt_event_t feed_str(skt_line_t *l, const char *s, counts_t *c)
{
    skt_event_t last = SKT_EV_NONE;
    for (; *s != '\0'; s++) {
        skt_event_t ev = skt_line_feed(l, (uint8_t)*s);
        if (ev == SKT_EV_ECHO) { c->echo++; }
        else if (ev == SKT_EV_ERASE) { c->erase++; }
        else if (ev == SKT_EV_LINE) { c->line++; }
        else if (ev == SKT_EV_TOO_LONG) { c->too_long++; }
        if (ev != SKT_EV_NONE) { last = ev; }
    }
    return last;
}

/* ---- line editor -------------------------------------------------------- */
static void test_line_simple(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "help\r", &c));
    TEST_ASSERT_EQUAL_STRING("help", l.buf);
    TEST_ASSERT_EQUAL_INT(4, c.echo);
}

static void test_line_crlf_is_one_enter(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    feed_str(&l, "ls\r\n", &c);
    TEST_ASSERT_EQUAL_INT(1, c.line);                       /* the LF must not make a second line */
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "x\r", &c));
    TEST_ASSERT_EQUAL_STRING("x", l.buf);                   /* and the next line starts clean */
}

static void test_line_lf_alone(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "uptime\n", &c));
    TEST_ASSERT_EQUAL_STRING("uptime", l.buf);
}

static void test_line_empty_line(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "\r", &c));
    TEST_ASSERT_EQUAL_STRING("", l.buf);
}

static void test_line_backspace(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    feed_str(&l, "ab", &c);
    feed_str(&l, "\x7f", &c);                               /* DEL removes 'b' */
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "c\r", &c));
    TEST_ASSERT_EQUAL_STRING("ac", l.buf);
    TEST_ASSERT_EQUAL_INT(3, c.echo);
    TEST_ASSERT_EQUAL_INT(1, c.erase);
}

static void test_line_backspace_on_empty_line(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    feed_str(&l, "xy", &c);
    feed_str(&l, "\x08", &c);
    feed_str(&l, "\x08", &c);
    feed_str(&l, "\x08", &c);                               /* third one has nothing to erase */
    TEST_ASSERT_EQUAL_INT(2, c.erase);
    feed_str(&l, "z\r", &c);
    TEST_ASSERT_EQUAL_STRING("z", l.buf);
}

static void test_line_ignores_arrow_keys(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    feed_str(&l, "ab", &c);
    feed_str(&l, "\x1b[A", &c);                             /* up arrow */
    feed_str(&l, "c", &c);
    feed_str(&l, "\x1b[1;5C", &c);                          /* ctrl+right: parameters before the final byte */
    feed_str(&l, "d", &c);
    feed_str(&l, "\x1bOP", &c);                             /* F1 in SS3 form */
    feed_str(&l, "e", &c);
    feed_str(&l, "\x1b" "x", &c);                           /* lone ESC plus one byte: both swallowed */
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "f\r", &c));
    TEST_ASSERT_EQUAL_STRING("abcdef", l.buf);
}

static void test_line_up_arrow_recalls_previous_command(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    feed_str(&l, "status\r", &c);
    TEST_ASSERT_EQUAL_STRING("status", l.buf);

    /* Up arrow recalls "status" */
    skt_event_t ev = feed_str(&l, "\x1b[A", &c);
    TEST_ASSERT_EQUAL_INT(SKT_EV_REPLACE, ev);
    TEST_ASSERT_EQUAL_STRING("status", l.buf);
    TEST_ASSERT_EQUAL_UINT(0, l.erased);

    /* Typing partial then up arrow replaces it and notes erased count */
    feed_str(&l, "\r", &c);
    feed_str(&l, "xyz", &c);
    ev = feed_str(&l, "\x1b[A", &c);
    TEST_ASSERT_EQUAL_INT(SKT_EV_REPLACE, ev);
    TEST_ASSERT_EQUAL_STRING("status", l.buf);
    TEST_ASSERT_EQUAL_UINT(3, l.erased);

    /* Down arrow clears */
    ev = feed_str(&l, "\x1b[B", &c);
    TEST_ASSERT_EQUAL_INT(SKT_EV_REPLACE, ev);
    TEST_ASSERT_EQUAL_STRING("", l.buf);
    TEST_ASSERT_EQUAL_UINT(6, l.erased);
}

static void test_line_ignores_tab_and_control_bytes(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    feed_str(&l, "a\tb\x01" "c\r", &c);
    TEST_ASSERT_EQUAL_STRING("abc", l.buf);
}

static void test_line_exactly_max_length_is_accepted(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    char text[SKT_LINE_MAX + 2];
    memset(text, 'a', SKT_LINE_MAX);
    text[SKT_LINE_MAX] = '\r';
    text[SKT_LINE_MAX + 1] = '\0';
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, text, &c));
    TEST_ASSERT_EQUAL_UINT32(SKT_LINE_MAX, strlen(l.buf));
}

static void test_line_too_long_is_dropped_then_recovers(void)
{
    skt_line_t l; counts_t c = {0}; skt_line_init(&l);
    char text[SKT_LINE_MAX + 12];
    memset(text, 'a', SKT_LINE_MAX + 10);
    text[SKT_LINE_MAX + 10] = '\r';
    text[SKT_LINE_MAX + 11] = '\0';
    TEST_ASSERT_EQUAL_INT(SKT_EV_TOO_LONG, feed_str(&l, text, &c));
    TEST_ASSERT_EQUAL_INT((int)SKT_LINE_MAX, c.echo);       /* only the first 120 were echoed */
    TEST_ASSERT_EQUAL_INT(SKT_EV_LINE, feed_str(&l, "ok\r", &c));
    TEST_ASSERT_EQUAL_STRING("ok", l.buf);                  /* the next line is clean */
}

/* ---- tokenizer ---------------------------------------------------------- */
static void test_tokenize_basic(void)
{
    char line[] = "drive 100  -5\t300";                     /* double space and a tab */
    char *argv[8];
    TEST_ASSERT_EQUAL_INT(4, skt_tokenize(line, argv, 8));
    TEST_ASSERT_EQUAL_STRING("drive", argv[0]);
    TEST_ASSERT_EQUAL_STRING("100", argv[1]);
    TEST_ASSERT_EQUAL_STRING("-5", argv[2]);
    TEST_ASSERT_EQUAL_STRING("300", argv[3]);
}

static void test_tokenize_empty_and_blank(void)
{
    char empty[] = "";
    char blank[] = "   \t ";
    char *argv[4];
    TEST_ASSERT_EQUAL_INT(0, skt_tokenize(empty, argv, 4));
    TEST_ASSERT_EQUAL_INT(0, skt_tokenize(blank, argv, 4));
}

static void test_tokenize_too_many_words(void)
{
    char line[] = "a b c";
    char *argv[2];
    TEST_ASSERT_EQUAL_INT(-1, skt_tokenize(line, argv, 2));
    char fits[] = "a b";
    TEST_ASSERT_EQUAL_INT(2, skt_tokenize(fits, argv, 2));  /* exactly max_args is fine */
}

/* ---- numbers ------------------------------------------------------------ */
static void test_parse_valid(void)
{
    int32_t v = 99;
    TEST_ASSERT_TRUE(skt_parse_i32("100", &v));    TEST_ASSERT_EQUAL_INT32(100, v);
    TEST_ASSERT_TRUE(skt_parse_i32("-5", &v));     TEST_ASSERT_EQUAL_INT32(-5, v);
    TEST_ASSERT_TRUE(skt_parse_i32("+7", &v));     TEST_ASSERT_EQUAL_INT32(7, v);
    TEST_ASSERT_TRUE(skt_parse_i32("0", &v));      TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_TRUE(skt_parse_i32("0x10", &v));   TEST_ASSERT_EQUAL_INT32(16, v);
    TEST_ASSERT_TRUE(skt_parse_i32("0XfF", &v));   TEST_ASSERT_EQUAL_INT32(255, v);
    TEST_ASSERT_TRUE(skt_parse_i32("-0x10", &v));  TEST_ASSERT_EQUAL_INT32(-16, v);
    TEST_ASSERT_TRUE(skt_parse_i32("0x3C", &v));   TEST_ASSERT_EQUAL_INT32(0x3C, v);   /* an I2C address */
}

static void test_parse_limits(void)
{
    int32_t v = 0;
    TEST_ASSERT_TRUE(skt_parse_i32("2147483647", &v));    TEST_ASSERT_EQUAL_INT32(2147483647, v);
    TEST_ASSERT_TRUE(skt_parse_i32("-2147483648", &v));   TEST_ASSERT_EQUAL_INT32(INT32_MIN, v);
    TEST_ASSERT_TRUE(skt_parse_i32("0x7FFFFFFF", &v));    TEST_ASSERT_EQUAL_INT32(2147483647, v);
    TEST_ASSERT_FALSE(skt_parse_i32("2147483648", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("-2147483649", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("0xFFFFFFFF", &v));   /* fits in 32 bits but not in int32_t */
    TEST_ASSERT_FALSE(skt_parse_i32("99999999999999999999", &v));
}

static void test_parse_rejects_bad_text(void)
{
    int32_t v = 0;
    TEST_ASSERT_FALSE(skt_parse_i32("", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("-", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("+", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("0x", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("12a", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("0xg1", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("abc", &v));          /* hex digits need the 0x prefix */
    TEST_ASSERT_FALSE(skt_parse_i32(" 1", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("1 ", &v));
    TEST_ASSERT_FALSE(skt_parse_i32("1.5", &v));
}

static void test_parse_failure_leaves_output_untouched(void)
{
    int32_t v = 1234;
    TEST_ASSERT_FALSE(skt_parse_i32("12a", &v));
    TEST_ASSERT_EQUAL_INT32(1234, v);
    TEST_ASSERT_FALSE(skt_parse_i32("2147483648", &v));
    TEST_ASSERT_EQUAL_INT32(1234, v);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_line_simple);
    RUN_TEST(test_line_crlf_is_one_enter);
    RUN_TEST(test_line_lf_alone);
    RUN_TEST(test_line_empty_line);
    RUN_TEST(test_line_backspace);
    RUN_TEST(test_line_backspace_on_empty_line);
    RUN_TEST(test_line_ignores_arrow_keys);
    RUN_TEST(test_line_up_arrow_recalls_previous_command);
    RUN_TEST(test_line_ignores_tab_and_control_bytes);
    RUN_TEST(test_line_exactly_max_length_is_accepted);
    RUN_TEST(test_line_too_long_is_dropped_then_recovers);
    RUN_TEST(test_tokenize_basic);
    RUN_TEST(test_tokenize_empty_and_blank);
    RUN_TEST(test_tokenize_too_many_words);
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_parse_limits);
    RUN_TEST(test_parse_rejects_bad_text);
    RUN_TEST(test_parse_failure_leaves_output_untouched);
    return UNITY_END();
}
