/*
 * registry.c - device manifest, status evaluation and the `devices` table text.
 *
 * Purpose : turn "what the board measured" (skr_probe_t) into one status row per device of the running
 *           role, exactly as console-commands.md describes, and print the table.
 * Called by: board glue src/core/registry.c (step 1.5b part 2, not written yet), doctor via `selftest`.
 * Test    : firmware/test/test_registry
 * Spec    : 10-specs/devices.yaml (the manifest below is a hand copy: keep both in step), console-commands.md.
 *
 * Rules kept here:
 *   - I2C: the status comes from skl_i2c_status(). Before asking about ALTERNATIVE addresses, an address that
 *     is the main address of ANOTHER listed device whose identity check passed is hidden, so a PCA9685
 *     sitting at 0x40 is never taken for an INA219 that "moved" there.
 *   - Two entries with the same main address (the two OLEDs, 0x3C) both report ok; the detail says so.
 *   - An answering address that no listed device claims gets its own row, status unexpected.
 */
#include <stdio.h>
#include <string.h>

#include "skynoid_registry.h"

#define NO3 {0, 0, 0}
#define NOALT {0, 0, 0, 0}

/* name, kind, kind_text, where, need{single,base,upper}, addr{single,base,upper}, alt[4], n_alt */
const skr_dev_t SKR_MANIFEST[] = {
    {"vl53l0x",         SKR_I2C,     "i2c",   NULL, {2, 2, 0}, {0x29, 0x29, 0x00}, NOALT, 0},
    {"ina219",          SKR_I2C,     "i2c",   NULL, {2, 2, 0}, {0x41, 0x40, 0x00}, {0x40, 0x41, 0x44, 0x45}, 4},
    {"pca9685",         SKR_I2C,     "i2c",   NULL, {1, 0, 1}, {0x40, 0x40, 0x40}, {0x41, 0x42, 0x43, 0x44}, 4},
    {"oled_left",       SKR_I2C,     "i2c",   NULL, {2, 0, 2}, {0x3C, 0x3C, 0x3C}, {0x3D, 0, 0, 0}, 1},
    {"oled_right",      SKR_I2C,     "i2c",   NULL, {1, 0, 1}, {0x3C, 0x3C, 0x3C}, {0x3D, 0, 0, 0}, 1},
    {"ir_front_left",   SKR_ADC,     "adc",   "GPIO36", {2, 2, 0}, NO3, NOALT, 0},
    {"ir_front_right",  SKR_ADC,     "adc",   "GPIO39", {2, 2, 0}, NO3, NOALT, 0},
    {"ir_rear_left",    SKR_ADC,     "adc",   "GPIO34", {2, 2, 0}, NO3, NOALT, 0},
    {"ir_rear_right",   SKR_ADC,     "adc",   "GPIO35", {2, 2, 0}, NO3, NOALT, 0},
    {"motor_current_is", SKR_ADC,    "adc",   "GPIO33", {1, 1, 0}, NO3, NOALT, 0},
    {"servo_current_acs712", SKR_ADC, "adc",   "GPIO32", {1, 0, 1}, NO3, NOALT, 0},
    {"bts7960_left",    SKR_OUTPUT,  "pwm",   "GPIO25/26 EN14", {2, 2, 0}, NO3, NOALT, 0},
    {"bts7960_right",   SKR_OUTPUT,  "pwm",   "GPIO27/16 EN14", {2, 2, 0}, NO3, NOALT, 0},
    {"buzzer",          SKR_OUTPUT,  "out",   "GPIO23", {2, 2, 0}, NO3, NOALT, 0},
    {"status_led",      SKR_OUTPUT,  "out",   "GPIO2",  {2, 2, 2}, NO3, NOALT, 0},
    {"touch_head",      SKR_GPIO_IN, "in",    "GPIO13", {2, 0, 2}, NO3, NOALT, 0},
    {"touch_hand_l",    SKR_GPIO_IN, "in",    "GPIO18", {1, 0, 1}, NO3, NOALT, 0},
    {"touch_hand_r",    SKR_GPIO_IN, "in",    "GPIO19", {1, 0, 1}, NO3, NOALT, 0},
    {"touch_extra",     SKR_GPIO_IN, "in",    "GPIO17", {1, 0, 1}, NO3, NOALT, 0},
    {"servos_8x",       SKR_OUTPUT,  "pwm",   "via PCA9685", {1, 0, 1}, NO3, NOALT, 0},
    {"link_uart0",      SKR_SYSTEM,  "uart",  "GPIO1/3",  {2, 2, 2}, NO3, NOALT, 0},
    {"console_uart2",   SKR_SYSTEM,  "uart",  "GPIO5/15", {1, 1, 1}, NO3, NOALT, 0},
    {"nvs_flash",       SKR_SYSTEM,  "flash", "nvs",      {2, 2, 2}, NO3, NOALT, 0},
};
const size_t SKR_MANIFEST_LEN = sizeof SKR_MANIFEST / sizeof SKR_MANIFEST[0];

_Static_assert(sizeof SKR_MANIFEST / sizeof SKR_MANIFEST[0] <= SKR_MAX_ENTRIES, "manifest larger than SKR_MAX_ENTRIES");

typedef struct { uint8_t addr; const char *text; } guess_t;
static const guess_t GUESSES[] = {
    {0x29, "vl53l0x"},
    {0x3C, "oled (SSD1306 or SH1106)"},
    {0x3D, "oled (second address)"},
    {0x40, "ina219 or pca9685 (reg 0x00: 0x399F ina219, 0x11 pca9685)"},
    {0x41, "ina219 (A0 closed) or pca9685 (A0)"},
    {0x57, "at24c32 eeprom (DS3231 module, Pi side)"},
    {0x68, "ds3231 or mpu6050"},
    {0x70, "pca9685 all-call"},
};

const char *skr_addr_guess(uint8_t addr)
{
    for (size_t i = 0; i < sizeof GUESSES / sizeof GUESSES[0]; i++) {
        if (GUESSES[i].addr == addr) { return GUESSES[i].text; }
    }
    return NULL;
}

/* ---- small helpers ---------------------------------------------------------------- */
static size_t written(int ret, size_t cap)
{
    if (ret < 0) { return 0; }
    return ((size_t)ret < cap) ? (size_t)ret : cap - 1u;
}

static uint8_t eff_addr(const skr_probe_t *p, size_t i, skr_role_t role)
{
    return (p->addr_override[i] != 0u) ? p->addr_override[i] : SKR_MANIFEST[i].addr[role];
}

static void add_row(skr_report_t *rep, const skr_row_t *r)
{
    if (rep->n < SKR_MAX_ROWS) {
        rep->rows[rep->n++] = *r;
        rep->counts[(int)r->status & 7]++;
    }
}

static void set_detail(skr_row_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void set_detail(skr_row_t *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(r->detail, sizeof r->detail, fmt, ap);
    va_end(ap);
}

static void eval_i2c(skr_role_t role, const skr_probe_t *p, size_t i, uint8_t need, skr_row_t *r)
{
    const skr_dev_t *d = &SKR_MANIFEST[i];
    const uint8_t eff = eff_addr(p, i, role);
    skl_i2c_dev_t dev;
    memset(&dev, 0, sizeof dev);
    dev.addr = eff;
    dev.n_alt = d->n_alt;
    memcpy(dev.alt, d->alt, sizeof dev.alt);
    dev.need = need;

    /* hide addresses that another listed device owns (its main address, identity check passed) */
    bool seen[128];
    memcpy(seen, p->present, sizeof seen);
    const skr_dev_t *twin = NULL;
    for (size_t j = 0; j < SKR_MANIFEST_LEN; j++) {
        const skr_dev_t *o = &SKR_MANIFEST[j];
        if (j == i || o->kind != SKR_I2C || o->need[role] == SKR_NEED_NO) { continue; }
        const uint8_t a = eff_addr(p, j, role);
        if (a == eff) { if (twin == NULL) { twin = o; } continue; }
        if (a < 128u && p->in[j].check == 1) { seen[a] = false; }
    }

    uint8_t found = 0;
    r->status = skl_i2c_status(&dev, seen, p->in[i].check, &found);
    r->found = found;
    (void)snprintf(r->where, sizeof r->where, "bus0 0x%02X", (unsigned)eff);
    const char *note = (p->in[i].note != NULL && p->in[i].note[0] != '\0') ? p->in[i].note : "ACK";
    switch (r->status) {
    case SKL_DEV_OK:
        if (twin != NULL) { set_detail(r, "%s; same address as %s", note, twin->name); }
        else              { set_detail(r, "%s", note); }
        break;
    case SKL_DEV_MOVED:
        set_detail(r, "found at 0x%02X -> i2c remap %s 0x%02X save", (unsigned)found, d->name, (unsigned)found);
        break;
    case SKL_DEV_MISSING: set_detail(r, "no ACK: check wiring, power, address pad"); break;
    case SKL_DEV_ABSENT:  set_detail(r, "optional, not found"); break;
    case SKL_DEV_FAULT:   set_detail(r, "ACK at 0x%02X but the identity check failed", (unsigned)found); break;
    default:              r->detail[0] = '\0'; break;
    }
}

/* Non-I2C kinds. "not fitted" on a required device is MISSING, on an optional one absent. */
static void eval_other(const skr_dev_t *d, const skr_input_t *in, uint8_t need, skr_row_t *r)
{
    (void)snprintf(r->where, sizeof r->where, "%s", d->where != NULL ? d->where : "-");
    r->found = 0;
    if (in->flags & SKR_F_DISABLED) {
        r->status = SKL_DEV_DISABLED; set_detail(r, "disabled by configuration"); return;
    }
    if (in->flags & SKR_F_NOT_FITTED) {
        if (need >= SKR_NEED_REQ) { r->status = SKL_DEV_MISSING; set_detail(r, "required but not fitted"); }
        else                      { r->status = SKL_DEV_ABSENT;  set_detail(r, "optional, not fitted"); }
        return;
    }
    const char *note = (in->note != NULL && in->note[0] != '\0') ? in->note : NULL;
    switch (d->kind) {
    case SKR_ADC:
        if (in->flags & SKR_F_READ_ERROR)      { r->status = SKL_DEV_FAULT; set_detail(r, "read error"); }
        else if (in->flags & SKR_F_SATURATED)  { r->status = SKL_DEV_FAULT; set_detail(r, "at a rail for 1 s (saturated or open)"); }
        else if (in->flags & SKR_F_STUCK)      { r->status = SKL_DEV_FAULT; set_detail(r, "constant for 5 s while driving"); }
        else { r->status = SKL_DEV_OK; if (note) { set_detail(r, "%s", note); } else { set_detail(r, "raw %ld, plausible", (long)in->value); } }
        break;
    case SKR_GPIO_IN:
        if (in->flags & SKR_F_READ_ERROR)      { r->status = SKL_DEV_FAULT; set_detail(r, "read error"); }
        else if (in->flags & SKR_F_STUCK)      { r->status = SKL_DEV_FAULT; set_detail(r, "high for over 20 s (stuck)"); }
        else { r->status = SKL_DEV_OK; if (note) { set_detail(r, "%s", note); } else { set_detail(r, "%s", in->value ? "high" : "low"); } }
        break;
    case SKR_OUTPUT:
        r->status = SKL_DEV_UNVERIFIED; set_detail(r, "outputs cannot be read back");
        break;
    case SKR_SYSTEM:
        if (in->flags & (SKR_F_FAULT | SKR_F_READ_ERROR)) { r->status = SKL_DEV_FAULT; set_detail(r, "%s", note ? note : "check failed"); }
        else { r->status = SKL_DEV_OK; set_detail(r, "%s", note ? note : "ok"); }
        break;
    default:
        r->status = SKL_DEV_FAULT; set_detail(r, "unknown kind");
        break;
    }
}

void skr_evaluate(skr_role_t role, const skr_probe_t *p, skr_report_t *rep)
{
    memset(rep, 0, sizeof *rep);
    if ((int)role < 0 || (int)role > 2) { return; }
    uint8_t claimed[SKR_MAX_ENTRIES * 2u];
    size_t n_claimed = 0;

    for (size_t i = 0; i < SKR_MANIFEST_LEN; i++) {
        const skr_dev_t *d = &SKR_MANIFEST[i];
        const uint8_t need = d->need[role];
        if (need == SKR_NEED_NO) { continue; }
        skr_row_t r;
        memset(&r, 0, sizeof r);
        r.name = d->name; r.kind_text = d->kind_text; r.need = need; r.index = (int)i;
        if (d->kind == SKR_I2C) {
            eval_i2c(role, p, i, need, &r);
            claimed[n_claimed++] = eff_addr(p, i, role);
            if (r.status == SKL_DEV_MOVED && r.found != 0u && n_claimed < sizeof claimed) { claimed[n_claimed++] = r.found; }
        } else {
            eval_other(d, &p->in[i], need, &r);
        }
        add_row(rep, &r);
    }

    uint8_t extra[SKR_MAX_UNEXPECTED];
    const size_t n_extra = skl_i2c_unexpected(p->present, claimed, n_claimed, extra, SKR_MAX_UNEXPECTED);
    for (size_t k = 0; k < n_extra; k++) {
        skr_row_t r;
        memset(&r, 0, sizeof r);
        r.name = "(unknown)"; r.kind_text = "i2c"; r.need = SKR_NEED_NO; r.found = extra[k];
        r.status = SKL_DEV_UNEXPECTED; r.index = -1;
        (void)snprintf(r.where, sizeof r.where, "bus0 0x%02X", (unsigned)extra[k]);
        const char *alt_owner = NULL;
        for (size_t i = 0; i < SKR_MANIFEST_LEN && alt_owner == NULL; i++) {
            const skr_dev_t *d = &SKR_MANIFEST[i];
            if (d->kind != SKR_I2C || d->need[role] == SKR_NEED_NO) { continue; }
            for (uint8_t a = 0; a < d->n_alt; a++) { if (d->alt[a] == extra[k]) { alt_owner = d->name; break; } }
        }
        const char *g = skr_addr_guess(extra[k]);
        if (alt_owner != NULL) { set_detail(&r, "could be %s (alt address)", alt_owner); }
        else if (g != NULL)    { set_detail(&r, "could be %s", g); }
        else                   { set_detail(&r, "no manifest entry claims this address"); }
        add_row(rep, &r);
    }
}

uint8_t skr_fail_count(const skr_report_t *rep)
{
    unsigned n = 0;
    for (size_t i = 0; i < rep->n; i++) {
        const skr_row_t *r = &rep->rows[i];
        if (r->status == SKL_DEV_MISSING || r->status == SKL_DEV_FAULT) { n++; }
        else if (r->status == SKL_DEV_MOVED && r->need >= SKR_NEED_REQ) { n++; }
    }
    return (uint8_t)(n > 255u ? 255u : n);
}

/* ---- text ---------------------------------------------------------------------------- */
#define ROW_FMT "%-15s %-5s %-15s %-5s %-6s %-11s %s"

size_t skr_format_header(char *buf, size_t cap)
{
    if (cap == 0) { return 0; }
    return written(snprintf(buf, cap, ROW_FMT, "NAME", "KIND", "WHERE", "NEED", "FOUND", "STATUS", "DETAIL"), cap);
}

size_t skr_format_row(const skr_row_t *r, char *buf, size_t cap)
{
    if (cap == 0) { return 0; }
    const char *need = (r->need >= SKR_NEED_REQ) ? "req" : (r->need == SKR_NEED_OPT ? "opt" : "-");
    char found[8];
    if (r->found != 0u) { (void)snprintf(found, sizeof found, "0x%02X", (unsigned)r->found); }
    else                { (void)snprintf(found, sizeof found, "-"); }
    size_t n = written(snprintf(buf, cap, ROW_FMT, r->name, r->kind_text, r->where, need, found,
                                skl_dev_status_name(r->status), r->detail), cap);
    while (n > 0 && buf[n - 1u] == ' ') { buf[--n] = '\0'; }     /* no trailing spaces when detail is empty */
    return n;
}

size_t skr_format_summary(const skr_report_t *rep, char *buf, size_t cap)
{
    static const skl_dev_status_t ORDER[] = { SKL_DEV_OK, SKL_DEV_MOVED, SKL_DEV_MISSING, SKL_DEV_FAULT,
                                              SKL_DEV_ABSENT, SKL_DEV_UNEXPECTED, SKL_DEV_UNVERIFIED, SKL_DEV_DISABLED };
    if (cap == 0) { return 0; }
    size_t n = written(snprintf(buf, cap, "summary:"), cap);
    bool any = false;
    for (size_t i = 0; i < sizeof ORDER / sizeof ORDER[0]; i++) {
        const unsigned c = rep->counts[(int)ORDER[i] & 7];
        if (c == 0u || n + 1u >= cap) { continue; }
        n += written(snprintf(buf + n, cap - n, "%s %u %s", any ? "," : "", c, skl_dev_status_name(ORDER[i])), cap - n);
        any = true;
    }
    if (!any && n + 1u < cap) { n += written(snprintf(buf + n, cap - n, " no devices"), cap - n); }
    return n;
}
