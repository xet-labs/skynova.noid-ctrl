# Step 4.2 (pure part): the eyes engine, and how to look at it without a board

**Goal (one line):** a program that knows what each OLED should show (ten expressions, smooth changes, blinking, looking around) and draws it into a 1 KB picture buffer, tested on the PC. You can look at every face in your terminal before any OLED is wired.

## The idea, in plain words
An eye is a **white rounded rectangle**. Everything that makes it look happy, sad or angry is cut away from it:
- an **upper lid** (a black line that comes down from the top; tilted, it makes the eye angry or sad),
- a **lower lid** (comes up from the bottom; a big one makes a smiling "^" eye),
- an optional **pupil hole** (a black circle in the middle, used for surprised and love).
Each expression is just a row of eight numbers per eye (`POSES` in `eyes.c`). To move from one face to the next the engine **tweens**: every number slides in a straight line from the old value to the new one over `tween_ms`. A **blink** squeezes the height to a thin line and back in 160 ms. The **idle** mode blinks at random (every 3.5 to 5.3 s) and nudges the gaze now and then, using a small repeatable random generator (same seed, same behaviour, so tests can check it).

The picture buffer has the layout of the OLED chip: 8 pages of 128 bytes, one byte = 8 pixels in a column, top pixel is bit 0. `eyes_dirty_pages(old, new)` says which pages changed, so later only those pages are sent over I2C (the spec wants one page of 128 bytes per transaction).

## Files
| file | what |
|---|---|
| `firmware/lib/skynoid_eyes/include/skynoid_eyes.h`, `src/eyes.c` | engine and renderer (pure C) |
| `firmware/test/test_eyes/test_eyes.c` | 21 PC tests |
| `scripts/eyes_preview.c` | draws the faces in the terminal |

Not in this step: the OLED driver (needs the answer to the questions below), and the commands `eyes_set` / `eyes_blink` / `eyes_idle` (CMD 0x30..0x32), which belong to the link dispatcher.

## How to run it
1. Tests: `cd firmware && pio test -e native -f test_eyes` (expect 21 passing).
2. Look at the faces, from the ctrl repo root:
   ```
   gcc -std=c11 -Ifirmware/lib/skynoid_eyes/include scripts/eyes_preview.c firmware/lib/skynoid_eyes/src/eyes.c -o /tmp/eyes_preview
   /tmp/eyes_preview            # all ten
   /tmp/eyes_preview thinking   # one by name or number
   /tmp/eyes_preview neutral 60 -30   # looking right and up
   ```
   Left of the `|` is eye 0 (left OLED), right is eye 1. One character is a 2x2 block of pixels.

## Tune the faces
The numbers in `POSES` are a **first guess made without seeing the panel**: width, height, corner radius, upper lid %, lower lid %, upper tilt, lower tilt, pupil. Change one, rebuild the preview, look again. The tilt convention: positive upper tilt = the corner next to the nose is lower (angry), negative = higher (sad). **Tell me which faces look wrong** ("love" is only a placeholder: a heart needs a bitmap, not lids).

## Questions for you (the OLED driver must wait for these)
1. Which controller are the two OLEDs: **SSD1306** or **SH1106**? (The 1.3-inch ones are usually SH1106; they need a 2-column offset.)
2. Do both panels answer at the same I2C address (0x3C)? If yes, the same image goes to both (the wink cannot work then); if you can move one to 0x3D, each eye can differ.

## Checkpoint
Why does `eyes_step` compare times with `(int32_t)(now - next)`, and why does the engine use its own random generator instead of `rand()`?
(Answer: so the clock can wrap past 2^32 ms without breaking the schedule; so a test with one seed gets the same blinks every time.)
