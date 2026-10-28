/*
 * touch.c - reads the four touch pads and sends EVT touch (0x07) to the host.
 *
 * Purpose : sample each pad every 10 ms, hand the sample to lib/skynoid_touch (debounce 30 ms, ignore the first
 *           500 ms, mute a pad that stays down 20 s) and turn its answer into link events:
 *             DOWN  -> EVT touch (zone, 1)       UP -> EVT touch (zone, 0)
 *             STUCK -> EVT sensor_fault (5 + zone, reason 1)
 *           No decisions about what a touch MEANS: the host does that (Robot.on_event, pat_sound).
 * Called by: base_start() -> touch_start(); console `touch` -> touch_snapshot(), touch_stats().
 * Test    : the rules are tested on the PC (test_touch). The pins are not: on the board run `touch` and press each
 *           pad; with `doctor` or any session running, EVT touch frames arrive.
 * Spec    : protocol-v0 section 6 (EVT 0x07 touch: zone u8, down u8), zones in section 5.
 *
 * Honest limits: written, not built, not run. The pad polarity is unmeasured (SKYNOID_TOUCH_ACTIVE_LEVEL, default
 * active high). Events are sent only while a host session is running (link_post_live_event): a touch with no Pi
 * connected is shown in the log and by `touch`, but not queued. No touch capability bit is set in HELLO yet.
 */
#include "touch.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board/board_pins.h"
#include "link/link.h"
#include "skynoid_base.h"
#include "skynoid_touch.h"

#if SKYNOID_TOUCH_ENABLE

#define TOUCH_PERIOD_MS 10
#define TOUCH_TASK_STACK 2560
#define TOUCH_TASK_PRIO 3

static const char *TAG = "touch";
static const char *const ZONE_NAME[SKT_ZONES] = { "head", "hand_l", "hand_r", "extra" };
static const int ZONE_PIN[SKT_ZONES] = { PIN_TOUCH_HEAD, PIN_TOUCH_HAND_L, PIN_TOUCH_HAND_R, PIN_TOUCH_EXTRA };

static volatile uint8_t s_raw[SKT_ZONES];
static volatile uint8_t s_down[SKT_ZONES];
static volatile uint8_t s_stuck[SKT_ZONES];
static volatile uint32_t s_posted;
static volatile uint32_t s_skipped;
static volatile bool s_running;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void post(uint8_t id, uint16_t a, uint8_t b)
{
    if (link_post_live_event(id, a, b)) { s_posted++; } else { s_skipped++; }
}

static void touch_task(void *arg)
{
    (void)arg;
    skt_t logic;
    skt_init(&logic, now_ms(), SKYNOID_TOUCH_BOOT_IGNORE_MS, SKYNOID_TOUCH_HOLD_MS, SKYNOID_TOUCH_STUCK_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = now_ms();
        for (uint8_t z = 0; z < SKT_ZONES; z++) {
            const bool pressed = (gpio_get_level(ZONE_PIN[z]) == SKYNOID_TOUCH_ACTIVE_LEVEL);
            s_raw[z] = pressed ? 1 : 0;
            const uint8_t f = skt_step(&logic, z, pressed, now);
            s_down[z] = skt_is_down(&logic, z) ? 1 : 0;
            s_stuck[z] = skt_is_stuck(&logic, z) ? 1 : 0;
            if (f & SKT_F_DOWN) {
                ESP_LOGI(TAG, "%s down", ZONE_NAME[z]);
                post(SKB_EV_TOUCH, z, 1);
            }
            if (f & SKT_F_UP) {
                ESP_LOGI(TAG, "%s up", ZONE_NAME[z]);
                post(SKB_EV_TOUCH, z, 0);
            }
            if (f & SKT_F_STUCK) {
                ESP_LOGW(TAG, "%s down for %d ms: treated as broken, muted until released", ZONE_NAME[z],
                         (int)SKYNOID_TOUCH_STUCK_MS);
                post(SKB_EV_SENSOR_FAULT, (uint16_t)(SKB_SENSOR_TOUCH0 + z), SKB_SENSOR_REASON_STUCK);
            }
        }
        const TickType_t ticks = pdMS_TO_TICKS(TOUCH_PERIOD_MS);
        vTaskDelayUntil(&last, ticks > 0 ? ticks : 1);
    }
}

void touch_start(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < SKT_ZONES; i++) { mask |= (1ULL << ZONE_PIN[i]); }
    const gpio_config_t in = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = SKYNOID_TOUCH_ACTIVE_LEVEL ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,       /* pull = the released level */
        .pull_down_en = SKYNOID_TOUCH_ACTIVE_LEVEL ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&in) != ESP_OK) {
        ESP_LOGW(TAG, "gpio_config failed: touch pads not started");
        return;
    }
    s_running = true;
    xTaskCreate(touch_task, "touch", TOUCH_TASK_STACK, NULL, TOUCH_TASK_PRIO, NULL);
    ESP_LOGI(TAG, "touch pads on GPIO %d %d %d %d, active level %d", ZONE_PIN[0], ZONE_PIN[1], ZONE_PIN[2],
             ZONE_PIN[3], (int)SKYNOID_TOUCH_ACTIVE_LEVEL);
}

bool touch_snapshot(uint8_t zone, bool *raw, bool *down, bool *stuck)
{
    if (zone >= SKT_ZONES || !s_running) { return false; }
    *raw = s_raw[zone] != 0;
    *down = s_down[zone] != 0;
    *stuck = s_stuck[zone] != 0;
    return true;
}

void touch_stats(uint32_t *posted, uint32_t *skipped_no_session)
{
    *posted = s_posted;
    *skipped_no_session = s_skipped;
}

#else /* SKYNOID_TOUCH_ENABLE == 0 */

void touch_start(void) {}
bool touch_snapshot(uint8_t zone, bool *raw, bool *down, bool *stuck) { (void)zone; (void)raw; (void)down; (void)stuck; return false; }
void touch_stats(uint32_t *posted, uint32_t *skipped_no_session) { *posted = 0; *skipped_no_session = 0; }

#endif
