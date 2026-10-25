/*
 * hazard.c - the direction rule for drive commands while a hazard is active.
 *
 * Purpose : decide whether a drive command may run (04-safety-and-reflexes, R1 detail).
 * Called by: the drive command handler (to answer NACK hazard_blocked) and the
 *            motion task (to re-check every cycle).
 * Test    : test_logic.c, test_hazard_*.
 */
#include "skynoid_logic.h"

bool skl_hazard_allows(uint8_t mask, int32_t v_mm_s, int32_t w_dps)
{
    mask &= SKL_HZ_ALL;
    if (mask == 0u) { return true; }
    if (v_mm_s == 0 && w_dps == 0) { return true; }      /* a stop is never refused */
    if (w_dps != 0) { return false; }                     /* never rotate near an edge */

    const bool front = (mask & SKL_HZ_FRONT_MASK) != 0u;
    const bool rear  = (mask & SKL_HZ_REAR_MASK) != 0u;
    if (front && rear) { return false; }                  /* stuck: a human must move it */
    if (front) { return v_mm_s < 0; }                     /* only straight reverse */
    if (rear)  { return v_mm_s > 0; }                     /* only straight forward */
    return false;                                         /* sensor fault / unreliable floor only */
}
