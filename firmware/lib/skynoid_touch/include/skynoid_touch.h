/*
 * skynoid_touch.h - touch pad logic as pure C (no ESP-IDF, no FreeRTOS): debounce, boot ignore, stuck pad.
 *
 * WHAT THIS FILE IS
 *   Four touch zones (protocol-v0 section 5: 0 head, 1 hand_l, 2 hand_r, 3 extra). The hardware side
 *   (src/touch.c) reads one GPIO per zone every 10 ms and calls skt_step(). This file decides what the
 *   samples mean and says which EVT touch frames to send. It touches no pin and no clock: time comes in
 *   as uint32 milliseconds from a free-running clock (differences are wrap safe).
 *
 * RULES
 *   - A level must stay the same for hold_ms before it counts (skl_deb_step in skynoid_core.h).
 *   - Nothing is reported for the first boot_ignore_ms after skt_init: touch modules recalibrate at power-up
 *     and may output a level for a moment. A pad that is still pressed when the window ends reports DOWN then.
 *   - A pad that stays down for stuck_ms is treated as broken (water, a wire shorted to the rail): skt_step
 *     returns UP and STUCK together, once, so the host's picture ends with the pad up and it also learns
 *     why. The zone then stays silent until it has been released. stuck_ms = 0 turns the check off.
 *
 * RETURN VALUE of skt_step: a mask of SKT_F_* (0 = nothing to report). The caller sends, in this order:
 *   DOWN  -> EVT touch (zone, 1)
 *   UP    -> EVT touch (zone, 0)
 *   STUCK -> EVT sensor_fault (SKB_SENSOR_TOUCH0 + zone, reason 1 stuck)
 *
 * TESTS: firmware/test/test_touch
 */
#ifndef SKYNOID_TOUCH_H
#define SKYNOID_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "skynoid_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SKT_ZONES 4

enum { SKT_F_DOWN = 1, SKT_F_UP = 2, SKT_F_STUCK = 4 };

typedef struct {
    skl_deb_t deb;                 /* debounced raw level: true = pressed */
    bool      down;                /* what the host was last told */
    bool      stuck;               /* ignored until released */
    uint32_t  down_since_ms;
} skt_zone_t;

typedef struct {
    skt_zone_t z[SKT_ZONES];
    uint32_t   start_ms;
    uint32_t   boot_ignore_ms;
    uint32_t   hold_ms;
    uint32_t   stuck_ms;           /* 0 = never */
} skt_t;

void skt_init(skt_t *t, uint32_t now_ms, uint32_t boot_ignore_ms, uint32_t hold_ms, uint32_t stuck_ms);

/* Feed one raw sample (pressed = the pad output is at its active level). Zones >= SKT_ZONES return 0. */
uint8_t skt_step(skt_t *t, uint8_t zone, bool pressed, uint32_t now_ms);

/* What the host has been told: true while a DOWN has been reported and no UP yet. */
bool skt_is_down(const skt_t *t, uint8_t zone);
bool skt_is_stuck(const skt_t *t, uint8_t zone);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_TOUCH_H */
