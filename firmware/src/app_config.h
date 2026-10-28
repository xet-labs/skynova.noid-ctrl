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
/* Servo rail current sensor (ACS712 behind a voltage divider, GPIO32). Both values are measured at the ADC PIN, after the
 * divider, and both stay unset until you measure them (the divider values are not in the specs): `acs` then prints
 * raw counts and millivolts only.
 *   SKYNOID_ACS712_ZERO_MV    the pin voltage with the servo rail ON and every servo idle (`acs` prints it)
 *   SKYNOID_ACS712_MV_PER_AMP pin millivolts per amp = sensor sensitivity x divider ratio (the 30 A part is 66 mV/A at the
 *                             sensor; with a 10k/20k divider the ratio is 2/3). Check it with a meter in line. */
#ifndef SKYNOID_ACS712_ZERO_MV
#define SKYNOID_ACS712_ZERO_MV SKL_UNSET
#endif
#ifndef SKYNOID_ACS712_MV_PER_AMP
#define SKYNOID_ACS712_MV_PER_AMP SKL_UNSET
#endif
#ifndef SKYNOID_INA219_ADDR
/* Bench I2C scan (docs/measurements.md §5, 2026-10-26) found nothing at 0x41 and a device at 0x40.
 * INA219: A0 open = 0x40, A0 closed = 0x41. Changed default to 0x40. #verify: confirm this is the
 * INA219 (not the PCA9685) by running `i2c read 0x40 0x00 2` in the console and checking the reply:
 *   INA219 default config register  => 0x39 0x9F
 *   PCA9685 default mode1 register  => 0x01 0x11 (or similar; depends on firmware state)
 * If it is a PCA9685, unplug it, rescan, and find the INA219 on its real address.
 * CORRECTION 2026-10-28: the 0x40 default described above came from the FIRST scan (2026-10-26 16:03), which showed
 * nothing at 0x41. The next scan listed 0x29, 0x40, 0x41, 0x70, and the user's `batt` on the board read 11404 mV from
 * 0x41 (task skynoid-console-base-cmds, step B). So the INA219 is at 0x41 and 0x40 is the PCA9685 (all-call 0x70):
 * with 0x40 here a battery read would hit the servo controller, and the servo driver refuses to start when both
 * addresses are equal. The default is 0x41 again; `devices -v` shows what answers where. */
#define SKYNOID_INA219_ADDR 0x41                 /* A0 closed; measured on the board, see the correction above */
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

/* ---- touch pads (step F; GPIO 13 head, 18 hand_l, 19 hand_r, 17 extra: board/board_pins.h) ----------------- */
#ifndef SKYNOID_TOUCH_ENABLE
#define SKYNOID_TOUCH_ENABLE 1                   /* 0 leaves the touch task out of the build */
#endif
#ifndef SKYNOID_TOUCH_ACTIVE_LEVEL
#define SKYNOID_TOUCH_ACTIVE_LEVEL 1             /* pin level while touched. UNMEASURED: TTP223-style modules are
                                                  * active high by default; flip to 0 if `touch` shows pressed at rest.
                                                  * The internal pull is the opposite level, so a pad that is not
                                                  * wired reads "released" instead of floating into phantom events. */
#endif
#ifndef SKYNOID_TOUCH_HOLD_MS
#define SKYNOID_TOUCH_HOLD_MS 30                 /* a level must be steady this long to count (debounce) */
#endif
#ifndef SKYNOID_TOUCH_BOOT_IGNORE_MS
#define SKYNOID_TOUCH_BOOT_IGNORE_MS 500         /* modules recalibrate at power-up: report nothing for this long */
#endif
#ifndef SKYNOID_TOUCH_STUCK_MS
#define SKYNOID_TOUCH_STUCK_MS 20000             /* down this long = broken pad: UP + sensor_fault once, zone muted until
                                                  * released. 0 turns the check off. */
#endif

/* ---- servos (step G; PCA9685 on I2C bus 0, output-enable on GPIO4: board/board_pins.h) ------------------------
 * LOCKED BY DEFAULT: with neither switch below set, the firmware never talks to the PCA9685 and GPIO4 stays HIGH
 * (outputs disabled). The BOM servos are probably continuous-rotation (decision D20), which cannot be aimed.
 *   -DSKYNOID_SERVO_TRIAL=1           console `servo` commands: raw pulse width with a time limit, `servo test`
 *                                     (the D20 two-minute pulse test). Safe for either servo type.
 *   -DSKYNOID_SERVO_TYPE_CONFIRMED=1  ONLY after the pulse test showed POSITIONAL servos: unlocks `servo angle`
 *                                     (soft limits, minimum-jerk move, speed limit, power budget). */
#ifndef SKYNOID_SERVO_TRIAL
#define SKYNOID_SERVO_TRIAL 0
#endif
#ifndef SKYNOID_SERVO_TYPE_CONFIRMED
#define SKYNOID_SERVO_TYPE_CONFIRMED 0
#endif
#define SKYNOID_SERVO_ACTIVE (SKYNOID_SERVO_TRIAL || SKYNOID_SERVO_TYPE_CONFIRMED)
#ifndef SKYNOID_PCA9685_ADDR
#define SKYNOID_PCA9685_ADDR 0x40                /* A0..A5 open. Must differ from SKYNOID_INA219_ADDR: the driver refuses to
                                                  * start otherwise (it would write to the battery monitor). */
#endif
#ifndef SKYNOID_PCA9685_OSC_HZ
#define SKYNOID_PCA9685_OSC_HZ 25000000u         /* nominal. A real chip is off by a few percent: measure the 50 Hz period
                                                  * on a scope (spec U1) and put the measured frequency here. */
#endif
#ifndef SKYNOID_SERVO_CHANNELS
#define SKYNOID_SERVO_CHANNELS 8                 /* channels 0..N-1 are accepted (the chip has 16); spec: 8 servos */
#endif
#ifndef SKYNOID_SERVO_TTL_DEFAULT_MS
#define SKYNOID_SERVO_TTL_DEFAULT_MS 1000        /* raw pulse commands expire: a continuous servo would spin forever */
#endif
#ifndef SKYNOID_SERVO_TTL_MAX_MS
#define SKYNOID_SERVO_TTL_MAX_MS 3000
#endif
#ifndef SKYNOID_SERVO_VMAX_DPS
#define SKYNOID_SERVO_VMAX_DPS 90                /* speed limit of angle moves, degrees per second (UNMEASURED, #verify) */
#endif
#ifndef SKYNOID_SERVO_BUDGET
#define SKYNOID_SERVO_BUDGET 6                   /* power budget: sum of joint weights moving at once (spec default 6) */
#endif
#ifndef SKYNOID_SERVO_STAGGER_MS
#define SKYNOID_SERVO_STAGGER_MS 40              /* motion starts at least this far apart (spec: 40 ms) */
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
#define CAP_RANGE  (1u << 2)             /* VL53L0X task exists (step C); TLM range_status is 2 while the sensor is missing */
#define CAP_TOUCH  (1u << 6)             /* touch task and EVT touch exist (step F) */
/* No CAP_SERVOS (bit 7) on purpose: that bit promises framed servo commands (0x20..0x3F), which do not exist yet (the
 * servo driver has console commands in a trial build only). Eyes (bit 5) are not built. */
#if SKYNOID_TOUCH_ENABLE
#define FW_CAPS_TOUCH CAP_TOUCH
#else
#define FW_CAPS_TOUCH 0u
#endif
#if SKYNOID_CONSOLE_ACTUATE
#define FW_CAPS (CAP_DRIVE | CAP_CLIFF | CAP_RANGE | CAP_POWER | CAP_BUZZER | FW_CAPS_TOUCH | CAP_ACTUATING_CONSOLE)
#else
#define FW_CAPS (CAP_DRIVE | CAP_CLIFF | CAP_RANGE | CAP_POWER | CAP_BUZZER | FW_CAPS_TOUCH)
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
