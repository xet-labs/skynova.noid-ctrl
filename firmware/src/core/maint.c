/*
 * core/maint.c - maintenance mode glue: mutex, clock, expiry timer and the side effects.
 *
 * Purpose : give the console its tier 1 gate. A command that can move something (servo, later drive and arm)
 *           runs only while maintenance mode is on, which a person turns on deliberately and which turns itself
 *           off after 10 minutes (maximum 60).
 * Side effects (the order matters: outputs first, bookkeeping after):
 *   entering (not renewing) : base disarmed, servo outputs off.
 *   leaving or expiring     : base disarmed, servo outputs off, parameters back to defaults.
 * Expiry : checked by a 1 s esp_timer AND every time the gate is asked, so a late timer can never leave a tier 1
 *          command allowed after the deadline.
 * Called by: console_start() (maint_init, gate), console/cmd_maint.c.
 * Test    : the rules ran on the PC (test_maint); this file in a sandbox with a fake clock and fake base (see the
 *           handoff); on the board only by typing `maint on 1` and waiting a minute.
 * Spec    : protocol-v0 section 9, sprint-steps 1.6. See core/maint.h for what is still open.
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "base.h"
#include "core/maint.h"
#include "hal.h"
#include "skynoid_maint.h"

static const char *TAG = "maint";

static skm_t s_m;
static SemaphoreHandle_t s_lock;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Everything that must stop when the mode starts or ends. Called without the lock held (base takes its own). */
static void stop_outputs(void)
{
    (void)base_cmd_disarm();
    hal_servo_request_off();
}

/* Deadline reached: leave the mode and clean up. Safe to call from any task, as often as you like. */
static void poll_expiry(void)
{
    bool expired = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (skm_expired(&s_m, now_ms())) { (void)skm_exit(&s_m); expired = true; }
    xSemaphoreGive(s_lock);
    if (expired) {
        stop_outputs();
        base_params_reset();
        ESP_LOGI(TAG, "maintenance mode timed out: disarmed, servos off, parameters reset");
    }
}

static void timer_cb(void *arg) { (void)arg; poll_expiry(); }

void maint_init(void)
{
    skm_init(&s_m);
    s_lock = xSemaphoreCreateMutex();
    const esp_timer_create_args_t args = { .callback = timer_cb, .name = "maint" };
    esp_timer_handle_t t = NULL;
    if (s_lock == NULL || esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_periodic(t, 1000000) != ESP_OK) {
        ESP_LOGE(TAG, "maintenance timer not started: the gate still checks the deadline on every command");
    }
}

bool maint_is_active(void)
{
    if (s_lock == NULL) { return false; }                 /* before maint_init: nothing is allowed */
    poll_expiry();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool on = s_m.on;
    xSemaphoreGive(s_lock);
    return on;
}

int maint_enter(int32_t minutes, bool *newly)
{
    if (s_lock == NULL) { return SKM_BAD_MINUTES; }
    bool was_off = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const skm_result_t r = skm_enter(&s_m, now_ms(), minutes, &was_off);
    xSemaphoreGive(s_lock);
    if (r != SKM_OK) { return (int)r; }
    if (was_off) {
        stop_outputs();
        ESP_LOGI(TAG, "maintenance mode on: disarmed, servos off");
    }
    if (newly) { *newly = was_off; }
    return 0;
}

bool maint_leave(void)
{
    if (s_lock == NULL) { return false; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool was_on = skm_exit(&s_m);
    xSemaphoreGive(s_lock);
    if (was_on) {
        stop_outputs();
        base_params_reset();
        ESP_LOGI(TAG, "maintenance mode off: disarmed, servos off, parameters reset");
    }
    return was_on;
}

uint32_t maint_remaining_s(void)
{
    if (s_lock == NULL) { return 0; }
    poll_expiry();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t s = skm_remaining_s(&s_m, now_ms());
    xSemaphoreGive(s_lock);
    return s;
}
