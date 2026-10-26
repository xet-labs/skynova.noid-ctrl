/*
 * link.h - the framed link to the Pi on UART0 (step 1.4): PING to PONG, HELLO, session end.
 *
 * Who calls it: the console task (src/console/console_uart.c) hands every valid frame to link_on_frame()
 *   and calls link_tick() every loop (at most 20 ms apart). Any task may call link_send().
 * Step 1.5a: CMD frames go to the dispatcher (lib/skynoid_dispatch); base events and the TLM stream are sent from link_tick().
 */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "skynoid_wire.h"

void link_init(void);                           /* once, before the console task starts */
void link_on_frame(const skw_frame_t *f);       /* console task only */
void link_tick(void);                           /* console task only (shared mode) or the link task (separate): ends the session after 500 ms of silence */
void link_start_rx(void);                       /* once, after chan_init and logger_start: starts the link reader task ONLY when the console has its own UART */
bool link_session_active(void);

/* Send one frame (node to host). payload may be NULL when len is 0. ack is the host seq being answered,
 * else 0. The node's own seq counter is added here. Returns false when the frame was refused (too long
 * for one logger record) or dropped (queue full). Safe from any task. */
bool link_send(uint8_t type, uint8_t msg, const uint8_t *payload, uint16_t len, uint16_t ack);

#endif /* LINK_H */
