/*
 * skynoid_logtext.h - pure text helpers for the logger: log-line format, level
 *                     detection, and "\n" to "\r\n" conversion.
 *
 * No ESP-IDF includes; tested on the PC (pio test -e native -f test_logtext).
 * Used by firmware/src/core/logger.c. Spec: sprint-steps.md step 1.3 (logger),
 * console-commands.md ("log level").
 */
#ifndef SKYNOID_LOGTEXT_H
#define SKYNOID_LOGTEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Levels: 0 error, 1 warn, 2 info, 3 debug, 4 verbose (same numbers as ESP-IDF's order). */

/* Level of a line that starts like an ESP-IDF log line ("I (123) tag: ..."), also when it is
 * preceded by an ANSI colour code. Returns -1 when the text is not a log line. */
int lt_level_from_text(const char *s);

/* Write "I (12345) text" (no newline) into out. Always NUL-terminates when cap > 0; text that
 * does not fit is cut. Returns the number of characters written, not counting the NUL. */
size_t lt_format(char *out, size_t cap, int level, uint32_t ms, const char *text);

/* Length of s without trailing '\r' and '\n' characters. */
size_t lt_trim_eol(const char *s, size_t len);

/* Copy in to out, turning each "\n" into "\r\n" unless the character before it was already '\r'.
 * *prev carries the last character between calls (start it at 0). out must hold 2 * len bytes.
 * Returns the number of bytes written. */
size_t lt_to_crlf(char *out, const char *in, size_t len, char *prev);

#ifdef __cplusplus
}
#endif
#endif /* SKYNOID_LOGTEXT_H */
