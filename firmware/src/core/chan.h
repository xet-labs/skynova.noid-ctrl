/*
 * chan.h - the two serial channels of the firmware: the LINK (binary frames) and the CONSOLE (text).
 *
 * Who owns what: this layer is the only code that knows which UART number, baud and pins each channel has
 * (macros UART_LINK_*, UART_CONSOLE_* in app_config.h, checked at build time by core/chan_cfg.h). Everyone
 * else asks for a channel, not for a UART: the logger writes with chan_write(), the console task and the
 * link task read with chan_read().
 *
 * Shared mode (same UART number, the default): both channels are one port, one byte stream.
 * Separate mode: frames only on the link UART, text only on the console UART.
 *
 * Spec: docs/steps/step-1.7-uart-channels.md. Pure rules (validation, routing): lib/skynoid_chan.
 */
#ifndef CHAN_H
#define CHAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "skynoid_chan.h"

/* Call once, before logger_start(). Validates the settings again (the build already refused bad ones; a
 * failure here prints the reason and aborts), installs the UART driver of each channel in use and, when
 * neither channel uses UART0, sets UART0 to UART0_FREE_BAUD on purpose. */
void chan_init(void);

bool chan_shared(void);                     /* true: link and console are one port */

/* Read up to cap bytes from a channel, waiting at most wait_ms. Returns the byte count (0 = nothing),
 * or a negative number on error. In shared mode both channels read the same port: only ONE task may read it. */
int chan_read(skc_ch_t ch, uint8_t *buf, size_t cap, uint32_t wait_ms);

/* Write bytes to a channel, untouched. Called only from the logger task (the single writer). */
void chan_write(skc_ch_t ch, const void *data, size_t len);

/* One line: "link uart0@460800 tx1 rx3; console shared". Valid after chan_init. Never NULL. */
const char *chan_describe(void);

#endif /* CHAN_H */
