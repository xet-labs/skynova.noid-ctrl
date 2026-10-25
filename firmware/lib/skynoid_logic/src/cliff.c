/* cliff.c - one IR sensor: edge detection with debounce and hysteresis, rail faults, floor check.
 * Spec: 04-firmware/02-base-node.md "Cliff protection". Pure logic.
 *
 * Thresholds are percentages of the learned baseline.
 *   FLOOR_HIGH  edge when reading < baseline * ratio / 100   (50: below half the floor level)
 *               clear when reading >= baseline * clear / 100 (65)
 *   FLOOR_LOW   the same, mirrored around the baseline:
 *               edge when reading > baseline * (200 - ratio) / 100
 *               clear when reading <= baseline * (200 - clear) / 100
 * The gap between ratio and clear is the hysteresis; skl_cliff_cfg_valid() checks it points the right way.
 */
#include "skynoid_logic.h"

#define ADC_FULL_SCALE 4095

void skl_cliff_default_cfg(skl_cliff_cfg_t *cfg)
{
    cfg->ratio_pct = 50;
    cfg->clear_pct = 65;
    cfg->debounce_samples = 2;
    cfg->clear_hold_ms = 200;
    cfg->rail_ms = 1000;
    cfg->sat_low = 40;
    cfg->sat_high = 4050;
    cfg->pol = SKL_POL_FLOOR_HIGH;
}

bool skl_cliff_cfg_valid(const skl_cliff_cfg_t *cfg)
{
    if (cfg->ratio_pct < 10 || cfg->ratio_pct > 90) { return false; }
    if (cfg->clear_pct < 10 || cfg->clear_pct > 95) { return false; }
    if (cfg->clear_pct <= cfg->ratio_pct) { return false; }          /* no hysteresis, or backwards */
    if (cfg->debounce_samples < 1 || cfg->debounce_samples > 10) { return false; }
    if (cfg->sat_low < 0 || cfg->sat_high > ADC_FULL_SCALE || cfg->sat_low >= cfg->sat_high) { return false; }
    return true;
}

void skl_cliff_init(skl_cliff_t *c)
{
    *c = (skl_cliff_t){0};
}

void skl_cliff_set_baseline(skl_cliff_t *c, int32_t baseline)
{
    c->baseline = baseline;
    c->have_baseline = true;
    c->active = c->clearing = false;
    c->beyond = 0;
}

static bool beyond_edge(const skl_cliff_t *c, const skl_cliff_cfg_t *cfg, int32_t r)
{
    if (cfg->pol == SKL_POL_FLOOR_HIGH) { return (int64_t)r * 100 < (int64_t)c->baseline * cfg->ratio_pct; }
    return (int64_t)r * 100 > (int64_t)c->baseline * (200 - cfg->ratio_pct);
}

static bool back_on_floor(const skl_cliff_t *c, const skl_cliff_cfg_t *cfg, int32_t r)
{
    if (cfg->pol == SKL_POL_FLOOR_HIGH) { return (int64_t)r * 100 >= (int64_t)c->baseline * cfg->clear_pct; }
    return (int64_t)r * 100 <= (int64_t)c->baseline * (200 - cfg->clear_pct);
}

skl_cliff_event_t skl_cliff_step(skl_cliff_t *c, const skl_cliff_cfg_t *cfg, int32_t reading,
                                 uint32_t now_ms)
{
    skl_cliff_event_t ev = SKL_CLIFF_NONE;
    if (!c->have_baseline) { return ev; }               /* blocking() already says "stopped" */

    /* rail check: stuck at either end of the scale for rail_ms is a sensor fault (fail-safe) */
    const bool on_rail = reading <= cfg->sat_low || reading >= cfg->sat_high;
    if (on_rail) {
        if (!c->at_rail) { c->at_rail = true; c->rail_start_ms = now_ms; }
        else if (!c->fault && (uint32_t)(now_ms - c->rail_start_ms) >= cfg->rail_ms) {
            c->fault = true;
            ev = SKL_CLIFF_FAULT;
        }
    } else {
        c->at_rail = false;
        if (c->fault) { c->fault = false; ev = SKL_CLIFF_FAULT_CLEARED; }
    }

    /* edge detection: debounce on the way in, hold time on the way out */
    if (!c->active) {
        if (beyond_edge(c, cfg, reading)) {
            if (++c->beyond >= cfg->debounce_samples) {
                c->active = true;
                c->clearing = false;
                ev = SKL_CLIFF_EDGE;
            }
        } else {
            c->beyond = 0;
        }
    } else if (back_on_floor(c, cfg, reading)) {
        if (!c->clearing) { c->clearing = true; c->clear_start_ms = now_ms; }
        else if ((uint32_t)(now_ms - c->clear_start_ms) >= cfg->clear_hold_ms) {
            c->active = c->clearing = false;
            c->beyond = 0;
            ev = SKL_CLIFF_CLEARED;
        }
    } else {
        c->clearing = false;                            /* bounced back over the edge: restart the hold */
    }
    return ev;
}

bool skl_cliff_blocking(const skl_cliff_t *c)
{
    return !c->have_baseline || c->active || c->fault;
}

skl_floor_t skl_floor_check(int32_t baseline, int32_t no_floor_adc, int32_t min_contrast_pct)
{
    if (min_contrast_pct == SKL_UNSET || no_floor_adc == SKL_UNSET) { return SKL_FLOOR_UNKNOWN; }
    if (baseline <= 40 || baseline >= 4050) { return SKL_FLOOR_UNRELIABLE; }   /* sitting at a rail */
    const int32_t diff = baseline > no_floor_adc ? baseline - no_floor_adc : no_floor_adc - baseline;
    return ((int64_t)diff * 100 >= (int64_t)min_contrast_pct * ADC_FULL_SCALE) ? SKL_FLOOR_OK
                                                                              : SKL_FLOOR_UNRELIABLE;
}
