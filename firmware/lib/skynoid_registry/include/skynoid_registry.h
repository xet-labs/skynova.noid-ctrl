/*
 * skynoid_registry.h - the device manifest and the `devices` / `selftest` table (pure C, no ESP-IDF).
 *
 * WHAT THIS IS
 *   The manifest lists every device a robot role should have (from 10-specs/devices.yaml). Board code
 *   probes the hardware and fills a skr_probe_t (what answered on the I2C bus, what the ADC pins read,
 *   which checks failed). skr_evaluate() turns that into one row per device with a status (ok, moved,
 *   MISSING, absent, FAULT, unexpected, unverified, disabled), and the format functions print the table
 *   of console-commands.md, section "The devices table". Nothing here touches hardware, so every rule
 *   is tested on the PC: pio test -e native -f test_registry.
 *
 * WHO DECIDES WHAT
 *   - The caller (board glue, step 1.5b part 2) does the electrical work: scan, identity check, ADC reads.
 *   - This library decides the status and reuses skl_i2c_status() from skynoid_core.h for the I2C rule,
 *     so there is one implementation of it.
 *
 * LIMITS (honest list)
 *   - skl_i2c_dev_t holds 4 alternative addresses, so an entry lists at most 4 (pca9685 has 7 in
 *     devices.yaml; only 0x41..0x44 are checked here).
 *   - Roles are indexed 0 single, 1 base, 2 upper in THIS file; the caller maps the protocol role id.
 *   - skr_report_t is about 3.5 KB: keep it static, not on a small task stack.
 */
#ifndef SKYNOID_REGISTRY_H
#define SKYNOID_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "skynoid_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SKR_ROLE_SINGLE = 0, SKR_ROLE_BASE = 1, SKR_ROLE_UPPER = 2 } skr_role_t;
typedef enum { SKR_I2C = 0, SKR_ADC, SKR_GPIO_IN, SKR_OUTPUT, SKR_SYSTEM } skr_kind_t;

#define SKR_NEED_NO   0u    /* not part of this role: no row */
#define SKR_NEED_OPT  1u
#define SKR_NEED_REQ  2u

#define SKR_MAX_ENTRIES    32u
#define SKR_MAX_UNEXPECTED 8u
#define SKR_MAX_ROWS       (SKR_MAX_ENTRIES + SKR_MAX_UNEXPECTED)
#define SKR_DETAIL_MAX     56u
#define SKR_WHERE_MAX      16u

/* One manifest entry (a row of devices.yaml). */
typedef struct {
    const char *name;
    skr_kind_t  kind;
    const char *kind_text;      /* column KIND: "i2c", "adc", "in", "pwm", "out", "uart", "flash" */
    const char *where;          /* text for non-I2C ("GPIO36"); NULL for I2C (printed as "bus0 0x29") */
    uint8_t     need[3];        /* per role: single, base, upper (SKR_NEED_*) */
    uint8_t     addr[3];        /* I2C: expected 7-bit address per role */
    uint8_t     alt[4];         /* I2C: known alternative addresses */
    uint8_t     n_alt;
} skr_dev_t;

extern const skr_dev_t SKR_MANIFEST[];
extern const size_t    SKR_MANIFEST_LEN;

/* Flags the caller sets in skr_input_t.flags. */
#define SKR_F_READ_ERROR 0x01u   /* the read failed (ADC error, bus error) */
#define SKR_F_SATURATED  0x02u   /* ADC at a rail for 1 s */
#define SKR_F_STUCK      0x04u   /* ADC constant for 5 s while driving, or a touch pad high over 20 s */
#define SKR_F_NOT_FITTED 0x08u   /* known not wired yet (optional device: shown as absent) */
#define SKR_F_DISABLED   0x10u   /* compiled out or disabled by configuration */
#define SKR_F_FAULT      0x20u   /* a system item reports a failure (link, NVS) */

/* What the caller measured for ONE manifest entry (indexed like SKR_MANIFEST). */
typedef struct {
    int8_t      check;   /* I2C identity check: 1 passed, 0 failed, -1 do not trust the ACK at the main address */
    uint8_t     flags;   /* SKR_F_* */
    int32_t     value;   /* ADC raw reading, or GPIO level 0/1 */
    const char *note;    /* optional text shown for status ok (for example "model 0xEE, range 342 mm") */
} skr_input_t;

typedef struct {
    bool        present[128];                   /* I2C addresses that acknowledged in the scan */
    uint8_t     addr_override[SKR_MAX_ENTRIES]; /* per entry: address from `i2c remap` (0 = use the manifest) */
    skr_input_t in[SKR_MAX_ENTRIES];
} skr_probe_t;

typedef struct {
    const char      *name;                      /* "(unknown)" for an unexpected address */
    const char      *kind_text;
    uint8_t          need;                      /* SKR_NEED_*; 0 prints "-" */
    uint8_t          found;                     /* I2C address where it answered, 0 if none */
    skl_dev_status_t status;
    int              index;                     /* manifest index, -1 for an unexpected address */
    char             where[SKR_WHERE_MAX];
    char             detail[SKR_DETAIL_MAX];
} skr_row_t;

typedef struct {
    skr_row_t rows[SKR_MAX_ROWS];
    size_t    n;
    uint8_t   counts[8];                        /* indexed by skl_dev_status_t */
} skr_report_t;

/* Evaluate every manifest entry that belongs to `role`, then add one row per unexpected I2C address.
 * Entries whose need for the role is 0 get no row. */
void skr_evaluate(skr_role_t role, const skr_probe_t *probe, skr_report_t *rep);

/* Rows that make `selftest` print FAIL: MISSING, FAULT, and a REQUIRED device that is `moved` (its driver
 * would look at the wrong address). absent, unexpected, unverified and disabled do not count. */
uint8_t skr_fail_count(const skr_report_t *rep);

/* Best guess for an address nobody claims (address_guess of devices.yaml); NULL if none. */
const char *skr_addr_guess(uint8_t addr);

/* Text output: one line WITHOUT a trailing newline, always NUL-terminated when cap > 0, cut if too small.
 * Returns the length written. */
size_t skr_format_header(char *buf, size_t cap);
size_t skr_format_row(const skr_row_t *r, char *buf, size_t cap);
size_t skr_format_summary(const skr_report_t *rep, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_REGISTRY_H */
