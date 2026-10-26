# Measurements the firmware is waiting for

Written 2026-10-25 by claude-code_c9a1. The firmware refuses to guess: every value below starts as
`SKL_UNSET` or a marked assumption, and the code says so instead of inventing a number. Fill in the
"Your value" column, then set the value as shown. Nothing here has been measured yet.

## 1. Compile-time values (`src/app_config.h`, set with `-D` in `platformio.ini`)

| Value | What it is | How to measure | Effect while unset | Your value |
|---|---|---|---|---|
| `SKYNOID_NO_FLOOR_ADC` | ADC reading of an IR sensor with nothing under it (raw 0..4095) | Lift the robot, read `ir_view` or `doctor` "ir raw" for each sensor, take the typical value | `cal_floor` is refused (`invalid_arg`), so with the default `SKYNOID_CLIFF_REQUIRED=1` no motion is allowed at all | |
| `SKYNOID_MIN_CONTRAST_PCT` | smallest gap between floor and no-floor that counts as usable, in percent | Compare the floor reading with the no-floor reading on your floor and on a dark mat | same as above | |
| `SKYNOID_IR_POLARITY` | does the reading fall or rise at an edge | Move a sensor over an edge while watching `ir_view` | default `SKL_POL_FLOOR_HIGH` (reading FALLS at an edge) is an assumption | |
| `SKYNOID_IS_MV_PER_AMP` | BTS7960 IS output, millivolts per amp at the ADC pin | Run one motor against a current meter, note the IS pin voltage | `motor_ma` reads 0, the stall guard cannot work | |
| `SKYNOID_LEFT_INVERT`, `SKYNOID_RIGHT_INVERT` | 1 if forward turns that wheel backwards | One short bench drive with the wheels in the air | default 0 | |
| `BUZZER_ACTIVE_LEVEL` (`src/hal_indicator.c`) | pin level that sounds the buzzer | Try 1, then 0 | assumed 1 (active high) | |
| ADC full scale | the 12 dB attenuation gives about 3100 mV on an ESP32 | Compare a known voltage with the raw reading | assumed 3100 mV | |

## 2. Runtime parameters (`set_param`, from the host or later the console; RAM only, send again after each HELLO)

| Id | Name | Needed for | Measure | Unset means |
|---|---|---|---|---|
| 10 | `wheel_base_mm` | turning (`w` not 0) | distance between the two wheel contact points, millimetres | a drive with `w` not 0 is refused (`invalid_arg`); straight drives work |
| 11 | `duty_per_mm_s_x1000` | real speeds in mm/s | duty (permille) needed per mm/s: run at a few duties, time a measured distance | the wheels still turn, but on an uncalibrated fallback with a capped duty, so the commanded mm/s are not real speeds |
| 12 | `min_start_duty_pct` | slow speeds | lowest duty at which both wheels start from standstill | no minimum applied |
| 8 | `stall_soft_ma` | stall guard | motor stall current from the datasheet or a measurement (hold the stall 2 s at most) | guard off |
| 9 | `stall_hard_ma` | stall guard | same, higher than the soft limit | guard off |

The other parameters (speed cap, turn cap, acceleration, cliff ratios, battery limits) have defaults from
`10-specs/config-defaults.yaml` in the vault.

## 3. Facts about the board to confirm once (mark each `#verify` in the code when done)
- Which board it really is (`board_pins.h` has the pin map; `pins` in the console prints it).
- I2C pull-up resistors present on the bus (the firmware also enables the internal ones).
- INA219 address 0x41 (A0 pad closed).
- BTS7960: both inputs low with EN high really brakes.
- Which OLED chip and address; VL53L0X needs a vendored, licensed driver (not written).

## 4. Bench order (wheels in the air, `pio run -e single_bench -t upload`)
1. `python -m skynoid.tools.ping_test --port <port>`: expect every PONG.
2. `python -m skynoid.tools.doctor --port <port>`: read-only, never arms.
3. Set `duty_per_mm_s_x1000` (id 11) and `wheel_base_mm` (id 10) with `set_param`, arm, then a short drive
   with a small TTL; confirm the wheels stop when the TTL ends and when you unplug the USB cable.
4. Only after the floor values in table 1 are set and built into the normal `single` env: `cal_floor` on the floor.
