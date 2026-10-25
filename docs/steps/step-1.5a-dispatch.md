# Step 1.5a: the dispatcher (CMD frames drive the base, telemetry and events flow back)

**Goal (one line):** from the Pi send `arm`, `drive`, `stop`, `estop`, `subscribe` and `set_param` frames and get an ACK or a NACK with the right reason; after `subscribe` get a TLM `base_status` frame every period; when the base raises an event (cliff, battery, link lost) get an EVT frame.

*Lib and test written by `claude-chat_4d9c` (18:25, never recorded). Verified and glued to the board by `claude-chat_d4b8` on 2026-10-25. Nothing here has been built with `pio` or run on a board. What was run: the pure library and its test on the PC (my sandbox, Unity stand-in, `-Wall -Wextra -Werror`, ASan and UBSan), and `link.c` itself on the PC against stub ESP and FreeRTOS headers and a fake base (see "What was actually checked").*

Step 1.5 of the sprint is split here: **1.5a** (this note: dispatcher, ACK/NACK, TLM, EVT) and **1.5b** (device registry, `devices`, `i2c` commands, boot HELLO). The split is ours, not in `sprint-steps`.

## The idea, in plain words
Step 1.4 answered every CMD with NACK `unknown_msg`. Now a CMD frame goes through three small stages:

```
 CMD frame -> link_on_frame -> on_cmd -> skd_handle_cmd (pure C: length check, decode) -> base_cmd_*  (the SAME functions the console calls)
                                   \-> ACK, or NACK(code, detail) -> link_send
 base safety task -> base event sink -> 16-slot queue ---(only while a session runs, in link_tick)---> EVT frame
 link_tick -> stream due? -> base_get_status -> TLM base_status (17 bytes)
```
Because the console and the frames call the same `base_cmd_*`, speed cap, TTL, hazard rule, e-stop latch and "not armed" apply identically on both paths.

**NACK details (protocol-v0 table 7):** `bad_len` carries the expected length; `hazard_blocked` carries the hazard mask; `invalid_arg` carries the argument index (for `set_param`: 0 = unknown id, 1 = value out of range or breaks a relation); all others 0. `console_line` (0x08) and every upper-node command answer `unknown_msg` for now.

## Files you will touch
| file | what |
|---|---|
| `firmware/lib/skynoid_dispatch/include/skynoid_dispatch.h`, `src/dispatch.c` | pure C: `skd_handle_cmd`, the stream timer, TLM/EVT/NACK packers |
| `firmware/test/test_dispatch/test_dispatch.c` | 21 PC tests (ACK, NACK, drive and EVT rows of `protocol-v0` section 8 byte for byte) |
| `firmware/src/link/link.c`, `link.h` | the real ops table over `base_cmd_*`, event queue, TLM sending |

## What you will see when it works
```
$ python -m skynoid.tools.doctor --port /dev/ttyUSB0      # starts a session, arms nothing, reads status
$ python -m skynoid.tools.drive_keys --port /dev/ttyUSB0  # wheels OFF THE GROUND first
```
`ir_view` shows live IR values from TLM. Without `cal_floor` the base refuses motion (hazard bit 5, NACK `hazard_blocked`, mask `0x20`); that is correct, not a bug (bench builds use `-DSKYNOID_CLIFF_REQUIRED=0`).

## How to run it
1. PC: `cd firmware && pio test -e native -f test_dispatch` (expect 21 passing); `-f test_link` still 11, `-f test_wire` 11.
2. Board: `pio run -e single -t clean`, `pio run -e single -t upload`. Needs the two `main.c` lines from step 1.4's notes first: `hal_motor_init(); base_start();`, otherwise the base tasks do not run.
3. Close picocom, run the host tools above.

## If it does not work (most likely causes)
1. **Build error in `link.c`: `freertos/queue.h` or `skynoid_dispatch.h` not found.** Add `freertos` to `REQUIRES` of `src/CMakeLists.txt`; the `lib/skynoid_dispatch` folder is picked up by PlatformIO automatically. Paste the exact error.
2. **`arm` or `drive` answered NACK `busy` (code 7):** no session is running. Send PINGs first (the Python `Robot` does). This is a deliberate choice, see below.
3. **No TLM after `subscribe`:** TLM flows only during a session and only after a successful `subscribe 1 <period>`; a new session starts with the stream off.

## Choices made here that the spec does not state (confirm or change)
- **`arm` and `drive` without a session are refused with NACK `busy`.** Reason: the 500 ms link-loss timer exists only inside a session. Without it, `arm` would leave the robot armed with nothing watching the link. `drive` has its own TTL, so it is refused only for consistency. Remove the guard in `on_cmd()` if you want raw scripts to arm without PINGs.
- **Events wait for a session.** An event raised between sessions (`link_lost`, `cliff`) stays in a 16-slot queue and is sent when the next session starts, together with HELLO. If more than 16 pile up, the oldest are dropped (logged as "event queue overflowed"). Old events from hours ago can therefore arrive at the start of a session.
- **A new session starts with the stream off.** The host subscribes again after every HELLO.

## What was actually checked (and what not)
- `test_dispatch`: 21 of 21 pass in my sandbox, plain and under ASan+UBSan. Not run with `pio`.
- `link.c`: compiled on the PC with stub `esp_*` and `freertos` headers, a fake base and the real wire, session and dispatch libraries; a harness sends real PING and CMD frames and decodes what `link.c` writes. 28 checks pass, plain and under sanitizers: busy before a session, HELLO then PONG, ACK/NACK with the right `ack`, hazard mask in the NACK, TLM at once then every period with the right bytes, session end calls `base_link_lost` once and stops the stream, `link_lost` delivered at the next session, overflow keeps the newest 16. Removing the busy guard or the session check from a copy of `link.c` makes it fail (7 and 5 failures), so the checks can fail. The stubs are mine: this does **not** prove the code against the real ESP-IDF 6.1 headers or FreeRTOS.
- Not checked at all: a real build, real UART timing, two tasks racing on the event queue, anything on the board.

## Known limits (honest list)
- No boot HELLO (1.5b). No LOG frames during a session (logs stay plain text). `console_line` frames and maintenance mode are step 1.6.
- `base_last_detail()` is read after the command in a separate lock; it is correct because the console task is the only caller of both paths, but it would be wrong if a second task issued commands at the same time.
- `main.c` still lacks `hal_motor_init(); base_start();` (the user's call).

## Checkpoint
Written, sandbox-tested, **not built, not run on hardware**. For you to run: `pio test -e native -f test_dispatch`, then the board steps above. Paste any output that is not green.
