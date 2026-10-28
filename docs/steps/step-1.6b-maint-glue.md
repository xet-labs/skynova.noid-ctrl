# Step 1.6b: maintenance mode on the board (console part)

**Goal (one line):** tier 1 console commands (today only `servo`) run only after you type `maint on`, and the mode switches itself off after 10 minutes, so nothing can move because of a stray command.

*Written 2026-10-28 by claude-chat_5d90. **Written, not built with ESP-IDF, not run on the board.** The timer rules were already tested on the PC (step 1.6a); the glue ran in a sandbox with a fake clock and a fake base. Only the console part is done; see "Not done yet".*

## The idea, in plain words
Think of a safety key. Reading commands (`status`, `batt`, `ir`, `devices`...) always work. Commands that can make something move are locked until you turn the key with `maint on`. The key turns itself back after 10 minutes (up to 60 if you ask), when you type `maint off`, or when the board reboots.

What happens at each moment:
| moment | what the robot does |
|---|---|
| `maint on` (was off) | base disarmed, servo outputs switched off, tier 1 unlocked |
| `maint on` again | only the clock restarts, nothing else |
| `maint off`, or the time runs out | base disarmed, servo outputs off, parameters back to defaults, tier 1 locked |
| e-stop, link loss, critical battery | servos off as before (independent of maintenance mode) |

The deadline is checked by a 1-second timer **and** every time a tier 1 command asks for permission, so a slow timer can never let a command through after the time is up. `maint on 90` is refused rather than turned into 60, because a typo should not be guessed at.

## Files
| file | what |
|---|---|
| `src/core/maint.{c,h}` | the glue: lock, clock, timer, side effects |
| `src/console/cmd_maint.c` | `maint`, `maint on [min]`, `maint off` (tier 0: it is the key) |
| `src/console/console_uart.{c,h}` | installs the gate and registers the command |
| `src/console/cmd_servo.c` | `servo` is now tier 1 |
| `lib/skynoid_maint/**` | the tested rules (step 1.6a), unchanged |

## Not done yet (needs changes in `link.c` and the dispatcher)
The full spec also says entering maintenance ends the Pi session, framed commands answer "busy" while it is on (PING excepted), the status LED blinks twice every 2 s, and `mute`, `stats` and `crash` exist. None of that is built. Until then: **a Pi that keeps sending PING and commands keeps working during maintenance**, so do not run maintenance with a host program driving the robot.

## Run these
```
pio run -t clean -e single_servo_trial     # new source files
pio run -e single_servo_trial -t upload
# picocom -b 460800
maint                 # expect: maintenance off
servo status          # expect: refused, maintenance needed
maint on 1            # expect: ON for 60 s
servo status          # now answers
maint on 90           # expect: refused, minutes 0..60
# wait 60 s, then:
maint                 # expect: off again (the log shows "timed out")
servo status          # refused again
```
Paste the output, including anything unexpected. If `servo status` is NOT refused while maintenance is off, tell me at once: the gate is not installed.

## Checkpoint (answer in your own words)
Why does the gate check the deadline itself every time it is asked, instead of trusting the 1-second timer alone?
