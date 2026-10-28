/*
 * hal.h - the hardware layer: everything that touches a pin or a bus.
 *
 * Rule (conventions): drivers do no thinking. They turn a number into a pin state, or a pin state into a
 * number. All decisions are in lib/skynoid_logic. Files: hal_motor.c, hal_sense.c, hal_indicator.c.
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
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
bool hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);   /* write reg, read len bytes; false on any error */
bool hal_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);   /* reg byte + up to 8 data bytes; false on any error */
bool hal_ina219_read_mv(int32_t *mv);              /* bus voltage register, (reg >> 3) * 4 mV */

/* ---- distance sensor (VL53L0X at 0x29, hal_range.c) ----------------------------- */
void hal_range_start(void);                        /* once, after hal_i2c_init: starts the ranging task */
void hal_range_get(uint16_t *mm, uint8_t *status); /* latest value; status 0 ok, 1 out of range, 2 error or no sensor */

/* ---- servos (PCA9685 at SKYNOID_PCA9685_ADDR, hal_servo.c) -------------------------------------------------- */
/* Locked unless the build sets SKYNOID_SERVO_TRIAL or SKYNOID_SERVO_TYPE_CONFIRMED (app_config.h). While locked,
 * hal_servo_start() does nothing, every command answers HAL_SERVO_LOCKED and GPIO4 (output enable) stays HIGH. */
enum { HAL_SERVO_OK = 0, HAL_SERVO_LOCKED = 1, HAL_SERVO_NOT_READY = 2, HAL_SERVO_BAD_CH = 3, HAL_SERVO_BAD_ARG = 4,
       HAL_SERVO_BUSY = 5, HAL_SERVO_WRONG_TYPE = 6 };
enum { HAL_SERVO_ST_LOCKED = 0, HAL_SERVO_ST_NOT_READY = 1, HAL_SERVO_ST_READY = 2 };
enum { HAL_SERVO_M_OFF = 0, HAL_SERVO_M_PULSE = 1, HAL_SERVO_M_MOVE = 2, HAL_SERVO_M_HOLD = 3 };
typedef struct {
    uint8_t  mode;                                 /* HAL_SERVO_M_* */
    int32_t  us;                                   /* pulse width being output, 0 = none */
    int32_t  pos_x10;                              /* angle mode: tenths of a degree (the ASSUMED rest value until known) */
    bool     pos_known;
    uint32_t ttl_left_ms;                          /* pulse mode: time until the output is switched off */
} hal_servo_ch_t;
void hal_servo_oe_safe(void);                      /* FIRST in app_main, next to hal_motor_init: GPIO4 HIGH = outputs disabled */
void hal_servo_start(void);                        /* once, after hal_i2c_init; no-op while locked */
void hal_servo_request_off(void);                  /* any task, never blocks: outputs disabled now, channels off within 20 ms */
uint8_t hal_servo_state(void);                     /* HAL_SERVO_ST_* */
bool hal_servo_outputs_enabled(void);              /* is GPIO4 LOW (outputs enabled) right now */
int  hal_servo_channels(void);                     /* number of channels accepted */
bool hal_servo_ch_info(int ch, hal_servo_ch_t *out);
int  hal_servo_pulse(int ch, int32_t us, uint32_t ttl_ms);                       /* us 0 = off; ttl_ms 0 = default */
int  hal_servo_angle(int ch, int32_t deg_x10, uint32_t dur_ms, bool *clamped);   /* needs SKYNOID_SERVO_TYPE_CONFIRMED */
int  hal_servo_off(int ch);                        /* ch -1 = all channels */

/* ---- buzzer and status LED ----------------------------------------------------- */
enum { BUZ_OFF = 0, BUZ_CHIRP = 1, BUZ_DOUBLE_CHIRP = 2, BUZ_LOW_BATTERY = 3, BUZ_CRITICAL = 4, BUZ_ERROR = 5 };
enum { LED_OFF = 0, LED_SLOW = 1, LED_SOLID = 2, LED_FAST = 3, LED_DOUBLE = 4 };
void hal_indicator_init(void);                     /* starts the buzzer and LED tasks */
void hal_buzzer_play(uint8_t pattern);
void hal_led_mode(int mode);

#endif /* HAL_H */
