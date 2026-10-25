/*
 * skynoid_link.h - pure logic of the PC link: session timing and the PING/PONG/HELLO payloads.
 *
 * WHAT THIS FILE IS
 *   No ESP-IDF includes, so it is tested on the PC (pio test -e native -f test_link).
 *   The board code (src/link/link.c) owns the UART and the clock and calls into this file.
 *
 * THE RULES IT IMPLEMENTS (spec protocol-v0 section 9)
 *   - A session starts with the first valid PING after boot or after the last session ended.
 *     The node then answers HELLO and PONG; the board code does that when sklk_on_ping() says "new".
 *   - A session ends when no PING arrives for timeout_ms (500). Then the board brakes and goes SAFE.
 *   - For hold_ms (3000) after the end, log output stays in frame format; after that it is plain text.
 *
 * TIME  All times are uint32 milliseconds from a free-running clock. Differences use unsigned
 *       subtraction, so a clock wrap after 49 days is harmless.
 * TESTS firmware/test/test_link
 */
#ifndef SKYNOID_LINK_H
#define SKYNOID_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKLK_PING_LEN   4u     /* host_t_ms u32 */
#define SKLK_PONG_LEN   9u     /* host_t_ms u32, node_t_ms u32, hazard u8 */
#define SKLK_HELLO_LEN  19u    /* see sklk_hello_t */

typedef struct {
    bool     active;           /* a session is running */
    bool     ever_ended;       /* a session has ended since boot (hold timer is meaningful) */
    uint32_t last_ping_ms;     /* when the newest PING arrived */
    uint32_t ended_ms;         /* when the last session ended */
} sklk_session_t;

typedef enum { SKLK_TICK_NONE = 0, SKLK_TICK_SESSION_ENDED = 1 } sklk_tick_t;

void sklk_init(sklk_session_t *s);

/* Call for every valid PING. Returns true when this PING STARTS a new session (send HELLO, then PONG). */
bool sklk_on_ping(sklk_session_t *s, uint32_t now_ms);

/* Call regularly (every 20 ms is enough). Returns SKLK_TICK_SESSION_ENDED once, on the call that sees the
 * timeout; the caller then brakes, goes SAFE and queues link_lost. */
sklk_tick_t sklk_tick(sklk_session_t *s, uint32_t now_ms, uint32_t timeout_ms);

/* May the node send binary frames now (a session runs, or it ended less than hold_ms ago)? Log output uses
 * this to choose between LOG frames and plain text. */
bool sklk_frames_allowed(const sklk_session_t *s, uint32_t now_ms, uint32_t hold_ms);

/* ---- payloads (little endian, fixed width) ---- */
/* Reads the PING payload. Returns false when len is not SKLK_PING_LEN. */
bool   sklk_parse_ping(const uint8_t *payload, uint16_t len, uint32_t *host_t_ms);
/* Writes SKLK_PONG_LEN bytes into out. Returns SKLK_PONG_LEN. */
size_t sklk_build_pong(uint8_t out[SKLK_PONG_LEN], uint32_t host_t_ms, uint32_t node_t_ms, uint8_t hazard);

typedef struct {
    uint8_t  role;             /* 1 base, 2 upper, 3 single */
    uint16_t caps;
    uint8_t  fw_major, fw_minor, proto;
    uint8_t  reset_reason;     /* 0 unknown, 1 power-on, 2 brownout, 3 watchdog, 4 software, 5 panic */
    uint8_t  chip_id[6];
    uint16_t boot_count;
    uint32_t schema_hash;      /* 0 until the generated schema exists */
} sklk_hello_t;

/* Writes SKLK_HELLO_LEN bytes into out. Returns SKLK_HELLO_LEN. */
size_t sklk_build_hello(uint8_t out[SKLK_HELLO_LEN], const sklk_hello_t *h);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_LINK_H */
