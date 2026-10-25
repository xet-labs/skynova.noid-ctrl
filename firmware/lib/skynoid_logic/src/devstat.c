/*
 * devstat.c - status of an I2C device from what answered on the bus.
 *
 * Purpose : the `devices` / `selftest` / doctor logic: ok, moved, MISSING, absent,
 *           FAULT, unexpected, in exactly the order console-commands.md says.
 * Called by: firmware/src/core/registry.c (after an I2C scan).
 * Test    : test_logic.c, test_devstat_*.
 * Note    : the caller hides addresses that another device already owns before
 *           asking about alternative addresses (see registry.c), so a PCA9685
 *           sitting at 0x40 is never mistaken for a "moved" INA219.
 */
#include "skynoid_core.h"

const char *skl_dev_status_name(skl_dev_status_t s)
{
    switch (s) {
    case SKL_DEV_DISABLED:   return "disabled";
    case SKL_DEV_OK:         return "ok";
    case SKL_DEV_MOVED:      return "moved";
    case SKL_DEV_MISSING:    return "MISSING";
    case SKL_DEV_ABSENT:     return "absent";
    case SKL_DEV_FAULT:      return "FAULT";
    case SKL_DEV_UNVERIFIED: return "unverified";
    case SKL_DEV_UNEXPECTED: return "unexpected";
    }
    return "?";
}

skl_dev_status_t skl_i2c_status(const skl_i2c_dev_t *d, const bool present[128], int check, uint8_t *found)
{
    *found = 0;
    if (d->addr < 128u && present[d->addr] && check != -1) {
        *found = d->addr;
        return (check == 1) ? SKL_DEV_OK : SKL_DEV_FAULT;      /* ACK but the identity check decides */
    }
    for (uint8_t i = 0; i < d->n_alt && i < 4u; i++) {
        if (d->alt[i] < 128u && present[d->alt[i]]) {
            *found = d->alt[i];
            return SKL_DEV_MOVED;
        }
    }
    return (d->need >= 2u) ? SKL_DEV_MISSING : SKL_DEV_ABSENT;
}

size_t skl_i2c_unexpected(const bool present[128], const uint8_t *claimed, size_t n_claimed,
                          uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (uint8_t a = 0x08u; a <= 0x77u; a++) {                 /* the valid 7-bit range */
        if (!present[a]) { continue; }
        bool mine = false;
        for (size_t i = 0; i < n_claimed; i++) {
            if (claimed[i] == a) { mine = true; break; }
        }
        if (!mine && n < cap) { out[n++] = a; }
    }
    return n;
}
