/*
 * device_probe.h - probe the real hardware and fill the device table (step 1.8, part B).
 *
 * The pure rules (statuses, table text) live in lib/skynoid_registry. This file does the electrical work:
 * scan the I2C bus, read identity registers, take the ADC readings the safety task already has, and hand
 * everything to skr_evaluate() for role "single".
 *
 * Not re-entrant: the result lives in static memory (about 8 KB) and is overwritten by the next call.
 * Call it only from the console task.
 */
#ifndef CORE_DEVICE_PROBE_H
#define CORE_DEVICE_PROBE_H

#include <stdint.h>
#include "skynoid_registry.h"

/* Probe now. Returns the evaluated report, or NULL when the bus scan took longer than 400 ms (stuck bus). */
const skr_report_t *device_probe_run(void);

/* Text of the identity read at an address during the last device_probe_run (for example "reg0=0x399F"),
 * or NULL if nothing was read there. */
const char *device_probe_ident(uint8_t addr);

#endif /* CORE_DEVICE_PROBE_H */
