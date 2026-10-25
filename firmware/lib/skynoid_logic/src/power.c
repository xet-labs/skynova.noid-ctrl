/* power.c - battery LOW / CRIT with persistence and hysteresis (R6). Pure logic.
 * Spec: 04-firmware/02-base-node.md "Power monitor", config-defaults.yaml power block. */
#include "skynoid_logic.h"

void skl_batt_default_cfg(skl_batt_cfg_t *cfg)
{
    cfg->low_mv = 10650;
    cfg->crit_mv = 10050;
    cfg->persist_ms = 5000;
    cfg->hyst_mv = 150;
}

void skl_batt_init(skl_batt_t *b)
{
    *b = (skl_batt_t){0};
}

int32_t skl_vcomp_mv(int32_t mv, int32_t ma, int32_t r_int_mohm)
{
    return mv + (int32_t)(((int64_t)ma * r_int_mohm) / 1000);
}

skl_batt_event_t skl_batt_step(skl_batt_t *b, const skl_batt_cfg_t *cfg, int32_t mv, uint32_t now_ms)
{
    if (mv <= 0 || b->state == SKL_BATT_CRIT) { return SKL_BATT_EV_NONE; }   /* no reading, or latched */

    /* CRIT: below the threshold for the whole persistence time */
    if (mv < cfg->crit_mv) {
        if (!b->crit_timing) { b->crit_timing = true; b->crit_start_ms = now_ms; }
        else if ((uint32_t)(now_ms - b->crit_start_ms) >= cfg->persist_ms) {
            b->state = SKL_BATT_CRIT;
            return SKL_BATT_EV_CRIT;
        }
    } else {
        b->crit_timing = false;
    }

    /* LOW: same rule; leaving LOW needs the voltage above low + hysteresis */
    if (b->state == SKL_BATT_OK) {
        if (mv < cfg->low_mv) {
            if (!b->low_timing) { b->low_timing = true; b->low_start_ms = now_ms; }
            else if ((uint32_t)(now_ms - b->low_start_ms) >= cfg->persist_ms) {
                b->state = SKL_BATT_LOW;
                return SKL_BATT_EV_LOW;
            }
        } else {
            b->low_timing = false;
        }
    } else if (b->state == SKL_BATT_LOW && mv >= cfg->low_mv + cfg->hyst_mv) {
        b->state = SKL_BATT_OK;
        b->low_timing = false;
    }
    return SKL_BATT_EV_NONE;
}
