# Step 1.8: see what the robot senses, and make a missing sensor visible and safe

**Goal (one line):** every sensor the base board has can be read from the console (`status`, `batt`, `ir`, `range`, `touch`, `devices`, `selftest`); a dead battery sensor stops the robot instead of hiding; the buzzer says when the battery is low; touches reach the Pi as events; the servo driver exists but cannot move anything until you unlock it.

*Written 2026-10-28 by claude-chat_a7d3 from the task file `skynoid-console-base-cmds.md`. Steps A to F were written by claude-chat_a7d3, the servo files of step G by claude-chat_c2e5 after a takeover, then checked by a7d3. **Everything here is written, not built with ESP-IDF, not run on the board.** Pure logic (touch, servo maths) ran on a PC in a sandbox; see "What was and was not tested".*

## The idea, in plain words
Before this step the robot could drive and brake, but you could not ask it what it sensed, and a broken battery sensor was invisible. Each part below answers one question:

| part | question it answers | how you ask |
|---|---|---|
| A, battery sensor fault | "what if the battery sensor dies?" | nothing to ask: no reading for 3 s = no motion and one `sensor_fault` event |
| B, `devices`, `selftest` | "what is on the I2C bus, and is it the right chip?" | `devices -v`, `selftest` |
| C, distance sensor | "how far is the thing in front?" | `range`, `status` |
| E, buzzer | "will I hear a low battery?" | beeps by themselves |
| F, touch | "did someone touch the head or a hand?" | `touch`, and EVT touch frames to the Pi |
| G, servos | "can I run the servo pulse test?" | `servo test 0` in a trial build only |

### A: a battery sensor that goes silent
The power task averages the INA219 reading for 1 s. If it gets no good reading for 1 s it now sets the average to 0 ("no reading") instead of keeping the last good voltage forever. The safety logic counts the time without a reading (`SKYNOID_BATT_STALE_MS`, default 3000). After that it raises hazard bit 4 (sensor fault), which means "no motion", and sends one `sensor_fault` event (sensor 4, reason 3 = no reading). When a reading comes back the hazard clears. **Consequence:** a build without a working INA219 refuses all motion. For a bench without the sensor use `-DSKYNOID_BATT_STALE_MS=0`.

### B: `devices` and `selftest`
`devices` scans the bus (0x08 to 0x77), asks each expected chip a question only that chip answers (INA219: register 0 reads 0x399F after power-up; PCA9685: MODE1 bit 0 is set; VL53L0X: register 0xC0 reads 0xEE) and prints one row per device with a status (ok, moved, MISSING, absent, unexpected, FAULT, unverified). `devices -v` also shows the raw register values. `selftest` does the same and ends with PASS or FAIL. **This is what settles "is 0x40 the INA219 or the PCA9685"** without unplugging anything.

### C: the distance sensor (VL53L0X, address 0x29)
A task reads it every 50 ms. The chip driver is not ours: it is vendored from your own `esp-tmp/components/vl53l0x` (see `firmware/vendor/vl53l0x/VENDOR.md` and `DEPENDENCIES.md`); only the I2C calls were changed. Result: `range_status` 0 = ok (`range_mm` is real), 1 = nothing in front (8190), 2 = no sensor or no good reading for 0.5 s. If the sensor is missing the task retries every 5 s. It reads one shot at a time (the spec asked for continuous mode; this driver has none).

### E: the buzzer
Low battery: three beeps at once, then every 30 s while the pack stays low; it stops when the pack recovers. Critical: a 1 s on, 1 s off alarm until power-off. A missing battery sensor plays one error beep. No other beep may replace the critical alarm. To test without draining the pack, build with `-DSKYNOID_TEST_BATT_LOW_MV=12000` (and `-DSKYNOID_TEST_BATT_CRIT_MV=11500`), then remove the flags.

### F: touch pads (GPIO 13 head, 18 hand_l, 19 hand_r, 17 extra)
Every 10 ms each pad is read. A level must hold 30 ms to count (so bounce does nothing), nothing is reported for the first 500 ms (the pad chips calibrate at power-up), and a pad held for 20 s is called broken: one "up" and one `sensor_fault` (sensor 5 + zone, reason 1) are sent and the pad stays muted until it is released. Events go to the Pi as EVT 0x07 (zone, 1 down or 0 up), **pushed by the firmware, not polled**, and only while a session is running (see `link.c`: a touch with no Pi connected is counted by `touch` but not queued). The Pi decides what a touch means (`Robot.on_event`, `pat_sound`).

### G: servos (locked)
The PCA9685 driver and its maths exist, but the default build never talks to the chip and keeps GPIO4 (output enable) HIGH, which disables all servo outputs. Your servos are probably continuous-rotation (decision D20): they spin at a speed set by the pulse width and cannot hold an angle. So there are two switches in `platformio.ini`/`app_config.h`:
- `pio run -e single_servo_trial -t upload`: unlocks the `servo` command with **raw pulses that expire by themselves** (default 1 s, at most 3 s, then the channel goes fully off). `servo test 0` runs the D20 pulse test: 1000, 1500, 2000 microseconds, 1.5 s each. A continuous servo spins one way, stops (or creeps), then spins the other way. A positional servo moves to three angles and holds each.
- `-DSKYNOID_SERVO_TYPE_CONFIRMED=1`: **only after the test showed positional servos.** Unlocks `servo angle <ch> <deg> [ms]` with soft limits, a minimum-jerk move, a speed limit (90 deg/s, unmeasured), a power budget (joint weights, at most 6 moving) and 40 ms between starts.
A link loss, an e-stop and a critical battery switch the servo outputs off at once (`hal_servo_request_off`, no lock, from any task).

## Files you will touch
| file | what |
|---|---|
| `lib/skynoid_touch/**`, `test/test_touch` | touch rules, pure C, 12 tests |
| `lib/skynoid_servo/**`, `test/test_servo` | servo maths, pure C, 21 tests |
| `src/touch.{c,h}`, `src/hal_servo.c`, `src/hal_range.c`, `src/vendor/vl53l0x/**` | the hardware sides |
| `src/core/device_probe.{c,h}`, `src/console/cmd_{base,devices,servo}.c` | the console commands |
| `src/app_config.h` | switches (touch, servo, battery stale time, INA219 address, HELLO capability bits) |
| `lib/skynoid_logic/**`, `lib/skynoid_dispatch/**`, `src/link/link.{c,h}`, `src/base.c` | additive edits (battery fault, EVT touch, live events, servo off on e-stop) |

## What was and was not tested
| thing | status |
|---|---|
| touch rules (`test_touch`, 12) | ran on a PC with a stand-in Unity under ASan and UBSan: 12 of 12 |
| servo maths (`test_servo`, 21) | ran on a PC the same way: 21 of 21. An earlier version had a backwards step in the move curve; claude-chat_c2e5 fixed it (Q30 maths) |
| battery fault tests in `test_base` (6 new), `test_dispatch` (1 new) | the six ran in a sandbox in an earlier session; the dispatch one is a three-line change and has **not** been run |
| everything with ESP-IDF in it (`touch.c`, `hal_servo.c`, `hal_range.c`, `link.c`, console commands) | **not built, not run** |
| the vendored VL53L0X driver | never run on this robot; licence unverified |

## Two corrections found while writing this
1. **INA219 address.** A 2026-10-27 edit moved the default `SKYNOID_INA219_ADDR` to 0x40 because the first scan showed nothing at 0x41. The next scan showed 0x41 present and your `batt` read 11404 mV from 0x41, so the INA219 is at **0x41** and 0x40 is the PCA9685. The default is 0x41 again (and the servo driver refuses to start if both addresses are equal).
2. **HELLO capabilities.** The firmware now reports `range` (bit 2) and `touch` (bit 6) as well as drive, cliff, power, buzzer and the actuating console. It does not report `servos` (bit 7), because that bit promises framed servo commands, which do not exist yet.

## Run these
```
pio test -e native
pio run -t clean -e single          # new source files: CMake must re-run
pio run -e single
# flash, then in picocom (-b 460800):
devices -v
selftest
batt
status
range            # wave a hand at 10, 30, 50 cm against a ruler
touch            # all four "released" at rest; press each pad, run again
```
For the servo pulse test: `pio run -t clean -e single_servo_trial`, build and flash, **one servo on channel 0, servo rail on, horn removed, hand near the power switch**, then `maint on` (the `servo` command is tier 1 since step 1.6b, see `step-1.6b-maint-glue.md`) and `servo test 0`. Tell me what happened (continuous or positional).

## Open decisions for you
- Keep the vendored VL53L0X driver (licence unverified) or replace it with a registry library?
- Pad polarity: if every pad shows TOUCHED at rest, rebuild with `-DSKYNOID_TOUCH_ACTIVE_LEVEL=0`.
- Buzzer polarity is still unmeasured (`BUZZER_ACTIVE_LEVEL` in `hal_indicator.c`).
- Servo type (D20): needs the pulse test above.
