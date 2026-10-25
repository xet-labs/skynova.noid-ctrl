/*
 * skynoid_core.h - small pure-C helpers for the "core" services: flight-recorder ring, debouncer,
 *                  buzzer pattern timing, and I2C device status. No ESP-IDF includes.
 *
 * WHY A SECOND HEADER: skynoid_logic.h is the decision maths for the base node (hazard, drive, cliff,
 * battery). These four helpers serve other services (recorder, touch, buzzer, device registry), so they
 * live next to it but are declared here. Implementations: src/ring.c and src/devstat.c.
 *
 * TESTS: firmware/test/test_core
 */
#ifndef SKYNOID_CORE_H
#define SKYNOID_CORE_H

#include "skynoid_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ flight recorder ring
 * The header (skl_ring_t) is kept apart from the data buffer so both can sit in RTC noinit RAM and
 * survive a software or watchdog reset. skl_ring_attach() trusts the header only if the magic (which
 * includes the capacity) and the indices make sense; random power-on RAM fails that check and the ring
 * starts empty. cap must be > 0 in every call. */
typedef struct { uint32_t magic, head, len; } skl_ring_t;

void   skl_ring_attach(skl_ring_t *r, uint32_t cap);
/* Append n bytes; when full the oldest bytes are overwritten. */
void   skl_ring_put(skl_ring_t *r, uint8_t *buf, uint32_t cap, const uint8_t *data, size_t n);
/* Copy out the most recent bytes, oldest first. If out_cap is smaller than the stored length the NEWEST
 * out_cap bytes are returned. Returns the number of bytes copied. */
size_t skl_ring_read(const skl_ring_t *r, const uint8_t *buf, uint32_t cap, uint8_t *out, size_t out_cap);

/* ------------------------------------------------------------------- debouncer
 * A level must stay different from the accepted one for hold_ms before it is accepted. */
typedef struct { bool stable, cand; uint32_t since_ms; } skl_deb_t;

void skl_deb_init(skl_deb_t *d, bool level);
/* Feed one raw sample. Returns true on the sample that CHANGES the accepted level (read d->stable). */
bool skl_deb_step(skl_deb_t *d, bool level, uint32_t now_ms, uint32_t hold_ms);

/* ------------------------------------------------------------ buzzer patterns
 * Pattern ids as in protocol-v0: 0 off, 1 chirp, 2 double_chirp, 3 low_battery, 4 critical, 5 error.
 * t is milliseconds since the pattern started. */
bool skl_buzzer_on(uint8_t pattern, uint32_t t_ms);     /* should the output be on at time t */
bool skl_buzzer_done(uint8_t pattern, uint32_t t_ms);   /* pattern has finished (critical never does) */

/* ------------------------------------------------------------ status LED patterns
 * Mode ids as in hal.h: 0 off, 1 slow (1 Hz), 2 solid, 3 fast (5 Hz), 4 double (two quick blinks every 2 s,
 * the maintenance-mode marker). t is any free-running millisecond clock. */
bool skl_led_on(int mode, uint32_t t_ms);

/* ------------------------------------------------------- I2C device status
 * Status of one expected device from what answered on the bus (console-commands.md, "devices" table).
 * `need`: 0 not part of this role, 1 optional, 2 required.
 * `check` (identity check result, done by the caller): 1 passed, 0 failed, -1 "do not trust the ACK at
 * the main address" (another device owns it, e.g. a PCA9685 sitting at 0x40 is not a missing INA219). */
typedef enum {
    SKL_DEV_DISABLED = 0, SKL_DEV_OK, SKL_DEV_MOVED, SKL_DEV_MISSING,
    SKL_DEV_ABSENT, SKL_DEV_FAULT, SKL_DEV_UNVERIFIED, SKL_DEV_UNEXPECTED
} skl_dev_status_t;

typedef struct {
    uint8_t addr;       /* expected 7-bit address */
    uint8_t alt[4];     /* known alternative addresses */
    uint8_t n_alt;      /* how many entries of alt are valid (at most 4) */
    uint8_t need;       /* 0 none, 1 optional, 2 required */
} skl_i2c_dev_t;

const char *skl_dev_status_name(skl_dev_status_t s);
/* present[a] is true when address a acknowledged in the scan. *found receives the address the device
 * was seen at (0 if none). */
skl_dev_status_t skl_i2c_status(const skl_i2c_dev_t *d, const bool present[128], int check, uint8_t *found);
/* Addresses that answered but are not in claimed[] (valid range 0x08..0x77). Returns how many were
 * written to out (at most cap). */
size_t skl_i2c_unexpected(const bool present[128], const uint8_t *claimed, size_t n_claimed,
                          uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_CORE_H */
