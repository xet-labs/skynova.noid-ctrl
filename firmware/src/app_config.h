/*
 * app_config.h - build-time switches and fixed numbers for the controller firmware.
 *
 * Switches can be overridden in platformio.ini with -DNAME=value (spec: config-defaults.yaml "console").
 * Runtime parameters (speed cap, wheel base, ...) live in params.c, not here.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "skynoid_logic.h"

/* ---- console and link channels (step 1.7, docs/steps/step-1.7-uart-channels.md) -------------------
 * Every default is an #ifndef: override with -D in platformio.ini. Pin defaults and all the build-time
 * checks live in core/chan_cfg.h (included below, after the numbers are known).
 * Shared mode (same UART number for both) is what the firmware always did: one port, frames and text. */
#ifndef UART_LINK_NUM
#define UART_LINK_NUM 0              /* UART carrying the framed link: 0 = USB bridge (easy to deploy, Pi connects here) */
#endif
#ifndef UART_LINK_BAUD
#define UART_LINK_BAUD 460800
#endif
#ifndef UART_CONSOLE_NUM
#define UART_CONSOLE_NUM UART_LINK_NUM   /* same number = shared; 2 = console on its own UART2 pins */
#endif
#ifndef UART_CONSOLE_BAUD
#define UART_CONSOLE_BAUD 115200     /* used only when the console has its own UART; shared mode runs at UART_LINK_BAUD */
#define UART_CONSOLE_BAUD_IS_DEFAULT 1   /* marker: lets chan_cfg.h tell "left out" from "set on purpose" */
#endif
#ifndef UART0_FREE_BAUD
#define UART0_FREE_BAUD 115200       /* baud of UART0 when NEITHER channel uses it: chan_init sets it on purpose */
#endif
/* UART_LINK_TX_PIN, UART_LINK_RX_PIN, UART_CONSOLE_TX_PIN, UART_CONSOLE_RX_PIN: defaults in core/chan_cfg.h */
#include "core/chan_cfg.h"
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
#ifndef SKYNOID_BATT_STALE_MS
#define SKYNOID_BATT_STALE_MS 3000               /* no battery reading for this long = sensor fault (no motion, one
                                                  * SENSOR_FAULT event). The 1 s average needs about 1 s after boot.
                                                  * 0 turns the check off: use -DSKYNOID_BATT_STALE_MS=0 on a bench
                                                  * without the INA219. */
#endif
/* Bench test of the low-battery beeps without draining the pack (leave both undefined for real use). Examples for
 * a platformio.ini env, with a pack at about 11.4 V:
 *   -DSKYNOID_TEST_BATT_LOW_MV=12000    LOW after 5 s: low-battery beep, repeats every 30 s; stops when above 12150 mV
 *   -DSKYNOID_TEST_BATT_CRIT_MV=11500 together with the LOW line: CRIT after 5 s, state FAULT, motion refused,
 *                                       critical alarm (1 s on, 1 s off) until power cycle
 * Both go through skp_set, so the same limits as the runtime parameters apply (low 9000..12600, crit 8500..12000,
 * crit must stay below low); a rejected value is logged as a warning at boot and the default stays. */

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
/* UART_BAUD (fixed 460800) is gone: the link baud is UART_LINK_BAUD, set in the channel block at the top. */
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
