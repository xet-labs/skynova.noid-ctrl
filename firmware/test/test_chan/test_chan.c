/*
 * test_chan.c - tests for the channel rules (link and console UARTs): validation, routing, describe.
 * Run on the PC:  pio test -e native -f test_chan
 *
 * The same macros that these tests exercise at run time are used in `#if` by src/core/chan_cfg.h to refuse
 * a bad flag at build time; the two `#if` blocks below prove the macros really work in the preprocessor.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_chan.h"

/* The macros must be usable by the preprocessor (this is what the build-time check relies on). */
#if !SKC_BAUD_OK(460800) || SKC_BAUD_OK(1199) || SKC_BAUD_OK(2000001)
#error "SKC_BAUD_OK does not work in #if"
#endif
#if !SKC_PIN_FORBIDDEN(12) || SKC_PIN_FORBIDDEN(13) || !SKC_PIN_EXISTS(39) || SKC_PIN_EXISTS(24)
#error "pin macros do not work in #if"
#endif

void setUp(void) {}
void tearDown(void) {}

static skc_uart_t uart(int num, int32_t baud, int tx, int rx)
{
    skc_uart_t u = { .num = num, .baud = baud, .tx_pin = tx, .rx_pin = rx };
    return u;
}

static skc_err_t check(skc_uart_t l, skc_uart_t c, bool given) { return skc_validate(&l, &c, given); }

/* ---- the three combinations the board envs use ----------------------------------------------------- */
void test_default_is_shared_on_uart0(void)
{
    const skc_uart_t l = uart(0, 460800, 1, 3);
    const skc_uart_t c = uart(0, 115200, 1, 3);        /* console baud is the unused default */
    TEST_ASSERT_TRUE(skc_shared(&l, &c));
    TEST_ASSERT_EQUAL_INT(SKC_OK, skc_validate(&l, &c, false));
}

void test_console_on_uart2_is_separate_and_valid(void)
{
    const skc_uart_t l = uart(0, 460800, 1, 3);
    const skc_uart_t c = uart(2, 115200, 5, 15);
    TEST_ASSERT_FALSE(skc_shared(&l, &c));
    TEST_ASSERT_EQUAL_INT(SKC_OK, skc_validate(&l, &c, true));
}

void test_link_on_uart2_console_on_uart0_is_valid(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(uart(2, 460800, 5, 15), uart(0, 115200, 1, 3), true));
}

void test_null_arguments_are_refused(void)
{
    const skc_uart_t u = uart(0, 460800, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_ERR_NUM, skc_validate(NULL, &u, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_NUM, skc_validate(&u, NULL, false));
    TEST_ASSERT_FALSE(skc_shared(NULL, &u));
}

/* ---- numbers and bauds ------------------------------------------------------------------------------ */
void test_uart_numbers(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_ERR_NUM, check(uart(3, 460800, 5, 15), uart(3, 115200, 5, 15), false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_NUM, check(uart(-1, 460800, 1, 3), uart(-1, 115200, 1, 3), false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_NUM, check(uart(0, 460800, 1, 3), uart(3, 115200, 5, 15), true));
}

void test_baud_limits_of_the_link(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(uart(0, 1199, 1, 3), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(uart(0, 1200, 1, 3), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(uart(0, 2000000, 1, 3), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(uart(0, 2000001, 1, 3), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(uart(0, 0, 1, 3), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(uart(0, -460800, 1, 3), c, false));
}

void test_console_baud_is_checked_only_when_separate(void)
{
    const skc_uart_t l = uart(0, 460800, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(l, uart(0, 5, 1, 3), false));              /* shared: ignored */
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(l, uart(2, 5, 5, 15), true));        /* separate: counts */
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(l, uart(2, 3000000, 5, 15), true));
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(l, uart(2, 9600, 5, 15), true));
}

void test_shared_port_refuses_a_different_explicit_console_baud(void)
{
    const skc_uart_t l = uart(0, 460800, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_ERR_SHARED_BAUD, check(l, uart(0, 9600, 1, 3), true));
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(l, uart(0, 460800, 1, 3), true));          /* same number: harmless */
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(l, uart(0, 9600, 1, 3), false));           /* default: ignored */
}

/* ---- pins ---------------------------------------------------------------------------------------------- */
void test_uart0_must_keep_the_usb_pins(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_ERR_UART0_PINS, check(uart(0, 460800, 5, 15), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_UART0_PINS, check(uart(0, 460800, 3, 1), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_UART0_PINS, check(uart(0, 460800, -1, -1), c, false));
}

void test_uart1_and_uart2_need_both_pins(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_MISSING, check(uart(1, 460800, -1, -1), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_MISSING, check(uart(1, 460800, 17, -1), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_MISSING, check(uart(2, 460800, -1, 15), c, false));
}

void test_explicit_pins_for_uart1_pass_the_pure_rules(void)
{
    /* The pure layer does not know which role owns GPIO17/18: chan_cfg.h refuses them at build time. */
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(uart(1, 460800, 17, 18), uart(0, 115200, 1, 3), false));
}

void test_forbidden_pins(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    const int bad[] = { 0, 12, 6, 7, 8, 9, 10, 11 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKC_ERR_PIN_FORBIDDEN, check(uart(1, 460800, bad[i], 18), c, false), "tx");
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKC_ERR_PIN_FORBIDDEN, check(uart(1, 460800, 17, bad[i]), c, false), "rx");
    }
}

void test_pins_that_do_not_exist(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    const int bad[] = { 20, 24, 28, 29, 30, 31, 37, 38, 40, 100 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKC_ERR_PIN_RANGE, check(uart(2, 460800, bad[i], 15), c, false), "tx");
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKC_ERR_PIN_RANGE, check(uart(2, 460800, 5, bad[i]), c, false), "rx");
    }
}

void test_usb_bridge_pins_cannot_go_to_another_uart(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_USB, check(uart(2, 460800, 1, 15), c, false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_USB, check(uart(2, 460800, 5, 3), c, false));
}

void test_input_only_pins_can_be_rx_but_not_tx(void)
{
    const skc_uart_t c = uart(0, 115200, 1, 3);
    for (int p = 34; p <= 39; p++) {
        if (p == 37 || p == 38) { continue; }                    /* do not exist */
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKC_ERR_TX_INPUT_ONLY, check(uart(2, 460800, p, 15), c, false), "tx");
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKC_OK, check(uart(2, 460800, 5, p), c, false), "rx");
    }
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(uart(2, 460800, 33, 15), c, false));      /* 33 is the last output */
}

void test_tx_and_rx_must_differ(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_SAME, check(uart(2, 460800, 5, 5), uart(0, 115200, 1, 3), false));
}

void test_link_and_console_must_not_share_a_pin(void)
{
    const skc_uart_t l = uart(1, 460800, 17, 18);
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(l, uart(2, 115200, 5, 15), true));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_SHARED, check(l, uart(2, 115200, 17, 15), true));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_SHARED, check(l, uart(2, 115200, 5, 17), true));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_SHARED, check(l, uart(2, 115200, 18, 15), true));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_SHARED, check(l, uart(2, 115200, 5, 18), true));
}

void test_shared_port_needs_the_same_pins_for_both(void)
{
    const skc_uart_t l = uart(2, 460800, 5, 15);
    TEST_ASSERT_EQUAL_INT(SKC_OK, check(l, uart(2, 460800, 5, 15), false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_SHARED_PINS, check(l, uart(2, 460800, 26, 15), false));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_SHARED_PINS, check(l, uart(2, 460800, 5, 27), false));
}

void test_the_first_problem_is_reported_link_before_console(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_ERR_BAUD, check(uart(0, 5, 1, 3), uart(2, 5, 12, 15), true));
    TEST_ASSERT_EQUAL_INT(SKC_ERR_PIN_FORBIDDEN, check(uart(0, 460800, 1, 3), uart(2, 115200, 12, 15), true));
}

/* ---- routing ------------------------------------------------------------------------------------------- */
void test_routing_when_shared_everything_uses_the_one_port(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_CH_LINK, skc_channel_for(true, SKC_REC_LOG));
    TEST_ASSERT_EQUAL_INT(SKC_CH_LINK, skc_channel_for(true, SKC_REC_TEXT));
    TEST_ASSERT_EQUAL_INT(SKC_CH_LINK, skc_channel_for(true, SKC_REC_BIN));
    TEST_ASSERT_EQUAL_INT(SKC_CH_LINK, skc_channel_for(true, 99));
}

void test_routing_when_separate_text_goes_to_the_console_and_frames_to_the_link(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_CH_CONSOLE, skc_channel_for(false, SKC_REC_LOG));
    TEST_ASSERT_EQUAL_INT(SKC_CH_CONSOLE, skc_channel_for(false, SKC_REC_TEXT));
    TEST_ASSERT_EQUAL_INT(SKC_CH_LINK, skc_channel_for(false, SKC_REC_BIN));
}

void test_routing_never_puts_an_unknown_record_on_the_frame_wire_when_separate(void)
{
    TEST_ASSERT_EQUAL_INT(SKC_CH_CONSOLE, skc_channel_for(false, 3));
    TEST_ASSERT_EQUAL_INT(SKC_CH_CONSOLE, skc_channel_for(false, -1));
    TEST_ASSERT_EQUAL_INT(SKC_CH_CONSOLE, skc_channel_for(false, 255));
}

void test_record_kinds_match_the_logger_raw_field(void)
{
    /* logger.c: raw 0 = log line, 1 = console text, 2 = binary frame. */
    TEST_ASSERT_EQUAL_INT(0, SKC_REC_LOG);
    TEST_ASSERT_EQUAL_INT(1, SKC_REC_TEXT);
    TEST_ASSERT_EQUAL_INT(2, SKC_REC_BIN);
}

/* ---- describe and error text --------------------------------------------------------------------------- */
void test_describe_shared(void)
{
    char buf[96];
    const skc_uart_t l = uart(0, 460800, 1, 3);
    const skc_uart_t c = uart(0, 115200, 1, 3);
    const size_t n = skc_describe(buf, sizeof buf, &l, &c);
    TEST_ASSERT_EQUAL_STRING("link uart0@460800 tx1 rx3; console shared", buf);
    TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
}

void test_describe_separate(void)
{
    char buf[96];
    const skc_uart_t l = uart(0, 460800, 1, 3);
    const skc_uart_t c = uart(2, 115200, 5, 15);
    skc_describe(buf, sizeof buf, &l, &c);
    TEST_ASSERT_EQUAL_STRING("link uart0@460800 tx1 rx3; console uart2@115200 tx5 rx15", buf);
}

void test_describe_never_overflows_and_always_terminates(void)
{
    const skc_uart_t l = uart(0, 460800, 1, 3);
    const skc_uart_t c = uart(2, 115200, 5, 15);
    for (size_t cap = 1; cap < 64; cap++) {
        char buf[64];
        memset(buf, 'X', sizeof buf);
        const size_t n = skc_describe(buf, cap, &l, &c);
        TEST_ASSERT_TRUE(n <= cap - 1);
        TEST_ASSERT_EQUAL_UINT(n, strlen(buf));
        TEST_ASSERT_EQUAL_INT('X', buf[cap]);                    /* nothing written past cap */
    }
    TEST_ASSERT_EQUAL_UINT(0, skc_describe(NULL, 10, &l, &c));
    char one[1] = { 'Y' };
    TEST_ASSERT_EQUAL_UINT(0, skc_describe(one, 0, &l, &c));
    TEST_ASSERT_EQUAL_INT('Y', one[0]);
}

void test_describe_with_null_settings_gives_an_empty_string(void)
{
    char buf[16] = "junk";
    TEST_ASSERT_EQUAL_UINT(0, skc_describe(buf, sizeof buf, NULL, NULL));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_every_error_has_its_own_text(void)
{
    for (int e = 0; e < SKC_ERR_COUNT; e++) {
        const char *t = skc_err_text((skc_err_t)e);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_TRUE(strlen(t) > 0);
        TEST_ASSERT_TRUE(strcmp(t, skc_err_text(SKC_ERR_COUNT)) != 0);          /* not the "unknown" text */
        for (int f = e + 1; f < SKC_ERR_COUNT; f++) {
            TEST_ASSERT_TRUE(strcmp(t, skc_err_text((skc_err_t)f)) != 0);
        }
    }
    TEST_ASSERT_NOT_NULL(skc_err_text((skc_err_t)99));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_is_shared_on_uart0);
    RUN_TEST(test_console_on_uart2_is_separate_and_valid);
    RUN_TEST(test_link_on_uart2_console_on_uart0_is_valid);
    RUN_TEST(test_null_arguments_are_refused);
    RUN_TEST(test_uart_numbers);
    RUN_TEST(test_baud_limits_of_the_link);
    RUN_TEST(test_console_baud_is_checked_only_when_separate);
    RUN_TEST(test_shared_port_refuses_a_different_explicit_console_baud);
    RUN_TEST(test_uart0_must_keep_the_usb_pins);
    RUN_TEST(test_uart1_and_uart2_need_both_pins);
    RUN_TEST(test_explicit_pins_for_uart1_pass_the_pure_rules);
    RUN_TEST(test_forbidden_pins);
    RUN_TEST(test_pins_that_do_not_exist);
    RUN_TEST(test_usb_bridge_pins_cannot_go_to_another_uart);
    RUN_TEST(test_input_only_pins_can_be_rx_but_not_tx);
    RUN_TEST(test_tx_and_rx_must_differ);
    RUN_TEST(test_link_and_console_must_not_share_a_pin);
    RUN_TEST(test_shared_port_needs_the_same_pins_for_both);
    RUN_TEST(test_the_first_problem_is_reported_link_before_console);
    RUN_TEST(test_routing_when_shared_everything_uses_the_one_port);
    RUN_TEST(test_routing_when_separate_text_goes_to_the_console_and_frames_to_the_link);
    RUN_TEST(test_routing_never_puts_an_unknown_record_on_the_frame_wire_when_separate);
    RUN_TEST(test_record_kinds_match_the_logger_raw_field);
    RUN_TEST(test_describe_shared);
    RUN_TEST(test_describe_separate);
    RUN_TEST(test_describe_never_overflows_and_always_terminates);
    RUN_TEST(test_describe_with_null_settings_gives_an_empty_string);
    RUN_TEST(test_every_error_has_its_own_text);
    return UNITY_END();
}
