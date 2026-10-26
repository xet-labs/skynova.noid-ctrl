/*
 * cmd_system.c - read-only console commands: version, uptime, pins. (`status` moved to cmd_base.c.)
 *
 * Purpose : the first four commands, so a person can check the board is alive and wired
 *           as expected. All tier 0 (always available, they change nothing).
 * Called by: console_start() -> console_register_system().
 * Test    : on the board (see docs/steps/step-1.3b-console-engine.md).
 * Spec    : 10-specs/console-commands.md, tier 0 table.
 *
 * `status` is a placeholder until the base node exists behind the console: it says so
 * instead of inventing a state (a wrong "armed" would be worse than no answer).
 *
 * IDF 6.1 note (checked against the installed headers): esp_get_idf_version() is declared in
 * esp_idf_version.h, not in esp_system.h.
 */
#include <inttypes.h>

#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "app_config.h"
#include "board/board_pins.h"
#include "console/console_uart.h"
#include "core/chan.h"
#include "skynoid_console.h"

static int cmd_version(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    console_printf(out, "skynoid fw %d.%d proto %d role single\n", FW_MAJOR, FW_MINOR, PROTO_VERSION);
    console_printf(out, "idf %s, built %s %s\n", esp_get_idf_version(), __DATE__, __TIME__);
    console_printf(out, "flags link_uart=%d console_uart=%d actuate=%d log_level=%d\n",
                   UART_LINK_NUM, UART_CONSOLE_NUM, SKYNOID_CONSOLE_ACTUATE, SKYNOID_LOG_LEVEL);
    console_printf(out, "channels %s\n", chan_describe());
    console_printf(out, "reset_reason %d\n", (int)esp_reset_reason());
    return 0;
}

static int cmd_uptime(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    const int64_t secs = esp_timer_get_time() / 1000000;
    console_printf(out, "uptime %" PRId64 " s\n", secs);
    return 0;
}

/* Print "PIN_NAME  GPIOnn" for every pin in board_pins.h (the same list the compile-time check uses). */
#define PRINT_PIN(p) console_printf(out, "%-20s GPIO%d\n", #p, (int)(p));
static int cmd_pins(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    SK_ALL_PINS(PRINT_PIN)
    return 0;
}

static const console_cmd_t SYSTEM_CMDS[] = {
    {"version", "firmware, protocol, IDF version, build flags", cmd_version, 0},
    {"uptime",  "seconds since boot",                           cmd_uptime,  0},
    {"pins",    "every GPIO role from board_pins.h",            cmd_pins,    0},
};

void console_register_system(void)
{
    console_register(SYSTEM_CMDS, sizeof SYSTEM_CMDS / sizeof SYSTEM_CMDS[0]);
}
