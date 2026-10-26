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
- INA219 address 0x41 (A0 pad closed). NOT confirmed on 2026-10-26: `i2c scan` shows no ACK at 0x41, see section 5.
- BTS7960: both inputs low with EN high really brakes.
- Which OLED chip and address; VL53L0X needs a vendored, licensed driver (not written).

## 5. Bench results (pasted by the user; nothing here was run by an agent)

Port `/dev/ttyUSB1`, link and console on UART0 at 460800. Firmware env not stated by the user (earlier log: `single_bench`).

### 2026-10-26 about 14:21, `python -m skynoid.tools.doctor --port /dev/ttyUSB1`, run twice, board on the bench
| Line | Run 1 (sensors untouched) | Run 2 (user triggered the IR sensors; how and which not stated) |
|---|---|---|
| hello | role single, fw 1.0, proto 1, boot #1, chip d4e9f4e26254 | same |
| capabilities | drive, cliff, power, buzzer, actuating_console | same |
| last reset | power-on | power-on |
| link | 50/50 PONGs, p95 50.24 ms | 50/50 PONGs, p95 50.30 ms |
| status stream, state, hazards | TLM arriving, safe, none | same |
| range | 0 mm (error) | same (stub: `base_get_status()` hard-codes it until step 3.5) |
| battery | no reading (0 mV) | same |
| ir raw | 0 0 0 0 | 783 862 4095 4095 |
| frame errors (host counter, restarts each run) | 972 | 974 |

Readings: the IR path works end to end (values changed). `boot #1` both times: opening the port did not reset the node. p95 of about 50 ms for a PING round trip is far above the spec target (under 15 ms, `#verify`) and nearly identical in both runs; cause not found. About 970 bad frames per run while PONGs and telemetry decode fine; cause not found (the host counts a piece between `0x00` delimiters that fails the frame check and is under 90 percent printable text, plus bursts over 512 bytes without a delimiter). Not yet done: the raw-byte capture to see what the junk is. The IR polarity, the sensor order and the floor/no-floor numbers of table 1 are still unmeasured.

### 2026-10-26 16:03, console `i2c scan` (step 1.5b part, first run on the board)
```
bus0 SDA GPIO21 SCL GPIO22
0x26  (no guess)
0x29  vl53l0x
0x40  ina219 or pca9685 (reg 0x00: 0x399F ina219, 0x11 pca9685)
0x70  pca9685 all-call
4 device(s)
```
Readings (interpretation, not measured): 0x29 is the VL53L0X. 0x40 together with 0x70 most likely is a PCA9685 (it answers its all-call address by default); not confirmed, because `i2c read` does not exist yet. Nothing answers at 0x41 or at the other INA219 addresses (0x44, 0x45), so unless the INA219 is the 0x40 device it is not on the bus, which would explain `battery 0 mV` (the power task reads it at `SKYNOID_INA219_ADDR`, 0x41). Test to tell them apart: unplug the PCA9685 board and rescan; 0x40 and 0x70 should both vanish if it was the PCA9685. 0x26 is unidentified. No OLED answers at 0x3C or 0x3D (not needed before step 4.1).

## 4. Bench order (wheels in the air, `pio run -e single_bench -t upload`)
1. `python -m skynoid.tools.ping_test --port <port>`: expect every PONG.
2. `python -m skynoid.tools.doctor --port <port>`: read-only, never arms.
3. Set `duty_per_mm_s_x1000` (id 11) and `wheel_base_mm` (id 10) with `set_param`, arm, then a short drive
   with a small TTL; confirm the wheels stop when the TTL ends and when you unplug the USB cable.
4. Only after the floor values in table 1 are set and built into the normal `single` env: `cal_floor` on the floor.
