/*
 * skynoid_console.h - the command table and the engine that runs one console line.
 *
 * WHAT THIS FILE IS
 *   Pure C, NO ESP-IDF includes (tested on the PC: pio test -e native -f test_console).
 *   Modules register commands; the UART task hands in finished lines; the engine
 *   finds the command, runs it and prints "OK" or "ERR <name> <detail>".
 *   Spec: vault 10-specs/console-commands.md and sprint-steps.md step 1.3.
 *
 * HOW A COMMAND IS WRITTEN
 *     static int cmd_uptime(int argc, char **argv, console_out_t *out) {
 *         console_printf(out, "uptime %d s\n", secs);
 *         return 0;                       // 0 = OK, or a NACK code 1..7
 *     }
 *     static const console_cmd_t CMDS[] = { {"uptime", "seconds since boot", cmd_uptime, 0} };
 *     console_register(CMDS, 1);          // table entries must stay alive (use static const)
 *   To explain an error: console_set_detail(out, "wheel_base_mm unset"); return NACK_INVALID_ARG;
 *
 * OUTPUT
 *   Handlers print with console_printf (uses "\n"; the UART writer turns it into "\r\n").
 *   The sink is console_out_t: later steps add a second sink that wraps text into LOG frames,
 *   so the same handlers serve the typed console and the framed console_line command.
 */
#ifndef SKYNOID_CONSOLE_H
#define SKYNOID_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_MAX_CMDS   48
#define CONSOLE_MAX_ARGS   8
#define CONSOLE_DETAIL_MAX 48

/* NACK codes, protocol-v0 section 7. A handler returns 0 or one of these. */
enum {
    CON_OK = 0, CON_UNKNOWN_MSG = 1, CON_BAD_LEN = 2, CON_NOT_ARMED = 3, CON_HAZARD_BLOCKED = 4,
    CON_ESTOPPED = 5, CON_INVALID_ARG = 6, CON_BUSY = 7
};

typedef struct console_out {
    void (*write)(const char *text, size_t len, void *ctx);   /* where the characters go */
    void *ctx;
    char  detail[CONSOLE_DETAIL_MAX];                          /* filled by console_set_detail */
} console_out_t;

typedef int (*console_fn_t)(int argc, char **argv, console_out_t *out);

typedef struct {
    const char  *name;
    const char  *help;    /* one line; for "help <name>" put the syntax first: "drive <v> <w> [ttl]  move" */
    console_fn_t fn;
    uint8_t      tier;    /* 0 read-only, always available; 1 actuation, needs maintenance mode */
} console_cmd_t;

/* Add commands. Returns how many were accepted (a duplicate name or a full table is refused). */
size_t console_register(const console_cmd_t *cmds, size_t count);
void   console_reset(void);                                   /* empty the table (tests) */

/* Tier 1 commands run only while this returns true. Default: never (step 1.6 installs the real check). */
void console_set_maint_check(bool (*is_active)(void));

int  console_printf(console_out_t *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void console_set_detail(console_out_t *out, const char *text);
const char *console_nack_name(int code);                      /* "unknown_msg" ... "busy" */

/* Run one line. `line` is modified (split in place). Prints the result lines and returns the
 * handler's code (0 for an empty line, CON_UNKNOWN_MSG for an unknown command). */
int console_execute(char *line, console_out_t *out);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_CONSOLE_H */
