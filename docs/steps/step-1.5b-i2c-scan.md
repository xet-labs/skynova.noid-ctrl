# Step 1.5b (part): `i2c scan` and `i2c probe` on the console

**Goal (one line):** type `i2c scan` and see which addresses on I2C bus 0 (GPIO21 SDA, GPIO22 SCL) answer, with a guess of what each one is; type `i2c probe 0x41` to test one address.

*Written in the 2026-10-26 chat session. The user built, flashed and ran it on 2026-10-26 (output below); there is no PC test because no pure logic was added. The split is ours, not in `sprint-steps`: step 1.5 lists `console/cmd_i2c.c` with `i2c scan`, `i2c probe` and `i2c speed` (tier 0). This step does the first two. The `devices` table and `selftest` (the rest of 1.5b) are not written yet.*

## The idea, in plain words
An I2C device answers its own 7-bit address with an ACK bit. A scan sends each address 0x08 to 0x77 and notes who ACKs. `hal_i2c_probe()` already does that for one address (`i2c_master_probe` in `hal_sense.c`); this step is a loop around it plus a lookup of a guess in the registry library (`skr_addr_guess`, the `address_guess` table of `devices.yaml`).

```
 "i2c scan" -> console_execute -> cmd_i2c -> for 0x08..0x77: hal_i2c_probe(addr)  (bus lock, 50 ms timeout)
                                                  \-> ACK: print "0x41  ina219 (A0 closed) or pca9685 (A0)"
 stops after 400 ms in total (the console task also ends a link session after 500 ms of silence)
```
One line per address. **Two devices on the same address show as one line**: a scan cannot see that. The two OLEDs at 0x3C, or an INA219 and a PCA9685 both at 0x40, look like a single device. Compare the list with what you expect from `devices.yaml`.

## Files you will touch
| file | what |
|---|---|
| `firmware/src/console/cmd_i2c.c` | new: the `i2c` command (scan, probe), about 60 lines of code |
| `firmware/src/console/console_uart.h`, `console_uart.c` | one line each: `console_register_i2c()` declared and called in `console_start()` |

## What you will see when it works
Connect with `python -m skynoid.tools.shell --port /dev/ttyUSB1` or `picocom -b 460800 /dev/ttyUSB1` and type:
```
skynoid> i2c scan
bus0 SDA GPIO21 SCL GPIO22
0x29  vl53l0x
0x41  ina219 (A0 closed) or pca9685 (A0)
2 device(s)
OK
skynoid> i2c probe 0x41
0x41 ACK
OK
skynoid> i2c probe 0x40
0x40 no ACK
OK
```
That is an **example of the format**, not a promise: the lines you get depend on what is wired. `i2c probe` takes decimal or `0x` hex and accepts 0x08 to 0x77; anything else prints `ERR invalid_arg usage: i2c probe 0x08..0x77`.

## Result on the board (2026-10-26 16:03, pasted by the user)
```
skynoid> i2c scan
bus0 SDA GPIO21 SCL GPIO22
0x26  (no guess)
0x29  vl53l0x
0x40  ina219 or pca9685 (reg 0x00: 0x399F ina219, 0x11 pca9685)
0x70  pca9685 all-call
4 device(s)
OK
```
So the command works. What the addresses mean (0x40 plus 0x70 most likely a PCA9685, no INA219 at 0x41, 0x26 unknown) is written up in `docs/measurements.md` section 5. `i2c probe` was not run yet.

## How to run it
1. Build and flash the env you used for the console (for example `pio run -e single_bench -t upload`).
2. Open the console as above and type `i2c scan`. Paste the output here.

## If it does not work (most likely causes)
1. **`ERR unknown_msg`:** the old firmware is still on the board; rebuild and upload. `help` must list `i2c`.
2. **`0 device(s)`:** either nothing is wired, SDA and SCL are swapped, the modules have no power, or there are no pull-ups (the firmware enables the internal ones, which are weak). `hal_i2c_probe` also returns "no ACK" if the bus failed to initialise: look for `i2c bus init failed` in the boot log.
3. **`ERR busy scan too slow, bus stuck?`:** a device is holding SDA low or SCL is shorted. Power-cycle the modules; unplug them one by one and rescan.
4. **A device is at the wrong address:** an INA219 whose A0 pad is not closed sits at 0x40, not 0x41 (the single topology expects 0x41); the firmware then reads no battery voltage (`batt_mv` 0, as `doctor` showed).

## Known limits (honest list)
- `i2c speed` is not implemented: the 400 kHz clock is a private constant in `hal_sense.c`. Showing it needs either a one-line change there (move the constant to `app_config.h`) or a getter in `hal.h`; I did not touch `hal_sense.c` without your OK.
- No identity check: the guess is a table lookup by address, not a register read. The `devices` table (next part of 1.5b) will do the register checks.
- A scan lists only addresses that ACK an address-only probe; a chip that ignores such probes (rare) would not show up.

## Checkpoint
Explain in your own words why the two OLEDs at 0x3C appear as one line in a scan, and what that means for showing different images on them.
