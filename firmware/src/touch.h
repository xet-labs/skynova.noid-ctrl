/*
 * touch.h - the four touch pads (GPIO 13 head, 18 hand_l, 19 hand_r, 17 extra): hardware side of step F.
 * The rules (debounce, boot ignore, stuck pad) are pure C in lib/skynoid_touch; this layer reads the pins.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>
#include <stdint.h>

void touch_start(void);          /* once, from base_start(): configures the pins and starts the task (nothing if SKYNOID_TOUCH_ENABLE is 0) */

/* Latest picture of one zone (0 head, 1 hand_l, 2 hand_r, 3 extra), for the `touch` console command.
 * raw = pin at its active level right now (not debounced); down = debounced and reported to the host;
 * stuck = muted as broken. Returns false for a bad zone or when the task is not running. */
bool touch_snapshot(uint8_t zone, bool *raw, bool *down, bool *stuck);

/* Event counters: posted to the link queue, and skipped because no session was running. */
void touch_stats(uint32_t *posted, uint32_t *skipped_no_session);

#endif /* TOUCH_H */
