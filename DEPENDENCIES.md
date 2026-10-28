# Dependencies (live register)

Policy and the starting register of known dependencies: vault `10-specs/dependencies.md`. This file lists only what has actually been added to this repo; framework and tool versions (ESP-IDF, PlatformIO, esptool) still need their rows once the user records them in `docs/measurements.md`.

### vl53l0x (VD)
- Source: the user's own file `components/vl53l0x/` in `/mnt/xlvd/@datx/@projects/@-platformIO/esp-tmp/`, copied 2026-10-26. Its header says the init follows ST's reference API as condensed in Pololu's vl53l0x-arduino. Original origin: unknown.
- Licence: **UNVERIFIED** (user decision pending), see `firmware/vendor/vl53l0x/VENDOR.md`.
- Purpose: time-of-flight range sensor driver (I2C 0x29).
- Used by: `firmware/src/hal_range.c`.
- Local changes: four, listed in `firmware/vendor/vl53l0x/VENDOR.md` (I2C primitives now go through `hal_sense.c`; no own I2C driver install).
- Update procedure: in `VENDOR.md`.
- Owner: claude-chat_a7d3 (vendored), the user decides keep or replace.
