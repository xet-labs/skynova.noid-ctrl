# Step 1.7: choose the link and console serial ports with macros

**Goal (one line):** the firmware has two "channels", the **link** (binary frames for the Pi) and the **console** (text you type). You choose which UART each one uses, at which baud rate and on which pins, with `-D` flags in `platformio.ini`; with no flags nothing changes.

*Written by `claude-chat_b3c8` on 2026-10-26, BEFORE the code, at the user's request. Nothing here has been built with `pio` or run on a board. This file is updated afterwards with what was really written and tested.*

## The idea, in plain words
Today one serial port (UART0, the USB cable) carries everything: the frames to and from the Pi **and** the text you type in `picocom`. The firmware already separates them in software (a frame starts with a marker, anything else is text), so it works. But it has three limits:
1. The baud rate is one fixed number (460800) for both.
2. You cannot move the console to its own wires, for example a cheap USB-serial adapter, while the Pi keeps the USB cable.
3. Three macros named `SKYNOID_LINK_UART`, `SKYNOID_CONSOLE_UART`, `SKYNOID_CONSOLE_BAUD` already exist, but the code that opens the port ignores them (`UART_NUM_0` and `UART_BAUD` are written straight into `console_uart.c` and `logger.c`). The only place that reads them is the `version` command, which prints them, so `version` could say "console_uart=2" while the board still used UART0. That is the logic bug this step fixes.

The fix is a small layer, the **channel layer**: one place that knows "which UART is the link, which is the console", and everyone else asks it.

```
 SHARED (default, same as today)               SEPARATE (-DUART_CONSOLE_NUM=2)

 Pi / PC --USB--> UART0 (460800)               Pi --USB--> UART0 (460800, link)
                   |  frames + text                          |  frames only
                   v                                         v
              [wire parser] --text--> line editor       [wire parser] --> link_on_frame
                   |  frames                                  (text bytes ignored)
                   v
              link_on_frame                       you --adapter--> UART2 GPIO5/15 (115200, console)
                                                                     |  text only
 logger writes everything to UART0                                   v
                                                                [line editor]

                                                    logger: text and logs -> UART2,
                                                            binary frames -> UART0
```

## The macros (all in `src/app_config.h`, every default is an `#ifndef`)
No `SKYNOID_` prefix, as the user asked.

| macro | default | meaning |
|---|---|---|
| `UART_LINK_NUM` | `0` | which UART carries the frames (0, 1 or 2) |
| `UART_LINK_BAUD` | `460800` | its baud rate |
| `UART_LINK_TX_PIN`, `UART_LINK_RX_PIN` | the pair of that UART (UART0: GPIO1/3, UART2: GPIO5/15) | pins; UART1 has **no default pair** (see below) |
| `UART_CONSOLE_NUM` | same as `UART_LINK_NUM` | which UART carries the console; same number = **shared** |
| `UART_CONSOLE_BAUD` | `115200` | console baud, **only used when the console has its own UART** |
| `UART_CONSOLE_TX_PIN`, `UART_CONSOLE_RX_PIN` | pair of that UART | only when separate |
| `UART0_FREE_BAUD` | `115200` | baud set on UART0 when neither channel uses it |

Old names: `SKYNOID_LINK_UART`, `SKYNOID_CONSOLE_UART`, `SKYNOID_CONSOLE_BAUD` are removed (their meaning changed, so keeping them would silently do nothing). `UART_BAUD` (460800, a fixed number) is replaced by `UART_LINK_BAUD`. `PIN_CONSOLE_TX/RX` in `board_pins.h` become `PIN_UART2_TX/RX` (same GPIO5 and GPIO15); the old names stay as aliases.

## What each combination does
| flags | link | console | result |
|---|---|---|---|
| none (`single`) | UART0 @460800 | same UART | exactly as today |
| `-DUART_CONSOLE_NUM=2` (`single_uart2`) | UART0 @460800, USB | UART2 @115200, GPIO5 TX / GPIO15 RX | USB shows frames only, console on an adapter |
| `-DUART_LINK_NUM=2 -DUART_CONSOLE_NUM=0` (`single_link2`) | UART2 @460800, GPIO5/15 | UART0 @115200, USB | USB is a plain console; the Pi talks on GPIO5/15 |
| `-DUART_LINK_BAUD=230400` | UART0 @230400 | shared | host tools need `--baud 230400` |
| `-DUART_CONSOLE_BAUD=9600` with no separate console | refused at build | | the shared port runs at the link baud; a console baud would be ignored, so the build says so |

In **shared** mode the one UART runs at `UART_LINK_BAUD` and `UART_CONSOLE_BAUD` has no effect. In **separate** mode "silent until spoken to" applies to the link UART (frames only after a PING); the console UART may print log lines at any time.

## What is refused at build time (readable `#error` text)
- a UART number other than 0, 1, 2;
- a baud outside 1200 to 2000000;
- an explicit `UART_CONSOLE_BAUD` that differs from `UART_LINK_BAUD` while both share one UART;
- UART1 without explicit pins, or any pin of a non-UART0 channel that is: missing (-1), not an existing GPIO, a flash/boot pin (0, 12, 6..11), the USB-bridge pins 1 or 3, or used by another role in `board_pins.h` (motors, IR, buzzer, touch, I2C ...);
- a TX pin that cannot output (GPIO34..39 are input-only);
- TX and RX the same pin, or link and console sharing a pin;
- UART0 with pins other than GPIO1/3 (the USB bridge is wired there).

The same number rules also exist as plain C in `lib/skynoid_chan` so the PC test can try every case (`pio test -e native -f test_chan`). The rules about "used by another role" need the board pin list, so they exist only in the build-time check.

## UART1: not enabled by default, you decide
`pins.yaml` shows **no free GPIO** in role `single`. The earlier proposal was TX GPIO17, RX GPIO18, which are the unwired `touch_extra` and `touch_hand_l`. That would cost touch zones, so I did not take it silently. With no default, `-DUART_LINK_NUM=1` fails with a message asking for explicit pins, and explicit pins that another role owns fail too. To use UART1 you free two pins in `board_pins.h` on purpose (and tell the touch step), then give them with `-DUART_LINK_TX_PIN=.. -DUART_LINK_RX_PIN=..`. If you confirm a pair, adding it as the default is a two-line change.

## What UART0 does when the link is somewhere else
Your decision: no "leave it to ESP-IDF". When neither channel uses UART0 (for example `-DUART_LINK_NUM=2`, console following it), `chan_init` installs a small driver on UART0 and sets its baud on purpose to `UART0_FREE_BAUD` (default `115200`, an `#ifndef` macro). The ROM boot text, bootloader log, panic dump and the first `printf` boot line still leave GPIO1 at the ESP-IDF console baud (`CONFIG_ESP_CONSOLE_UART_BAUDRATE`, 115200 in `sdkconfig.single`), so keep `UART0_FREE_BAUD` equal to it unless you also change the sdkconfig. If a channel uses UART0 (`single_link2` puts the console there at 115200) nothing extra happens. Sandbox check with stub drivers only; not run on the board.

## Pure vs board code
| file | kind | what |
|---|---|---|
| `lib/skynoid_chan/include/skynoid_chan.h`, `src/chan.c` | pure C | number rules as macros (usable in `#if`), `skc_validate`, `skc_channel_for` (which channel a log line, console text or binary frame goes to), `skc_describe` (one-line summary) |
| `test/test_chan/test_chan.c` | PC test | defaults, every refusal, routing in both modes, describe text |
| `src/core/chan_cfg.h` | build check | derives `CHAN_SHARED`, runs the `#error` checks (included by `app_config.h`) |
| `src/core/chan.c`, `chan.h` | board | installs the UART driver(s) from the macros, `chan_read`, `chan_write` |
| `src/core/logger.c` | board | sends binary records to the link channel, text to the console channel |
| `src/console/console_uart.c` | board | console task; reads the link UART too when shared |
| `src/link/link.c` | board | one extra link task only when separate |
| `src/console/cmd_system.c` | board | `version` prints the real channel summary |

`main.c` is not touched: `console_start()` calls `chan_init()` before `logger_start()`. The build globs `src/*`, so the new files are found, but **re-run CMake once** (delete `.pio/build/<env>` or `pio run -t clean`).

## How to run it (after the code exists)
1. PC: `cd firmware && pio test -e native -f test_chan`, then the whole `pio test -e native`.
2. Board, default: `pio run -e single -t upload`, `picocom -b 460800 /dev/ttyUSB0`, `version`, expect a flags line that says link and console share UART0.
3. Console on its own port (`single_uart2`): wire a 3.3 V USB-serial adapter: adapter RX to GPIO5, adapter TX to GPIO15, GND to GND. Do not wire DTR or RTS. `picocom -b 115200 /dev/ttyUSBx` on the adapter shows the `skynoid>` prompt. The USB cable still answers `python -m skynoid.tools.ping_test`.
4. Link on UART2 (`single_link2`): `picocom -b 115200` on the USB port shows the console; the frames go to GPIO5/15 (the Pi, or the adapter with `ping_test --baud 460800`).
Paste the output of each; until then all of this is "written, not run".

## If it does not work (the 3 most likely causes)
1. **Build stops with `#error` from `chan_cfg.h`:** read the text, it names the macro and the rule. Fix the flag; do not edit the check.
2. **No text on UART2:** GPIO5 and GPIO15 are boot-strapping pins. They are safe as UART (TX idles high, RX is pulled up), but a wire that holds GPIO15 low at power-on silences boot messages, and an adapter's RTS/DTR held on them can upset boot. Disconnect and retry.
3. **Frames work but the console does not (or the reverse) after the change:** check the baud of each port: link 460800, console 115200 when separate.

## Known limits (honest list)
- In separate mode the link UART ignores text bytes and the console UART does not parse frames. A frame typed on the console port is just text.
- Nothing here is run on the board. The ESP-IDF calls (`uart_driver_install`, `uart_set_pin`) are the same ones the old code used, but pin numbers other than UART0's were never driven by this firmware before.
- `lib/skynoid_registry` has a manifest entry `console_uart2`; I did not change it, so `devices` may not know the console is on UART2 yet.
- The vault specs and notes that named the old macros are updated in the last sub-step of this task; they may lag this file by a few minutes.

## Open questions for the user
1. UART1 pins: leave it disabled (default now), or name a pair and which role gives them up.
2. UART0 policy when the link leaves it: answered, see above (`UART0_FREE_BAUD`).
3. Confirm "console defaults to the link port; a different port gets its own baud" instead of the old rule "console is UART0 or UART2".

## Checkpoint
In separate mode, a log line and a PONG frame are queued in the same logger queue, one right after the other. Why is it still correct that they leave on two different UARTs with no ordering between them? (Answer: they are different wires with different readers; ordering only matters inside one byte stream, and in shared mode they use one UART in queue order.)
