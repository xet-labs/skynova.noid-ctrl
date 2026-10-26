/*
 * skynoid_chan.h - pure rules for the two serial channels (link and console). No ESP-IDF here.
 *
 * What it is : the link carries binary frames, the console carries text. Each runs on one of the three
 *              ESP32 UARTs. When both use the same UART they are "shared" (as the firmware always did);
 *              when they differ the console has its own port.
 * Used by    : src/core/chan_cfg.h (build-time #error checks use the MACROS below),
 *              src/core/chan.c (runs skc_validate at boot), src/core/logger.c (skc_channel_for),
 *              src/console/cmd_system.c (skc_describe). Tested on the PC: test/test_chan.
 * Spec       : docs/steps/step-1.7-uart-channels.md.
 *
 * The number rules are macros on purpose: the preprocessor can evaluate them in `#if`, so the build
 * refuses a bad flag with a readable message, and the same macros back the runtime function that the
 * PC test drives with every combination. Arguments must be plain integers (no casts, no side effects).
 * What the macros cannot know is which GPIO another ROLE owns (motors, IR ...): that check needs
 * board_pins.h and lives only in src/core/chan_cfg.h.
 */
#ifndef SKYNOID_CHAN_H
#define SKYNOID_CHAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- number rules (usable in #if) ------------------------------------------------------------- */
#define SKC_UART_MAX      2                 /* UART numbers 0, 1, 2 */
#define SKC_BAUD_MIN      1200
#define SKC_BAUD_MAX      2000000
#define SKC_UART0_TX_PIN  1                 /* the USB bridge is wired here; UART0 may not move */
#define SKC_UART0_RX_PIN  3
#define SKC_TX_PIN_MAX    33                /* GPIO34..39 are input-only */

#define SKC_NUM_OK(n)       ((n) >= 0 && (n) <= SKC_UART_MAX)
#define SKC_BAUD_OK(b)      ((b) >= SKC_BAUD_MIN && (b) <= SKC_BAUD_MAX)
/* A GPIO that exists on a 38-pin ESP32 dev board (WROOM-32): 0..39 without 20, 24, 28..31, 37, 38. */
#define SKC_PIN_EXISTS(p)   ((p) >= 0 && (p) <= 39 && (p) != 20 && (p) != 24 && \
                             !((p) >= 28 && (p) <= 31) && (p) != 37 && (p) != 38)
/* Same set as SK_NEVER_USE_MASK in board_pins.h: GPIO0 and 12 are boot-strap pins, 6..11 hold the flash. */
#define SKC_PIN_FORBIDDEN(p) ((p) == 0 || (p) == 12 || ((p) >= 6 && (p) <= 11))
#define SKC_PIN_CAN_OUTPUT(p) ((p) <= SKC_TX_PIN_MAX)
#define SKC_PIN_IS_USB(p)   ((p) == SKC_UART0_TX_PIN || (p) == SKC_UART0_RX_PIN)

/* ---- one channel's settings -------------------------------------------------------------------- */
typedef struct {
    int     num;        /* UART number 0..2 */
    int32_t baud;
    int     tx_pin;     /* GPIO number, -1 = not given */
    int     rx_pin;
} skc_uart_t;

typedef enum {
    SKC_OK = 0,
    SKC_ERR_NUM,            /* UART number not 0..2 (or a NULL argument) */
    SKC_ERR_BAUD,           /* baud outside SKC_BAUD_MIN..SKC_BAUD_MAX */
    SKC_ERR_UART0_PINS,     /* UART0 with pins other than GPIO1/3 */
    SKC_ERR_PIN_MISSING,    /* UART1/2 without both pins */
    SKC_ERR_PIN_RANGE,      /* not an existing GPIO */
    SKC_ERR_PIN_FORBIDDEN,  /* boot-strap or flash pin */
    SKC_ERR_PIN_USB,        /* GPIO1/3 used by UART1/2 */
    SKC_ERR_TX_INPUT_ONLY,  /* TX on GPIO34..39 */
    SKC_ERR_PIN_SAME,       /* TX and RX are the same pin */
    SKC_ERR_PIN_SHARED,     /* link and console use a common pin */
    SKC_ERR_SHARED_BAUD,    /* shared port, console baud given and different from the link baud */
    SKC_ERR_SHARED_PINS,    /* shared port, console pins differ from the link pins */
    SKC_ERR_COUNT           /* not an error: number of codes */
} skc_err_t;

/* Same UART number = one port carries both. */
bool skc_shared(const skc_uart_t *link, const skc_uart_t *console);

/* Check a combination. console_baud_given is true when the person set UART_CONSOLE_BAUD explicitly (a
 * default never counts). In shared mode only the link's baud and pins count; the console's must equal the
 * link's pins, and its baud must be absent or equal. Returns SKC_OK or the FIRST problem found:
 * link first, then console (if separate), then the link/console pin overlap. */
skc_err_t skc_validate(const skc_uart_t *link, const skc_uart_t *console, bool console_baud_given);

/* One short sentence naming the rule that failed; never NULL. */
const char *skc_err_text(skc_err_t err);

/* ---- routing: which channel a record leaves on -------------------------------------------------- */
/* The values equal the `raw` field of a logger record (src/core/logger.c). */
typedef enum { SKC_REC_LOG = 0, SKC_REC_TEXT = 1, SKC_REC_BIN = 2 } skc_rec_t;
typedef enum { SKC_CH_LINK = 0, SKC_CH_CONSOLE = 1 } skc_ch_t;

/* Shared: everything goes to the one port, reported as LINK (one byte stream, queue order).
 * Separate: binary frames to LINK; log lines and console text to CONSOLE. A record kind that is not
 * 0..2 is never put on the frame wire when separate (CONSOLE), and goes to LINK when shared. */
skc_ch_t skc_channel_for(bool shared, int kind);

/* "link uart0@460800 tx1 rx3; console shared" or "...; console uart2@115200 tx5 rx15".
 * Always NUL-terminated when cap > 0; returns the number of characters written (without the NUL). */
size_t skc_describe(char *out, size_t cap, const skc_uart_t *link, const skc_uart_t *console);

#endif /* SKYNOID_CHAN_H */
