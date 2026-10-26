/*
 * skynoid_eyes.h - the eyes engine: ten expressions, tween, blink, gaze, and a renderer.
 *
 * WHAT THIS FILE IS
 *   Pure C, NO hardware and NO ESP-IDF includes. It decides what each OLED shows and
 *   draws it into a 128x64 1-bit frame buffer in the layout SSD1306/SH1106 want:
 *   8 pages of 128 bytes, byte = 8 vertical pixels, bit 0 on top (frame[(y/8)*128 + x]).
 *   Tested on the PC (pio test -e native -f test_eyes); see the look of every
 *   expression with scripts/eyes_preview.c, no board needed.
 *
 * HOW IT IS USED (step 4.2, after the OLED driver exists)
 *     eyes_t e;  eyes_init(&e, seed, now_ms);
 *     eyes_set(&e, EX_HAPPY, 0, 0, 200, now_ms);       // CMD 0x30 eyes_set
 *     every 40 ms (25 fps):  eyes_step(&e, now_ms);
 *         eyes_render(&e, 0, frame0);  eyes_render(&e, 1, frame1);
 *         pages = eyes_dirty_pages(prev, frame0);  push only those pages
 *   If both panels share one I2C address, render eye 0 and send the same image to both.
 *
 * The look of an eye (all in pixels, panel 128x64): a rounded rectangle, cut by an upper
 * lid and a lower lid (percent of its height, plus a tilt), with an optional dark pupil hole.
 * Tilt convention: positive slope_top = the INNER corner (nose side) of the upper lid is lower
 * (angry); negative = inner corner higher (sad). Eye 0 has its inner side on the right.
 * The numbers in the expression table are a first guess made without seeing the panel:
 * tune them with the preview tool and the real OLED.
 */
#ifndef SKYNOID_EYES_H
#define SKYNOID_EYES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EYES_W            128
#define EYES_H            64
#define EYES_PAGES        8
#define EYES_FRAME_BYTES  (EYES_W * EYES_PAGES)
#define EYES_COUNT        10          /* number of expressions */

/* Expression ids: fixed by the protocol (messages.py EXPRESSIONS). */
enum {
    EX_NEUTRAL = 0, EX_HAPPY, EX_SAD, EX_ANGRY, EX_SURPRISED,
    EX_SLEEPY, EX_LOVE, EX_THINKING, EX_WINK, EX_ALERT
};

/* Idle behaviour defaults (spec: blink_interval_ms, saccade_interval_ms; the yaml may override). */
#define EYES_BLINK_MS            160   /* one blink, close and open */
#define EYES_BLINK_INTERVAL_MS   3500  /* average time between blinks, plus up to half again at random */
#define EYES_SACCADE_INTERVAL_MS 1800  /* average time between small gaze jumps */
#define EYES_GAZE_MAX_X          24    /* pixels the eye moves at gaze +-100 */
#define EYES_GAZE_MAX_Y          10

typedef struct {
    int16_t w, h, r;                 /* box and corner radius */
    int16_t lid_top, lid_bot;        /* percent of the height covered, 0..100 */
    int16_t slope_top, slope_bot;    /* lid tilt in 1/64 pixel per pixel */
    int16_t pupil;                   /* radius of a dark hole in the middle, 0 = none */
} eye_shape_t;

typedef struct {
    eye_shape_t eye[2];
    int16_t gaze_x, gaze_y;          /* -100..100 */
} eyes_pose_t;

typedef struct {
    eyes_pose_t cur, from, to;       /* what is drawn, tween start, tween target */
    uint32_t tween_start, tween_ms;
    bool     tweening;
    uint8_t  expression;
    uint32_t rng;
    bool     idle_on;
    bool     blinking;
    uint32_t blink_start, next_blink;
    uint8_t  blink_pct;              /* 0 open .. 100 closed */
    uint32_t next_saccade;
    int16_t  idle_gx, idle_gy;       /* gaze offset from the idle engine */
} eyes_t;

void eyes_init(eyes_t *e, uint32_t seed, uint32_t now_ms);
/* Start a tween to an expression. gaze -100..100. False (and nothing changes) when the
 * expression id or a gaze value is out of range. tween_ms 0 jumps at once. */
bool eyes_set(eyes_t *e, uint8_t expression, int gaze_x, int gaze_y, uint32_t tween_ms, uint32_t now_ms);
void eyes_blink(eyes_t *e, uint32_t now_ms);                 /* one blink now */
void eyes_idle(eyes_t *e, bool on, uint32_t now_ms);          /* random blinks and gaze jumps */
void eyes_step(eyes_t *e, uint32_t now_ms);                   /* call every 40 ms */
/* Draw eye 0 or 1 into frame (EYES_FRAME_BYTES). Clears the buffer first. */
void eyes_render(const eyes_t *e, int eye_index, uint8_t *frame);
/* Bit mask of pages (bit 0 = page 0) whose 128 bytes differ. */
uint8_t eyes_dirty_pages(const uint8_t *prev, const uint8_t *cur);
const char *eyes_expression_name(uint8_t expression);         /* "?" for an unknown id */

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_EYES_H */
