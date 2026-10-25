/* basectl.c - base node state machine. Pure logic, see skynoid_base.h for the contract. */
#include <string.h>
#include "skynoid_base.h"

#define DEFAULT_ACCEL_MM_S2 500      /* only if the parameter store somehow holds nothing */
#define DEFAULT_MOTION_DT_MS 10
#define NOMINAL_BATT_MV 12000        /* used for the duty scaling when no voltage reading exists */

/* ---- small helpers ----------------------------------------------------- */
static void push_event(skb_t *b, uint8_t id, uint16_t a, uint8_t reason)
{
    if (b->ev_len >= SKB_EVQ_LEN) { b->ev_dropped++; return; }
    skb_event_t *e = &b->evq[(b->ev_head + b->ev_len) % SKB_EVQ_LEN];
    e->id = id; e->a = a; e->b = reason;
    b->ev_len++;
}

static int refuse(skb_t *b, int code, uint16_t detail, const char *reason)
{
    b->last_detail = detail;
    b->last_reason = reason;
    return code;
}

static int accept(skb_t *b)
{
    b->last_detail = 0;
    b->last_reason = "";
    return 0;
}

static bool moving(const skb_t *b)
{
    return b->mb.active || b->cur[0] != 0 || b->cur[1] != 0;
}

/* Drop every motion request and ask for a brake now (the caller applies it, see skb_take_reflex). */
static void stop_motion(skb_t *b)
{
    skl_mb_clear(&b->mb);
    b->cur[0] = b->cur[1] = 0;
    b->reflex = true;
}

static bool all_baselines(const skb_t *b)
{
    for (int i = 0; i < SKB_N_IR; i++) {
        if (!b->cliff[i].have_baseline) { return false; }
    }
    return true;
}

static uint8_t compute_hazard(const skb_t *b)
{
    uint8_t mask = 0;
    for (int i = 0; i < SKB_N_IR; i++) {
        if (b->cliff[i].have_baseline) {
            if (b->cliff[i].active) { mask |= (uint8_t)(1u << i); }
            if (b->cliff[i].fault)  { mask |= SKL_HZ_SENSOR_FAULT; }
        }
    }
    if (b->floor == SKL_FLOOR_UNRELIABLE) { mask |= SKL_HZ_UNRELIABLE; }
    if (b->cfg.cliff_required && !all_baselines(b)) { mask |= SKL_HZ_UNRELIABLE; }   /* fail towards stopped */
    return mask;
}

static void enter_fault(skb_t *b, uint8_t code)
{
    if (b->state == SKB_FAULT) { return; }
    b->state = SKB_FAULT;
    b->fault = code;
    stop_motion(b);
    push_event(b, SKB_EV_NODE_FAULT, code, 0);
}

/* ---- setup --------------------------------------------------------------- */
void skb_cfg_default(skb_cfg_t *cfg)
{
    cfg->pol = SKL_POL_FLOOR_HIGH;
    cfg->no_floor_adc = SKL_UNSET;
    cfg->min_contrast_pct = SKL_UNSET;
    cfg->invert[0] = cfg->invert[1] = false;
    cfg->cliff_required = true;
    cfg->uncal_cap_pct = 40;
    cfg->r_int_mohm = SKL_UNSET;
    cfg->brake_hold_ms = 200;
    cfg->cal_window_ms = 200;
    cfg->stall_soft_ms = 300;
    cfg->stall_hard_ms = 50;
}

void skb_init(skb_t *b, const skp_store_t *params, const skb_cfg_t *cfg)
{
    memset(b, 0, sizeof *b);
    b->params = params;
    b->cfg = *cfg;
    b->state = SKB_SAFE;
    b->floor = SKL_FLOOR_UNKNOWN;
    for (int i = 0; i < SKB_N_IR; i++) { skl_cliff_init(&b->cliff[i]); }
    skp_cliff_cfg(params, &b->ccfg, cfg->pol);
    skl_batt_init(&b->batt);
    skp_batt_cfg(params, &b->bcfg);
    skl_motion_reset(&b->motion);
    skl_mb_clear(&b->mb);
    b->hazard = compute_hazard(b);        /* so the first safety step does not report a "new" hazard */
    b->last_reason = "";
}

/* ---- commands ------------------------------------------------------------ */
int skb_cmd_arm(skb_t *b)
{
    if (b->state == SKB_ESTOP) { return refuse(b, SKB_NACK_ESTOPPED, 0, "estop latched, send clear_estop"); }
    if (b->state == SKB_FAULT) { return refuse(b, SKB_NACK_BUSY, 0, "node fault"); }
    if (b->state == SKB_SAFE) {
        skl_mb_clear(&b->mb);
        b->cur[0] = b->cur[1] = 0;
        b->state = SKB_ARMED;
    }
    return accept(b);
}

int skb_cmd_disarm(skb_t *b)
{
    if (b->state == SKB_ARMED) {
        b->state = SKB_SAFE;
        stop_motion(b);
    }
    b->cal_active = false;
    return accept(b);
}

int skb_cmd_estop(skb_t *b)
{
    if (b->state != SKB_FAULT) { b->state = SKB_ESTOP; }
    b->cal_active = false;
    stop_motion(b);
    return accept(b);
}

int skb_cmd_clear_estop(skb_t *b)
{
    if (b->state == SKB_ESTOP) {
        b->state = SKB_SAFE;
    } else if (b->state == SKB_FAULT && b->fault == SKB_FAULT_STALL_HARD) {
        b->state = SKB_SAFE;
        b->fault = SKB_FAULT_NONE;
        b->stall_hard_on = b->stall_soft_on = b->stall_reported = false;
    }
    return accept(b);
}

/* Commands that move or measure need ARMED; say why when they are not. */
static int require_armed(skb_t *b)
{
    switch (b->state) {
    case SKB_ARMED: return 0;
    case SKB_ESTOP: return refuse(b, SKB_NACK_ESTOPPED, 0, "estop latched");
    case SKB_FAULT: return refuse(b, SKB_NACK_BUSY, 0, "node fault");
    default:        return refuse(b, SKB_NACK_NOT_ARMED, 0, "arm first");
    }
}

int skb_cmd_drive(skb_t *b, int32_t v, int32_t w, uint32_t ttl_ms, uint32_t now_ms)
{
    int rc = require_armed(b);
    if (rc != 0) { return rc; }
    if (ttl_ms < 1 || ttl_ms > 2000) { return refuse(b, SKB_NACK_INVALID_ARG, 2, "ttl must be 1..2000 ms"); }

    skl_cap_command(&v, &w, skp_get(b->params, SKP_SPEED_CAP_MM_S), skp_get(b->params, SKP_TURN_CAP_DPS));
    if (!skl_hazard_allows(b->hazard, v, w)) {          /* safety first: a hazard answer beats an argument answer */
        return refuse(b, SKB_NACK_HAZARD_BLOCKED, b->hazard, "hazard blocks this motion");
    }
    if (w != 0) {
        const int32_t base = skp_get(b->params, SKP_WHEEL_BASE_MM);
        if (base == SKL_UNSET || base <= 0) {
            return refuse(b, SKB_NACK_INVALID_ARG, 1, "wheel_base_mm is unset: measure it, then param set");
        }
    }
    skl_mb_put(&b->mb, v, w, ttl_ms, now_ms);
    return accept(b);
}

int skb_cmd_stop(skb_t *b)
{
    int rc = require_armed(b);
    if (rc != 0) { return rc; }
    stop_motion(b);
    return accept(b);
}

int skb_cmd_cal_floor(skb_t *b, uint32_t now_ms)
{
    int rc = require_armed(b);
    if (rc != 0) { return rc; }
    if (moving(b)) { return refuse(b, SKB_NACK_BUSY, 0, "stop first, the robot must stand still"); }
    if (b->cfg.no_floor_adc == SKL_UNSET || b->cfg.min_contrast_pct == SKL_UNSET) {
        return refuse(b, SKB_NACK_INVALID_ARG, 0, "no_floor_adc / min_contrast_pct unset (step 3.1 measurements)");
    }
    if (!b->have_ir) { return refuse(b, SKB_NACK_BUSY, 0, "no IR readings yet"); }
    b->cal_active = true;
    b->cal_end_ms = now_ms + b->cfg.cal_window_ms;
    b->cal_n = 0;
    for (int i = 0; i < SKB_N_IR; i++) { b->cal_sum[i] = 0; }
    return accept(b);
}

void skb_link_lost(skb_t *b)
{
    if (b->state != SKB_ARMED) { return; }
    b->state = SKB_SAFE;
    b->cal_active = false;
    stop_motion(b);
    push_event(b, SKB_EV_LINK_LOST, 0, 0);
}

/* ---- safety side --------------------------------------------------------- */
static void finish_calibration(skb_t *b)
{
    const int64_t n = b->cal_n ? (int64_t)b->cal_n : 1;
    skl_floor_t worst = SKL_FLOOR_OK;
    for (int i = 0; i < SKB_N_IR; i++) {
        const int32_t base = (int32_t)(b->cal_sum[i] / n);
        skl_cliff_init(&b->cliff[i]);
        skl_cliff_set_baseline(&b->cliff[i], base);
        if (skl_floor_check(base, b->cfg.no_floor_adc, b->cfg.min_contrast_pct) != SKL_FLOOR_OK) {
            worst = SKL_FLOOR_UNRELIABLE;
        }
    }
    b->floor = worst;
    b->cal_active = false;
}

static void watch_stall(skb_t *b, int32_t ma, uint32_t now_ms)
{
    const int32_t soft = skp_get(b->params, SKP_STALL_SOFT_MA);
    const int32_t hard = skp_get(b->params, SKP_STALL_HARD_MA);
    if (b->state != SKB_ARMED || ma <= 0) {
        b->stall_soft_on = b->stall_hard_on = b->stall_reported = false;
        return;
    }
    if (soft != SKL_UNSET) {                          /* unset = guard off, never guess a limit */
        if (ma > soft) {
            if (!b->stall_soft_on) { b->stall_soft_on = true; b->stall_soft_start_ms = now_ms; }
            else if (!b->stall_reported && (uint32_t)(now_ms - b->stall_soft_start_ms) >= b->cfg.stall_soft_ms) {
                b->stall_reported = true;
                push_event(b, SKB_EV_STALL, (uint16_t)skl_clamp(ma, 0, 65535), 0);
                stop_motion(b);
            }
        } else {
            b->stall_soft_on = b->stall_reported = false;
        }
    }
    if (hard != SKL_UNSET) {
        if (ma > hard) {
            if (!b->stall_hard_on) { b->stall_hard_on = true; b->stall_hard_start_ms = now_ms; }
            else if ((uint32_t)(now_ms - b->stall_hard_start_ms) >= b->cfg.stall_hard_ms) {
                enter_fault(b, SKB_FAULT_STALL_HARD);
            }
        } else {
            b->stall_hard_on = false;
        }
    }
}

void skb_safety_step(skb_t *b, const int32_t ir[SKB_N_IR], int32_t batt_mv, int32_t motor_ma,
                     uint32_t now_ms)
{
    const uint8_t old_mask = b->hazard;
    skp_cliff_cfg(b->params, &b->ccfg, b->cfg.pol);
    skp_batt_cfg(b->params, &b->bcfg);
    b->batt_mv = batt_mv;
    b->motor_ma = motor_ma;

    for (int i = 0; i < SKB_N_IR; i++) { b->ir[i] = ir[i]; }
    b->have_ir = true;

    if (b->cal_active) {
        for (int i = 0; i < SKB_N_IR; i++) { b->cal_sum[i] += ir[i]; }
        b->cal_n++;
        if (skl_time_reached(now_ms, b->cal_end_ms)) { finish_calibration(b); }
    }

    for (int i = 0; i < SKB_N_IR; i++) {
        if (!b->cliff[i].have_baseline) { continue; }
        if (skl_cliff_step(&b->cliff[i], &b->ccfg, ir[i], now_ms) == SKL_CLIFF_FAULT) {
            push_event(b, SKB_EV_SENSOR_FAULT, (uint16_t)i, 2);       /* only the rail check can fault */
        }
    }

    b->hazard = compute_hazard(b);
    const uint8_t added = (uint8_t)(b->hazard & ~old_mask & (SKL_HZ_FRONT_ANY | SKL_HZ_REAR_ANY | SKL_HZ_UNRELIABLE));
    const uint8_t gone = (uint8_t)(old_mask & ~b->hazard);
    if (added) { push_event(b, SKB_EV_CLIFF, b->hazard, 0); }
    if (gone)  { push_event(b, SKB_EV_HAZARD_CLEARED, b->hazard, 0); }

    /* R1: a hazard that appears while moving brakes at once, without waiting for the motion task. */
    if (b->state == SKB_ARMED && b->hazard != 0) {
        bool bad = !skl_hazard_allows(b->hazard, (b->cur[0] + b->cur[1]) / 2, b->cur[1] - b->cur[0]);
        if (b->mb.active && !skl_hazard_allows(b->hazard, b->mb.v, b->mb.w)) { bad = true; }
        if (bad) { stop_motion(b); }
    }

    /* R6 battery */
    int32_t mv = batt_mv;
    if (b->cfg.r_int_mohm != SKL_UNSET && mv > 0 && motor_ma > 0) {
        mv = skl_vcomp_mv(mv, motor_ma, b->cfg.r_int_mohm);
    }
    const skl_batt_event_t be = skl_batt_step(&b->batt, &b->bcfg, mv, now_ms);
    if (be == SKL_BATT_EV_LOW) {
        push_event(b, SKB_EV_BATT_LOW, (uint16_t)skl_clamp(mv, 0, 65535), 0);
    } else if (be == SKL_BATT_EV_CRIT) {
        push_event(b, SKB_EV_BATT_CRIT, (uint16_t)skl_clamp(mv, 0, 65535), 0);
        enter_fault(b, SKB_FAULT_BATT_CRIT);
    }

    watch_stall(b, motor_ma, now_ms);
}

bool skb_take_reflex(skb_t *b)
{
    const bool r = b->reflex;
    b->reflex = false;
    return r;
}

/* ---- motion side --------------------------------------------------------- */
void skb_motion_step(skb_t *b, uint32_t now_ms, skl_motion_out_t *out)
{
    static const int32_t zero[2] = { 0, 0 };

    if (b->state == SKB_FAULT) {                       /* outputs off, EN low (coast) */
        skl_mb_clear(&b->mb);
        b->cur[0] = b->cur[1] = 0;
        skl_motion_reset(&b->motion);
        out->duty_permille[0] = out->duty_permille[1] = 0;
        out->en = false;
        out->brake = false;
        return;
    }
    if (b->state != SKB_ARMED) {                       /* SAFE and ESTOP: brake window, then coast */
        skl_mb_clear(&b->mb);
        b->cur[0] = b->cur[1] = 0;
        skl_motion_step(&b->motion, zero, now_ms, b->cfg.brake_hold_ms, out);
        return;
    }

    const uint32_t dt = b->have_last_motion ? (uint32_t)(now_ms - b->last_motion_ms) : DEFAULT_MOTION_DT_MS;
    b->last_motion_ms = now_ms;
    b->have_last_motion = true;

    int32_t v = 0, w = 0, target[2] = { 0, 0 };
    const skl_mb_state_t st = skl_mb_get(&b->mb, now_ms, &v, &w);
    if (st == SKL_MB_EXPIRED) {                        /* R2: brake now, no ramp (design 02-base-node, drive step 8) */
        push_event(b, SKB_EV_DRIVE_EXPIRED, 0, 0);
        b->cur[0] = b->cur[1] = 0;
    }
    if (st == SKL_MB_ACTIVE) {
        if (!skl_hazard_allows(b->hazard, v, w)) {
            skl_mb_clear(&b->mb);                      /* the safety task usually got here first */
        } else if (w == 0) {
            target[0] = target[1] = v;
        } else if (!skl_mix(v, w, skp_get(b->params, SKP_WHEEL_BASE_MM), &target[0], &target[1])) {
            skl_mb_clear(&b->mb);                      /* base unset after the command was accepted */
        }
    }

    int32_t accel = skp_get(b->params, SKP_ACCEL_MM_S2);
    if (accel == SKL_UNSET || accel <= 0) { accel = DEFAULT_ACCEL_MM_S2; }
    int32_t duty[2];
    const int32_t batt = b->batt_mv > 0 ? b->batt_mv : NOMINAL_BATT_MV;
    for (int i = 0; i < 2; i++) {
        b->cur[i] = skl_ramp(b->cur[i], target[i], accel, dt);
        (void)skl_duty_permille(b->cur[i], skp_get(b->params, SKP_DUTY_PER_MM_S_X1000), batt,
                                skp_get(b->params, SKP_MIN_START_DUTY_PCT),
                                skp_get(b->params, SKP_SPEED_CAP_MM_S), b->cfg.uncal_cap_pct, &duty[i]);
        if (b->cfg.invert[i]) { duty[i] = -duty[i]; }
    }
    skl_motion_step(&b->motion, duty, now_ms, b->cfg.brake_hold_ms, out);
}

/* ---- reading state -------------------------------------------------------- */
bool skb_pop_event(skb_t *b, skb_event_t *ev)
{
    if (b->ev_len == 0) { return false; }
    *ev = b->evq[b->ev_head];
    b->ev_head = (uint8_t)((b->ev_head + 1) % SKB_EVQ_LEN);
    b->ev_len--;
    return true;
}

skb_state_t skb_state(const skb_t *b)      { return b->state; }
uint8_t skb_hazard(const skb_t *b)         { return b->hazard; }
uint16_t skb_last_detail(const skb_t *b)   { return b->last_detail; }
const char *skb_last_reason(const skb_t *b){ return b->last_reason; }
