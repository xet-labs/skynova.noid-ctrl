/*
 * touch_logic.c - debounce, boot ignore and stuck-pad rules for the four touch zones (pure C).
 * See include/skynoid_touch.h for the rules and test/test_touch for the checks.
 */
#include "skynoid_touch.h"

void skt_init(skt_t *t, uint32_t now_ms, uint32_t boot_ignore_ms, uint32_t hold_ms, uint32_t stuck_ms)
{
    for (int i = 0; i < SKT_ZONES; i++) {
        skl_deb_init(&t->z[i].deb, false);
        t->z[i].down = false;
        t->z[i].stuck = false;
        t->z[i].down_since_ms = 0;
    }
    t->start_ms = now_ms;
    t->boot_ignore_ms = boot_ignore_ms;
    t->hold_ms = hold_ms;
    t->stuck_ms = stuck_ms;
}

uint8_t skt_step(skt_t *t, uint8_t zone, bool pressed, uint32_t now_ms)
{
    if (zone >= SKT_ZONES) { return 0; }
    skt_zone_t *z = &t->z[zone];

    (void)skl_deb_step(&z->deb, pressed, now_ms, t->hold_ms);   /* always keep the debouncer current */
    const bool level = z->deb.stable;

    if ((uint32_t)(now_ms - t->start_ms) < t->boot_ignore_ms) { return 0; }

    if (z->stuck) {                                             /* silent until the pad has been released */
        if (!level) { z->stuck = false; }
        return 0;
    }
    if (level && !z->down) {
        z->down = true;
        z->down_since_ms = now_ms;
        return SKT_F_DOWN;
    }
    if (!level && z->down) {
        z->down = false;
        return SKT_F_UP;
    }
    if (level && z->down && t->stuck_ms != 0 && (uint32_t)(now_ms - z->down_since_ms) >= t->stuck_ms) {
        z->down = false;                                        /* the host sees UP, then the fault */
        z->stuck = true;
        return (uint8_t)(SKT_F_UP | SKT_F_STUCK);
    }
    return 0;
}

bool skt_is_down(const skt_t *t, uint8_t zone)  { return zone < SKT_ZONES && t->z[zone].down; }
bool skt_is_stuck(const skt_t *t, uint8_t zone) { return zone < SKT_ZONES && t->z[zone].stuck; }
