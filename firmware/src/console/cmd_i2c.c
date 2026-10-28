/*
 * cmd_i2c.c - the `i2c` console command: scan and probe (tier 0, read-only).
 *
 *   i2c scan            every address 0x08..0x77 on bus 0 that answers, with a guess of what it is
 *   i2c probe <addr>    ACK or no ACK at one address (decimal or 0x hex)
 *
 * Why: before trusting the INA219, the VL53L0X or the PCA9685, see what really answers on the bus.
 * A scan lists each address once, so two devices on the SAME address look like one line (the two OLEDs
 * at 0x3C, or an INA219 and a PCA9685 both at 0x40). Compare the list with devices.yaml.
 *
 * Not here yet (console-commands.md): `i2c speed` (the bus clock is a private constant of hal_sense.c)
 * and the tier 1 commands (remap, read, write, recover, setaddr), and `devices` / `selftest` (step 1.5b part 2).
 *
 * Called by: console_start() -> console_register_i2c().
 * Test    : on the board. The only logic is a loop and a table lookup (skr_addr_guess, tested in test_registry).
 * Spec    : 10-specs/console-commands.md "Devices and the I2C bus"; guesses from devices.yaml address_guess.
 *
 * "no ACK" is also what hal_i2c_probe() returns when the bus was never initialised or another task held
 * the bus lock for over 50 ms; it cannot tell these apart.
 */
#include <stdint.h>
#include <string.h>

#include "esp_timer.h"

#include "board/board_pins.h"
#include "console/console_uart.h"
#include "hal.h"
#include "skynoid_console.h"
#include "skynoid_registry.h"
#include "skynoid_text.h"

#define I2C_ADDR_FIRST 0x08      /* 0x00..0x07 and 0x78..0x7F are reserved by the I2C specification */
#define I2C_ADDR_LAST  0x77
#define SCAN_MAX_US    400000    /* this runs in the console task, which also ends a link session after 500 ms
                                  * of silence; a stuck bus (probe timeouts of 50 ms each) must not hold it longer */

static int i2c_scan(console_out_t *out)
{
    const int64_t start_us = esp_timer_get_time();
    int found = 0;
    console_printf(out, "bus0 SDA GPIO%d SCL GPIO%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    for (int a = I2C_ADDR_FIRST; a <= I2C_ADDR_LAST; a++) {
        if (esp_timer_get_time() - start_us > SCAN_MAX_US) {
            console_printf(out, "scan stopped at 0x%02X, %d found so far\n", (unsigned)a, found);
            console_set_detail(out, "scan too slow, bus stuck?");
            return CON_BUSY;
        }
        if (!hal_i2c_probe((uint8_t)a)) { continue; }
        const char *guess = skr_addr_guess((uint8_t)a);
        console_printf(out, "0x%02X  %s\n", (unsigned)a, guess != NULL ? guess : "(no guess)");
        found++;
    }
    console_printf(out, "%d device(s)\n", found);
    return CON_OK;
}

static int i2c_probe(int argc, char **argv, console_out_t *out)
{
    int32_t addr;
    if (argc != 3 || !skt_parse_i32(argv[2], &addr) || addr < I2C_ADDR_FIRST || addr > I2C_ADDR_LAST) {
        console_set_detail(out, "usage: i2c probe 0x08..0x77");
        return CON_INVALID_ARG;
    }
    console_printf(out, "0x%02X %s\n", (unsigned)addr, hal_i2c_probe((uint8_t)addr) ? "ACK" : "no ACK");
    return CON_OK;
}

static int i2c_read(int argc, char **argv, console_out_t *out)
{
    int32_t addr, reg, n = 1;
    if (argc < 4 || argc > 5 ||
        !skt_parse_i32(argv[2], &addr) || addr < I2C_ADDR_FIRST || addr > I2C_ADDR_LAST ||
        !skt_parse_i32(argv[3], &reg) || reg < 0 || reg > 255) {
        console_set_detail(out, "usage: i2c read <addr> <reg> [1..16]");
        return CON_INVALID_ARG;
    }
    if (argc == 5) {
        if (!skt_parse_i32(argv[4], &n) || n < 1 || n > 16) {
            console_set_detail(out, "n must be 1..16");
            return CON_INVALID_ARG;
        }
    }
    uint8_t buf[16];
    if (!hal_i2c_read_reg((uint8_t)addr, (uint8_t)reg, buf, (size_t)n)) {
        console_set_detail(out, "read failed or no ACK");
        return CON_BUSY;
    }
    if (n == 1) {
        console_printf(out, "0x%02X\n", (unsigned)buf[0]);
    } else if (n == 2) {
        const unsigned w = (unsigned)((buf[0] << 8) | buf[1]);
        console_printf(out, "0x%02X 0x%02X (0x%04X)\n", (unsigned)buf[0], (unsigned)buf[1], w);
    } else {
        for (int i = 0; i < n; i++) {
            console_printf(out, "%s0x%02X", i > 0 ? " " : "", (unsigned)buf[i]);
        }
        console_printf(out, "\n");
    }
    return CON_OK;
}

static int cmd_i2c(int argc, char **argv, console_out_t *out)
{
    if (argc == 2 && strcmp(argv[1], "scan") == 0) { return i2c_scan(out); }
    if (argc >= 2 && strcmp(argv[1], "probe") == 0) { return i2c_probe(argc, argv, out); }
    if (argc >= 2 && strcmp(argv[1], "read") == 0) { return i2c_read(argc, argv, out); }
    console_set_detail(out, "usage: i2c scan | i2c probe <addr> | i2c read <addr> <reg> [n]");
    return CON_INVALID_ARG;
}

static const console_cmd_t I2C_CMDS[] = {
    {"i2c", "i2c scan | probe <addr> | read <addr> <reg> [n]  I2C bus operations", cmd_i2c, 0},
};

void console_register_i2c(void)
{
    console_register(I2C_CMDS, 1);
}
