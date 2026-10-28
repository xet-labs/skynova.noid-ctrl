/*
 * device_probe.c - fill a skr_probe_t from the real board, then evaluate it (role: single).
 *
 * Purpose : give `devices` and `selftest` honest inputs. I2C devices are found by a scan and then checked by
 *           reading an identity register, so an INA219 and a PCA9685 that share 0x40/0x41 are told apart by
 *           what they answer, not by a guess:
 *             ina219   register 0x00 reads 0x399F (power-on configuration; this firmware never writes it)
 *             pca9685  MODE1 (register 0x00) has bit 0 (ALL CALL) set, and the 16-bit value is not 0x399F
 *             vl53l0x  register 0xC0 reads 0xEE (model id)
 *             oled     no readable id: an ACK is accepted and the row says so
 *           ADC rows reuse the values the safety task already measured (base_get_status): no second reader
 *           on the ADC unit. They are a SINGLE sample, not the 1 s / 5 s rules of the spec; the row text says so.
 * Called by: console/cmd_devices.c.
 * Test    : on the board only (hardware). The status rules are tested in test_registry.
 * Spec    : 10-specs/devices.yaml, console-commands.md "The devices table".
 *
 * Honest limits: touch inputs, NVS and the servo current input have no firmware yet, so they are reported as
 * disabled / not fitted instead of being guessed. Outputs (motor drivers, buzzer, LED) stay "unverified".
 */
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "app_config.h"
#include "base.h"
#include "board/board_pins.h"
#include "core/device_probe.h"
#include "hal.h"

#define I2C_ADDR_FIRST 0x08
#define I2C_ADDR_LAST  0x77
#define SCAN_MAX_US    400000
#define INA219_CONFIG_POWER_ON 0x399F
#define VL53L0X_MODEL_ID       0xEE
#define ADC_RAIL_HIGH 4090
#define ADC_RAIL_LOW  5

static skr_probe_t  s_probe;
static skr_report_t s_report;
static char         s_notes[SKR_MAX_ENTRIES][44];
static char         s_ident[128][20];            /* identity text per address, "" = nothing read */

const char *device_probe_ident(uint8_t addr)
{
    return (addr < 128u && s_ident[addr][0] != '\0') ? s_ident[addr] : NULL;
}

static void set_note(size_t i, const char *fmt, int a)
{
    (void)snprintf(s_notes[i], sizeof s_notes[i], fmt, a);
    s_probe.in[i].note = s_notes[i];
}

/* Identity check of the I2C entry `name` at its address. Returns 1 passed, 0 failed. */
static int ident_check(const char *name, uint8_t addr, size_t i)
{
    uint8_t b[2] = { 0, 0 };
    if (strcmp(name, "ina219") == 0) {
        if (!hal_i2c_read_reg(addr, 0x00, b, 2)) {
            (void)snprintf(s_ident[addr], sizeof s_ident[addr], "reg0 read failed");
            return 0;
        }
        const unsigned w = (unsigned)((b[0] << 8) | b[1]);
        (void)snprintf(s_ident[addr], sizeof s_ident[addr], "reg0=0x%04X", w);
        set_note(i, "config reg 0x%04X", (int)w);
        return w == INA219_CONFIG_POWER_ON;
    }
    if (strcmp(name, "pca9685") == 0) {
        if (!hal_i2c_read_reg(addr, 0x00, b, 2)) {
            (void)snprintf(s_ident[addr], sizeof s_ident[addr], "reg0 read failed");
            return 0;
        }
        const unsigned w = (unsigned)((b[0] << 8) | b[1]);
        (void)snprintf(s_ident[addr], sizeof s_ident[addr], "reg0=0x%04X", w);
        set_note(i, "MODE1 0x%02X", (int)b[0]);
        return ((b[0] & 0x01u) != 0u) && (w != INA219_CONFIG_POWER_ON);
    }
    if (strcmp(name, "vl53l0x") == 0) {
        if (!hal_i2c_read_reg(addr, 0xC0, b, 1)) {
            (void)snprintf(s_ident[addr], sizeof s_ident[addr], "reg0xC0 read failed");
            return 0;
        }
        (void)snprintf(s_ident[addr], sizeof s_ident[addr], "reg0xC0=0x%02X", (unsigned)b[0]);
        set_note(i, "model id 0x%02X", (int)b[0]);
        return b[0] == VL53L0X_MODEL_ID;
    }
    /* oled_left, oled_right: no readable id */
    (void)snprintf(s_notes[i], sizeof s_notes[i], "ACK, no id register to check");
    s_probe.in[i].note = s_notes[i];
    return 1;
}

static void fill_adc(size_t i, uint16_t raw, const char *what)
{
    s_probe.in[i].value = raw;
    if (raw >= ADC_RAIL_HIGH || raw <= ADC_RAIL_LOW) { s_probe.in[i].flags |= SKR_F_SATURATED; }
    (void)snprintf(s_notes[i], sizeof s_notes[i], "%s %u, one sample", what, (unsigned)raw);
    s_probe.in[i].note = s_notes[i];
}

const skr_report_t *device_probe_run(void)
{
    memset(&s_probe, 0, sizeof s_probe);
    memset(s_ident, 0, sizeof s_ident);
    memset(s_notes, 0, sizeof s_notes);
    for (size_t i = 0; i < SKR_MAX_ENTRIES; i++) { s_probe.in[i].check = -1; }

    /* 1. scan */
    const int64_t start_us = esp_timer_get_time();
    for (int a = I2C_ADDR_FIRST; a <= I2C_ADDR_LAST; a++) {
        if (esp_timer_get_time() - start_us > SCAN_MAX_US) { return NULL; }
        s_probe.present[a] = hal_i2c_probe((uint8_t)a);
    }

    base_status_t st;
    base_get_status(&st);

    /* 2. per entry: identity check, ADC value, or known-not-built flags */
    for (size_t i = 0; i < SKR_MANIFEST_LEN; i++) {
        const skr_dev_t *d = &SKR_MANIFEST[i];
        if (d->need[SKR_ROLE_SINGLE] == SKR_NEED_NO) { continue; }

        if (d->kind == SKR_I2C) {
            if (strcmp(d->name, "ina219") == 0) { s_probe.addr_override[i] = (uint8_t)SKYNOID_INA219_ADDR; }
            const uint8_t addr = (s_probe.addr_override[i] != 0u) ? s_probe.addr_override[i] : d->addr[SKR_ROLE_SINGLE];
            if (addr < 128u && s_probe.present[addr]) { s_probe.in[i].check = (int8_t)ident_check(d->name, addr, i); }
            continue;
        }

        const char *n = d->name;
        if (strcmp(n, "ir_front_left") == 0)       { fill_adc(i, st.ir_raw[0], "raw"); }
        else if (strcmp(n, "ir_front_right") == 0) { fill_adc(i, st.ir_raw[1], "raw"); }
        else if (strcmp(n, "ir_rear_left") == 0)   { fill_adc(i, st.ir_raw[2], "raw"); }
        else if (strcmp(n, "ir_rear_right") == 0)  { fill_adc(i, st.ir_raw[3], "raw"); }
        else if (strcmp(n, "motor_current_is") == 0) {
            if (SKYNOID_IS_MV_PER_AMP == SKL_UNSET) { s_probe.in[i].flags |= SKR_F_NOT_FITTED; }   /* unset = not used */
            else { set_note(i, "motor %d mA", (int)st.motor_ma); }
        }
        else if (strcmp(n, "servo_current_acs712") == 0) { s_probe.in[i].flags |= SKR_F_NOT_FITTED; } /* GPIO32 has no reader yet */
        else if (strncmp(n, "touch_", 6) == 0)    { s_probe.in[i].flags |= SKR_F_DISABLED; }        /* no touch driver yet */
        else if (strcmp(n, "link_uart0") == 0) {
            if (UART_LINK_NUM != 0) { s_probe.in[i].flags |= SKR_F_DISABLED; }
            else { set_note(i, "link on UART0, %d baud", UART_LINK_BAUD); }
        }
        else if (strcmp(n, "console_uart2") == 0) {
            if (UART_CONSOLE_NUM != 2 && UART_LINK_NUM != 2) { s_probe.in[i].flags |= SKR_F_DISABLED; }
            else { set_note(i, "UART2 in use, GPIO%d/GPIO15", PIN_UART2_TX); }
        }
        else if (strcmp(n, "nvs_flash") == 0)     { s_probe.in[i].flags |= SKR_F_DISABLED; }        /* params are RAM only for now */
        /* outputs (bts7960, buzzer, status_led, servos_8x): no flags, they stay "unverified" */
    }

    skr_evaluate(SKR_ROLE_SINGLE, &s_probe, &s_report);
    return &s_report;
}
