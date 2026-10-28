/*
 * skynoid_text.h - the text side of the console: line editor, tokenizer, numbers.
 *
 * WHAT THIS FILE IS
 *   Pure C, NO ESP-IDF includes, so it is tested on the PC (pio test -e native)
 *   and runs unchanged on the ESP32. The console engine (step 1.3b) feeds it
 *   bytes from the UART and acts on what it reports.
 *
 * HOW IT IS USED (receiving side of the console)
 *     skt_line_t line;  skt_line_init(&line);
 *     for each received byte c (that the frame parser called text):
 *         switch (skt_line_feed(&line, c)) {
 *         case SKT_EV_ECHO:  send c back;                    break;
 *         case SKT_EV_ERASE: send "\b \b";                   break;
 *         case SKT_EV_LINE:  tokenize line.buf, run it;      break;
 *         case SKT_EV_TOO_LONG: print "ERR invalid_arg line too long"; break;
 *         default: break;
 *         }
 *
 * LIFETIME RULE
 *   After SKT_EV_LINE or SKT_EV_TOO_LONG, line.buf stays valid until the NEXT
 *   call to skt_line_feed. Finish with the line (and any argv pointing into it)
 *   before feeding more bytes.
 *
 * TESTS: firmware/test/test_text
 */
#ifndef SKYNOID_TEXT_H
#define SKYNOID_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKT_LINE_MAX 120u   /* longest accepted line in characters (spec: console, step 1.3) */

typedef enum {
    SKT_EV_NONE = 0,   /* byte ignored or consumed silently */
    SKT_EV_ECHO,       /* printable byte stored: echo it back to the terminal */
    SKT_EV_ERASE,      /* last character removed: send "\b \b" */
    SKT_EV_LINE,       /* Enter pressed: line.buf holds the NUL-terminated line (may be empty) */
    SKT_EV_TOO_LONG,   /* Enter pressed after more than SKT_LINE_MAX characters: line dropped */
    SKT_EV_REPLACE     /* arrow key (up): line replaced with previous command */
} skt_event_t;

typedef struct {
    char    buf[SKT_LINE_MAX + 1];  /* the line being typed, always NUL-terminated after Enter */
    size_t  n;                      /* characters stored so far */
    char    prev[SKT_LINE_MAX + 1]; /* previous executed line */
    size_t  prev_len;               /* length of previous line */
    size_t  erased;                 /* characters to erase on screen (for SKT_EV_REPLACE) */
    uint8_t esc;                    /* inside an arrow-key style escape sequence (internal) */
    bool    overflow;               /* line too long: ignore everything until Enter */
    bool    last_cr;                /* previous byte was CR, so a following LF is the same Enter */
    bool    done;                   /* a line was delivered: start a fresh one on the next byte */
} skt_line_t;

void        skt_line_init(skt_line_t *l);
skt_event_t skt_line_feed(skt_line_t *l, uint8_t byte);

/* Split line in place at spaces and tabs (writes NUL over them, no copy).
 * Fills argv[0..argc-1] with pointers into line. Returns argc (0 for an empty
 * line), or -1 if there are more than max_args words. */
int skt_tokenize(char *line, char *argv[], int max_args);

/* Parse a whole string as a 32-bit integer: decimal ("-5", "+7") or hex
 * ("0x1F", "-0x10"). Returns false for empty text, stray characters or values
 * that do not fit in int32_t; *out is left untouched then. */
bool skt_parse_i32(const char *s, int32_t *out);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_TEXT_H */
