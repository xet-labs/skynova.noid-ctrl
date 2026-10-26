/*
 * test_eyes.c - tests for the eyes engine (poses, tween, blink, idle, renderer, dirty pages).
 * Run on the PC:  pio test -e native -f test_eyes
 * To LOOK at the expressions, build scripts/eyes_preview.c (see docs/steps/step-4.2-eyes.md).
 */
#include <string.h>
#include <unity.h>
#include "skynoid_eyes.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t g_frame[EYES_FRAME_BYTES];

static int px(const uint8_t *f, int x, int y) { return (f[(y >> 3) * EYES_W + x] >> (y & 7)) & 1; }

typedef struct { int count, minx, maxx, miny, maxy; } stats_t;

static stats_t stats(const uint8_t *f)
{
    stats_t s = {0, EYES_W, -1, EYES_H, -1};
    for (int y = 0; y < EYES_H; y++) {
        for (int x = 0; x < EYES_W; x++) {
            if (px(f, x, y)) {
                s.count++;
                if (x < s.minx) { s.minx = x; }
                if (x > s.maxx) { s.maxx = x; }
                if (y < s.miny) { s.miny = y; }
                if (y > s.maxy) { s.maxy = y; }
            }
        }
    }
    return s;
}

static int count_in(const uint8_t *f, int x0, int x1, int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++) { for (int x = x0; x < x1; x++) { n += px(f, x, y); } }
    return n;
}

static eyes_t make(uint8_t ex)
{
    eyes_t e; eyes_init(&e, 1234, 0);
    TEST_ASSERT_TRUE(eyes_set(&e, ex, 0, 0, 0, 0));
    return e;
}

static void test_frame_layout_is_page_format(void)
{
    uint8_t f[EYES_FRAME_BYTES]; memset(f, 0, sizeof f);
    f[0] = 0x01;                                                    /* byte 0, bit 0 = pixel (0,0) */
    TEST_ASSERT_EQUAL_INT(1, px(f, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, px(f, 0, 1));
    f[EYES_W * 3 + 5] = 0x80;                                       /* page 3, column 5, bit 7 = y 31 */
    TEST_ASSERT_EQUAL_INT(1, px(f, 5, 31));
}

static void test_neutral_is_centred_and_inside_the_panel(void)
{
    eyes_t e = make(EX_NEUTRAL);
    eyes_render(&e, 0, g_frame);
    stats_t s = stats(g_frame);
    TEST_ASSERT_TRUE(s.count > 1500);                               /* a solid, visible eye */
    TEST_ASSERT_INT_WITHIN(1, 63, (s.minx + s.maxx) / 2);           /* centred left to right */
    TEST_ASSERT_INT_WITHIN(1, 31, (s.miny + s.maxy) / 2);           /* and top to bottom */
    TEST_ASSERT_EQUAL_INT(60, s.maxx - s.minx + 1);                 /* exactly the configured size */
    TEST_ASSERT_EQUAL_INT(44, s.maxy - s.miny + 1);
}

static void test_neutral_eye_is_mirror_symmetric_in_both_axes(void)
{
    eyes_t e = make(EX_NEUTRAL);
    eyes_render(&e, 0, g_frame);
    stats_t s = stats(g_frame);
    for (int y = s.miny; y <= s.maxy; y++) {
        for (int x = s.minx; x <= s.maxx; x++) {
            TEST_ASSERT_EQUAL_INT(px(g_frame, x, y), px(g_frame, s.minx + s.maxx - x, y));
            TEST_ASSERT_EQUAL_INT(px(g_frame, x, y), px(g_frame, x, s.miny + s.maxy - y));
        }
    }
}

static void test_every_expression_renders_both_eyes_without_touching_the_border_rows(void)
{
    for (uint8_t ex = 0; ex < EYES_COUNT; ex++) {
        eyes_t e = make(ex);
        for (int eye = 0; eye < 2; eye++) {
            eyes_render(&e, eye, g_frame);
            stats_t s = stats(g_frame);
            TEST_ASSERT_TRUE_MESSAGE(s.count > 100, eyes_expression_name(ex));
            TEST_ASSERT_TRUE(s.minx >= 0 && s.maxx < EYES_W && s.miny >= 0 && s.maxy < EYES_H);
        }
    }
}

static void test_expressions_look_different_from_each_other(void)
{
    static uint8_t frames[EYES_COUNT][EYES_FRAME_BYTES];
    for (uint8_t ex = 0; ex < EYES_COUNT; ex++) {
        eyes_t e = make(ex);
        eyes_render(&e, 0, frames[ex]);
    }
    for (int a = 0; a < EYES_COUNT; a++) {
        for (int b = a + 1; b < EYES_COUNT; b++) {
            TEST_ASSERT_TRUE(memcmp(frames[a], frames[b], EYES_FRAME_BYTES) != 0);
        }
    }
}

static void test_wink_closes_only_eye_zero(void)
{
    eyes_t e = make(EX_WINK);
    eyes_render(&e, 0, g_frame);  stats_t closed = stats(g_frame);
    eyes_render(&e, 1, g_frame);  stats_t open = stats(g_frame);
    TEST_ASSERT_TRUE(closed.count < 600);
    TEST_ASSERT_TRUE(open.count > 1500);
}

static void test_angry_lids_slope_down_towards_the_nose_on_both_eyes(void)
{
    eyes_t e = make(EX_ANGRY);
    /* eye 0: inner corner is on the right; eye 1: inner corner is on the left */
    eyes_render(&e, 0, g_frame);
    int outer0 = count_in(g_frame, 34, 64, 20, 30), inner0 = count_in(g_frame, 64, 94, 20, 30);
    TEST_ASSERT_TRUE(outer0 > inner0);
    eyes_render(&e, 1, g_frame);
    int inner1 = count_in(g_frame, 34, 64, 20, 30), outer1 = count_in(g_frame, 64, 94, 20, 30);
    TEST_ASSERT_TRUE(outer1 > inner1);
}

static void test_sad_is_the_opposite_tilt_of_angry(void)
{
    eyes_t e = make(EX_SAD);
    eyes_render(&e, 0, g_frame);
    int outer0 = count_in(g_frame, 34, 64, 20, 32), inner0 = count_in(g_frame, 64, 94, 20, 32);
    TEST_ASSERT_TRUE(inner0 > outer0);
}

static void test_gaze_moves_the_eye(void)
{
    eyes_t e; eyes_init(&e, 1, 0);
    eyes_render(&e, 0, g_frame);  stats_t c = stats(g_frame);
    TEST_ASSERT_TRUE(eyes_set(&e, EX_NEUTRAL, 100, 0, 0, 0));
    eyes_render(&e, 0, g_frame);  stats_t r = stats(g_frame);
    TEST_ASSERT_EQUAL_INT(EYES_GAZE_MAX_X, r.minx - c.minx);
    TEST_ASSERT_TRUE(eyes_set(&e, EX_NEUTRAL, -100, -100, 0, 0));
    eyes_render(&e, 0, g_frame);  stats_t l = stats(g_frame);
    TEST_ASSERT_EQUAL_INT(-EYES_GAZE_MAX_X, l.minx - c.minx);
    TEST_ASSERT_EQUAL_INT(-EYES_GAZE_MAX_Y, l.miny - c.miny);
}

static void test_set_rejects_bad_arguments_and_changes_nothing(void)
{
    eyes_t e = make(EX_HAPPY);
    eyes_t before = e;
    TEST_ASSERT_FALSE(eyes_set(&e, EYES_COUNT, 0, 0, 100, 5));
    TEST_ASSERT_FALSE(eyes_set(&e, EX_SAD, 101, 0, 100, 5));
    TEST_ASSERT_FALSE(eyes_set(&e, EX_SAD, 0, -101, 100, 5));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &e, sizeof e));
}

static void test_tween_goes_smoothly_from_old_to_new(void)
{
    eyes_t e = make(EX_NEUTRAL);
    const int h0 = e.cur.eye[0].h;                                  /* 44 */
    TEST_ASSERT_TRUE(eyes_set(&e, EX_SURPRISED, 0, 0, 200, 1000));
    eyes_step(&e, 1000);
    TEST_ASSERT_EQUAL_INT(h0, e.cur.eye[0].h);                      /* starts where it was */
    eyes_step(&e, 1100);
    TEST_ASSERT_EQUAL_INT(50, e.cur.eye[0].h);                      /* halfway between 44 and 56 */
    eyes_step(&e, 1200);
    TEST_ASSERT_EQUAL_INT(56, e.cur.eye[0].h);                      /* arrives exactly */
    TEST_ASSERT_FALSE(e.tweening);
    eyes_step(&e, 5000);
    TEST_ASSERT_EQUAL_INT(56, e.cur.eye[0].h);                      /* and stays */
}

static void test_set_without_tween_jumps_at_once(void)
{
    eyes_t e = make(EX_NEUTRAL);
    TEST_ASSERT_TRUE(eyes_set(&e, EX_ALERT, 0, 0, 0, 10));
    TEST_ASSERT_EQUAL_INT(54, e.cur.eye[0].h);
    TEST_ASSERT_EQUAL_UINT8(EX_ALERT, e.expression);
}

static void test_blink_squeezes_the_eye_and_comes_back(void)
{
    eyes_t e = make(EX_NEUTRAL);
    eyes_render(&e, 0, g_frame);  const int open = stats(g_frame).count;
    eyes_blink(&e, 100);
    eyes_step(&e, 100);                                             /* t = 0: still open */
    eyes_render(&e, 0, g_frame);  TEST_ASSERT_EQUAL_INT(open, stats(g_frame).count);
    eyes_step(&e, 140);                                             /* t = 40: half closed */
    eyes_render(&e, 0, g_frame);  const int mid = stats(g_frame).count;
    TEST_ASSERT_TRUE(mid < open * 7 / 10);
    eyes_step(&e, 180);                                             /* t = 80: fully closed (thin line) */
    eyes_render(&e, 0, g_frame);  const int shut = stats(g_frame).count;
    TEST_ASSERT_TRUE(shut < open / 5);
    eyes_step(&e, 100 + EYES_BLINK_MS);                             /* finished */
    TEST_ASSERT_FALSE(e.blinking);
    eyes_render(&e, 0, g_frame);  TEST_ASSERT_EQUAL_INT(open, stats(g_frame).count);
}

static void test_idle_off_never_blinks_or_moves(void)
{
    eyes_t e = make(EX_NEUTRAL);
    for (uint32_t t = 0; t < 30000; t += 40) { eyes_step(&e, t); }
    TEST_ASSERT_FALSE(e.blinking);
    TEST_ASSERT_EQUAL_INT(0, e.idle_gx);
    TEST_ASSERT_EQUAL_INT(0, e.idle_gy);
}

static void test_idle_on_blinks_regularly_and_looks_around(void)
{
    eyes_t e = make(EX_NEUTRAL);
    eyes_idle(&e, true, 0);
    int blinks = 0, moved = 0; bool was = false;
    for (uint32_t t = 0; t < 60000; t += 40) {
        eyes_step(&e, t);
        if (e.blinking && !was) { blinks++; }
        was = e.blinking;
        if (e.idle_gx != 0 || e.idle_gy != 0) { moved = 1; }
    }
    TEST_ASSERT_TRUE(blinks >= 8 && blinks <= 20);                  /* one every 3.5 to 5.3 s, about 60 s */
    TEST_ASSERT_EQUAL_INT(1, moved);
    eyes_idle(&e, false, 60000);
    TEST_ASSERT_EQUAL_INT(0, e.idle_gx);                            /* turning idle off recentres the gaze */
}

static void test_idle_is_repeatable_for_one_seed_and_differs_between_seeds(void)
{
    eyes_t a, b, c;
    eyes_init(&a, 7, 0); eyes_init(&b, 7, 0); eyes_init(&c, 8, 0);
    eyes_idle(&a, true, 0); eyes_idle(&b, true, 0); eyes_idle(&c, true, 0);
    int diff_ab = 0, diff_ac = 0;
    for (uint32_t t = 0; t < 20000; t += 40) {
        eyes_step(&a, t); eyes_step(&b, t); eyes_step(&c, t);
        if (a.idle_gx != b.idle_gx || a.blinking != b.blinking) { diff_ab++; }
        if (a.idle_gx != c.idle_gx || a.blinking != c.blinking) { diff_ac++; }
    }
    TEST_ASSERT_EQUAL_INT(0, diff_ab);
    TEST_ASSERT_TRUE(diff_ac > 0);
}

static void test_time_wrap_does_not_break_the_schedule(void)
{
    eyes_t e; eyes_init(&e, 3, 0xFFFFF000u);                        /* clock about to wrap */
    eyes_idle(&e, true, 0xFFFFF000u);
    int blinks = 0; bool was = false;
    for (uint32_t i = 0; i < 1500; i++) {
        eyes_step(&e, 0xFFFFF000u + i * 40u);                       /* crosses 0 after 4096 ms */
        if (e.blinking && !was) { blinks++; }
        was = e.blinking;
    }
    TEST_ASSERT_TRUE(blinks >= 8);
}

static void test_dirty_pages_mask(void)
{
    static uint8_t a[EYES_FRAME_BYTES], b[EYES_FRAME_BYTES];
    memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    TEST_ASSERT_EQUAL_UINT8(0x00, eyes_dirty_pages(a, b));
    b[EYES_W * 3 + 10] = 1;                                         /* page 3 */
    TEST_ASSERT_EQUAL_UINT8(0x08, eyes_dirty_pages(a, b));
    b[EYES_W * 7 + 127] = 1;                                        /* page 7, last byte */
    TEST_ASSERT_EQUAL_UINT8(0x88, eyes_dirty_pages(a, b));
}

static void test_a_blink_only_dirties_pages_around_the_eye(void)
{
    static uint8_t open[EYES_FRAME_BYTES], shut[EYES_FRAME_BYTES];
    eyes_t e = make(EX_NEUTRAL);
    eyes_render(&e, 0, open);
    eyes_blink(&e, 0); eyes_step(&e, 0); eyes_step(&e, 80);
    eyes_render(&e, 0, shut);
    const uint8_t mask = eyes_dirty_pages(open, shut);
    TEST_ASSERT_TRUE((mask & 0x01) == 0);                           /* the top strip of the panel is never touched */
    TEST_ASSERT_TRUE((mask & 0x80) == 0);                           /* nor the bottom one */
    TEST_ASSERT_TRUE(mask != 0);
}

static void test_bad_eye_index_gives_an_empty_frame(void)
{
    eyes_t e = make(EX_NEUTRAL);
    memset(g_frame, 0xFF, sizeof g_frame);
    eyes_render(&e, 2, g_frame);
    TEST_ASSERT_EQUAL_INT(0, stats(g_frame).count);
}

static void test_expression_names_match_the_protocol_order(void)
{
    TEST_ASSERT_EQUAL_STRING("neutral", eyes_expression_name(0));
    TEST_ASSERT_EQUAL_STRING("happy", eyes_expression_name(1));
    TEST_ASSERT_EQUAL_STRING("wink", eyes_expression_name(8));
    TEST_ASSERT_EQUAL_STRING("alert", eyes_expression_name(9));
    TEST_ASSERT_EQUAL_STRING("?", eyes_expression_name(10));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_layout_is_page_format);
    RUN_TEST(test_neutral_is_centred_and_inside_the_panel);
    RUN_TEST(test_neutral_eye_is_mirror_symmetric_in_both_axes);
    RUN_TEST(test_every_expression_renders_both_eyes_without_touching_the_border_rows);
    RUN_TEST(test_expressions_look_different_from_each_other);
    RUN_TEST(test_wink_closes_only_eye_zero);
    RUN_TEST(test_angry_lids_slope_down_towards_the_nose_on_both_eyes);
    RUN_TEST(test_sad_is_the_opposite_tilt_of_angry);
    RUN_TEST(test_gaze_moves_the_eye);
    RUN_TEST(test_set_rejects_bad_arguments_and_changes_nothing);
    RUN_TEST(test_tween_goes_smoothly_from_old_to_new);
    RUN_TEST(test_set_without_tween_jumps_at_once);
    RUN_TEST(test_blink_squeezes_the_eye_and_comes_back);
    RUN_TEST(test_idle_off_never_blinks_or_moves);
    RUN_TEST(test_idle_on_blinks_regularly_and_looks_around);
    RUN_TEST(test_idle_is_repeatable_for_one_seed_and_differs_between_seeds);
    RUN_TEST(test_time_wrap_does_not_break_the_schedule);
    RUN_TEST(test_dirty_pages_mask);
    RUN_TEST(test_a_blink_only_dirties_pages_around_the_eye);
    RUN_TEST(test_bad_eye_index_gives_an_empty_frame);
    RUN_TEST(test_expression_names_match_the_protocol_order);
    return UNITY_END();
}
