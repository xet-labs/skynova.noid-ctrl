/* token.c - split a command line into words, and read numbers from words.
 *
 * Both functions work on plain C strings and allocate nothing, so they are
 * safe to call from the console task with fixed buffers.
 */
#include "skynoid_text.h"

int skt_tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {   /* skip the gap before a word */
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= max_args) {
            return -1;
        }
        argv[argc++] = p;                   /* a word starts here */
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';                    /* end the word in place */
        }
    }
    return argc;
}

bool skt_parse_i32(const char *s, int32_t *out)
{
    bool negative = false;
    unsigned base = 10;

    if (*s == '-' || *s == '+') {
        negative = (*s == '-');
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (*s == '\0') {
        return false;                       /* nothing to read: "", "-", "0x" */
    }

    /* The magnitude may be 2^31 only when negative (INT32_MIN). */
    const uint64_t limit = negative ? 2147483648ull : 2147483647ull;
    uint64_t value = 0;

    for (; *s != '\0'; s++) {
        unsigned digit;
        if (*s >= '0' && *s <= '9') {
            digit = (unsigned)(*s - '0');
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            digit = (unsigned)(*s - 'a') + 10u;
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            digit = (unsigned)(*s - 'A') + 10u;
        } else {
            return false;                   /* stray character */
        }
        value = value * base + digit;       /* cannot overflow 64 bits: value <= limit before this */
        if (value > limit) {
            return false;
        }
    }
    *out = negative ? (int32_t)(-(int64_t)value) : (int32_t)value;
    return true;
}
