/*
 * cmd_log.c - the `log` console command: level, dump, stats.
 *
 *   log                 show the current level
 *   log level <0..3>    what is printed: 0 error, 1 warn, 2 info, 3 debug (the recorder keeps all)
 *   log dump            print the flight recorder (also what was logged before the last reset)
 *   log stats           lines dropped because the queue was full
 *
 * Tier 0 (changes only the print filter, never an actuator).
 * Test: on the board. Spec: console-commands.md, tier 0 ("log level").
 */
#include <stdint.h>
#include <string.h>

#include "console/console_uart.h"
#include "core/logger.h"
#include "skynoid_console.h"
#include "skynoid_text.h"

#define DUMP_MAX 2048

static int cmd_log(int argc, char **argv, console_out_t *out)
{
    if (argc == 1) {
        console_printf(out, "log level %d\n", logger_get_level());
        return CON_OK;
    }
    if (strcmp(argv[1], "level") == 0) {
        int32_t v;
        if (argc == 2) { console_printf(out, "log level %d\n", logger_get_level()); return CON_OK; }
        if (argc != 3 || !skt_parse_i32(argv[2], &v) || v < 0 || v > 3) {
            console_set_detail(out, "usage: log level 0..3");
            return CON_INVALID_ARG;
        }
        logger_set_level((int)v);
        return CON_OK;
    }
    if (strcmp(argv[1], "dump") == 0) {
        static uint8_t buf[DUMP_MAX];                       /* static: the console task stack is small */
        const size_t n = logger_recorder_read(buf, sizeof buf);
        if (n == 0) { console_printf(out, "recorder empty\n"); return CON_OK; }
        for (size_t i = 0; i < n; i += 128) {
            const size_t chunk = (n - i < 128) ? n - i : 128;
            out->write((const char *)buf + i, chunk, out->ctx);
        }
        return CON_OK;
    }
    if (strcmp(argv[1], "stats") == 0) {
        console_printf(out, "dropped %u\n", (unsigned)logger_dropped());
        return CON_OK;
    }
    console_set_detail(out, "usage: log [level n | dump | stats]");
    return CON_INVALID_ARG;
}

static const console_cmd_t LOG_CMDS[] = {
    {"log", "log [level 0..3 | dump | stats]  print filter, flight recorder, drops", cmd_log, 0},
};

void console_register_log(void)
{
    console_register(LOG_CMDS, 1);
}
