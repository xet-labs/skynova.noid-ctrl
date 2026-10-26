/*
 * eyes_preview.c - draw the eyes in the terminal, no board needed.
 *
 * Build and run from the ctrl repo root:
 *   gcc -std=c11 -Ifirmware/lib/skynoid_eyes/include scripts/eyes_preview.c \
 *       firmware/lib/skynoid_eyes/src/eyes.c -o /tmp/eyes_preview
 *   /tmp/eyes_preview                 all ten expressions
 *   /tmp/eyes_preview happy           one expression (name or number 0..9)
 *   /tmp/eyes_preview thinking 60 -30 one expression looking right and up (gaze -100..100)
 *
 * Each panel is 128x64 pixels; the picture is shrunk by 2 in both directions, so one
 * character is a 2x2 block ('#' when 2 or more of its 4 pixels are lit). Left = eye 0,
 * right = eye 1 (what the two OLEDs show). Edit the numbers in lib/skynoid_eyes/src/eyes.c
 * (POSES) and run it again until the faces look right, then try them on the real panels.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "skynoid_eyes.h"

static int px(const uint8_t *f, int x, int y)
{
    return (f[(y >> 3) * EYES_W + x] >> (y & 7)) & 1;
}

static void show(uint8_t ex, int gx, int gy)
{
    eyes_t e;
    uint8_t f0[EYES_FRAME_BYTES], f1[EYES_FRAME_BYTES];
    eyes_init(&e, 1, 0);
    eyes_set(&e, ex, gx, gy, 0, 0);
    eyes_render(&e, 0, f0);
    eyes_render(&e, 1, f1);
    printf("=== %d %s ===\n", ex, eyes_expression_name(ex));
    for (int y = 0; y < EYES_H; y += 2) {
        for (int pass = 0; pass < 2; pass++) {
            const uint8_t *f = pass ? f1 : f0;
            for (int x = 0; x < EYES_W; x += 2) {
                int n = px(f, x, y) + px(f, x + 1, y) + px(f, x, y + 1) + px(f, x + 1, y + 1);
                putchar(n >= 2 ? '#' : ' ');
            }
            printf(pass ? "" : "|");
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    int gx = (argc > 2) ? atoi(argv[2]) : 0;
    int gy = (argc > 3) ? atoi(argv[3]) : 0;
    if (argc > 1) {
        int ex = -1;
        for (int i = 0; i < EYES_COUNT; i++) {
            if (strcmp(argv[1], eyes_expression_name((uint8_t)i)) == 0) { ex = i; }
        }
        if (ex < 0) { ex = atoi(argv[1]); }
        if (ex < 0 || ex >= EYES_COUNT) { fprintf(stderr, "expression 0..%d or a name\n", EYES_COUNT - 1); return 1; }
        show((uint8_t)ex, gx, gy);
        return 0;
    }
    for (int ex = 0; ex < EYES_COUNT; ex++) { show((uint8_t)ex, 0, 0); }
    return 0;
}
