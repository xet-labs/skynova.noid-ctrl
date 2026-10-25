/* line.c - the console line editor: one byte in, one instruction out.
 *
 * Why a state machine and not "read until newline": bytes arrive one at a time
 * from an interrupt-fed UART buffer, and the same UART may also carry binary
 * frames, so we can never block waiting for a whole line.
 *
 * Arrow keys and function keys arrive as escape sequences:
 *   ESC [ <parameters> <final byte 0x40..0x7E>      e.g. up arrow = 1B 5B 41
 *   ESC O <one byte>                                e.g. F1 on some terminals = 1B 4F 50
 * We swallow the whole sequence so none of it lands in the line.
 */
#include <string.h>
#include "skynoid_text.h"

enum { ESC_NONE = 0, ESC_START, ESC_CSI, ESC_SS3 };

void skt_line_init(skt_line_t *l)
{
    memset(l, 0, sizeof *l);
}

skt_event_t skt_line_feed(skt_line_t *l, uint8_t c)
{
    if (l->done) {                      /* the previous line was handed out: start fresh */
        l->n = 0;
        l->overflow = false;
        l->done = false;
    }
    const bool was_cr = l->last_cr;
    l->last_cr = false;

    switch (l->esc) {                   /* inside an escape sequence: swallow */
    case ESC_START:
        l->esc = (c == '[') ? ESC_CSI : (c == 'O') ? ESC_SS3 : ESC_NONE;
        return SKT_EV_NONE;
    case ESC_CSI:
        if (c >= 0x40 && c <= 0x7E) { l->esc = ESC_NONE; }   /* final byte ends it */
        return SKT_EV_NONE;
    case ESC_SS3:
        l->esc = ESC_NONE;
        return SKT_EV_NONE;
    default:
        break;
    }

    if (c == 0x1B) {
        l->esc = ESC_START;
        return SKT_EV_NONE;
    }
    if (c == '\r' || c == '\n') {
        if (c == '\n' && was_cr) {      /* the LF of a CR+LF pair: same Enter */
            return SKT_EV_NONE;
        }
        l->last_cr = (c == '\r');
        l->buf[l->n] = '\0';
        l->done = true;
        return l->overflow ? SKT_EV_TOO_LONG : SKT_EV_LINE;
    }
    if (c == 0x08 || c == 0x7F) {       /* backspace or delete */
        if (l->overflow || l->n == 0) {
            return SKT_EV_NONE;         /* dropped characters cannot be un-dropped */
        }
        l->n--;
        return SKT_EV_ERASE;
    }
    if (c >= 0x20 && c <= 0x7E) {       /* printable ASCII */
        if (l->overflow) {
            return SKT_EV_NONE;
        }
        if (l->n >= SKT_LINE_MAX) {
            l->overflow = true;         /* the 121st character: drop the whole line at Enter */
            return SKT_EV_NONE;
        }
        l->buf[l->n++] = (char)c;
        return SKT_EV_ECHO;
    }
    return SKT_EV_NONE;                 /* Tab and other control bytes */
}
