# Step 1.3c: the logger (one task owns the serial port)

**Goal (one line):** any task can log a line without ever waiting for the UART, the last ~2 KB of log text survives a software reset, and you can read it back with `log dump`.

*Written afterwards by `claude-chat_e3a7`: the code was already on disk (written by an earlier agent session) and had no explainer. Nothing here has been built with `pio` or run on a board.*

## The idea, in plain words
Printing 100 characters at 460800 baud takes about 2 ms. The cliff loop runs every 5 ms. If the cliff loop called `printf`, one log line could eat 40% of its time. So **real-time tasks never print**. They drop a short record into a queue and return at once. One low-priority task, the **logger**, owns the UART:

```
 any task: LOGI(...) / ESP_LOGx / console echo
        |  copy into queue (never waits; full queue = drop + count)
        v
   [queue] --> logger task --> flight recorder (2 KB ring in RTC RAM, keeps ALL levels)
                         \---> UART0 (only if the line's level <= current `log level`)
```
Three details that matter:
1. **Console text goes through the same queue** ("raw" records) so a log line never lands in the middle of what you are typing.
2. **`ESP_LOGx` is redirected** (`esp_log_set_vprintf`), so IDF's own messages use the same path and level filter.
3. **The recorder lives in RTC "noinit" RAM**, so after a software or watchdog reset `log dump` still shows what happened just before. After power-on the RAM is random; `skl_ring_attach` rejects it (magic and index checks) and starts empty. A reset through the USB bridge's EN line probably clears it too (`#verify`).

## Pure vs board code
| file | kind | what |
|---|---|---|
| `lib/skynoid_console/include/skynoid_logtext.h`, `src/logtext.c` | pure C | log-line format `I (ms) text`, level detection (also behind an ANSI colour code), trim end of line, `\n` to `\r\n` |
| `lib/skynoid_logic/src/ring.c` (`skl_ring_*`) | pure C | the recorder ring (tested in `test_core`) |
| `test/test_logtext/test_logtext.c` | PC test | 9 tests |
| `src/core/logger.c`, `logger.h` | board | queue, logger task (priority 2, stack 3072), recorder in RTC RAM, ESP_LOG hook |
| `src/console/cmd_log.c` | board | `log`, `log level 0..3`, `log dump`, `log stats` (tier 0: changes only the print filter) |

`console_start()` calls `logger_start()` right after the UART driver is installed.

## What you will see when it works
```
skynoid> log
log level 2
OK
skynoid> log level 3
OK
skynoid> log dump
I (1234) logger started, recorder holds 0 bytes from before this boot
...
OK
skynoid> log stats
dropped 0
OK
```
Test of the recorder: type `reboot` is not available yet (tier 1, step 1.6); instead press the board's EN button (hardware reset) and run `log dump`: expect a note that the recorder may be empty. A software reset test comes with step 1.6.

## How to run it
1. PC: `cd firmware && pio test -e native -f test_logtext` (expect 9 passing) and `-f test_core` (the ring).
2. Board: `pio run -e single -t upload`, `picocom -b 460800 /dev/ttyUSB0`, run the commands above.

## If it does not work (the 3 most likely causes)
1. **Build error in `logger.c` about `esp_timer.h`, `driver/uart.h` or `esp_attr.h`:** ESP-IDF 6.x split the drivers. Add `REQUIRES esp_driver_uart esp_timer` to `src/CMakeLists.txt`. Paste the exact error.
2. **Lines appear but garbage or doubled line breaks:** `lt_to_crlf` only adds `\r` before a bare `\n`; if a producer already sends `\r\n` check `test_crlf_*`. Also confirm the terminal is at 460800.
3. **`log stats` shows dropped lines:** the queue (`LOGGER_RING_RECORDS` in `app_config.h`) is too small for the burst. That is the designed behaviour (drop, never block); raise the size only if drops happen in normal use.

## Known limits (honest list)
- Each record is a fixed 163 bytes and `LOGGER_RING_RECORDS` is 64 (`app_config.h`), so the queue costs about 10 KB of RAM.
- Lines longer than 159 characters are cut.
- The logger prints at most the level filter; `log level 0` hides IDF's boot info lines too, by design.
- Core dump to flash (second half of "production debugging") is **not** part of this step.

## Checkpoint
Why does `logger_write_raw` wait up to 20 ms when `log_msg` waits 0? (Answer: console echo must not be lost while a person is typing, and the console task is not a real-time task; `log_msg` may be called from the cliff loop and must never wait.)
