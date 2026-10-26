/*
 * board_pins.h - the ONE place that knows which GPIO does what (role: single).
 *
 * Source of truth: vault 10-specs/pins.yaml. If a pin changes, change pins.yaml
 * first, then this file. The compile-time check at the bottom fails the build if
 * one GPIO is listed twice, or if a forbidden pin (boot strap, flash) sneaks in.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ---- motors: two BTS7960 modules, RPWM = forward, LPWM = reverse ---------- */
#define PIN_MOTOR_L_RPWM   25
#define PIN_MOTOR_L_LPWM   26
#define PIN_MOTOR_R_RPWM   27
#define PIN_MOTOR_R_LPWM   16
#define PIN_MOTOR_EN       14   /* R_EN + L_EN of both modules; external 10k pull-down keeps it low at boot */

/* ---- cliff sensors (TCRT5000 analog out) on ADC1: GPIO -> ADC1 channel ------ */
#define PIN_IR_FRONT_LEFT  36   /* ADC1 channel 0 */
#define PIN_IR_FRONT_RIGHT 39   /* ADC1 channel 3 */
#define PIN_IR_REAR_LEFT   34   /* ADC1 channel 6 */
#define PIN_IR_REAR_RIGHT  35   /* ADC1 channel 7 */
#define ADC_CH_IR_FRONT_LEFT   0
#define ADC_CH_IR_FRONT_RIGHT  3
#define ADC_CH_IR_REAR_LEFT    6
#define ADC_CH_IR_REAR_RIGHT   7
#define PIN_MOTOR_IS       33   /* BTS7960 IS pins, diode-OR-ed; optional until wired */
#define ADC_CH_MOTOR_IS        5
#define PIN_ACS712         32   /* servo rail current (upper functions); not used yet */

/* ---- misc outputs and inputs ----------------------------------------------- */
#define PIN_BUZZER         23
#define PIN_STATUS_LED      2
#define PIN_UART2_TX       5   /* UART2 default pair (step 1.7): the console when UART_CONSOLE_NUM=2, or the link when UART_LINK_NUM=2 */
#define PIN_UART2_RX      15
#define PIN_CONSOLE_TX     PIN_UART2_TX   /* old names, kept as aliases (not in the pin lists below) */
#define PIN_CONSOLE_RX     PIN_UART2_RX
#define PIN_PCA9685_OE      4   /* upper functions; not used yet */
#define PIN_TOUCH_HEAD     13
#define PIN_TOUCH_HAND_L   18
#define PIN_TOUCH_HAND_R   19
#define PIN_TOUCH_EXTRA    17
#define PIN_I2C_SDA        21
#define PIN_I2C_SCL        22
#define PIN_UART0_TX        1
#define PIN_UART0_RX        3

/* ---- compile-time checks ----------------------------------------------------
 * Trick: the sum of distinct powers of two equals their OR; a repeated pin makes
 * the sum larger. */
#define SK_PINBIT(g) (1ULL << (g))
/* SK_ROLE_PINS: every pin that belongs to a robot function. SK_ALL_PINS adds the UART pairs.
 * UART1 has no pair here on purpose (step 1.7): a UART1 channel must be given explicit pins, and
 * core/chan_cfg.h refuses a pin listed in SK_ROLE_PINS. */
#define SK_ROLE_PINS(X) \
    X(PIN_MOTOR_L_RPWM) X(PIN_MOTOR_L_LPWM) X(PIN_MOTOR_R_RPWM) X(PIN_MOTOR_R_LPWM) X(PIN_MOTOR_EN) \
    X(PIN_IR_FRONT_LEFT) X(PIN_IR_FRONT_RIGHT) X(PIN_IR_REAR_LEFT) X(PIN_IR_REAR_RIGHT) \
    X(PIN_MOTOR_IS) X(PIN_ACS712) X(PIN_BUZZER) X(PIN_STATUS_LED) \
    X(PIN_PCA9685_OE) X(PIN_TOUCH_HEAD) X(PIN_TOUCH_HAND_L) X(PIN_TOUCH_HAND_R) X(PIN_TOUCH_EXTRA) \
    X(PIN_I2C_SDA) X(PIN_I2C_SCL)
#define SK_ALL_PINS(X) \
    SK_ROLE_PINS(X) X(PIN_UART2_TX) X(PIN_UART2_RX) X(PIN_UART0_TX) X(PIN_UART0_RX)
#define SK_SUM(p) + SK_PINBIT(p)
#define SK_OR(p)  | SK_PINBIT(p)
#define SK_NEVER_USE_MASK (SK_PINBIT(0) | SK_PINBIT(12) | (0x3FULL << 6))   /* boot strap, strap, flash 6..11 */

_Static_assert((0ULL SK_ALL_PINS(SK_SUM)) == (0ULL SK_ALL_PINS(SK_OR)), "a GPIO is used twice in board_pins.h");
_Static_assert(((0ULL SK_ALL_PINS(SK_OR)) & SK_NEVER_USE_MASK) == 0, "a forbidden GPIO (0, 12, 6..11) is used");

#endif /* BOARD_PINS_H */
