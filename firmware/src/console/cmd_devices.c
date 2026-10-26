/*
 * cmd_devices.c - the `devices` and `selftest` console commands (tier 0, read-only).
 *
 *   devices       the device table of console-commands.md: one row per device of this role, plus one row
 *                 per I2C address that answered but belongs to nobody
 *   devices -v    same, then the identity register values that were read (for example reg0=0x399F)
 *   selftest      probes once, prints the table, then PASS or FAIL <n>
 *
 * Why: settles which of 0x40 and 0x41 is the INA219 and which the PCA9685 by what they answer, and shows
 * missing or faulty sensors in one place. Read-only: it scans and reads registers, it moves nothing.
 *
 * Called by: console_start() -> console_register_devices().
 * Test    : on the board. The table rules and text are tested on the PC in test_registry.
 * Spec    : 10-specs/console-commands.md "Devices and the I2C bus", "The devices table".
 *
 * Not here yet: `devices -v` columns for probe and error counts and the driver origin (no such counters
 * exist), and the Pi-side summary in the status stream.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "core/device_probe.h"
#include "console/console_uart.h"
#include "skynoid_console.h"
#include "skynoid_registry.h"

#define LINE_MAX_CHARS 160

static void print_table(console_out_t *out, const skr_report_t *rep)
{
    char line[LINE_MAX_CHARS];
    (void)skr_format_header(line, sizeof line);
    console_printf(out, "%s\n", line);
    for (size_t i = 0; i < rep->n; i++) {
        (void)skr_format_row(&rep->rows[i], line, sizeof line);
        console_printf(out, "%s\n", line);
    }
    (void)skr_format_summary(rep, line, sizeof line);
    console_printf(out, "%s\n", line);
}

static void print_ident(console_out_t *out)
{
    bool any = false;
    for (int a = 0x08; a <= 0x77; a++) {
        const char *t = device_probe_ident((uint8_t)a);
        if (t == NULL) { continue; }
        if (!any) { console_printf(out, "identity reads:\n"); any = true; }
        console_printf(out, "  0x%02X  %s\n", (unsigned)a, t);
    }
    if (!any) { console_printf(out, "identity reads: none (nothing answered at a manifest address)\n"); }
}

static int cmd_devices(int argc, char **argv, console_out_t *out)
{
    const bool verbose = (argc == 2 && strcmp(argv[1], "-v") == 0);
    if (argc > 2 || (argc == 2 && !verbose)) {
        console_set_detail(out, "usage: devices [-v]");
        return CON_INVALID_ARG;
    }
    const skr_report_t *rep = device_probe_run();
    if (rep == NULL) {
        console_set_detail(out, "scan too slow, bus stuck?");
        return CON_BUSY;
    }
    print_table(out, rep);
    if (verbose) { print_ident(out); }
    return CON_OK;
}

static int cmd_selftest(int argc, char **argv, console_out_t *out)
{
    (void)argv;
    if (argc != 1) {
        console_set_detail(out, "usage: selftest");
        return CON_INVALID_ARG;
    }
    const skr_report_t *rep = device_probe_run();
    if (rep == NULL) {
        console_set_detail(out, "scan too slow, bus stuck?");
        return CON_BUSY;
    }
    print_table(out, rep);
    const uint8_t fails = skr_fail_count(rep);
    if (fails == 0u) { console_printf(out, "PASS\n"); }
    else             { console_printf(out, "FAIL %u\n", (unsigned)fails); }
    return CON_OK;
}

static const console_cmd_t DEVICES_CMDS[] = {
    {"devices",  "devices [-v]  what is attached, missing or unexpected (I2C identity checks, ADC rows)", cmd_devices, 0},
    {"selftest", "selftest  probe everything once, print the devices table, then PASS or FAIL <n>", cmd_selftest, 0},
};

void console_register_devices(void)
{
    console_register(DEVICES_CMDS, 2);
}
