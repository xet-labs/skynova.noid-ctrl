/*
 * main.c - steps 1.1 and 1.3b: blink the LED, print one boot line, start the text console.
 *
 * Purpose : if this builds, flashes and blinks, PlatformIO + ESP-IDF + the board work;
 *           the console then answers commands typed in a terminal (picocom -b 460800).
 * Called by: the ESP-IDF startup code calls app_main() once, in its own FreeRTOS task.
 * Test    : pio run -e single -t upload, then picocom -b 460800 /dev/ttyUSB0, type "help".
 *           Expect "skynoid single boot, reset_reason=<n>" and a 1 Hz blink on GPIO2.
 * Spec    : vault 10-specs/sprint-steps.md steps 1.1 and 1.3.
 *
 * Later steps replace the body of this file with "init order and task creation only".
 */
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "base.h"
#include "console/console_uart.h"
#include "core/logger.h"
#include "hal.h"

void app_main(void)
{
    /* Motor outputs safe FIRST (EN low, PWM low), then the base layer (tasks, state machine, reflexes).
     * base_start() must run before any frame can reach the dispatcher: base_cmd_* take its mutex.
     * Added 2026-10-25 by claude-code_c9a1; before this the base layer was never started. */
    hal_motor_init();
    hal_servo_oe_safe();               /* servo outputs disabled (GPIO4 high) before anything else can run */
    base_start();
    console_start();                   /* step 1.3b: UART at baud + logger task + console */

    LOGI("skynoid single boot, reset_reason=%d", (int)esp_reset_reason());

    /* The 1 Hz blink loop of step 1.1 is gone: GPIO2 (PIN_STATUS_LED) is now driven by the indicator task
     * (hal_indicator.c, LED mode follows the base state), and two writers would fight. app_main may return;
     * the base, logger and console tasks keep running. */
}
