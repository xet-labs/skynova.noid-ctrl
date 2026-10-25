/* params.c - parameter table, bounds and relations. Pure logic, see skynoid_params.h. */
#include <string.h>
#include "skynoid_params.h"

static const skp_def_t TABLE[SKP_COUNT] = {
    { 1, "speed_cap_mm_s",      250,       0,   500 },
    { 2, "turn_cap_dps",         90,       0,   180 },
    { 3, "accel_mm_s2",         500,      50,  2000 },
    { 4, "cliff_ratio_pct",      50,      10,    90 },
    { 5, "clear_ratio_pct",      65,      10,    95 },
    { 6, "batt_low_mv",       10650,    9000, 12600 },
    { 7, "batt_crit_mv",      10050,    8500, 12000 },
    { 8, "stall_soft_ma",  SKL_UNSET,      0, 20000 },
    { 9, "stall_hard_ma",  SKL_UNSET,      0, 20000 },
    {10, "wheel_base_mm",  SKL_UNSET,     50,   500 },
    {11, "duty_per_mm_s_x1000", SKL_UNSET, 1, 100000 },
    {12, "min_start_duty_pct",  SKL_UNSET, 0,   100 },
};

const skp_def_t *skp_find_id(uint8_t id)
{
    return (id >= 1 && id <= SKP_COUNT) ? &TABLE[id - 1] : NULL;
}

const skp_def_t *skp_find_name(const char *name)
{
    for (size_t i = 0; i < SKP_COUNT; i++) {
        if (strcmp(TABLE[i].name, name) == 0) { return &TABLE[i]; }
    }
    return NULL;
}

const skp_def_t *skp_def_at(size_t index)
{
    return index < SKP_COUNT ? &TABLE[index] : NULL;
}

void skp_reset(skp_store_t *s)
{
    for (size_t i = 0; i < SKP_COUNT; i++) { s->v[i] = TABLE[i].def; }
}

int32_t skp_get(const skp_store_t *s, uint8_t id)
{
    return skp_find_id(id) ? s->v[id - 1] : SKL_UNSET;
}

static bool relation_ok(const skp_store_t *s)
{
    const int32_t ratio = s->v[SKP_CLIFF_RATIO_PCT - 1], clear = s->v[SKP_CLEAR_RATIO_PCT - 1];
    const int32_t low = s->v[SKP_BATT_LOW_MV - 1], crit = s->v[SKP_BATT_CRIT_MV - 1];
    const int32_t soft = s->v[SKP_STALL_SOFT_MA - 1], hard = s->v[SKP_STALL_HARD_MA - 1];
    if (clear <= ratio) { return false; }                   /* hysteresis must point the right way */
    if (crit >= low) { return false; }
    if (soft != SKL_UNSET && hard != SKL_UNSET && soft >= hard) { return false; }
    return true;
}

skp_result_t skp_set(skp_store_t *s, uint8_t id, int32_t value)
{
    const skp_def_t *d = skp_find_id(id);
    if (!d) { return SKP_UNKNOWN_ID; }
    if (value < d->min || value > d->max) { return SKP_RANGE; }
    const int32_t old = s->v[id - 1];
    s->v[id - 1] = value;
    if (!relation_ok(s)) {
        s->v[id - 1] = old;
        return SKP_CONSTRAINT;
    }
    return SKP_OK;
}

void skp_cliff_cfg(const skp_store_t *s, skl_cliff_cfg_t *cfg, skl_polarity_t pol)
{
    skl_cliff_default_cfg(cfg);
    cfg->ratio_pct = s->v[SKP_CLIFF_RATIO_PCT - 1];
    cfg->clear_pct = s->v[SKP_CLEAR_RATIO_PCT - 1];
    cfg->pol = pol;
}

void skp_batt_cfg(const skp_store_t *s, skl_batt_cfg_t *cfg)
{
    skl_batt_default_cfg(cfg);
    cfg->low_mv = s->v[SKP_BATT_LOW_MV - 1];
    cfg->crit_mv = s->v[SKP_BATT_CRIT_MV - 1];
}
