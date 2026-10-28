# Step 3.4b: the servo current sensor (`acs`) and the `buzz` command

**Goal (one line):** read the ACS712 servo-rail current sensor on GPIO32 from the console, and play any buzzer pattern on demand, so the low-battery beep can be heard and the servo current can be measured.

*Written 2026-10-28 in a chat session. Not built with `pio`, not run on a board. The split is ours, not in `sprint-steps`: step 3.4 (buzzer and battery) was already in the firmware; this adds the two missing console pieces.*

## What was already there (not changed)
The low-battery beep exists: `lib/skynoid_logic/src/power.c` decides LOW (10650 mV) and CRIT (10050 mV) with 5 s persistence and 150 mV hysteresis; `src/base.c` plays `low_battery` (three 120 ms beeps) on LOW and repeats it every 30 s until the pack recovers; CRIT starts the 1 s on / 1 s off alarm and turns motors and servos off. The **INA219 (0x41) is the only battery source**. To hear it without draining the pack, flash `pio run -e single_batt_test -t upload` (LOW threshold raised to 12000 mV; read `batt` first and pick a value a few hundred mV above the pack).

## The idea, in plain words
```
 ACS712 (5 V part) -> divider -> GPIO32 = ADC1 channel 4 -> hal_adc_read(ADC_CH_ACS712) -> `acs` (16 reads averaged)
 "buzz <pattern>" -> (tier 1: needs maint on) -> hal_buzzer_play(id)   same call the base layer uses for real events
```
The ACS712 output is bidirectional and sits at half of its supply when no current flows, so a current is a **difference from a zero point**. The divider values are not in the specs, so the firmware does not guess: until you give it two numbers it prints raw counts and millivolts only.

## Files you will touch
| file | what |
|---|---|
| `src/board/board_pins.h` | `ADC_CH_ACS712` 4 (GPIO32 = ADC1 channel 4) |
| `src/hal_sense.c` | channel 4 is configured in `hal_adc_init` (it was not, so a read would have failed) |
| `src/app_config.h` | `SKYNOID_ACS712_ZERO_MV`, `SKYNOID_ACS712_MV_PER_AMP`, both `SKL_UNSET` by default |
| `src/console/cmd_base.c` | the tier 0 command `acs` |
| `src/console/cmd_buzz.c` (new), `console_uart.h`, `console_uart.c` | the tier 1 command `buzz` and its registration |

## What you will see when it works
```
skynoid> acs
acs712 GPIO32 (ADC1 ch 4): raw 2048 of 4095, about 1550 mV at the pin
current not calibrated (no mA shown). Servo rail ON and every servo idle:
this reading is the zero point. Rebuild with -DSKYNOID_ACS712_ZERO_MV=1550
and -DSKYNOID_ACS712_MV_PER_AMP=<sensor mV per A x divider ratio>
OK
skynoid> maint on
skynoid> buzz low_battery        (three 120 ms beeps)
buzz low_battery
OK
```
The numbers above are an **example of the format**, not a promise. A value at a rail (raw 5 or less, or 4090 or more) prints `verdict NOT SENSING`.

## How to run it
1. `pio run -t clean -e single` once (a new source file), then build and flash your usual env.
2. `acs` with the servo rail OFF: expect a low reading or NOT SENSING (the sensor has no 5 V). With the rail ON and servos idle: a mid-range value. Note it; that is the zero point.
3. Calibrate: `SKYNOID_ACS712_MV_PER_AMP` = sensor sensitivity x divider ratio (the 30 A part is 66 mV/A at the sensor; the divider is whatever you built, 10k over 20k would give 2/3, so 44 mV/A). Check it with a meter in line on the servo rail, then add both `-D` flags to an env and re-run `acs` while a servo moves.
4. `maint on`, then `buzz chirp`, `buzz low_battery`, `buzz critical`, `buzz off`. No sound at all, or an inverted sound, means `BUZZER_ACTIVE_LEVEL` in `hal_indicator.c` is wrong for your module.

## If it does not work (most likely causes)
1. **`ERR busy ADC read failed`:** the ADC channel setup failed; look for `adc channel 4 config failed` in the boot log.
2. **`NOT SENSING` with the rail on:** divider or wiring; measure the GPIO32 pin voltage with a multimeter. It must stay below about 3.1 V or the ADC clips.
3. **Reading wanders by tens of mV:** normal for the ESP32 ADC with no filtering; the zero point is a typical value, and the 16-read average only removes fast noise.

## Known limits (honest list)
- Nothing in the safety logic uses the ACS712 yet: no soft or hard servo-current threshold, no `V_comp` term, `sks_current_scale_pct` of the servo library is not fed. Those need measured thresholds (spec S11) and are later steps.
- `ACS_FULL_SCALE_MV` 3100 is the nominal 12 dB figure, as in `base.c`; the ESP32 ADC is not linear, so mV at the pin can be off by tens of mV (`#verify` against a multimeter).
- `buzz` does not know about a real critical-battery alarm in progress and would replace it; it is tier 1, so a person is at the terminal. `CMD 0x40` (framed buzzer command) is still not in the dispatcher.

## Checkpoint
Explain in your own words why the ACS712 reading cannot be turned into amps without a zero point, and why the battery voltage does not need one.
