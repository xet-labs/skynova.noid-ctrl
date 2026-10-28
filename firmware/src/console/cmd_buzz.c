/*
 * cmd_buzz.c - the `buzz` console command: play one buzzer pattern on demand (tier 1).
 *
 *   buzz <pattern>    pattern is a name (off, chirp, double_chirp, low_battery, critical, error) or its id 0..5
 *
 * Why: to hear the buzzer without waiting for a real event: check the pin polarity (BUZZER_ACTIVE_LEVEL in
 * hal_indicator.c is an assumption) and what the low_battery and critical patterns sound like.
 *
 * `critical` never ends by itself (1 s on, 1 s off): stop it with `buzz off`. The same call is what the base
 * layer makes on a real low or critical battery (hal_buzzer_play), so this plays exactly the real pattern.
 * Limit: it does not know about a real critical-battery alarm in progress, so `buzz off` or any other pattern
 * would replace that alarm. It is tier 1 (needs `maint on`) and a person is at the terminal by then.
 *
 * Not a framed command: CMD 0x40 (protocol-v0) is not in the dispatcher yet.
 *
 * Called by: console_start() -> console_register_buzz().
 * Test    : on the board: `maint on`, then `buzz chirp` (one 80 ms beep), `buzz low_battery` (three 120 ms beeps),
 *           `buzz critical` (1 s on / 1 s off), `buzz off`. Not built and not run when this was written.
 * Spec    : 10-specs/console-commands.md "Actuation and features" (`buzz <pattern>`), Buzzer table in 02-base-node.md.
 */
#include <stdint.h>
#include <string.h>

#include "console/console_uart.h"
#include "hal.h"
#include "skynoid_console.h"
#include "skynoid_text.h"

typedef struct { const char *name; uint8_t id; } buzz_pattern_t;

static const buzz_pattern_t PATTERNS[] = {
    { "off", BUZ_OFF }, { "chirp", BUZ_CHIRP }, { "double_chirp", BUZ_DOUBLE_CHIRP },
    { "low_battery", BUZ_LOW_BATTERY }, { "critical", BUZ_CRITICAL }, { "error", BUZ_ERROR },
};
#define N_PATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

static int cmd_buzz(int argc, char **argv, console_out_t *out)
{
    int idx = -1;
    if (argc == 2) {
        for (size_t i = 0; i < N_PATTERNS; i++) {
            if (strcmp(argv[1], PATTERNS[i].name) == 0) { idx = (int)i; break; }
        }
        int32_t v;
        if (idx < 0 && skt_parse_i32(argv[1], &v) && v >= 0 && v < (int32_t)N_PATTERNS) { idx = (int)v; }
    }
    if (idx < 0) {
        console_printf(out, "patterns: off chirp double_chirp low_battery critical error (or 0..5)\n");
        console_set_detail(out, "usage: buzz <pattern>");
        return CON_INVALID_ARG;
    }
    hal_buzzer_play(PATTERNS[idx].id);
    console_printf(out, "buzz %s\n", PATTERNS[idx].name);
    if (PATTERNS[idx].id == BUZ_CRITICAL) { console_printf(out, "critical runs until `buzz off`\n"); }
    return CON_OK;
}

static const console_cmd_t BUZZ_CMDS[] = {
    {"buzz", "buzz <pattern>  play a buzzer pattern: off chirp double_chirp low_battery critical error", cmd_buzz, 1},
};

void console_register_buzz(void)
{
    console_register(BUZZ_CMDS, 1);
}
