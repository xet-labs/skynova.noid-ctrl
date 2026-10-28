/*
 * cmd_maint.c - the `maint` console command.
 *
 *   maint            state: off, or on with the seconds left
 *   maint on [min]   enter maintenance mode for min minutes (default 10, maximum 60); typing it again renews
 *   maint off        leave it
 *
 * Tier 0 on purpose: it is the key that unlocks the tier 1 commands, so it must work without the key.
 * Entering disarms the base and switches the servo outputs off; leaving or timing out also resets the parameters.
 * Not built or run on the board when this was written. Spec: console-commands.md, protocol-v0 section 9.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "console/console_uart.h"
#include "core/maint.h"
#include "skynoid_console.h"
#include "skynoid_maint.h"

static int cmd_maint(int argc, char **argv, console_out_t *out)
{
    if (argc == 1) {
        const uint32_t left = maint_remaining_s();
        if (left == 0 || !maint_is_active()) {
            console_printf(out, "maintenance off (tier 1 commands are locked)\n");
        } else {
            console_printf(out, "maintenance ON, %lu s left (tier 1 commands unlocked)\n", (unsigned long)left);
        }
        return CON_OK;
    }
    if (strcmp(argv[1], "off") == 0 && argc == 2) {
        console_printf(out, maint_leave() ? "maintenance off: disarmed, servos off, parameters reset\n"
                                          : "maintenance was already off\n");
        return CON_OK;
    }
    if (strcmp(argv[1], "on") == 0 && argc <= 3) {
        int32_t minutes = 0;
        if (argc == 3) {
            char *end = NULL;
            const long v = strtol(argv[2], &end, 10);
            if (end == argv[2] || *end != '\0') {
                console_set_detail(out, "usage: maint on [minutes 1..60]");
                return CON_INVALID_ARG;
            }
            minutes = (int32_t)v;
        }
        bool newly = false;
        if (maint_enter(minutes, &newly) != 0) {
            console_set_detail(out, "minutes 0..60 (0 = default 10)");
            return CON_INVALID_ARG;
        }
        console_printf(out, "maintenance ON for %lu s%s: base disarmed, servos off, tier 1 commands unlocked\n",
                       (unsigned long)maint_remaining_s(), newly ? "" : " (renewed)");
        return CON_OK;
    }
    console_set_detail(out, "usage: maint [on [min] | off]");
    return CON_INVALID_ARG;
}

static const console_cmd_t MAINT_CMDS[] = {
    {"maint", "maint [on [min] | off]  maintenance mode: unlocks tier 1 commands, ends by itself (default 10 min)", cmd_maint, 0},
};

void console_register_maint(void)
{
    console_register(MAINT_CMDS, sizeof MAINT_CMDS / sizeof MAINT_CMDS[0]);
}
