/*
 * hal.h - the hardware layer: everything that touches a pin or a bus.
 *
 * Rule (conventions): drivers do no thinking. They turn a number into a pin state, or a pin state into a
 * number. All decisions are in lib/skynoid_logic. Files: hal_motor.c, hal_sense.c, hal_indicator.c.
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stdint.h>
#include "skynoid_logic.h"

/* ---- motors (BTS7960 x2, LEDC 20 kHz 10 bit) --------------------------------- */
void hal_motor_init(void);                         /* FIRST thing in app_main: EN low, PWM low */
void hal_motor_apply(const skl_motion_out_t *o);   /* duty (permille, signed), EN and brake as decided */
void hal_motor_brake_now(void);                    /* reflex: PWM 0, EN stays high = brake */
void hal_motor_off(void);                          /* PWM 0, EN low = coast */

/* ---- analog and I2C ------------------------------------------------------------ */
void hal_adc_init(void);
int  hal_adc_read(int adc1_channel);               /* raw 0..4095, or -1 on error */
void hal_i2c_init(void);
bool hal_i2c_probe(uint8_t addr);                  /* true if a device acknowledges the address */
bool hal_ina219_read_mv(int32_t *mv);              /* bus voltage register, (reg >> 3) * 4 mV */

/* ---- buzzer and status LED ----------------------------------------------------- */
enum { BUZ_OFF = 0, BUZ_CHIRP = 1, BUZ_DOUBLE_CHIRP = 2, BUZ_LOW_BATTERY = 3, BUZ_CRITICAL = 4, BUZ_ERROR = 5 };
enum { LED_OFF = 0, LED_SLOW = 1, LED_SOLID = 2, LED_FAST = 3, LED_DOUBLE = 4 };
void hal_indicator_init(void);                     /* starts the buzzer and LED tasks */
void hal_buzzer_play(uint8_t pattern);
void hal_led_mode(int mode);

#endif /* HAL_H */
