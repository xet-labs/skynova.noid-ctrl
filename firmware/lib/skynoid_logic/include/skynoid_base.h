/*
 * skynoid_base.h - the base node's state machine, as pure C (no ESP-IDF, no FreeRTOS).
 *
 * WHAT THIS FILE IS
 *   Glue between the single-purpose helpers in skynoid_logic.h and the two real-time
 *   tasks. It owns the arm / estop / fault state, the drive mailbox, the four cliff
 *   sensors, the battery watcher, the stall guard and a small event queue. It touches no
 *   hardware: the safety task feeds it readings, the motion task asks it for wheel duties,
 *   and the command handlers (framed API and console) call the skb_cmd_* functions.
 *
 * HOW THE TASKS USE IT (src/base.c wires this; see docs/steps/step-2.x)
 *   safety task, every SAFETY_LOOP_MS (5 ms):  skb_safety_step(...), then if skb_take_reflex()
 *       returns true call hal_motor_brake_now() RIGHT THERE (no queue), then drain events.
 *   motion task, every MOTION_LOOP_MS (10 ms): skb_motion_step(...) -> hal_motor_apply(&out).
 *   link task: skb_link_lost() when the 500 ms PING timer runs out.
 *   Both tasks hold one mutex around every call (the "base lock"); every function here is short.
 *
 * RETURN VALUES of skb_cmd_*: 0 = accepted, otherwise a NACK code (SKB_NACK_*, protocol-v0
 *   table 7). skb_last_detail() gives the NACK detail (for hazard_blocked: the hazard mask)
 *   and skb_last_reason() a short human reason for the console ("ERR invalid_arg <reason>").
 *
 * CHOICES THAT GO BEYOND THE WORDING OF THE SPEC (also logged in the handoff):
 *   - Cliff protection needs a floor baseline from cal_floor. While none exists and
 *     cfg.cliff_required is true (the default) every motion is refused with hazard bit
 *     SKL_HZ_UNRELIABLE ("fail towards stopped"). Bench builds with the wheels in the air set
 *     cliff_required = false and then the guard only runs once a baseline exists.
 *   - disarm and clear_estop and estop are accepted in every state, as are arm and the
 *     host-only commands; every motion command while not armed answers not_armed.
 *   - A latched estop survives disarm. clear_estop also clears a hard-stall fault.
 *   - Battery CRIT is latched until power cycle (state FAULT, code SKB_FAULT_BATT_CRIT).
 *
 * TESTS: firmware/test/test_base
 */
#ifndef SKYNOID_BASE_H
#define SKYNOID_BASE_H

#include "skynoid_logic.h"
#include "skynoid_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Same numbers as app_config.h (ST_*, NACK_*, protocol-v0); src/base.c asserts they match. */
typedef enum { SKB_BOOT = 0, SKB_SAFE = 1, SKB_ARMED = 2, SKB_ESTOP = 3, SKB_FAULT = 4 } skb_state_t;
enum { SKB_NACK_NOT_ARMED = 3, SKB_NACK_HAZARD_BLOCKED = 4, SKB_NACK_ESTOPPED = 5,
       SKB_NACK_INVALID_ARG = 6, SKB_NACK_BUSY = 7 };
enum { SKB_FAULT_NONE = 0, SKB_FAULT_BATT_CRIT = 1, SKB_FAULT_STALL_HARD = 2 };

/* SENSOR_FAULT event (id 10): payload a = sensor index, b = reason. Indexes 0..3 are the IR sensors
 * (reasons 1 stuck, 2 saturated, from the cliff logic). The battery sensor (INA219) is index 4 and
 * reports reason 3 = no reading (added 2026-10-26, claude-chat_a7d3; no new message id or field). */
enum { SKB_SENSOR_BATT = 4 };
/* Touch pads (step F, 2026-10-28, claude-chat_a7d3): a pad that stays down too long is reported as SENSOR_FAULT with
 * sensor = SKB_SENSOR_TOUCH0 + zone (5 head, 6 hand_l, 7 hand_r, 8 extra) and reason 1 (stuck). */
enum { SKB_SENSOR_TOUCH0 = 5 };
enum { SKB_SENSOR_REASON_STUCK = 1, SKB_SENSOR_REASON_SATURATED = 2, SKB_SENSOR_REASON_NO_READING = 3 };

/* Event ids = the EVT msg ids of protocol-v0 section 6. */
enum { SKB_EV_CLIFF = 1, SKB_EV_HAZARD_CLEARED = 2, SKB_EV_BATT_LOW = 3, SKB_EV_BATT_CRIT = 4,
       SKB_EV_DRIVE_EXPIRED = 5, SKB_EV_STALL = 6, SKB_EV_TOUCH = 7, SKB_EV_NODE_FAULT = 8,
       SKB_EV_LINK_LOST = 9, SKB_EV_SENSOR_FAULT = 10 };   /* SKB_EV_TOUCH: a = zone, b = 1 down / 0 up; not made by the base logic, posted by src/touch.c */

typedef struct {
    uint8_t  id;      /* SKB_EV_* */
    uint16_t a;       /* mask, mV, mA, fault code or sensor index, depending on id */
    uint8_t  b;       /* sensor_fault reason (1 stuck, 2 saturated); else 0 */
} skb_event_t;

#define SKB_EVQ_LEN 16
#define SKB_N_IR 4                         /* front-left, front-right, rear-left, rear-right */

typedef struct {
    skl_polarity_t pol;
    int32_t  no_floor_adc;                 /* SKL_UNSET until measured (step 3.1) */
    int32_t  min_contrast_pct;             /* SKL_UNSET until measured */
    bool     invert[2];                    /* wheel direction fix: left, right */
    bool     cliff_required;               /* see header comment */
    int32_t  uncal_cap_pct;                /* duty ceiling until duty_per_mm_s_x1000 is calibrated */
    int32_t  r_int_mohm;                   /* pack internal resistance for V_comp; SKL_UNSET = off */
    uint32_t brake_hold_ms;
    uint32_t cal_window_ms;                /* baseline averaging window (200) */
    uint32_t stall_soft_ms, stall_hard_ms; /* 300 and 50 */
    uint32_t batt_stale_ms;                /* no battery reading for this long = sensor fault (hazard bit 4, no
                                            * motion). 0 = check off, which is the library default so the old
                                            * tests keep their meaning; src/base.c sets SKYNOID_BATT_STALE_MS. */
} skb_cfg_t;

typedef struct {
    const skp_store_t *params;             /* owned by the caller; read on every step */
    skb_cfg_t          cfg;
    skb_state_t        state;
    uint8_t            fault;
    uint8_t            hazard;             /* SKL_HZ_* mask, the value for PONG and TLM */

    skl_cliff_t        cliff[SKB_N_IR];
    skl_cliff_cfg_t    ccfg;
    skl_floor_t        floor;
    int32_t            ir[SKB_N_IR];       /* latest readings */
    bool               have_ir;
    bool               cal_active;         /* collecting a baseline */
    uint32_t           cal_end_ms, cal_n;
    int64_t            cal_sum[SKB_N_IR];

    skl_batt_t         batt;
    skl_batt_cfg_t     bcfg;
    int32_t            batt_mv, motor_ma;
    bool               have_batt_ref;      /* batt_ref_ms is valid (first safety step seen) */
    bool               batt_missing;       /* latched while no reading for cfg.batt_stale_ms; clears on a reading */
    uint32_t           batt_ref_ms;        /* time of the last good reading, or of the first safety step */

    skl_mailbox_t      mb;
    skl_motion_t       motion;
    int32_t            cur[2];             /* ramped wheel speeds, mm/s, left then right */
    uint32_t           last_motion_ms;
    bool               have_last_motion;
    bool               reflex;             /* brake was forced by the safety side; take once */
    uint32_t           stall_soft_start_ms, stall_hard_start_ms;
    bool               stall_soft_on, stall_hard_on, stall_reported;

    skb_event_t        evq[SKB_EVQ_LEN];
    uint8_t            ev_head, ev_len;
    uint16_t           ev_dropped;

    uint16_t           last_detail;
    const char        *last_reason;
} skb_t;

void skb_cfg_default(skb_cfg_t *cfg);
void skb_init(skb_t *b, const skp_store_t *params, const skb_cfg_t *cfg);

/* ---- commands (same handlers for the framed API and the console) ---- */
int skb_cmd_arm(skb_t *b);
int skb_cmd_disarm(skb_t *b);
int skb_cmd_estop(skb_t *b);
int skb_cmd_clear_estop(skb_t *b);
int skb_cmd_drive(skb_t *b, int32_t v_mm_s, int32_t w_dps, uint32_t ttl_ms, uint32_t now_ms);
int skb_cmd_stop(skb_t *b);
int skb_cmd_cal_floor(skb_t *b, uint32_t now_ms);   /* returns at once; the baseline is ready cal_window_ms later */

/* link lost (R3): while armed, brake, go SAFE and queue the event. Any other state: nothing. */
void skb_link_lost(skb_t *b);

/* ---- periodic work ---- */
/* Safety task. ir: four filtered ADC readings; batt_mv: 0 if unknown; motor_ma: 0 if not measured. */
void skb_safety_step(skb_t *b, const int32_t ir[SKB_N_IR], int32_t batt_mv, int32_t motor_ma,
                     uint32_t now_ms);
/* True once after the safety side forced a brake; the caller must brake the hardware immediately. */
bool skb_take_reflex(skb_t *b);
/* Motion task. Wheel duties and the EN/brake state for this cycle. */
void skb_motion_step(skb_t *b, uint32_t now_ms, skl_motion_out_t *out);

/* ---- reading state ---- */
bool skb_pop_event(skb_t *b, skb_event_t *ev);
skb_state_t skb_state(const skb_t *b);
uint8_t skb_hazard(const skb_t *b);
uint16_t skb_last_detail(const skb_t *b);
const char *skb_last_reason(const skb_t *b);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_BASE_H */
