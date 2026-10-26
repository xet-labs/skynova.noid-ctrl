/*
 * skynoid_maint.h - maintenance mode rules, mute flags and loop statistics, as pure C (no ESP-IDF).
 *
 * WHAT THIS FILE IS
 *   Step 1.6a. The decisions behind `maint on|off`, `mute` and `stats`, kept apart from the board so they
 *   are tested on the PC (pio test -e native -f test_maint). The board glue (step 1.6b, src/core/maint.c)
 *   owns the mutex, the clock and the side effects (end the session, disarm, reset parameters).
 *
 * MAINTENANCE MODE (spec: protocol-v0 section 9, sprint-steps 1.6)
 *   Entered with `maint on [minutes]`, left with `maint off`, after the timeout, or at reboot.
 *   Default 10 minutes, maximum 60. Minutes 0 means "default". Anything above 60 or below 0 is refused.
 *   `maint on` while already on renews the deadline from now.
 *
 * TIME  uint32 milliseconds from a free-running clock; differences use unsigned subtraction, so the
 *       49-day wrap is harmless.
 * TESTS firmware/test/test_maint
 */
#ifndef SKYNOID_MAINT_H
#define SKYNOID_MAINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKM_DEFAULT_MIN 10
#define SKM_MAX_MIN     60

/* Streams that `mute` can silence while maintenance mode is on. */
enum { SKM_MUTE_TLM = 1, SKM_MUTE_EVT = 2, SKM_MUTE_LOG = 4, SKM_MUTE_ALL = 7 };

typedef struct {
    bool     on;
    uint32_t deadline_ms;
    uint8_t  mute;            /* SKM_MUTE_* bits; only effective while on */
} skm_t;

typedef enum { SKM_OK = 0, SKM_BAD_MINUTES = 1 } skm_result_t;

void skm_init(skm_t *m);

/* Enter (or renew) maintenance mode. minutes: 0 = default, 1..60 as given, else SKM_BAD_MINUTES and nothing
 * changes. *newly (optional) is set true when the mode was off before: the caller then ends the session,
 * disarms and so on; false when this only renewed the deadline. */
skm_result_t skm_enter(skm_t *m, uint32_t now_ms, int32_t minutes, bool *newly);

/* Leave maintenance mode and clear the mute flags. Returns true if it was on (the caller then disarms,
 * stops outputs and resets the parameters), false if there was nothing to leave. */
bool skm_exit(skm_t *m);

/* True once the deadline has passed while on. The caller calls skm_exit() and does the same clean-up. */
bool skm_expired(const skm_t *m, uint32_t now_ms);

/* Seconds left, rounded up; 0 when off or already expired. */
uint32_t skm_remaining_s(const skm_t *m, uint32_t now_ms);

/* `mute <word>`: "tlm", "evt", "log" add that stream; "all" mutes everything; "none" clears. Returns false for
 * any other word (the mask is unchanged). Words are case-insensitive. */
bool skm_mute_apply(skm_t *m, const char *word);
/* Is this stream (one SKM_MUTE_* bit) muted right now? Always false while maintenance is off. */
bool skm_muted(const skm_t *m, uint8_t stream);

/* ------------------------------------------------------------- loop statistics (`stats`) */
typedef struct {
    uint32_t count;           /* passes recorded */
    uint32_t last_us, max_us;
    uint64_t sum_us;
    uint32_t late;            /* passes that took longer than the period */
} sks_loop_t;

void     sks_loop_init(sks_loop_t *s);
/* One pass took duration_us; the loop is supposed to finish within period_us. */
void     sks_loop_record(sks_loop_t *s, uint32_t duration_us, uint32_t period_us);
uint32_t sks_loop_avg_us(const sks_loop_t *s);            /* 0 when nothing is recorded */
/* "safety   n=1200 avg=240us max=910us late=0". Returns the length (snprintf rules: truncated if cap is small). */
int      skm_format_loop(char *out, size_t cap, const char *name, const sks_loop_t *s);

/* ------------------------------------------------------------- printout helpers (`crash`, `stats`) */
/* Reset-reason names of the HELLO table: 0 unknown, 1 power-on, 2 brownout, 3 watchdog, 4 software, 5 panic.
 * Any other value gives "unknown". */
const char *skm_reset_name(uint8_t reason);
/* Seconds as "02:03:04", or "1d 02:03:04" from one day on. Returns the length (snprintf rules). */
int skm_format_hms(char *out, size_t cap, uint32_t seconds);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_MAINT_H */
