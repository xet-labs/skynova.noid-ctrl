# Step 1.3a: the text core of the console (pure C, tested on the PC)

**Goal (one line):** turn typed bytes into a clean command line, split it into words, and read numbers, with no board and no ESP-IDF involved.

Step 1.3 (the console) is too big for one commit, so it is split:
- **1.3a (this step):** the pure logic in `lib/skynoid_text/`. Runs on the PC.
- **1.3b (next):** the console engine on the board: UART driver, logger task, command table, the commands `help`, `version`, `uptime`, `status`, `pins`, `i2c scan`. Needs your bench.

## The idea, in plain words
When you type `drive 100 0 300` in minicom, the ESP32 receives **one byte at a time**. Three small jobs turn that into a command:

```
 bytes from UART  --->  [1 line editor]  --->  "drive 100 0 300"
                          echo, backspace,        |
                          ignore arrow keys       v
                                           [2 tokenizer]  --->  argv = {"drive","100","0","300"}
                                                                      |
                                                                      v
                                                      [3 number parser]  "100" -> 100, "0x10" -> 16
```

1. **Line editor** (`skt_line_feed`): you call it once per received byte. It tells you what to do with that byte:
   - `ECHO`: printable character accepted; send the same byte back so you see what you type.
   - `ERASE`: backspace worked; send `\b \b` (back, space, back) so the character vanishes on screen.
   - `LINE`: Enter was pressed; the finished line is in `line.buf`.
   - `TOO_LONG`: Enter was pressed but the line had more than 120 characters; it was dropped.
   - `NONE`: byte ignored (arrow keys, Tab, other control codes, the `\n` of a `\r\n` pair).
   Arrow keys arrive as 3 bytes starting with `ESC` (`1B 5B 41` for up). The editor swallows the whole sequence so it never ends up in your line.
2. **Tokenizer** (`skt_tokenize`): cuts the line at spaces **in place** (it writes `\0` over the spaces, no copy, no `malloc`) and fills an `argv` array like `main()` has.
3. **Number parser** (`skt_parse_i32`): accepts decimal (`-5`, `+7`) and hex (`0x1F`), and refuses anything else: empty text, `12a`, a lone `-`, values that do not fit in 32 bits. On failure the output is left untouched, so the caller answers `ERR invalid_arg <index>`.

Why separate from the console on the board? Everything here is plain logic with no hardware, so `pio test -e native` can prove it works before it ever meets a UART. This is the "pure logic goes in `lib/`" rule from `conventions.md`.

## Files you will touch
| file | what |
|---|---|
| `firmware/lib/skynoid_text/include/skynoid_text.h` | the interface, with comments |
| `firmware/lib/skynoid_text/src/line.c` | the line editor |
| `firmware/lib/skynoid_text/src/token.c` | tokenizer and number parser |
| `firmware/test/test_text/test_text.c` | the tests |

No existing file changes.

## What you will see when it works
```
cd skynoid-v1-ctrl/firmware
pio test -e native
  test_wire:  11 test cases: 11 succeeded
  test_text:  17 test cases: 17 succeeded
```
(PlatformIO's wording differs by version; nothing may fail.)

## If it does not work (three likeliest causes)
1. `skynoid_text.h: No such file`: PlatformIO did not pick up `lib/skynoid_text/include`. Same cause as for `skynoid_wire` in step 1.2; paste the error and the fix is a one-line `library.json` for both libs.
2. A hex test fails: check you are on the current `token.c` (`0x` prefix handling) and that the test file is the one from this step.
3. `pio test` runs only one of the two suites: use `pio test -e native -f test_text` to run just this one.

## Checkpoint (answer in your own words)
The tokenizer overwrites spaces with `\0` instead of copying words out. What does that mean for the lifetime of `argv[i]`, and what happens if the line editor receives the next byte while you are still using `argv`?
