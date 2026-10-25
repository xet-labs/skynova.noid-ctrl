/*
 * app_config.h - build-time switches and fixed numbers for the controller firmware.
 *
 * Switches can be overridden in platformio.ini with -DNAME=value (spec: config-defaults.yaml "console").
 * Runtime parameters (speed cap, wheel base, ...) live in params.c, not here.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "skynoid_logic.h"

/* ---- console and link channels ------------------------------------------- */
#ifndef SKYNOID_LINK_UART
#define SKYNOID_LINK_UART 0          /* UART carrying the framed link (0 = USB bridge) */
#endif
#ifndef SKYNOID_CONSOLE_UART
#define SKYNOID_CONSOLE_UART 0       /* 0 = console shares the link UART, 2 = separate UART2 */
#endif
#ifndef SKYNOID_CONSOLE_BAUD
#define SKYNOID_CONSOLE_BAUD 115200  /* only used when SKYNOID_CONSOLE_UART=2 */
#endif
#ifndef SKYNOID_CONSOLE_ACTUATE
#define SKYNOID_CONSOLE_ACTUATE 1    /* 0 compiles tier 1 (actuation) console commands out */
#endif
#ifndef SKYNOID_LOG_LEVEL
#define SKYNOID_LOG_LEVEL 2          /* 0 error, 1 warn, 2 info, 3 debug */
#endif

/* ---- hardware facts that are unknown until measured (step 3.1) --------------
 * SKL_UNSET means "refuse to use, and say so". */
#ifndef SKYNOID_IR_POLARITY
#define SKYNOID_IR_POLARITY SKL_POL_FLOOR_HIGH   /* reading FALLS at an edge; measure in step 3.1 */
#endif
#ifndef SKYNOID_NO_FLOOR_ADC
#define SKYNOID_NO_FLOOR_ADC SKL_UNSET           /* reading with nothing under the sensor */
#endif
#ifndef SKYNOID_MIN_CONTRAST_PCT
#define SKYNOID_MIN_CONTRAST_PCT SKL_UNSET       /* smallest floor/no-floor gap that counts as usable */
#endif
#ifndef SKYNOID_LEFT_INVERT
#define SKYNOID_LEFT_INVERT 0                    /* 1 if forward drives the left wheel backwards */
#endif
#ifndef SKYNOID_RIGHT_INVERT
#define SKYNOID_RIGHT_INVERT 0
#endif
#ifndef SKYNOID_CLIFF_REQUIRED
#define SKYNOID_CLIFF_REQUIRED 1                 /* 1: no motion until cal_floor has learned a floor (fail safe).
                                                  * 0: bench builds with the wheels in the air (Day 2 only). */
#endif
#ifndef SKYNOID_IS_MV_PER_AMP
#define SKYNOID_IS_MV_PER_AMP SKL_UNSET          /* BTS7960 IS output: millivolts per amp at the ADC pin. Calibrate
                                                  * against a current meter (step 2.4); unset = motor_ma reads 0 */
#endif
#ifndef SKYNOID_INA219_ADDR
#define SKYNOID_INA219_ADDR 0x41                 /* single topology (A0 pad closed); 0x40 in dual */
#endif

/* ---- identity ---------------------------------------------------------------- */
#define FW_MAJOR 1
#define FW_MINOR 0
#define PROTO_VERSION 1
#define ROLE_SINGLE 3

/* capability bits in HELLO (protocol-v0 section 3). Only what this firmware really has. */
#define CAP_DRIVE  (1u << 0)
#define CAP_CLIFF  (1u << 1)
#define CAP_POWER  (1u << 3)
#define CAP_BUZZER (1u << 4)
#define CAP_ACTUATING_CONSOLE (1u << 8)
#if SKYNOID_CONSOLE_ACTUATE
#define FW_CAPS (CAP_DRIVE | CAP_CLIFF | CAP_POWER | CAP_BUZZER | CAP_ACTUATING_CONSOLE)
#else
#define FW_CAPS (CAP_DRIVE | CAP_CLIFF | CAP_POWER | CAP_BUZZER)
#endif

/* ---- timing and sizes (config-defaults.yaml) ---------------------------------- */
#define UART_BAUD            460800
#define LINK_TIMEOUT_MS      500
#define SESSION_HOLD_MS      3000
#define SAFETY_LOOP_MS       5
#define MOTION_LOOP_MS       10
#define BRAKE_HOLD_MS        200
#define DRIVE_TTL_MAX_MS     2000
#define UART_RX_BUF          2048
#define UART_TX_BUF          2048
#define CONSOLE_LINE_MAX     120
#define LOGGER_RING_RECORDS  64
#define LOG_TEXT_MAX         120
#define MAINT_DEFAULT_MIN    10
#define MAINT_MAX_MIN        60
#define UNCAL_DUTY_CAP_PCT   40
#define BENCH_MAX_MS         2000

/* Node state ids (TLM state field, protocol-v0 section 6). */
typedef enum { ST_BOOT = 0, ST_SAFE = 1, ST_ARMED = 2, ST_ESTOP = 3, ST_FAULT = 4, ST_MAINT = 5 } base_state_t;

/* NACK codes (protocol-v0 section 7). */
enum { NACK_UNKNOWN_MSG = 1, NACK_BAD_LEN = 2, NACK_NOT_ARMED = 3, NACK_HAZARD_BLOCKED = 4,
       NACK_ESTOPPED = 5, NACK_INVALID_ARG = 6, NACK_BUSY = 7 };

#endif /* APP_CONFIG_H */
