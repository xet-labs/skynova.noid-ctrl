/*
 * hal_motor.c - two BTS7960 modules on LEDC PWM (20 kHz, 10 bit) plus the shared enable pin.
 *
 * Per module: RPWM carries the duty for forward, LPWM for reverse, the other stays low.
 * Both inputs low with EN high = brake (low-side switches on, #verify on the bench).
 * EN low = coast. The EN pin has an external pull-down, so the driver is off during boot and reset.
 *
 * Called by: base.c (motion task and safety task, always under the base lock).
 * Tested: on the bench only (steps 2.1 to 2.3). The decisions that feed it are tested on the PC.
 */
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "app_config.h"
#include "board/board_pins.h"
#include "hal.h"

#define PWM_MODE   LEDC_LOW_SPEED_MODE
#define PWM_MAX    1023u            /* 10 bit */

/* channel index: 0 L fwd, 1 L rev, 2 R fwd, 3 R rev */
static const int PWM_PIN[4] = { PIN_MOTOR_L_RPWM, PIN_MOTOR_L_LPWM, PIN_MOTOR_R_RPWM, PIN_MOTOR_R_LPWM };

static void pwm_set(int ch, int32_t permille)
{
    uint32_t duty = 0;
    if (permille > 0) {
        duty = permille >= 1000 ? PWM_MAX : (uint32_t)((permille * (int32_t)PWM_MAX + 500) / 1000);
    }
    ledc_set_duty(PWM_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(PWM_MODE, (ledc_channel_t)ch);
}

static void pwm_all_zero(void)
{
    for (int ch = 0; ch < 4; ch++) { pwm_set(ch, 0); }
}

void hal_motor_init(void)
{
    gpio_set_level((gpio_num_t)PIN_MOTOR_EN, 0);                   /* low before and after becoming an output */
    const gpio_config_t en = {
        .pin_bit_mask = 1ULL << PIN_MOTOR_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&en);
    gpio_set_level((gpio_num_t)PIN_MOTOR_EN, 0);

    const ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    for (int ch = 0; ch < 4; ch++) {
        const ledc_channel_config_t c = {
            .gpio_num = PWM_PIN[ch],
            .speed_mode = PWM_MODE,
            .channel = (ledc_channel_t)ch,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ledc_channel_config(&c);
    }
}

/* One wheel: signed permille to the forward or reverse input. invert swaps them. */
static void wheel(int fwd_ch, int rev_ch, int32_t permille, bool invert)
{
    if (invert) { permille = -permille; }
    pwm_set(fwd_ch, permille > 0 ? permille : 0);
    pwm_set(rev_ch, permille < 0 ? -permille : 0);
}

void hal_motor_apply(const skl_motion_out_t *o)
{
    if (!o->en) {
        pwm_all_zero();
        gpio_set_level((gpio_num_t)PIN_MOTOR_EN, 0);                /* coast */
        return;
    }
    /* Set EN first when starting, so the first PWM edge already finds an enabled driver. */
    gpio_set_level((gpio_num_t)PIN_MOTOR_EN, 1);
    wheel(0, 1, o->brake ? 0 : o->duty_permille[0], SKYNOID_LEFT_INVERT);
    wheel(2, 3, o->brake ? 0 : o->duty_permille[1], SKYNOID_RIGHT_INVERT);
}

void hal_motor_brake_now(void)
{
    pwm_all_zero();
    gpio_set_level((gpio_num_t)PIN_MOTOR_EN, 1);
}

void hal_motor_off(void)
{
    pwm_all_zero();
    gpio_set_level((gpio_num_t)PIN_MOTOR_EN, 0);
}
