# Step 1.4: the link (PING gets a PONG, a session starts with HELLO)

**Goal (one line):** from the Pi (or your laptop) run the ping test and get 200 of 200 PONGs back, with the round-trip time measured; stop pinging and the board brakes itself after 500 ms.

*Written by `claude-chat_e3a7` on 2026-10-25. Nothing here has been built with `pio` or run on a board. The pure library is tested on the PC (in my sandbox, with a Unity stand-in); the board files were only syntax-checked against stub headers.*

## The idea, in plain words
Until now the board only talks text (step 1.3). The binary link on the **same UART0** adds three rules from `protocol-v0` section 9:

1. **Silent until spoken to.** The node sends nothing binary unless a PING arrived.
2. **A session starts with the first PING.** The node answers **HELLO** (who am I: role, firmware, why did I reset, chip id, boot count), then **PONG** (echo of the host's time, the node's time, the hazard mask). Every later PING gets only a PONG.
3. **A session ends after 500 ms without a PING.** The board then calls `base_link_lost()` (brake, SAFE). If the Pi crashes, the robot stops by itself.

```
 USB bytes -> console task -> [wire parser] --frame--> link_on_frame -> PING? -> HELLO (first) + PONG
                                   \--text--> line editor (step 1.3b)          other CMD -> NACK unknown_msg
 every loop (<= 20 ms): link_tick -> 500 ms silent? -> base_link_lost()
 all output: link_send -> encode -> logger_write_bin -> logger task -> UART  (one writer, ordered with text)
```
The session timing and the byte layout of PING, PONG and HELLO are **pure C** in `lib/skynoid_link` (tested on the PC). Two of the tests build a whole PONG and HELLO frame and compare it with the rows of `protocol-v0` section 8 byte for byte.

## Files you will touch
| file | what |
|---|---|
| `firmware/lib/skynoid_link/include/skynoid_link.h`, `src/session.c` | session start/end/hold, PING parse, PONG and HELLO payloads (pure C) |
| `firmware/test/test_link/test_link.c` | 11 PC tests |
| `firmware/src/link/link.h`, `link.c` | clock, node sequence counter, HELLO content, calls into `base_*` |
| `firmware/src/core/logger.{h,c}` | new `logger_write_bin`: binary frames go through the same queue, without `\n` to `\r\n` conversion |
| `firmware/src/console/console_uart.c` | hands valid frames to `link_on_frame`, calls `link_tick`, calls `link_init` |
Host side already exists: `skynoid/tools/ping_test.py` and `doctor.py` in the cmd repo.

## What you will see when it works
```
$ python -m skynoid.tools.ping_test --port /dev/ttyUSB0 --count 200
received 200/200 (0.0% lost)
round trip ms: min <a>  median <b>  p95 <c>  max <d>
$ python -m skynoid.tools.doctor --port /dev/ttyUSB0
PASS  hello  role single, firmware 1.0, proto 1, boot #1, chip <6 bytes>
```
Record the p95 round trip in `docs/measurements.md` (the spec target is measured, not assumed). Stop the tool: in a terminal on the board `log dump` shows `link lost: no PING for 500 ms`.

## How to run it
1. PC: `cd firmware && pio test -e native -f test_link` (expect 11 passing); `-f test_wire` still 11.
2. Board: `pio run -e single -t clean` (new `src/link/` folder is only picked up after CMake re-runs), `pio run -e single -t upload`.
3. Close picocom (only one program may hold the port), run `ping_test` and `doctor` as above. Opening the port resets the board (DTR/RTS); the tool waits for HELLO.

## If it does not work (the 3 most likely causes)
1. **Build error: `esp_mac.h`, `esp_timer.h` or `driver/uart.h` not found:** ESP-IDF 6.1 splits components. Add `REQUIRES esp_driver_uart esp_timer esp_hw_support` to `src/CMakeLists.txt`. Paste the exact error.
2. **`ping_test` says no HELLO / all lost, but the console works:** baud (460800 on both sides), or another program holds the port. A frame that fails CRC is counted in the host's `crc_errors` line; if it is not zero, check the cable and ferrite.
3. **Garbled text in picocom after a ping test:** expected: binary frames look like garbage in a terminal. That is why the test tools exist.

## Known limits (honest list)
- **No HELLO at boot.** The spec says one HELLO at boot and one per session; this step sends HELLO only at the session start (the host tools always start with a PING, so they get it). The boot HELLO needs a rule for "who listens at boot" and is left for step 1.5.
- **Logs stay plain text during a session** (spec: LOG frames while a session runs). Text between frames is harmless to the host (its splitter separates frames from text), but the LOG frame switch is not done.
- **`boot_count`** counts resets since the last power-on (RTC RAM), not a lifetime total; that needs NVS (step 1.6).
- **CMD frames get NACK `unknown_msg`** until the dispatcher (step 1.5) exists.
- A frame longer than 160 bytes cannot be sent (one logger record). All frames of this step are under 40 bytes.
- `main.c` still does not call `hal_motor_init(); base_start();`. `base_hazard()` and `base_link_lost()` are called without the base tasks running; that is harmless but untested. Wiring them is the user's call (it changes GPIO behaviour of the blink test).

## Checkpoint
Why does `link_send` hand the bytes to the logger instead of calling `uart_write_bytes` itself? (Answer: one writer for the UART keeps frames and text in order, so a log line can never land in the middle of a frame.)
