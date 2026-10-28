/*
 * core/maint.h - maintenance mode on the board (step 1.6b, console part).
 *
 * The rules (timer, minutes, renewal) are the tested pure code in lib/skynoid_maint; this layer adds the mutex,
 * the clock, the expiry timer and the side effects. Tier 1 console commands run only while maint_is_active().
 *
 * DONE here : `maint on [min]`, `maint off`, `maint` (status); expiry after the timeout even if nobody types;
 *             entering disarms the base and switches the servos off; leaving or expiring also resets the
 *             parameters to their defaults; the console gate (console_set_maint_check) is installed.
 * NOT DONE  : ending the Pi session on entry, "framed commands answer NACK busy", the two-blink status LED,
 *             `mute`, `stats`, `crash`. They need hooks in link.c and the dispatcher and are still open (1.6b rest).
 */
#ifndef CORE_MAINT_H
#define CORE_MAINT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     maint_init(void);                              /* once, before the console task runs */
bool     maint_is_active(void);                         /* the console gate; also notices an expired deadline */
int      maint_enter(int32_t minutes, bool *newly);     /* 0 = ok; SKM_BAD_MINUTES (1) = refused, nothing changed */
bool     maint_leave(void);                             /* true if it was on */
uint32_t maint_remaining_s(void);                       /* 0 when off */

#ifdef __cplusplus
}
#endif
#endif /* CORE_MAINT_H */
