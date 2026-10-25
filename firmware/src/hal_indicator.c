/*
 * hal_indicator.c - buzzer and status LED output.
 *
 * Purpose : play the buzzer patterns and the LED modes. Timing tables are pure logic in
 *           lib/skynoid_logic (skl_buzzer_on, skl_led_on); this file only owns the pins and one task.
 * Called by: base.c (battery and state indications), later the dispatcher for CMD buzzer (0x40).
 * Test    : PC tests cover the timing (test_core). On the board: expect a chirp from
 *           hal_buzzer_play(BUZ_CHIRP) and the LED slow-blinking in SAFE.
 * Spec    : vault 04-firmware/02-base-node.md (Buzzer table). Buzzer is assumed ACTIVE-HIGH and
 *           active (self-oscillating): #verify against the actual part, flip BUZZER_ACTIVE_LEVEL if not.
 */
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board/board_pins.h"
#include "hal.h"
#include "skynoid_core.h"

#define TICK_MS 20
#define BUZZER_ACTIVE_LEVEL 1

static volatile uint8_t s_pattern = BUZ_OFF;
static volatile uint32_t s_pattern_start_ms;
static volatile int s_led_mode = LED_SLOW;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void indicator_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = now_ms();
        const uint8_t pat = s_pattern;
        if (pat != BUZ_OFF) {
            const uint32_t t = now - s_pattern_start_ms;
            if (skl_buzzer_done(pat, t)) { s_pattern = BUZ_OFF; }
            const bool on = (s_pattern != BUZ_OFF) && skl_buzzer_on(pat, t);
            gpio_set_level(PIN_BUZZER, on ? BUZZER_ACTIVE_LEVEL : !BUZZER_ACTIVE_LEVEL);
        } else {
            gpio_set_level(PIN_BUZZER, !BUZZER_ACTIVE_LEVEL);
        }
        gpio_set_level(PIN_STATUS_LED, skl_led_on(s_led_mode, now) ? 1 : 0);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
    }
}

void hal_indicator_init(void)
{
    const gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_BUZZER) | (1ULL << PIN_STATUS_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);
    gpio_set_level(PIN_BUZZER, !BUZZER_ACTIVE_LEVEL);
    xTaskCreate(indicator_task, "indicator", 2048, NULL, 3, NULL);
}

void hal_buzzer_play(uint8_t pattern)
{
    s_pattern_start_ms = now_ms();
    s_pattern = pattern;
}

void hal_led_mode(int mode)
{
    s_led_mode = mode;
}
