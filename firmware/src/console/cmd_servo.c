/*
 * cmd_servo.c - the `servo` console command: bench control of the PCA9685 servo outputs.
 *
 *   servo | servo status       driver state, output-enable pin, one line per channel
 *   servo <ch> <us> [ttl_ms]   raw pulse width on one channel (600..2400 us, 0 = off); it ends by itself after
 *                              ttl_ms (default 1000, maximum 3000), then the channel is switched fully off
 *   servo off [ch]             switch one channel (or all) off
 *   servo test <ch>            the D20 pulse test: 1000, 1500 and 2000 us for 1.5 s each, then off (blocks 5 s)
 *   servo angle <ch> <deg> [ms]  aim a POSITIONAL servo: soft limits, minimum-jerk move, speed limit, power budget.
 *                              Only in a build with -DSKYNOID_SERVO_TYPE_CONFIRMED=1.
 *
 * Why: decision D20 says the servos on hand are probably continuous-rotation, which cannot be aimed. `servo test`
 * answers that in two minutes, and the raw pulse command is safe for either type because every pulse expires.
 *
 * Locked by default: in a normal build the command exists but answers "locked" and nothing touches the chip.
 * Build the trial firmware with -DSKYNOID_SERVO_TRIAL=1 (see platformio.ini env single_servo_trial).
 *
 * GATE: tier 1, so the command runs only after `maint on` (core/maint.c); `maint off`, the timeout, an e-stop and a
 * critical battery switch the outputs off. The build flag is a second lock.
 * Not a framed command: protocol ids 0x20..0x3F stay reserved for the later servo messages.
 *
 * Called by: console_start() -> console_register_servo().
 * Test    : on the board with a servo on channel 0 and the servo rail on. Not built or run when this was written.
 * Spec    : 10-specs/console-commands.md "Actuation and features", 04-firmware/03-upper-node.md, D20.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "console/console_uart.h"
#include "hal.h"
#include "skynoid_console.h"

#define SERVO_CMD_TIER 1                 /* tier 1: runs only while `maint on` is active (step 1.6b). The build flag is a second lock */
#define TEST_STEP_MS   1500u
#define TEST_PAUSE_MS  1700u

static bool parse_int(const char *s, int32_t *out)
{
    char *end = NULL;
    const long v = strtol(s, &end, 0);
    if (s == end || *end != '\0') { return false; }
    *out = (int32_t)v;
    return true;
}

static int fail(console_out_t *out, int rc)
{
    switch (rc) {
    case HAL_SERVO_LOCKED:
        console_set_detail(out, "locked: build with SERVO_TRIAL=1");
        return CON_BUSY;
    case HAL_SERVO_NOT_READY:
        console_set_detail(out, "no PCA9685 answering, see devices");
        return CON_BUSY;
    case HAL_SERVO_BAD_CH:
        console_set_detail(out, "channel out of range");
        return CON_INVALID_ARG;
    case HAL_SERVO_BAD_ARG:
        console_set_detail(out, "pulse 600..2400 us (0 off), ttl max 3000");
        return CON_INVALID_ARG;
    case HAL_SERVO_BUSY:
        console_set_detail(out, "power budget or start gap, retry");
        return CON_BUSY;
    case HAL_SERVO_WRONG_TYPE:
        console_set_detail(out, "angle needs SERVO_TYPE_CONFIRMED=1");
        return CON_INVALID_ARG;
    default:
        console_set_detail(out, "servo error");
        return CON_INVALID_ARG;
    }
}

static const char *state_text(uint8_t st)
{
    switch (st) {
    case HAL_SERVO_ST_LOCKED:    return "LOCKED (build without SERVO_TRIAL/SERVO_TYPE_CONFIRMED: the chip is never touched)";
    case HAL_SERVO_ST_NOT_READY: return "NOT READY (no PCA9685 answered at the address, or it failed: run devices)";
    default:                     return "ready";
    }
}

static int servo_status(console_out_t *out)
{
    const uint8_t st = hal_servo_state();
    console_printf(out, "servo driver %s\n", state_text(st));
    console_printf(out, "pca9685 address 0x%02X, outputs %s (GPIO4 OE %s), type %s\n", (unsigned)SKYNOID_PCA9685_ADDR,
                   hal_servo_outputs_enabled() ? "ENABLED" : "disabled",
                   hal_servo_outputs_enabled() ? "low" : "high",
                   SKYNOID_SERVO_TYPE_CONFIRMED ? "positional (confirmed by the builder)" : "unknown (raw pulses only)");
    if (st == HAL_SERVO_ST_LOCKED) { return CON_OK; }
    for (int ch = 0; ch < hal_servo_channels(); ch++) {
        hal_servo_ch_t c;
        if (!hal_servo_ch_info(ch, &c)) { continue; }
        switch (c.mode) {
        case HAL_SERVO_M_PULSE:
            console_printf(out, "ch%d pulse %d us, %u ms left\n", ch, (int)c.us, (unsigned)c.ttl_left_ms);
            break;
        case HAL_SERVO_M_MOVE:
        case HAL_SERVO_M_HOLD:
            console_printf(out, "ch%d angle %d.%d deg (%s) pulse %d us%s\n", ch, (int)(c.pos_x10 / 10),
                           (int)(c.pos_x10 % 10 < 0 ? -(c.pos_x10 % 10) : c.pos_x10 % 10),
                           c.mode == HAL_SERVO_M_MOVE ? "moving" : "holding", (int)c.us,
                           c.pos_known ? "" : " [start position is an assumption]");
            break;
        default:
            console_printf(out, "ch%d off\n", ch);
            break;
        }
    }
    return CON_OK;
}

static int servo_test(int argc, char **argv, console_out_t *out)
{
    int32_t ch;
    if (argc != 3 || !parse_int(argv[2], &ch)) {
        console_set_detail(out, "usage: servo test <ch>");
        return CON_INVALID_ARG;
    }
    static const int32_t STEPS[3] = { 1000, 1500, 2000 };
    for (int i = 0; i < 3; i++) {
        const int rc = hal_servo_pulse((int)ch, STEPS[i], TEST_STEP_MS);
        if (rc != HAL_SERVO_OK) { (void)hal_servo_off((int)ch); return fail(out, rc); }
        console_printf(out, "ch%d %d us for %u ms ...\n", (int)ch, (int)STEPS[i], (unsigned)TEST_STEP_MS);
        vTaskDelay(pdMS_TO_TICKS(TEST_PAUSE_MS));
    }
    (void)hal_servo_off((int)ch);
    console_printf(out, "done, channel off. What happened?\n");
    console_printf(out, "  CONTINUOUS rotation: 1000 spun one way, 1500 stopped (or crept), 2000 spun the other way.\n");
    console_printf(out, "  POSITIONAL: three different angles, the shaft held each one and never kept turning.\n");
    console_printf(out, "Record the answer (D20). Only for positional servos: rebuild with -DSKYNOID_SERVO_TYPE_CONFIRMED=1.\n");
    return CON_OK;
}

static int cmd_servo(int argc, char **argv, console_out_t *out)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0)) { return servo_status(out); }

    if (strcmp(argv[1], "off") == 0) {
        int32_t ch = -1;
        if (argc > 3 || (argc == 3 && !parse_int(argv[2], &ch))) {
            console_set_detail(out, "usage: servo off [ch]");
            return CON_INVALID_ARG;
        }
        const int rc = hal_servo_off((int)ch);
        if (rc != HAL_SERVO_OK) { return fail(out, rc); }
        console_printf(out, "%s off\n", argc == 3 ? "channel" : "all channels");
        return CON_OK;
    }

    if (strcmp(argv[1], "test") == 0) { return servo_test(argc, argv, out); }

    if (strcmp(argv[1], "angle") == 0) {
        int32_t ch, deg, ms = 0;
        if (argc < 4 || argc > 5 || !parse_int(argv[2], &ch) || !parse_int(argv[3], &deg) ||
            (argc == 5 && (!parse_int(argv[4], &ms) || ms < 0))) {
            console_set_detail(out, "usage: servo angle <ch> <deg> [ms]");
            return CON_INVALID_ARG;
        }
        bool clamped = false;
        const int rc = hal_servo_angle((int)ch, deg * 10, (uint32_t)ms, &clamped);
        if (rc != HAL_SERVO_OK) { return fail(out, rc); }
        console_printf(out, "ch%d moving to %d deg%s, speed limit %d deg/s\n", (int)ch, (int)deg,
                       clamped ? " (clamped by the soft limits)" : "", (int)SKYNOID_SERVO_VMAX_DPS);
        return CON_OK;
    }

    int32_t ch, us, ttl = 0;
    if (argc < 3 || argc > 4 || !parse_int(argv[1], &ch) || !parse_int(argv[2], &us) ||
        (argc == 4 && (!parse_int(argv[3], &ttl) || ttl < 0))) {
        console_set_detail(out, "usage: servo <ch> <us> [ttl_ms] | off | test | angle | status");
        return CON_INVALID_ARG;
    }
    const int rc = hal_servo_pulse((int)ch, us, (uint32_t)ttl);
    if (rc != HAL_SERVO_OK) { return fail(out, rc); }
    if (us == 0) { console_printf(out, "ch%d off\n", (int)ch); }
    else         { console_printf(out, "ch%d %d us, ends by itself after %u ms\n", (int)ch, (int)us,
                                  (unsigned)(ttl > 0 ? ttl : SKYNOID_SERVO_TTL_DEFAULT_MS)); }
    return CON_OK;
}

static const console_cmd_t SERVO_CMDS[] = {
    {"servo", "servo <ch> <us> [ttl] | off [ch] | test <ch> | angle <ch> <deg> [ms] | status  (locked unless built with SERVO_TRIAL=1)",
     cmd_servo, SERVO_CMD_TIER},
};

void console_register_servo(void)
{
    console_register(SERVO_CMDS, sizeof SERVO_CMDS / sizeof SERVO_CMDS[0]);
}
