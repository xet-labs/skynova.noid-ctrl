/*
 * hal_range.c - the VL53L0X distance sensor (I2C 0x29): one task, latest value for base_get_status().
 *
 * Purpose : read the distance every 50 ms and publish (mm, status) with the meaning of the TLM base_status fields:
 *             status 0 ok            mm is the distance in millimetres
 *             status 1 out of range  nothing in front of the sensor (driver value >= 8190); mm = 8190
 *             status 2 error         no sensor, bus error, or no good reading for 500 ms; mm = 0
 *           The chip driver is vendored (src/vendor/vl53l0x, see VENDOR.md). This file only calls it, retries,
 *           and maps results. No decisions: the "hand near" rule lives on the Pi (Robot.when_range_below).
 * Called by: base_start() -> hal_range_start(); base_get_status() -> hal_range_get().
 * Test    : on the board only (hardware): console `range`, wave a hand at 10 to 50 cm, compare with a ruler.
 * Spec    : sprint-steps 3.5. Written for single-shot ranging (about 20 to 35 ms per read). Spec asked for
 *           continuous mode with a 33 ms budget: not done, the vendored driver has no continuous mode.
 *
 * Honest limits: written, not built, not run. If the sensor is missing the task retries the init every 5 s
 * (quietly: one log line when the state changes). A wrong address (`i2c scan` shows another one) needs the
 * `i2c setaddr` step of 3.5, which does not exist yet.
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal.h"
#include "vendor/vl53l0x/vl53l0x.h"

#define RANGE_PERIOD_MS     50
#define RANGE_RETRY_MS      5000      /* between init attempts while the sensor is missing */
#define RANGE_STALE_MS      500       /* no good reading for this long = status 2 */
#define RANGE_NO_TARGET_MM  8190      /* driver values at or above this mean "no target" */
#define RANGE_FAILS_REINIT  5         /* consecutive failed reads before the init is run again */
#define RANGE_TASK_STACK    3072
#define RANGE_TASK_PRIO     4

static const char *TAG = "range";

/* mm in the low 16 bits, status in bits 16..23: one 32-bit word, so a reader never sees a mismatched pair. */
static volatile uint32_t s_packed = (2u << 16);
static volatile uint32_t s_last_ok_ms;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void publish(uint16_t mm, uint8_t status)
{
    s_packed = ((uint32_t)status << 16) | mm;
}

void hal_range_get(uint16_t *mm, uint8_t *status)
{
    const uint32_t p = s_packed;
    uint16_t m = (uint16_t)(p & 0xFFFFu);
    uint8_t st = (uint8_t)((p >> 16) & 0xFFu);
    if (st != 2u && (uint32_t)(now_ms() - s_last_ok_ms) > RANGE_STALE_MS) { m = 0; st = 2u; }
    *mm = m;
    *status = st;
}

static void range_task(void *arg)
{
    (void)arg;
    vl53l0x_t dev = { .addr = VL53L0X_DEFAULT_ADDR, .stop_variable = 0 };
    bool ready = false;
    bool logged_missing = false;
    int fails = 0;

    for (;;) {
        if (!ready) {
            const esp_err_t err = vl53l0x_init(&dev);
            if (err == ESP_OK) {
                ready = true;
                fails = 0;
                logged_missing = false;
                ESP_LOGI(TAG, "vl53l0x ready at 0x%02X", (unsigned)dev.addr);
            } else {
                publish(0, 2);
                if (!logged_missing) {
                    logged_missing = true;
                    ESP_LOGW(TAG, "vl53l0x init failed (err 0x%x): no sensor at 0x%02X? retrying every %d s",
                             (unsigned)err, (unsigned)VL53L0X_DEFAULT_ADDR, RANGE_RETRY_MS / 1000);
                }
                vTaskDelay(pdMS_TO_TICKS(RANGE_RETRY_MS));
                continue;
            }
        }

        uint16_t mm = 0;
        if (vl53l0x_read_mm(&dev, &mm) == ESP_OK) {
            fails = 0;
            s_last_ok_ms = now_ms();
            if (mm >= RANGE_NO_TARGET_MM) { publish(RANGE_NO_TARGET_MM, 1); }
            else                          { publish(mm, 0); }
        } else if (++fails >= RANGE_FAILS_REINIT) {
            ready = false;                                  /* run the full init again after a run of bad reads */
            publish(0, 2);
        }
        vTaskDelay(pdMS_TO_TICKS(RANGE_PERIOD_MS));
    }
}

void hal_range_start(void)
{
    publish(0, 2);
    s_last_ok_ms = now_ms();
    xTaskCreate(range_task, "range", RANGE_TASK_STACK, NULL, RANGE_TASK_PRIO, NULL);
}
