/*
 * base.c - the base node's real-time tasks and the "base lock".
 *
 * Purpose : run lib/skynoid_logic/skynoid_base.h on the hardware. Three tasks:
 *             safety (5 ms)  read IR + motor current, run the cliff/battery/stall logic, brake by reflex
 *             motion (10 ms) ask the logic for wheel duties and apply them
 *             power  (50 ms) read the INA219 battery voltage, 1 s average
 *           One mutex (the base lock) protects the logic state; every critical section is short.
 * Called by: main.c (base_start), the dispatcher and the console (base_cmd_*).
 * Test    : the logic is tested on the PC (test_base). The glue is only compile-checked here.
 *           On the board: after boot "status" should show state safe; hazard 0x20 until cal_floor.
 * Spec    : vault 04-firmware/02-base-node.md, 02-system/04-safety-and-reflexes.md (R1, R2, R3, R5, R6).
 *
 * Reflex rule: when the safety side forces a brake (cliff, stall, estop, link loss) this file calls
 * hal_motor_brake_now() from the safety task itself, with no queue in between.
 */
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "base.h"
#include "board/board_pins.h"
#include "hal.h"
#include "skynoid_params.h"
#include "touch.h"

static const char *TAG = "base";

/* The logic library and app_config.h must agree on the numbers that go on the wire. */
_Static_assert((int)SKB_SAFE == (int)ST_SAFE && (int)SKB_ARMED == (int)ST_ARMED &&
               (int)SKB_ESTOP == (int)ST_ESTOP && (int)SKB_FAULT == (int)ST_FAULT, "state ids differ");
_Static_assert((int)SKB_NACK_NOT_ARMED == (int)NACK_NOT_ARMED && (int)SKB_NACK_HAZARD_BLOCKED == (int)NACK_HAZARD_BLOCKED &&
               (int)SKB_NACK_ESTOPPED == (int)NACK_ESTOPPED && (int)SKB_NACK_INVALID_ARG == (int)NACK_INVALID_ARG &&
               (int)SKB_NACK_BUSY == (int)NACK_BUSY, "nack codes differ");

#define PRIO_SAFETY 22
#define PRIO_MOTION 20
#define PRIO_POWER   5
#define LOW_BATT_REPEAT_MS 30000u
#define ADC_OVERSAMPLE 4
#define ADC_FULL_SCALE_MV 3100           /* 12 dB attenuation, nominal; the ESP32 ADC is not linear: #verify */

static skp_store_t s_params;
static skb_t s_base;
static SemaphoreHandle_t s_lock;
static base_event_fn s_sink;
static int32_t s_ir_hist[4][3];
static uint8_t s_ir_pos;
static int32_t s_batt_avg_mv;            /* 0 = no reading yet */
static int s_led_last = -1;
static uint32_t s_low_batt_next_ms;
static bool s_low_batt_active;           /* LOW and still LOW: repeat the low-battery beep */
static bool s_crit_batt_active;          /* CRIT: BUZ_CRITICAL runs until power cycle; no other beep may replace it */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* ---- sensing --------------------------------------------------------------------- */
static int32_t read_oversampled(int channel)
{
    int32_t sum = 0;
    for (int i = 0; i < ADC_OVERSAMPLE; i++) {
        const int r = hal_adc_read(channel);
        if (r < 0) { return 0; }         /* a dead ADC reads as 0 = rail: the cliff logic turns that into a sensor fault */
        sum += r;
    }
    return sum / ADC_OVERSAMPLE;
}

static void read_ir(int32_t out[4])
{
    static const int channels[4] = { ADC_CH_IR_FRONT_LEFT, ADC_CH_IR_FRONT_RIGHT,
                                     ADC_CH_IR_REAR_LEFT, ADC_CH_IR_REAR_RIGHT };
    for (int i = 0; i < 4; i++) { s_ir_hist[i][s_ir_pos] = read_oversampled(channels[i]); }
    s_ir_pos = (uint8_t)((s_ir_pos + 1) % 3);
    for (int i = 0; i < 4; i++) {
        out[i] = skl_median3(s_ir_hist[i][0], s_ir_hist[i][1], s_ir_hist[i][2]);    /* spike removal */
    }
}

static int32_t read_motor_ma(void)
{
    const int32_t mv_per_amp = SKYNOID_IS_MV_PER_AMP;
    if (mv_per_amp == SKL_UNSET || mv_per_amp <= 0) { return 0; }
    const int32_t counts = read_oversampled(ADC_CH_MOTOR_IS);
    const int32_t mv = counts * ADC_FULL_SCALE_MV / 4095;
    return mv * 1000 / mv_per_amp;
}

/* ---- tasks ------------------------------------------------------------------------- */
static void deliver_events(void)
{
    skb_event_t ev;
    for (;;) {
        lock();
        const bool got = skb_pop_event(&s_base, &ev);
        unlock();
        if (!got) { return; }
        ESP_LOGI(TAG, "event %u a=%u b=%u", (unsigned)ev.id, (unsigned)ev.a, (unsigned)ev.b);
        if (ev.id == SKB_EV_BATT_LOW) {                       /* R6: local, even when the Pi is gone */
            s_low_batt_active = true;
            s_low_batt_next_ms = now_ms() + LOW_BATT_REPEAT_MS;
            hal_buzzer_play(BUZ_LOW_BATTERY);
        } else if (ev.id == SKB_EV_BATT_CRIT) {
            s_low_batt_active = false;
            s_crit_batt_active = true;
            hal_servo_request_off();                              /* servos are loads too: off, same as the motors */
            hal_buzzer_play(BUZ_CRITICAL);                    /* 1 s on / 1 s off, never ends by itself */
        } else if (ev.id == SKB_EV_NODE_FAULT && ev.a != SKB_FAULT_BATT_CRIT) {
            if (!s_crit_batt_active) { hal_buzzer_play(BUZ_ERROR); }   /* an error beep would cut the critical alarm */
        } else if (ev.id == SKB_EV_SENSOR_FAULT && ev.a == SKB_SENSOR_BATT) {
            if (!s_crit_batt_active) { hal_buzzer_play(BUZ_ERROR); }   /* battery sensor gone: no protection, say so once */
        }
        if (s_sink) { s_sink(&ev); }
    }
}

static void update_led(void)
{
    int mode = LED_SLOW;
    switch (s_base.state) {
    case SKB_ARMED: mode = LED_SOLID; break;
    case SKB_ESTOP:
    case SKB_FAULT: mode = LED_FAST; break;
    default: break;
    }
    if (mode != s_led_last) { s_led_last = mode; hal_led_mode(mode); }
}

static void safety_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    int32_t ir[4];
    for (;;) {
        read_ir(ir);
        const int32_t ma = read_motor_ma();
        lock();
        skb_safety_step(&s_base, ir, s_batt_avg_mv, ma, now_ms());
        if (skb_take_reflex(&s_base)) { hal_motor_brake_now(); }      /* R1 / R3 / R5: no queue, no host */
        update_led();
        const bool batt_ok = (s_base.batt.state == SKL_BATT_OK);
        unlock();
        deliver_events();
        if (batt_ok) { s_low_batt_active = false; }          /* pack recovered above low + hysteresis: stop beeping */
        if (s_low_batt_active && (int32_t)(now_ms() - s_low_batt_next_ms) >= 0) {
            s_low_batt_next_ms = now_ms() + LOW_BATT_REPEAT_MS;
            hal_buzzer_play(BUZ_LOW_BATTERY);
        }
        const TickType_t ticks = pdMS_TO_TICKS(SAFETY_LOOP_MS);
        vTaskDelayUntil(&last, ticks > 0 ? ticks : 1);
    }
}

static void motion_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    skl_motion_out_t out;
    for (;;) {
        lock();
        skb_motion_step(&s_base, now_ms(), &out);
        hal_motor_apply(&out);
        unlock();
        const TickType_t ticks = pdMS_TO_TICKS(MOTION_LOOP_MS);
        vTaskDelayUntil(&last, ticks > 0 ? ticks : 1);
    }
}

static void power_task(void *arg)
{
    (void)arg;
    int32_t sum = 0;
    int n = 0;
    int fails = 0;                                                /* consecutive failed or zero reads */
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        int32_t mv;
        if (hal_ina219_read_mv(&mv) && mv > 0) {
            fails = 0;
            sum += mv;
            if (++n >= 20) {                                      /* 20 x 50 ms = 1 s average */
                lock();
                s_batt_avg_mv = sum / n;
                unlock();
                sum = 0;
                n = 0;
            }
        } else if (++fails >= 20) {                               /* 1 s without a good read: forget the old average, */
            fails = 0;                                            /* so a sensor that died mid-run cannot keep a stale */
            sum = 0;                                              /* healthy voltage alive (the logic then sees 0 = no reading) */
            n = 0;
            lock();
            s_batt_avg_mv = 0;
            unlock();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
    }
}

/* ---- start ------------------------------------------------------------------------- */
void base_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    skp_reset(&s_params);
#ifdef SKYNOID_TEST_BATT_LOW_MV     /* bench test of the beeps without draining the pack: see app_config.h */
    if (skp_set(&s_params, SKP_BATT_LOW_MV, SKYNOID_TEST_BATT_LOW_MV) != SKP_OK) {
        ESP_LOGW(TAG, "SKYNOID_TEST_BATT_LOW_MV rejected (range 9000..12600, must stay above crit)");
    }
#endif
#ifdef SKYNOID_TEST_BATT_CRIT_MV
    if (skp_set(&s_params, SKP_BATT_CRIT_MV, SKYNOID_TEST_BATT_CRIT_MV) != SKP_OK) {
        ESP_LOGW(TAG, "SKYNOID_TEST_BATT_CRIT_MV rejected (range 8500..12000, must stay below low)");
    }
#endif
    skb_cfg_t cfg;
    skb_cfg_default(&cfg);
    cfg.pol = (skl_polarity_t)SKYNOID_IR_POLARITY;
    cfg.no_floor_adc = SKYNOID_NO_FLOOR_ADC;
    cfg.min_contrast_pct = SKYNOID_MIN_CONTRAST_PCT;
    cfg.invert[0] = SKYNOID_LEFT_INVERT;
    cfg.invert[1] = SKYNOID_RIGHT_INVERT;
    cfg.cliff_required = SKYNOID_CLIFF_REQUIRED;
    cfg.uncal_cap_pct = UNCAL_DUTY_CAP_PCT;
    cfg.brake_hold_ms = BRAKE_HOLD_MS;
    cfg.batt_stale_ms = SKYNOID_BATT_STALE_MS;
    skb_init(&s_base, &s_params, &cfg);

    hal_adc_init();
    hal_i2c_init();
    hal_indicator_init();
    hal_range_start();                                            /* VL53L0X task: status 2 until the sensor answers */
    hal_servo_start();                                            /* PCA9685 task; does nothing in a locked build */
    touch_start();                                                /* touch pads: EVT touch to the host during a session */

    xTaskCreate(power_task, "power", 3072, NULL, PRIO_POWER, NULL);
    xTaskCreate(motion_task, "motion", 3072, NULL, PRIO_MOTION, NULL);
    xTaskCreate(safety_task, "safety", 4096, NULL, PRIO_SAFETY, NULL);
    ESP_LOGI(TAG, "base started: cliff_required=%d", (int)SKYNOID_CLIFF_REQUIRED);
}

/* ---- command handlers (same ones for the framed API and the console) ------------------- */
#define LOCKED(call) do { lock(); const int rc_ = (call); \
    const bool refl_ = skb_take_reflex(&s_base); if (refl_) { hal_motor_brake_now(); } \
    unlock(); return rc_; } while (0)

int base_cmd_arm(void)        { LOCKED(skb_cmd_arm(&s_base)); }
int base_cmd_disarm(void)     { LOCKED(skb_cmd_disarm(&s_base)); }
int base_cmd_estop(void)      { hal_servo_request_off(); LOCKED(skb_cmd_estop(&s_base)); }   /* servos off first: no lock, instant */
int base_cmd_clear_estop(void){ LOCKED(skb_cmd_clear_estop(&s_base)); }
int base_cmd_stop(void)       { LOCKED(skb_cmd_stop(&s_base)); }
int base_cmd_cal_floor(void)  { LOCKED(skb_cmd_cal_floor(&s_base, now_ms())); }
int base_cmd_drive(int32_t v, int32_t w, uint32_t ttl_ms)
{
    LOCKED(skb_cmd_drive(&s_base, v, w, ttl_ms, now_ms()));
}

uint16_t base_last_detail(void)
{
    lock(); const uint16_t d = skb_last_detail(&s_base); unlock();
    return d;
}

const char *base_last_reason(void)
{
    lock(); const char *r = skb_last_reason(&s_base); unlock();    /* points at a string literal: safe to use after unlock */
    return r;
}

void base_link_lost(void)
{
    hal_servo_request_off();                                      /* host gone: a servo must not keep a raw pulse */
    lock();
    skb_link_lost(&s_base);
    if (skb_take_reflex(&s_base)) { hal_motor_brake_now(); }
    unlock();
}

void base_get_status(base_status_t *out)
{
    lock();
    out->hazard = skb_hazard(&s_base);
    out->state = (uint8_t)skb_state(&s_base);
    hal_range_get(&out->range_mm, &out->range_status);      /* own latch, no base state involved */
    out->batt_mv = (uint16_t)skl_clamp(s_batt_avg_mv, 0, 65535);
    for (int i = 0; i < 4; i++) { out->ir_raw[i] = (uint16_t)skl_clamp(s_base.ir[i], 0, 65535); }
    out->motor_ma = (uint16_t)skl_clamp(s_base.motor_ma, 0, 65535);
    unlock();
}

uint8_t base_hazard(void)
{
    lock(); const uint8_t h = skb_hazard(&s_base); unlock();
    return h;
}

int base_param_set(uint8_t id, int32_t value)
{
    lock(); const int rc = (int)skp_set(&s_params, id, value); unlock();
    return rc;
}

int32_t base_param_get(uint8_t id)
{
    lock(); const int32_t v = skp_get(&s_params, id); unlock();
    return v;
}

void base_params_reset(void)
{
    lock(); skp_reset(&s_params); unlock();
}

void base_set_event_sink(base_event_fn fn)
{
    s_sink = fn;
}
