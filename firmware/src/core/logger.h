/*
 * logger.h - the one place that writes text to the UART, plus the flight recorder.
 *
 * Any task may call log_msg() or ESP_LOGx(): the call only copies a short record into a
 * queue and returns at once, so a real-time task never waits for the serial port. The
 * logger task drains the queue, keeps every line in a small ring that survives a reset
 * (the flight recorder) and prints the lines at or below the current log level.
 * The console task sends its output through logger_write_raw() so everything stays in order.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Call once, after the UART driver is installed. Also routes ESP_LOGx through the logger. */
void logger_start(void);

/* Console output and echo: sent to the UART in order with the log lines. May wait up to 20 ms. */
void logger_write_raw(const char *text, size_t len);

/* One binary frame (step 1.4): sent byte for byte, no \n to \r\n conversion, in order with the text.
 * len must be 1..LOGGER_BIN_MAX (one queue record; a longer frame is refused so it can never be split
 * and interleaved with text). May wait up to 20 ms. Returns false when refused or the queue was full. */
#define LOGGER_BIN_MAX 160
bool logger_write_bin(const uint8_t *data, size_t len);

/* level 0 error, 1 warn, 2 info, 3 debug. Never blocks; a full queue drops the line and counts it. */
void log_msg(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define LOGE(...) log_msg(0, __VA_ARGS__)
#define LOGW(...) log_msg(1, __VA_ARGS__)
#define LOGI(...) log_msg(2, __VA_ARGS__)
#define LOGD(...) log_msg(3, __VA_ARGS__)

void     logger_set_level(int level);      /* 0..3: what is PRINTED; the recorder keeps all */
int      logger_get_level(void);
uint32_t logger_dropped(void);             /* lines lost because the queue was full */

/* Copy the flight recorder (oldest first) into out. Returns the number of bytes copied. */
size_t logger_recorder_read(uint8_t *out, size_t cap);

#endif /* LOGGER_H */
