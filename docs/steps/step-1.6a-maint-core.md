# Step 1.6a: the maintenance-mode core (pure C, tested on the PC)

**Goal (one line):** the rules of maintenance mode, the mute switches and the loop-time statistics as plain functions you can test without a board.

Step 1.6 is split like the others. **1.6a (this step)** is the pure logic in `lib/skynoid_maint/`. **1.6b (next)** is the board glue: the `maint` and tier-1 console commands, `stats`, `crash`, and the two small hooks in the link and the console task. Nothing in this step touches the UART, the motors or FreeRTOS.

## The idea, in plain words
A robot in the field must be debuggable without reflashing, and it must never move because someone typed a command by accident. Maintenance mode is the "I am holding the robot and I know what I am doing" switch.

```
 normal:       Pi tool talks framed data, console is read-only (tier 0)
      |  maint on 10
      v
 maintenance:  session ended, robot SAFE, no unsolicited frames,
               every framed command except PING answers NACK busy,
               tier-1 console commands (arm, drive, ...) unlocked,
               status LED shows two quick blinks every 2 s
      |  maint off   or   10 minutes pass   or   reboot
      v
 normal again: robot disarmed, outputs stopped, parameters back to defaults
```

This step provides the three small pieces that decision needs:

1. **The maintenance timer** (`skm_enter`, `skm_exit`, `skm_expired`, `skm_remaining_s`). Default 10 minutes, maximum 60. `maint on 0` means "default"; `maint on 90` is refused (`ERR invalid_arg 1`) instead of silently becoming 60, because a typo should not be guessed at. Typing `maint on` again while it is on renews the timer. The deadline is compared with unsigned subtraction, so the 49-day clock wrap is harmless.
2. **Mute flags** (`skm_mute_apply`, `skm_muted`). `mute tlm|evt|log|all|none` silences individual streams while in maintenance, so a noisy log does not drown what you are testing. They only count while maintenance is on, and leaving maintenance clears them.
3. **Loop statistics** (`sks_loop_record`, `sks_loop_avg_us`, `skm_format_loop`). The safety loop must finish inside 5 ms and the motion loop inside 10 ms. Each task records how long one pass took; `stats` shows the average, the worst case and how many passes ran late. A late pass is the first sign that something blocks.

Two helpers for the printouts: `skm_reset_name` (the reset-reason names of the HELLO table, used by `crash`) and `skm_format_hms` (seconds as `1d 02:03:04`).

## Files you will touch
| file | what |
|---|---|
| `firmware/lib/skynoid_maint/include/skynoid_maint.h` | interface, with comments |
| `firmware/lib/skynoid_maint/src/maint.c` | the functions |
| `firmware/test/test_maint/test_maint.c` | the tests |

No existing file changes.

## What you will see when it works
```
cd skynoid-v1-ctrl/firmware
pio test -e native -f test_maint
  test_maint: all test cases succeeded
pio test -e native
  11 suites, all PASSED
```

## If it does not work (likeliest causes)
1. `skynoid_maint.h: No such file`: PlatformIO did not pick up the new library folder. Run `pio test -e native -f test_maint` once more (the library finder runs per test), or `pio run -t clean`.
2. A format test fails on a different platform: the tests use `snprintf` with fixed-width integers cast to `unsigned long`, so they should not; paste the failing line.
3. Two `test_maint` folders: the older empty `lib/skynoid_session/` (mine, unused) is moved to `firmware/_superseded/`; if it is still in `lib/`, delete it.

## Checkpoint (answer in your own words)
`maint on 90` is refused, but `maint on` typed while maintenance is already on is accepted. Why is the second case not a problem for safety, and what would go wrong if refusing it meant leaving the old deadline in place?
