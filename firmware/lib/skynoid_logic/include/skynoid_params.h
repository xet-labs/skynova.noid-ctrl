/*
 * skynoid_params.h - the runtime parameter store (set_param ids), pure C.
 *
 * Source: vault 10-specs/config-defaults.yaml "params". Values live in RAM only; the host
 * re-sends them after each HELLO. A default of SKL_UNSET means "unknown until measured":
 * code that needs it must refuse and say so (see skynoid_logic.h).
 *
 * Tests: firmware/test/test_logic
 */
#ifndef SKYNOID_PARAMS_H
#define SKYNOID_PARAMS_H

#include "skynoid_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SKP_COUNT 12

enum {
    SKP_SPEED_CAP_MM_S = 1, SKP_TURN_CAP_DPS = 2, SKP_ACCEL_MM_S2 = 3,
    SKP_CLIFF_RATIO_PCT = 4, SKP_CLEAR_RATIO_PCT = 5,
    SKP_BATT_LOW_MV = 6, SKP_BATT_CRIT_MV = 7,
    SKP_STALL_SOFT_MA = 8, SKP_STALL_HARD_MA = 9,
    SKP_WHEEL_BASE_MM = 10, SKP_DUTY_PER_MM_S_X1000 = 11, SKP_MIN_START_DUTY_PCT = 12
};

typedef struct {
    uint8_t     id;
    const char *name;
    int32_t     def, min, max;      /* def may be SKL_UNSET */
} skp_def_t;

typedef struct { int32_t v[SKP_COUNT]; } skp_store_t;

typedef enum {
    SKP_OK = 0,
    SKP_UNKNOWN_ID,     /* no such parameter */
    SKP_RANGE,          /* outside min..max */
    SKP_CONSTRAINT      /* breaks a relation (clear must be above ratio, crit below low, soft below hard) */
} skp_result_t;

const skp_def_t *skp_find_id(uint8_t id);
const skp_def_t *skp_find_name(const char *name);
const skp_def_t *skp_def_at(size_t index);                /* 0..SKP_COUNT-1, for listing */

void    skp_reset(skp_store_t *s);                        /* all defaults */
int32_t skp_get(const skp_store_t *s, uint8_t id);        /* SKL_UNSET if unset or unknown */
skp_result_t skp_set(skp_store_t *s, uint8_t id, int32_t value);   /* store untouched on failure */

/* Fill the pure-logic configs from the store. */
void skp_cliff_cfg(const skp_store_t *s, skl_cliff_cfg_t *cfg, skl_polarity_t pol);
void skp_batt_cfg(const skp_store_t *s, skl_batt_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_PARAMS_H */
