# vl53l0x (VD, vendored)

- **Files:** `firmware/src/vendor/vl53l0x/vl53l0x.c` and `vl53l0x.h` (inside `src/` so the CMake glob builds them). This note sits in `firmware/vendor/vl53l0x/` (outside `src/` so the glob does not treat it as a source file); that is the one deviation from the policy path `firmware/vendor/<name>/`.
- **Source:** the user's own file `/mnt/xlvd/@datx/@projects/@-platformIO/esp-tmp/components/vl53l0x/vl53l0x.c` and `include/vl53l0x.h`, copied 2026-10-26 by claude-chat_a7d3. The original's header says: "Init sequence follows ST's reference API as condensed in Pololu's vl53l0x-arduino." Nobody has recorded where the original file came from (written by the user, taken from a repository, or generated): **UNVERIFIED**.
- **Licence: UNVERIFIED.** Pololu's `vl53l0x-arduino` and the ST API it condenses have their own licences; check both and the origin of the original file before this robot or its source is shared. Policy: [[dependencies]] rule 2 (MIT, BSD, Apache-2.0 are fine; unknown needs the user's decision). The user has not decided yet.
- **Purpose:** time-of-flight range sensor at I2C 0x29 (role `single`), single-shot ranging.
- **Used by:** `src/hal_range.c` (own task, every 50 ms) -> `base_get_status()` -> TLM `base_status.range_mm / range_status` and the console command `range`.
- **Correctness:** the init sequence and the `tuning` table are copied unchanged from the user's file, which built for an ESP32. **It has not been run on this robot** by anyone I know of. If `range` reads nothing or nonsense, suspect the driver, the wiring (XSHUT, 2V8 mode of the breakout) or the address before suspecting `hal_range.c`.

## Local changes (every edit, one line each)
1. `vl53l0x.c`: the six I2C primitives (`wr8`, `wr16`, `rd8`, `rd16`, `rd_multi`, `wr_multi`) now call `hal_i2c_write_reg` / `hal_i2c_read_reg` of `hal_sense.c`, because the bus is owned there and the legacy `driver/i2c.h` is end-of-life in ESP-IDF 6.x (the original also installed its own I2C driver on the same port, which would clash).
2. `vl53l0x.c`: `vl53l0x_init` lost its port, pin and frequency arguments and the `i2c_param_config` / `i2c_driver_install` block; everything after the model-id read is unchanged.
3. `vl53l0x.h`: no `driver/i2c.h` include; `vl53l0x_t` has no `port` member.
4. `vl53l0x.c`: added the `#include "hal.h"` and the header comment about the origin.
Not changed: register names, init order, SPAD handling, the `tuning` table, calibration, `vl53l0x_read_mm`.

## Update procedure
Re-copy the original, then re-apply the four changes above. After any update run the board checks: `devices` (vl53l0x row ok), `range` with a hand at 10, 30 and 50 cm and a ruler.

## Owner
claude-chat_a7d3 (vendored it); the user decides keep or replace (for example a library from the ESP component registry) and records the licence here.
