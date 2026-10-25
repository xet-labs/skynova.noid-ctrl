/*
 * logtext.c - pure text helpers for the logger.
 *
 * Purpose : keep every character-level decision of the logger testable on the PC.
 * Called by: firmware/src/core/logger.c
 * Test    : firmware/test/test_logtext/test_logtext.c
 */
#include "skynoid_logtext.h"

#include <stdio.h>
#include <string.h>

int lt_level_from_text(const char *s)
{
    if (s == NULL) { return -1; }
    if (s[0] == '\033') {                                   /* skip a colour code such as ESC[0;32m */
        const char *m = strchr(s, 'm');
        if (m == NULL) { return -1; }
        s = m + 1;
    }
    if (s[0] == '\0' || s[1] != ' ' || s[2] != '(') { return -1; }   /* "X (" is the shape of a log line */
    switch (s[0]) {
    case 'E': return 0;
    case 'W': return 1;
    case 'I': return 2;
    case 'D': return 3;
    case 'V': return 4;
    default:  return -1;
    }
}

size_t lt_format(char *out, size_t cap, int level, uint32_t ms, const char *text)
{
    static const char LETTERS[] = "EWIDV";
    if (cap == 0) { return 0; }
    const char letter = (level >= 0 && level <= 4) ? LETTERS[level] : 'I';
    const int n = snprintf(out, cap, "%c (%lu) %s", letter, (unsigned long)ms, text);
    if (n < 0) { out[0] = '\0'; return 0; }
    return ((size_t)n < cap) ? (size_t)n : cap - 1;
}

size_t lt_trim_eol(const char *s, size_t len)
{
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) { len--; }
    return len;
}

size_t lt_to_crlf(char *out, const char *in, size_t len, char *prev)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (in[i] == '\n' && *prev != '\r') { out[n++] = '\r'; }
        out[n++] = in[i];
        *prev = in[i];
    }
    return n;
}
