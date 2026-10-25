# Step 1.3b: the console engine (type commands, get answers)

**Goal (one line):** open a terminal on the ESP32's USB port, type `help`, `version`, `uptime`, `pins`, `status` and get plain-text answers ending in `OK`.

## The idea, in plain words
Step 1.3a gave us three small tools that work on bytes: a line editor (collects what you type), a tokenizer (splits `drive 100 0` into words) and a number parser. This step adds the part that **uses** them:

1. A **command table**: a list of `{name, help text, function}`. Every part of the firmware registers its own commands into it (`console_register`). Later steps add `drive`, `ir`, `devices` the same way.
2. An **engine** (`console_execute`): takes one finished line, splits it, finds the command, runs it, and prints `OK` or `ERR <name> <detail>`.
3. A **UART task**: reads bytes from the USB port, feeds the wire parser (so binary frames are skipped, not typed as text), feeds the line editor, echoes what you type, and calls the engine when you press Enter.

```
 USB bytes -> [wire parser] --text bytes--> [line editor] --line--> [engine] --> command function
                  |                              |                                    |
             frames: ignored               echo / backspace                     console_printf
             (until step 1.4)                                                          |
                                                                              UART writer ("\n" -> "\r\n")
```
The engine and table are **pure C** (no ESP-IDF), in `lib/skynoid_console`, so they are tested on the PC. Only the UART task and the system commands are board code.

Reply rules (spec `console-commands.md`): every command ends with `OK` or `ERR <nack_name> <detail>`; an unknown word prints `ERR unknown_msg`; a command marked tier 1 prints `ERR busy maintenance required` until maintenance mode exists (step 1.6).

## Files you will touch
| file | what |
|---|---|
| `firmware/lib/skynoid_console/include/skynoid_console.h`, `src/console.c` | table, `console_printf`, `console_execute`, built-in `help` (pure C) |
| `firmware/test/test_console/test_console.c` | PC tests for all of the above |
| `firmware/src/console/console_uart.c`, `console_uart.h` | UART0 setup at 460800 and the console task |
| `firmware/src/console/cmd_system.c` | `version`, `uptime`, `status`, `pins` |
| `firmware/src/main.c` | one added line: `console_start();` |

## What you will see when it works
```
$ picocom -b 460800 /dev/ttyUSB0
skynoid> help
help      list commands, or help <name>
version   firmware, protocol, IDF, build flags
...
OK
skynoid> bogus
ERR unknown_msg
skynoid> uptime
uptime 42 s
OK
```
Opening the terminal resets the board (DTR/RTS); that is expected and is why step 1.7 exists.

## How to run it
1. PC tests (no board): `cd firmware && pio test -e native -f test_console` (expect every test passing; `-f test_text` and `-f test_wire` should still pass).
2. Board: `pio run -e single -t upload`, then `picocom -b 460800 /dev/ttyUSB0` (the existing `monitor_speed = 115200` in `platformio.ini` will show garbage; use picocom or change it to 460800).

## If it does not work (the 3 most likely causes)
1. **Garbage or nothing in the terminal:** baud mismatch. The firmware moves UART0 to 460800 as soon as `console_start()` runs; the ROM boot text before that is at 115200 and looks like noise. That is normal.
2. **Build error "driver/uart.h: no such file":** this project runs ESP-IDF 6.1 (read from your blinky `sdkconfig`), where drivers are split into components. Add `REQUIRES esp_driver_uart` (and `esp_timer`) to `src/CMakeLists.txt`'s `idf_component_register`, and tell me the exact error.
3. **A library header not found (`skynoid_console.h`):** PlatformIO's library finder did not pick up the new `lib/skynoid_console`. Add a `library.json` with `{"name": "skynoid_console"}` next to its `include/`, or tell me; the same risk exists for the other libs and has not been tested on a real build yet.

## Checkpoint
Explain in your own words why the engine lives in a library that has no ESP-IDF includes, and what `console_execute` prints if a command function returns 4.
(Answer: so it can be tested on the PC; `ERR hazard_blocked`, because 4 is that NACK code.)
