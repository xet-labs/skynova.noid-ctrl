/*
 * base.h - the base node as the rest of the firmware sees it.
 *
 * WHAT THIS IS
 *   One small API in front of lib/skynoid_logic/skynoid_base.h plus the real-time tasks. The framed
 *   dispatcher (CMD frames) and the console call the SAME base_cmd_* functions, so caps, TTL, hazard rule
 *   and e-stop apply unchanged (sprint-steps 1.3 "same code path rule").
 *
 * RETURN VALUES  base_cmd_*: 0 = accepted, otherwise a NACK code from app_config.h (NACK_*).
 *   After a refusal base_last_detail() is the NACK detail (hazard_blocked: the mask) and
 *   base_last_reason() a short text for the console. Both describe the most recent command only.
 *
 * START  app_main calls hal_motor_init() FIRST, then base_start() once. base_start() initialises the
 *   ADC, I2C and indicators, then creates three tasks: safety (5 ms), motion (10 ms), power (50 ms).
 */
#ifndef BASE_H
#define BASE_H

#include <stdbool.h>
#include <stdint.h>

#include "skynoid_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  hazard;          /* SKL_HZ_* mask */
    uint8_t  state;           /* ST_* (app_config.h) */
    uint16_t range_mm;        /* 0 until the VL53L0X driver exists (step 3.5) */
    uint8_t  range_status;    /* 0 ok, 1 out of range, 2 error (2 = no sensor yet) */
    uint16_t batt_mv;         /* 0 = no reading */
    uint16_t ir_raw[4];       /* front-left, front-right, rear-left, rear-right */
    uint16_t motor_ma;        /* 0 until SKYNOID_IS_MV_PER_AMP is calibrated */
} base_status_t;

typedef void (*base_event_fn)(const skb_event_t *ev);

void base_start(void);

int base_cmd_arm(void);
int base_cmd_disarm(void);
int base_cmd_estop(void);
int base_cmd_clear_estop(void);
int base_cmd_drive(int32_t v_mm_s, int32_t w_dps, uint32_t ttl_ms);
int base_cmd_stop(void);
int base_cmd_cal_floor(void);

uint16_t    base_last_detail(void);
const char *base_last_reason(void);

void base_link_lost(void);                    /* the link task calls this when the 500 ms PING timer runs out */
void base_get_status(base_status_t *out);
uint8_t base_hazard(void);                    /* for PONG */

/* Parameters (set_param ids of config-defaults.yaml). Return an skp_result_t (0 = OK). */
int     base_param_set(uint8_t id, int32_t value);
int32_t base_param_get(uint8_t id);
void    base_params_reset(void);              /* maintenance mode exit */

/* Events (cliff, batt_low, drive_expired, ...) are handed to this sink from the safety task. The sink must
 * not block (push to the TX queue and return). With no sink set they are dropped after being logged. */
void base_set_event_sink(base_event_fn fn);

#ifdef __cplusplus
}
#endif
#endif /* BASE_H */
