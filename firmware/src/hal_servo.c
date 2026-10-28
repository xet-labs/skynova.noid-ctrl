/*
 * hal_servo.c - the PCA9685 servo driver: one task, per-channel state, output-enable pin.
 *
 * Purpose : turn "channel + pulse width" or "channel + angle" into PCA9685 registers, safely.
 *           The maths (angle to pulse, soft limits, minimum-jerk move, speed limit, power budget) is in
 *           lib/skynoid_servo and is tested on the PC; this file only owns the chip and the pin.
 * Safety  : (1) LOCKED BY DEFAULT. Unless the build sets SKYNOID_SERVO_TRIAL or SKYNOID_SERVO_TYPE_CONFIRMED the
 *               chip is never touched; GPIO4 (PCA9685 OE, active LOW) is driven HIGH at boot and stays there.
 *           (2) A raw pulse always has a time limit (default 1 s, maximum 3 s): a continuous-rotation servo given
 *               1.7 ms would spin until power-off. When it runs out the channel is switched fully OFF (no pulses),
 *               not set to 1.5 ms, because a worn continuous servo creeps at its "centre".
 *           (3) Outputs are enabled (OE low) only while some channel has a pulse; otherwise OE is high.
 *           (4) hal_servo_request_off() (e-stop, critical battery) sets OE high on the spot from any task and the
 *               task switches every channel off within one tick (20 ms). It never waits for a lock.
 *           (5) The chip is identity-checked before it is configured (MODE1 bit 0 set and not an INA219's 0x399F),
 *               and the driver refuses to start if SKYNOID_PCA9685_ADDR equals SKYNOID_INA219_ADDR.
 *           (6) Pulses are clamped to 600..2400 us by the library whatever the caller asks.
 * Not done: servo rail current (ACS712 on GPIO32 has no reader yet), so sks_current_scale_pct is not used;
 *           the power budget only counts channels that are moving, by joint weight (channel number = joint index).
 * Called by: base_start() (hal_servo_start), main.c (hal_servo_oe_safe), console/cmd_servo.c, base.c (request_off).
 * Test    : the maths: test_servo on the PC. This file: a scripted test in the sandbox of the session that wrote it
 *           (fake I2C, fake clock); on the board only with the real servos: `servo test 0`.
 * Spec    : vault 04-firmware/03-upper-node.md (Servo engine, Servo power budget), devices.yaml (pca9685), D20.
 *
 * Honest limits: written, not built with ESP-IDF, not run on the board. The 50 Hz period depends on the chip's
 * internal oscillator (nominal 25 MHz, a few percent off): pulse widths are as accurate as that, #verify with a scope.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board/board_pins.h"
#include "hal.h"
#include "skynoid_servo.h"

static volatile bool s_oe_high = true;       /* what we last drove on GPIO4 (HIGH = outputs disabled) */

/* ---- output-enable pin: the same code with the driver locked or not ------------------------------------ */
static void oe_set_high(bool high)
{
    gpio_set_level((gpio_num_t)PIN_PCA9685_OE, high ? 1 : 0);
    s_oe_high = high;
}

void hal_servo_oe_safe(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_PCA9685_OE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level((gpio_num_t)PIN_PCA9685_OE, 1);    /* level first: no low glitch when the pin becomes an output */
    (void)gpio_config(&cfg);
    oe_set_high(true);
}

bool hal_servo_outputs_enabled(void) { return !s_oe_high; }

#if SKYNOID_SERVO_ACTIVE
/* ================================================================== the driver (trial or confirmed builds) */

#define NCH SKYNOID_SERVO_CHANNELS
_Static_assert(NCH >= 1 && NCH <= 16, "the PCA9685 has 16 channels");

static const char *TAG = "servo";

#define PCA_MODE1          0x00
#define PCA_MODE2          0x01
#define PCA_LED0_ON_L      0x06
#define PCA_ALL_LED_ON_H   0xFB
#define PCA_ALL_LED_OFF_H  0xFD
#define PCA_PRESCALE       0xFE
#define PCA_M1_ALLCALL     0x01     /* kept SET: devices/selftest identify the chip by this bit (and `devices` shows 0x70) */
#define PCA_M1_SLEEP       0x10
#define PCA_M1_AI          0x20     /* register auto-increment */
#define PCA_M1_RESTART     0x80
#define PCA_M2_OUTDRV      0x04     /* totem-pole outputs */
#define PCA_FULL_OFF       0x10     /* bit 4 of LEDn_OFF_H / ALL_LED_OFF_H */
#define INA219_CONFIG_POWER_ON 0x399F

#define TICK_MS            20
#define INIT_RETRY_MS      5000
#define FAILS_TO_DROP      5        /* consecutive failed channel writes before the chip is declared lost */
#define TASK_STACK         3072
#define TASK_PRIO          4

typedef struct {
    uint8_t         mode;           /* HAL_SERVO_M_* */
    int32_t         us;             /* what the tick last decided: 0 = off */
    int32_t         written_us;     /* what the chip holds now; -1 = unknown */
    int32_t         pos_x10;        /* angle mode: last position (assumed rest until the first move) */
    bool            pos_known;
    uint32_t        expire_ms;      /* pulse mode */
    sks_joint_cal_t cal;
    sks_move_t      move;
} ch_t;

static ch_t s_ch[NCH];
static SemaphoreHandle_t s_lock;
static volatile bool s_off_request;
static bool s_ready;                /* chip identified and configured */
static int32_t s_period_us = SKS_PERIOD_US;
static uint32_t s_last_start_ms;
static int s_fails;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---- chip access (caller holds s_lock, or is the init path before the task runs) ----------------------------- */
static bool pca_wr(uint8_t reg, uint8_t v) { return hal_i2c_write_reg(SKYNOID_PCA9685_ADDR, reg, &v, 1); }

static bool pca_init(void)
{
    if (SKYNOID_PCA9685_ADDR == SKYNOID_INA219_ADDR) {
        ESP_LOGE(TAG, "SKYNOID_PCA9685_ADDR equals SKYNOID_INA219_ADDR (0x%02X): refusing to touch the chip",
                 (unsigned)SKYNOID_PCA9685_ADDR);
        return false;
    }
    uint8_t b[2] = { 0, 0 };
    if (!hal_i2c_read_reg(SKYNOID_PCA9685_ADDR, PCA_MODE1, b, 2)) { return false; }       /* nobody there: quiet */
    const unsigned w = (unsigned)((b[0] << 8) | b[1]);
    if ((b[0] & PCA_M1_ALLCALL) == 0u || w == INA219_CONFIG_POWER_ON) {
        ESP_LOGE(TAG, "0x%02X answers but is not a PCA9685 (reg0=0x%04X): not touching it",
                 (unsigned)SKYNOID_PCA9685_ADDR, w);
        return false;
    }
    const uint8_t pre = sks_prescale(SKYNOID_PCA9685_OSC_HZ, SKS_PWM_HZ);
    bool ok = pca_wr(PCA_MODE1, PCA_M1_SLEEP | PCA_M1_ALLCALL)           /* sleep: the prescaler may only change asleep */
           && pca_wr(PCA_ALL_LED_ON_H, 0x00)
           && pca_wr(PCA_ALL_LED_OFF_H, PCA_FULL_OFF)                    /* every channel fully OFF before it wakes */
           && pca_wr(PCA_PRESCALE, pre)
           && pca_wr(PCA_MODE2, PCA_M2_OUTDRV)
           && pca_wr(PCA_MODE1, PCA_M1_AI | PCA_M1_ALLCALL);             /* wake */
    vTaskDelay(pdMS_TO_TICKS(5));                                        /* oscillator needs 500 us */
    ok = ok && pca_wr(PCA_MODE1, PCA_M1_RESTART | PCA_M1_AI | PCA_M1_ALLCALL);
    if (!ok) { return false; }
    s_period_us = (int32_t)(((uint64_t)SKS_PCA_COUNTS * ((uint64_t)pre + 1u) * 1000000u) / SKYNOID_PCA9685_OSC_HZ);
    for (int i = 0; i < NCH; i++) { s_ch[i].written_us = 0; }
    ESP_LOGI(TAG, "pca9685 ready at 0x%02X, prescale %u, period %d us", (unsigned)SKYNOID_PCA9685_ADDR,
             (unsigned)pre, (int)s_period_us);
    return true;
}

/* Put one channel on the chip: us 0 = fully off, otherwise ON=0, OFF=counts. */
static bool write_ch(int ch, int32_t us)
{
    uint8_t d[4] = { 0, 0, 0, 0 };
    if (us <= 0) {
        d[3] = PCA_FULL_OFF;
    } else {
        const uint16_t counts = sks_us_to_counts(us, s_period_us);
        d[2] = (uint8_t)(counts & 0xFFu);
        d[3] = (uint8_t)((counts >> 8) & 0x0Fu);
    }
    return hal_i2c_write_reg(SKYNOID_PCA9685_ADDR, (uint8_t)(PCA_LED0_ON_L + 4 * ch), d, sizeof d);
}

static void all_off_locked(void)
{
    for (int i = 0; i < NCH; i++) { s_ch[i].mode = HAL_SERVO_M_OFF; s_ch[i].us = 0; }
    if (pca_wr(PCA_ALL_LED_OFF_H, PCA_FULL_OFF)) {
        for (int i = 0; i < NCH; i++) { s_ch[i].written_us = 0; }
    }
    oe_set_high(true);
}

/* One engine pass at time `now`. Caller holds s_lock. */
static void tick(uint32_t now)
{
    bool any = false;
    bool failed = false;
    for (int i = 0; i < NCH; i++) {
        ch_t *c = &s_ch[i];
        int32_t want = 0;
        switch (c->mode) {
        case HAL_SERVO_M_PULSE:
            if ((int32_t)(now - c->expire_ms) >= 0) { c->mode = HAL_SERVO_M_OFF; }       /* time limit reached: off */
            else { want = c->us; }
            break;
        case HAL_SERVO_M_MOVE:
            c->pos_x10 = sks_move_pos(&c->move, now);
            c->pos_known = true;
            want = sks_deg_to_us(&c->cal, c->pos_x10, NULL);
            if (sks_move_done(&c->move, now)) { c->mode = HAL_SERVO_M_HOLD; }
            break;
        case HAL_SERVO_M_HOLD:
            want = sks_deg_to_us(&c->cal, c->pos_x10, NULL);
            break;
        default:
            break;
        }
        c->us = want;
        if (want > 0) { any = true; }
        if (want != c->written_us) {
            if (write_ch(i, want)) { c->written_us = want; } else { failed = true; }
        }
    }
    if (failed) {
        if (++s_fails >= FAILS_TO_DROP) {
            ESP_LOGE(TAG, "pca9685 stopped answering: outputs disabled, will retry");
            for (int i = 0; i < NCH; i++) { s_ch[i].mode = HAL_SERVO_M_OFF; s_ch[i].us = 0; s_ch[i].written_us = -1; }
            s_ready = false;
            s_fails = 0;
            oe_set_high(true);
            return;
        }
    } else {
        s_fails = 0;
    }
    /* outputs enabled only while something is being driven, and never after an off request */
    oe_set_high(!(any && !s_off_request && !failed));
}

static void servo_task(void *arg)
{
    (void)arg;
    uint32_t next_init = 0;
    bool logged_missing = false;
    for (;;) {
        const uint32_t now = now_ms();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!s_ready) {
            if ((int32_t)(now - next_init) >= 0) {
                if (pca_init()) { s_ready = true; s_fails = 0; logged_missing = false; }
                else {
                    next_init = now + INIT_RETRY_MS;
                    if (!logged_missing) {
                        logged_missing = true;
                        ESP_LOGW(TAG, "no PCA9685 at 0x%02X (see `devices`); retrying every %d s",
                                 (unsigned)SKYNOID_PCA9685_ADDR, INIT_RETRY_MS / 1000);
                    }
                }
            }
        } else {
            if (s_off_request) { s_off_request = false; all_off_locked(); }
            else { tick(now); }
        }
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(s_ready ? TICK_MS : 200));
    }
}

void hal_servo_start(void)
{
    for (int i = 0; i < NCH; i++) {
        memset(&s_ch[i], 0, sizeof s_ch[i]);
        sks_cal_default(&s_ch[i].cal);
        s_ch[i].pos_x10 = s_ch[i].cal.rest_deg_x10;
        s_ch[i].written_us = -1;
    }
    s_last_start_ms = now_ms() - 1000u;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) { ESP_LOGE(TAG, "no mutex: servo driver not started"); return; }
    xTaskCreate(servo_task, "servo", TASK_STACK, NULL, TASK_PRIO, NULL);
}

void hal_servo_request_off(void)
{
    s_off_request = true;
    oe_set_high(true);                              /* instant, no lock: this runs from the safety path */
}

uint8_t hal_servo_state(void)
{
    if (s_lock == NULL) { return HAL_SERVO_ST_NOT_READY; }
    return s_ready ? HAL_SERVO_ST_READY : HAL_SERVO_ST_NOT_READY;
}

int hal_servo_channels(void) { return NCH; }

bool hal_servo_ch_info(int ch, hal_servo_ch_t *out)
{
    if (ch < 0 || ch >= NCH || out == NULL || s_lock == NULL) { return false; }
    const uint32_t now = now_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const ch_t *c = &s_ch[ch];
    out->mode = c->mode;
    out->us = c->us;
    out->pos_x10 = c->pos_x10;
    out->pos_known = c->pos_known;
    out->ttl_left_ms = (c->mode == HAL_SERVO_M_PULSE && (int32_t)(c->expire_ms - now) > 0) ? (c->expire_ms - now) : 0u;
    xSemaphoreGive(s_lock);
    return true;
}

/* Sum of the weights of channels that are moving, leaving `except` out. Caller holds s_lock. */
static int32_t moving_weight(int except)
{
    int32_t sum = 0;
    for (int i = 0; i < NCH; i++) {
        if (i == except) { continue; }
        if (s_ch[i].mode == HAL_SERVO_M_PULSE || s_ch[i].mode == HAL_SERVO_M_MOVE) { sum += sks_joint_weight(i); }
    }
    return sum;
}

/* A channel that is not moving now is about to start: check the power budget and the stagger gap. */
static int admit_start(int ch, uint32_t now)
{
    const uint8_t m = s_ch[ch].mode;
    if (m == HAL_SERVO_M_PULSE || m == HAL_SERVO_M_MOVE) { return HAL_SERVO_OK; }      /* already counted, just changing */
    if (!sks_budget_admit(moving_weight(ch), sks_joint_weight(ch), SKYNOID_SERVO_BUDGET)) { return HAL_SERVO_BUSY; }
    if (!sks_stagger_ok(s_last_start_ms, now, SKYNOID_SERVO_STAGGER_MS)) { return HAL_SERVO_BUSY; }
    s_last_start_ms = now;
    return HAL_SERVO_OK;
}

static int precheck(int ch)
{
    if (s_lock == NULL) { return HAL_SERVO_NOT_READY; }
    if (ch < 0 || ch >= NCH) { return HAL_SERVO_BAD_CH; }
    if (!s_ready) { return HAL_SERVO_NOT_READY; }
    return HAL_SERVO_OK;
}

int hal_servo_pulse(int ch, int32_t us, uint32_t ttl_ms)
{
    int rc = precheck(ch);
    if (rc != HAL_SERVO_OK) { return rc; }
    if (us != 0 && (us < SKS_PULSE_ABS_MIN_US || us > SKS_PULSE_ABS_MAX_US)) { return HAL_SERVO_BAD_ARG; }
    if (ttl_ms == 0u) { ttl_ms = SKYNOID_SERVO_TTL_DEFAULT_MS; }
    if (ttl_ms > SKYNOID_SERVO_TTL_MAX_MS) { return HAL_SERVO_BAD_ARG; }
    const uint32_t now = now_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (us == 0) {
        s_ch[ch].mode = HAL_SERVO_M_OFF;
        rc = HAL_SERVO_OK;
    } else {
        rc = admit_start(ch, now);
        if (rc == HAL_SERVO_OK) {
            s_ch[ch].mode = HAL_SERVO_M_PULSE;
            s_ch[ch].us = us;
            s_ch[ch].expire_ms = now + ttl_ms;
        }
    }
    xSemaphoreGive(s_lock);
    return rc;
}

int hal_servo_angle(int ch, int32_t deg_x10, uint32_t dur_ms, bool *clamped)
{
#if SKYNOID_SERVO_TYPE_CONFIRMED
    int rc = precheck(ch);
    if (rc != HAL_SERVO_OK) { return rc; }
    const uint32_t now = now_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ch_t *c = &s_ch[ch];
    rc = admit_start(ch, now);
    if (rc == HAL_SERVO_OK) {
        const int32_t target = sks_clamp_deg_x10(&c->cal, deg_x10, clamped);
        const int32_t from = c->pos_known ? c->pos_x10 : c->cal.rest_deg_x10;      /* rest is an ASSUMPTION until a move */
        sks_move_start(&c->move, from, target, dur_ms, SKYNOID_SERVO_VMAX_DPS, now);
        c->pos_x10 = from;
        if (c->move.active) { c->mode = HAL_SERVO_M_MOVE; }
        else { c->pos_x10 = target; c->pos_known = true; c->mode = HAL_SERVO_M_HOLD; }
    }
    xSemaphoreGive(s_lock);
    return rc;
#else
    (void)ch; (void)deg_x10; (void)dur_ms; (void)clamped;
    return HAL_SERVO_WRONG_TYPE;
#endif
}

int hal_servo_off(int ch)
{
    if (s_lock == NULL) { return HAL_SERVO_NOT_READY; }
    if (ch < -1 || ch >= NCH) { return HAL_SERVO_BAD_CH; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < NCH; i++) {
        if (ch == -1 || ch == i) { s_ch[i].mode = HAL_SERVO_M_OFF; }
    }
    xSemaphoreGive(s_lock);
    return HAL_SERVO_OK;
}

#else  /* ============================================================================ locked: nothing touches the chip */

void hal_servo_start(void) { }
void hal_servo_request_off(void) { oe_set_high(true); }
uint8_t hal_servo_state(void) { return HAL_SERVO_ST_LOCKED; }
int  hal_servo_channels(void) { return SKYNOID_SERVO_CHANNELS; }
bool hal_servo_ch_info(int ch, hal_servo_ch_t *out) { (void)ch; (void)out; return false; }
int  hal_servo_pulse(int ch, int32_t us, uint32_t ttl_ms) { (void)ch; (void)us; (void)ttl_ms; return HAL_SERVO_LOCKED; }
int  hal_servo_angle(int ch, int32_t d, uint32_t t, bool *c) { (void)ch; (void)d; (void)t; (void)c; return HAL_SERVO_LOCKED; }
int  hal_servo_off(int ch) { (void)ch; return HAL_SERVO_LOCKED; }

#endif
