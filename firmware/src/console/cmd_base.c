/*
 * cmd_base.c - read-only console commands that show what the base node senses: status, batt, ir.
 *
 *   status   state, hazard mask with names, battery, IR raw values, motor current, range
 *   batt     battery voltage: a direct INA219 read, the 1 s average the safety logic uses, thresholds,
 *            and a one-line verdict (sensing or not, and why not)
 *   ir       the four cliff sensors as raw 12-bit ADC counts
 *
 * Why: `doctor` shows these over the framed link, but a person at the terminal had no way to see them
 * (`status` was a placeholder). `batt` also answers "is the voltage really being sensed?" without the
 * Python tools: it separates "the INA219 does not answer" (address or wiring) from "it answers but reads
 * 0 mV" (VIN+, VIN- or GND wiring) from "reading fine, the average is not ready yet".
 *
 * All three are tier 0: they change nothing. They call base_get_status() (the same function TLM uses) and
 * hal_ina219_read_mv() (takes the I2C lock for one short transfer, like `i2c probe`).
 *
 * Called by: console_start() -> console_register_base().
 * Test    : on the board. Expect `batt` within about 0.1 V of a multimeter on the battery.
 *           Not built with pio and not run when this was written (2026-10-26, claude-chat_a7d3).
 * Spec    : 10-specs/console-commands.md tier 0 (status); `batt` and `ir` are an addition, see the task
 *           file skynoid-console-base-cmds.md.
 */
#include <stdint.h>

#include "app_config.h"
#include "base.h"
#include "console/console_uart.h"
#include "hal.h"
#include "skynoid_console.h"
#include "skynoid_logic.h"
#include "skynoid_params.h"

static const char *state_name(uint8_t s)
{
    switch (s) {
    case ST_BOOT:  return "boot";
    case ST_SAFE:  return "safe";
    case ST_ARMED: return "armed";
    case ST_ESTOP: return "estop";
    case ST_FAULT: return "fault";
    case ST_MAINT: return "maint";
    default:       return "?";
    }
}

/* Print the names of the set hazard bits, or "none". */
static void print_hazard(console_out_t *out, uint8_t mask)
{
    static const struct { uint8_t bit; const char *name; } HZ[] = {
        { SKL_HZ_FRONT_LEFT,   "front_left" },  { SKL_HZ_FRONT_RIGHT,  "front_right" },
        { SKL_HZ_REAR_LEFT,    "rear_left" },   { SKL_HZ_REAR_RIGHT,   "rear_right" },
        { SKL_HZ_SENSOR_FAULT, "sensor_fault" }, { SKL_HZ_UNRELIABLE,  "unreliable(no floor baseline)" },
    };
    console_printf(out, "hazard 0x%02X ", (unsigned)mask);
    if (mask == 0) { console_printf(out, "none\n"); return; }
    for (size_t i = 0; i < sizeof HZ / sizeof HZ[0]; i++) {
        if (mask & HZ[i].bit) { console_printf(out, "%s ", HZ[i].name); }
    }
    console_printf(out, "\n");
}

static void print_volts(console_out_t *out, int32_t mv)
{
    console_printf(out, "%d mV (%d.%02d V)", (int)mv, (int)(mv / 1000), (int)((mv % 1000) / 10));
}

static void print_ir(console_out_t *out, const base_status_t *st)
{
    console_printf(out, "ir raw  front_left %u  front_right %u  rear_left %u  rear_right %u\n",
                   (unsigned)st->ir_raw[0], (unsigned)st->ir_raw[1],
                   (unsigned)st->ir_raw[2], (unsigned)st->ir_raw[3]);
}

static int cmd_status(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    base_status_t st;
    base_get_status(&st);
    console_printf(out, "state %s\n", state_name(st.state));
    print_hazard(out, st.hazard);
    if (st.batt_mv == 0) {
        console_printf(out, "battery NO READING (run `batt`)\n");
    } else {
        console_printf(out, "battery ");
        print_volts(out, st.batt_mv);
        console_printf(out, "\n");
    }
    print_ir(out, &st);
    if (st.motor_ma == 0) {
        console_printf(out, "motor current 0 mA (also 0 while SKYNOID_IS_MV_PER_AMP is not calibrated)\n");
    } else {
        console_printf(out, "motor current %u mA\n", (unsigned)st.motor_ma);
    }
    console_printf(out, "range %u mm status %u (2 = no sensor driver yet)\n",
                   (unsigned)st.range_mm, (unsigned)st.range_status);
    return CON_OK;
}

static void print_threshold(console_out_t *out, const char *label, uint8_t id)
{
    const int32_t v = base_param_get(id);
    if (v == SKL_UNSET) {
        console_printf(out, "%s default (see power.c)\n", label);
    } else {
        console_printf(out, "%s %d mV (set_param)\n", label, (int)v);
    }
}

static int cmd_batt(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    base_status_t st;
    base_get_status(&st);

    int32_t raw_mv = 0;
    const bool answers = hal_ina219_read_mv(&raw_mv);
    console_printf(out, "ina219 at 0x%02X: %s\n", (unsigned)SKYNOID_INA219_ADDR,
                   answers ? "register read ok" : "NO ANSWER");
    if (answers) {
        console_printf(out, "direct read   ");
        print_volts(out, raw_mv);
        console_printf(out, "\n");
    }
    console_printf(out, "1 s average   ");
    if (st.batt_mv == 0) { console_printf(out, "none yet\n"); }
    else { print_volts(out, st.batt_mv); console_printf(out, "\n"); }
    print_threshold(out, "low  threshold", SKP_BATT_LOW_MV);
    print_threshold(out, "crit threshold", SKP_BATT_CRIT_MV);

    if (!answers) {
        console_printf(out, "verdict NOT SENSING: the INA219 does not answer at 0x%02X. Check the address "
                            "(`i2c scan`; override with -DSKYNOID_INA219_ADDR), SDA/SCL and VCC.\n",
                       (unsigned)SKYNOID_INA219_ADDR);
    } else if (raw_mv <= 0) {
        console_printf(out, "verdict NOT SENSING: it answers but reads 0 mV. Check battery + on VIN+, "
                            "VIN- on the load side, and GND shared with the ESP32.\n");
    } else if (st.batt_mv == 0) {
        console_printf(out, "verdict reading ok; the average is not ready (needs 1 s after boot), run again\n");
    } else {
        console_printf(out, "verdict SENSING. Compare with a multimeter; the safety logic uses the average.\n");
    }
    return CON_OK;
}

static int cmd_ir(int argc, char **argv, console_out_t *out)
{
    (void)argc; (void)argv;
    base_status_t st;
    base_get_status(&st);
    print_ir(out, &st);
    return CON_OK;
}

static const console_cmd_t BASE_CMDS[] = {
    {"status", "state, hazards, battery, IR, motor current, range",           cmd_status, 0},
    {"batt",   "battery voltage: INA219 read, average, thresholds, verdict",  cmd_batt,   0},
    {"ir",     "the four cliff sensors, raw ADC counts",                      cmd_ir,     0},
};

void console_register_base(void)
{
    console_register(BASE_CMDS, sizeof BASE_CMDS / sizeof BASE_CMDS[0]);
}
