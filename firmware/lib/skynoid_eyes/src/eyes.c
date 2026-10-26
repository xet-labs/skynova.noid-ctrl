/*
 * eyes.c - expression table, tween, blink and gaze idle, and the 1-bit renderer.
 *
 * Purpose : everything about what the eyes look like, with no hardware in it.
 * Called by: (step 4.2 board glue, not written yet) the eyes task at 25 fps, and the
 *            CMD 0x30..0x32 handlers; today only the tests and scripts/eyes_preview.c.
 * Test    : firmware/test/test_eyes/test_eyes.c
 * Spec    : 10-specs/sprint-steps.md step 4.2.
 *
 * Renderer in one paragraph: for every pixel inside the eye's box we ask three questions,
 * "inside the rounded corners?", "not under the upper lid line?", "not above the lower lid
 * line?", plus "not in the pupil hole?". All integer maths; coordinates are doubled so the
 * shape is exactly symmetric around the pixel centres. 8192 pixels, a few operations each.
 */
#include "skynoid_eyes.h"

#include <string.h>

/* The ten expressions. Fields: w, h, r, lid_top, lid_bot, slope_top, slope_bot, pupil.
 * FIRST GUESS: judged on paper, not on the panel. Tune with scripts/eyes_preview.c. */
#define NEUTRAL_EYE {60, 44, 14,  0,  0,   0, 0,  0}
static const eyes_pose_t POSES[EYES_COUNT] = {
    /* neutral   */ {{ NEUTRAL_EYE,                 NEUTRAL_EYE                 },   0,   0},
    /* happy     */ {{ {60, 44, 18,  0, 42,   0, 0,  0}, {60, 44, 18,  0, 42,   0, 0,  0} },   0,   0},
    /* sad       */ {{ {56, 44, 14, 24,  0, -24, 0,  0}, {56, 44, 14, 24,  0, -24, 0,  0} },   0,  10},
    /* angry     */ {{ {60, 40, 10, 32,  0,  30, 0,  0}, {60, 40, 10, 32,  0,  30, 0,  0} },   0,   0},
    /* surprised */ {{ {50, 56, 25,  0,  0,   0, 0,  7}, {50, 56, 25,  0,  0,   0, 0,  7} },   0,   0},
    /* sleepy    */ {{ {60, 40, 14, 55,  0,   0, 0,  0}, {60, 40, 14, 55,  0,   0, 0,  0} },   0,  15},
    /* love      */ {{ {56, 52, 26,  0, 18,   0, 0, 12}, {56, 52, 26,  0, 18,   0, 0, 12} },   0,   0},
    /* thinking  */ {{ {56, 42, 14, 18,  0,  10, 0,  0}, {56, 42, 14, 18,  0,  10, 0,  0} },  45, -40},
    /* wink      */ {{ {60,  8,  4,  0,  0,   0, 0,  0}, NEUTRAL_EYE                 },   0,   0},
    /* alert     */ {{ {54, 54, 10,  0,  0,   0, 0,  0}, {54, 54, 10,  0,  0,   0, 0,  0} },   0,   0},
};

_Static_assert(sizeof(eyes_pose_t) == 18 * sizeof(int16_t), "eyes_pose_t must be 18 int16 values (tween walks them)");

static const char *const NAMES[EYES_COUNT] = {
    "neutral", "happy", "sad", "angry", "surprised", "sleepy", "love", "thinking", "wink", "alert"
};

const char *eyes_expression_name(uint8_t expression)
{
    return (expression < EYES_COUNT) ? NAMES[expression] : "?";
}

/* ---- small helpers --------------------------------------------------------------------- */
static int clampi(int x, int lo, int hi) { return (x < lo) ? lo : (x > hi) ? hi : x; }
static int absi(int x) { return (x < 0) ? -x : x; }

static uint32_t rnd(eyes_t *e)                         /* xorshift32: small, fast, repeatable */
{
    uint32_t x = e->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    e->rng = x;
    return x;
}

static int rnd_range(eyes_t *e, int lo, int hi)        /* lo..hi inclusive */
{
    return lo + (int)(rnd(e) % (uint32_t)(hi - lo + 1));
}

static void schedule_blink(eyes_t *e, uint32_t now_ms)
{
    e->next_blink = now_ms + EYES_BLINK_INTERVAL_MS + rnd(e) % (EYES_BLINK_INTERVAL_MS / 2);
}

static void schedule_saccade(eyes_t *e, uint32_t now_ms)
{
    e->next_saccade = now_ms + EYES_SACCADE_INTERVAL_MS / 2 + rnd(e) % EYES_SACCADE_INTERVAL_MS;
}

/* ---- the engine -------------------------------------------------------------------------- */
void eyes_init(eyes_t *e, uint32_t seed, uint32_t now_ms)
{
    memset(e, 0, sizeof *e);
    e->rng = (seed != 0) ? seed : 1u;                  /* xorshift must not start at 0 */
    e->cur = e->from = e->to = POSES[EX_NEUTRAL];
    e->expression = EX_NEUTRAL;
    schedule_blink(e, now_ms);
    schedule_saccade(e, now_ms);
}

bool eyes_set(eyes_t *e, uint8_t expression, int gaze_x, int gaze_y, uint32_t tween_ms, uint32_t now_ms)
{
    if (expression >= EYES_COUNT || gaze_x < -100 || gaze_x > 100 || gaze_y < -100 || gaze_y > 100) {
        return false;
    }
    e->from = e->cur;
    e->to = POSES[expression];
    if (gaze_x != 0 || gaze_y != 0) {                  /* a given gaze overrides the pose's own */
        e->to.gaze_x = (int16_t)gaze_x;
        e->to.gaze_y = (int16_t)gaze_y;
    }
    e->expression = expression;
    if (tween_ms == 0) {
        e->cur = e->to;
        e->tweening = false;
    } else {
        e->tween_start = now_ms;
        e->tween_ms = tween_ms;
        e->tweening = true;
    }
    return true;
}

void eyes_blink(eyes_t *e, uint32_t now_ms)
{
    if (!e->blinking) { e->blinking = true; e->blink_start = now_ms; }
}

void eyes_idle(eyes_t *e, bool on, uint32_t now_ms)
{
    e->idle_on = on;
    if (on) {
        schedule_blink(e, now_ms);
        schedule_saccade(e, now_ms);
    } else {
        e->idle_gx = 0;
        e->idle_gy = 0;
    }
}

void eyes_step(eyes_t *e, uint32_t now_ms)
{
    if (e->tweening) {                                 /* walk every number from "from" to "to" */
        const uint32_t t = now_ms - e->tween_start;
        if (t >= e->tween_ms) {
            e->cur = e->to;
            e->tweening = false;
        } else {
            const int16_t *a = (const int16_t *)&e->from;
            const int16_t *b = (const int16_t *)&e->to;
            int16_t *o = (int16_t *)&e->cur;
            for (size_t i = 0; i < sizeof e->cur / sizeof(int16_t); i++) {
                o[i] = (int16_t)(a[i] + ((int32_t)(b[i] - a[i]) * (int32_t)t) / (int32_t)e->tween_ms);
            }
        }
    }

    if (e->idle_on && !e->blinking && (int32_t)(now_ms - e->next_blink) >= 0) {
        eyes_blink(e, now_ms);
    }
    if (e->blinking) {                                 /* close over the first half, open over the second */
        const uint32_t t = now_ms - e->blink_start;
        if (t >= EYES_BLINK_MS) {
            e->blinking = false;
            e->blink_pct = 0;
            schedule_blink(e, now_ms);
        } else {
            const uint32_t half = EYES_BLINK_MS / 2;
            e->blink_pct = (uint8_t)((t < half) ? (t * 100u / half) : ((EYES_BLINK_MS - t) * 100u / half));
        }
    }

    if (e->idle_on && (int32_t)(now_ms - e->next_saccade) >= 0) {
        e->idle_gx = (int16_t)rnd_range(e, -40, 40);
        e->idle_gy = (int16_t)rnd_range(e, -20, 20);
        schedule_saccade(e, now_ms);
    }
}

/* ---- the renderer ------------------------------------------------------------------------- */
static void set_pixel(uint8_t *frame, int x, int y)
{
    frame[(y >> 3) * EYES_W + x] |= (uint8_t)(1u << (y & 7));
}

void eyes_render(const eyes_t *e, int eye_index, uint8_t *frame)
{
    memset(frame, 0, EYES_FRAME_BYTES);
    if (eye_index < 0 || eye_index > 1) { return; }

    const eye_shape_t *s = &e->cur.eye[eye_index];
    const int gx = clampi(e->cur.gaze_x + e->idle_gx, -100, 100);
    const int gy = clampi(e->cur.gaze_y + e->idle_gy, -100, 100);
    const int cx = EYES_W / 2 + gx * EYES_GAZE_MAX_X / 100;
    const int cy = EYES_H / 2 + gy * EYES_GAZE_MAX_Y / 100;

    const int open_pct = 100 - (int)e->blink_pct;      /* a blink squeezes the height */
    int w = (s->w < 2) ? 2 : s->w;
    int h = s->h * open_pct / 100;
    if (h < 2) { h = 2; }
    const int hw = w / 2, hh = h / 2;
    int r = s->r;
    if (r > hw) { r = hw; }
    if (r > hh) { r = hh; }
    if (r < 1) { r = 1; }
    const int pupil = s->pupil * open_pct / 100;

    const int top = cy - hh, bot = cy + hh;           /* box rows are top .. bot-1 */
    const int cover_t = s->lid_top * (2 * hh) / 100;
    const int cover_b = s->lid_bot * (2 * hh) / 100;
    const int sign = (eye_index == 0) ? 1 : -1;       /* eye 1 is the mirror image of eye 0 */
    const int R = 2 * r;                              /* doubled units, see below */

    for (int y = clampi(top, 0, EYES_H); y < clampi(bot, 0, EYES_H); y++) {
        const int uy = absi(2 * y + 1 - 2 * cy);      /* doubled coordinates: pixel centres are odd */
        int qy = uy - (2 * hh - R);
        if (qy < 0) { qy = 0; }
        for (int x = clampi(cx - hw, 0, EYES_W); x < clampi(cx + hw, 0, EYES_W); x++) {
            const int ux = absi(2 * x + 1 - 2 * cx);
            int qx = ux - (2 * hw - R);
            if (qx < 0) { qx = 0; }
            if (qx * qx + qy * qy > R * R) { continue; }                          /* outside the rounded box */
            if (y < top + cover_t + sign * (x - cx) * s->slope_top / 64) { continue; }   /* under the upper lid */
            if (y >= bot - cover_b - sign * (x - cx) * s->slope_bot / 64) { continue; }  /* under the lower lid */
            if (pupil > 0 && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= pupil * pupil) { continue; }
            set_pixel(frame, x, y);
        }
    }
}

uint8_t eyes_dirty_pages(const uint8_t *prev, const uint8_t *cur)
{
    uint8_t mask = 0;
    for (int p = 0; p < EYES_PAGES; p++) {
        if (memcmp(prev + p * EYES_W, cur + p * EYES_W, EYES_W) != 0) { mask |= (uint8_t)(1u << p); }
    }
    return mask;
}
