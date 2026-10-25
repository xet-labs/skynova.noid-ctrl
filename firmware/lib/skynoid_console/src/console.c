/*
 * console.c - command table, built-in help, and the engine that runs one line.
 *
 * Purpose : turn a finished text line into "run this function, print OK or ERR".
 * Called by: firmware/src/console/console_uart.c (typed lines) and, later, the framed
 *            console_line command. Modules call console_register at start-up.
 * Test    : firmware/test/test_console/test_console.c
 * Spec    : vault 10-specs/console-commands.md ("Syntax", "Output rules").
 */
#include "skynoid_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "skynoid_text.h"

static const console_cmd_t *s_cmds[CONSOLE_MAX_CMDS];
static size_t s_count;
static bool (*s_maint_active)(void);

/* ---- table ---------------------------------------------------------------- */
static const console_cmd_t *find(const char *name)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_cmds[i]->name, name) == 0) { return s_cmds[i]; }
    }
    return NULL;
}

size_t console_register(const console_cmd_t *cmds, size_t count)
{
    size_t accepted = 0;
    for (size_t i = 0; i < count; i++) {
        if (s_count >= CONSOLE_MAX_CMDS) { break; }                    /* table full */
        if (strcmp(cmds[i].name, "help") == 0 || find(cmds[i].name) != NULL) { continue; }   /* names are unique */
        s_cmds[s_count++] = &cmds[i];
        accepted++;
    }
    return accepted;
}

void console_reset(void)
{
    s_count = 0;
    s_maint_active = NULL;
}

void console_set_maint_check(bool (*is_active)(void))
{
    s_maint_active = is_active;
}

/* ---- output --------------------------------------------------------------- */
int console_printf(console_out_t *out, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) { return n; }
    if ((size_t)n >= sizeof buf) { n = (int)sizeof buf - 1; }          /* long text is cut, never overflows */
    if (out != NULL && out->write != NULL) { out->write(buf, (size_t)n, out->ctx); }
    return n;
}

void console_set_detail(console_out_t *out, const char *text)
{
    strncpy(out->detail, text, CONSOLE_DETAIL_MAX - 1);
    out->detail[CONSOLE_DETAIL_MAX - 1] = '\0';
}

const char *console_nack_name(int code)
{
    static const char *const names[] = {"ok", "unknown_msg", "bad_len", "not_armed", "hazard_blocked",
                                        "estopped", "invalid_arg", "busy"};
    return (code >= 0 && code <= 7) ? names[code] : "invalid_arg";
}

/* ---- built-in help ------------------------------------------------------------ */
static int cmd_help(int argc, char **argv, console_out_t *out)
{
    if (argc >= 2) {
        const console_cmd_t *c = find(argv[1]);
        if (c == NULL) { console_set_detail(out, "no such command"); return CON_INVALID_ARG; }
        console_printf(out, "%s  %s%s\n", c->name, c->help, c->tier ? "  [needs maint on]" : "");
        return 0;
    }
    console_printf(out, "%-10s list commands, or help <name>\n", "help");
    for (size_t i = 0; i < s_count; i++) {
        console_printf(out, "%-10s %s%s\n", s_cmds[i]->name, s_cmds[i]->help, s_cmds[i]->tier ? "  [maint]" : "");
    }
    return 0;
}

/* ---- the engine ----------------------------------------------------------------- */
int console_execute(char *line, console_out_t *out)
{
    char *argv[CONSOLE_MAX_ARGS];
    out->detail[0] = '\0';
    const int argc = skt_tokenize(line, argv, CONSOLE_MAX_ARGS);
    if (argc == 0) { return 0; }                                       /* empty line: just a new prompt */

    int rc;
    if (argc < 0) {
        console_set_detail(out, "too many arguments");
        rc = CON_INVALID_ARG;
    } else if (strcmp(argv[0], "help") == 0) {
        rc = cmd_help(argc, argv, out);
    } else {
        const console_cmd_t *c = find(argv[0]);
        if (c == NULL) {
            rc = CON_UNKNOWN_MSG;
        } else if (c->tier >= 1 && !(s_maint_active != NULL && s_maint_active())) {
            console_set_detail(out, "maintenance required");
            rc = CON_BUSY;
        } else {
            rc = c->fn(argc, argv, out);
        }
    }

    if (rc == 0) {
        console_printf(out, "OK\n");
    } else if (out->detail[0] != '\0') {
        console_printf(out, "ERR %s %s\n", console_nack_name(rc), out->detail);
    } else {
        console_printf(out, "ERR %s\n", console_nack_name(rc));
    }
    return rc;
}
