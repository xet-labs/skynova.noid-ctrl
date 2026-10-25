/*
 * test_console.c - tests for the console engine (table, help, execute, replies).
 *
 * Run on the PC:  pio test -e native -f test_console
 * Output is captured into a string by a test sink, so we check the exact text a
 * terminal would show (with "\n"; the UART writer adds the "\r").
 */
#include <string.h>
#include <unity.h>
#include "skynoid_console.h"

static char g_text[1024];
static size_t g_len;

static void sink(const char *t, size_t n, void *ctx)
{
    (void)ctx;
    if (g_len + n < sizeof g_text) { memcpy(g_text + g_len, t, n); g_len += n; g_text[g_len] = '\0'; }
}

static console_out_t g_out;
static int g_calls, g_argc;
static char g_arg1[32];
static bool g_maint;

static bool maint(void) { return g_maint; }

void setUp(void)
{
    console_reset();
    g_len = 0; g_text[0] = '\0';
    g_calls = 0; g_argc = 0; g_arg1[0] = '\0'; g_maint = false;
    memset(&g_out, 0, sizeof g_out);
    g_out.write = sink;
}
void tearDown(void) {}

/* run a line given as a C literal (the engine edits it in place, so copy it first) */
static int run(const char *text)
{
    char line[160];
    strncpy(line, text, sizeof line - 1); line[sizeof line - 1] = '\0';
    g_len = 0; g_text[0] = '\0';
    return console_execute(line, &g_out);
}

static int cmd_echo(int argc, char **argv, console_out_t *out)
{
    g_calls++; g_argc = argc;
    if (argc > 1) { strncpy(g_arg1, argv[1], sizeof g_arg1 - 1); }
    console_printf(out, "echo");
    for (int i = 1; i < argc; i++) { console_printf(out, " %s", argv[i]); }
    console_printf(out, "\n");
    return 0;
}
static int cmd_fail4(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv; (void)out;
    return CON_HAZARD_BLOCKED;
}
static int cmd_fail_detail(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    console_set_detail(out, "wheel_base_mm unset");
    return CON_INVALID_ARG;
}
static int cmd_move(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv; (void)out;
    g_calls++;
    return 0;
}

static const console_cmd_t CMDS[] = {
    {"echo",  "echo <words>  print the words back", cmd_echo, 0},
    {"fail4", "always refuses",                      cmd_fail4, 0},
    {"fails", "refuses with a reason",               cmd_fail_detail, 0},
    {"move",  "pretend actuation",                   cmd_move, 1},
};

static void register_all(void) { TEST_ASSERT_EQUAL_UINT32(4, console_register(CMDS, 4)); }

static void test_runs_a_command_and_prints_ok(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(0, run("echo hello world"));
    TEST_ASSERT_EQUAL_STRING("echo hello world\nOK\n", g_text);
    TEST_ASSERT_EQUAL_INT(3, g_argc);
    TEST_ASSERT_EQUAL_STRING("hello", g_arg1);
}

static void test_extra_spaces_and_tabs_are_fine(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(0, run("   echo \t a   b  "));
    TEST_ASSERT_EQUAL_STRING("echo a b\nOK\n", g_text);
}

static void test_empty_line_prints_nothing(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(0, run(""));
    TEST_ASSERT_EQUAL_STRING("", g_text);
    TEST_ASSERT_EQUAL_INT(0, run("   "));
    TEST_ASSERT_EQUAL_STRING("", g_text);
}

static void test_unknown_command(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(CON_UNKNOWN_MSG, run("bogus 1 2"));
    TEST_ASSERT_EQUAL_STRING("ERR unknown_msg\n", g_text);
}

static void test_handler_code_becomes_err_name(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(CON_HAZARD_BLOCKED, run("fail4"));
    TEST_ASSERT_EQUAL_STRING("ERR hazard_blocked\n", g_text);
}

static void test_detail_text_is_appended_and_cleared_for_the_next_line(void)
{
    register_all();
    run("fails");
    TEST_ASSERT_EQUAL_STRING("ERR invalid_arg wheel_base_mm unset\n", g_text);
    run("fail4");
    TEST_ASSERT_EQUAL_STRING("ERR hazard_blocked\n", g_text);      /* the old detail must not leak */
}

static void test_too_many_arguments(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(CON_INVALID_ARG, run("echo 1 2 3 4 5 6 7 8 9"));
    TEST_ASSERT_EQUAL_STRING("ERR invalid_arg too many arguments\n", g_text);
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

static void test_tier1_is_locked_until_maintenance(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(CON_BUSY, run("move"));
    TEST_ASSERT_EQUAL_STRING("ERR busy maintenance required\n", g_text);
    TEST_ASSERT_EQUAL_INT(0, g_calls);                              /* the handler must not even run */
    console_set_maint_check(maint);
    TEST_ASSERT_EQUAL_INT(CON_BUSY, run("move"));                   /* check installed but not active */
    g_maint = true;
    TEST_ASSERT_EQUAL_INT(0, run("move"));
    TEST_ASSERT_EQUAL_INT(1, g_calls);
}

static void test_help_lists_commands_and_marks_tier1(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(0, run("help"));
    TEST_ASSERT_NOT_NULL(strstr(g_text, "help"));
    TEST_ASSERT_NOT_NULL(strstr(g_text, "echo"));
    TEST_ASSERT_NOT_NULL(strstr(g_text, "pretend actuation  [maint]"));
    TEST_ASSERT_EQUAL_STRING("OK\n", g_text + strlen(g_text) - 3);
}

static void test_help_for_one_command(void)
{
    register_all();
    TEST_ASSERT_EQUAL_INT(0, run("help echo"));
    TEST_ASSERT_EQUAL_STRING("echo  echo <words>  print the words back\nOK\n", g_text);
    TEST_ASSERT_EQUAL_INT(CON_INVALID_ARG, run("help nothing"));
    TEST_ASSERT_EQUAL_STRING("ERR invalid_arg no such command\n", g_text);
}

static void test_register_refuses_duplicates_help_and_overflow(void)
{
    register_all();
    TEST_ASSERT_EQUAL_UINT32(0, console_register(CMDS, 1));         /* "echo" again */
    static const console_cmd_t h = {"help", "mine", cmd_echo, 0};
    TEST_ASSERT_EQUAL_UINT32(0, console_register(&h, 1));           /* "help" is built in */

    static char names[CONSOLE_MAX_CMDS][8];
    static console_cmd_t many[CONSOLE_MAX_CMDS];
    for (int i = 0; i < CONSOLE_MAX_CMDS; i++) {
        names[i][0] = 'c'; names[i][1] = (char)('a' + i / 26); names[i][2] = (char)('a' + i % 26); names[i][3] = '\0';
        many[i] = (console_cmd_t){names[i], "x", cmd_echo, 0};
    }
    TEST_ASSERT_EQUAL_UINT32(CONSOLE_MAX_CMDS - 4, console_register(many, CONSOLE_MAX_CMDS));  /* table fills up */
}

static void test_long_output_is_cut_not_overflowed(void)
{
    char big[400];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = '\0';
    int n = console_printf(&g_out, "%s", big);
    TEST_ASSERT_TRUE(n > 100 && n < 200);
    TEST_ASSERT_EQUAL_UINT32((size_t)n, g_len);
}

static void test_nack_names(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown_msg", console_nack_name(1));
    TEST_ASSERT_EQUAL_STRING("busy", console_nack_name(7));
    TEST_ASSERT_EQUAL_STRING("invalid_arg", console_nack_name(99));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_runs_a_command_and_prints_ok);
    RUN_TEST(test_extra_spaces_and_tabs_are_fine);
    RUN_TEST(test_empty_line_prints_nothing);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_handler_code_becomes_err_name);
    RUN_TEST(test_detail_text_is_appended_and_cleared_for_the_next_line);
    RUN_TEST(test_too_many_arguments);
    RUN_TEST(test_tier1_is_locked_until_maintenance);
    RUN_TEST(test_help_lists_commands_and_marks_tier1);
    RUN_TEST(test_help_for_one_command);
    RUN_TEST(test_register_refuses_duplicates_help_and_overflow);
    RUN_TEST(test_long_output_is_cut_not_overflowed);
    RUN_TEST(test_nack_names);
    return UNITY_END();
}
