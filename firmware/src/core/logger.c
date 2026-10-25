/*
 * logger.c - the logger task: log queue, UART writer, flight recorder.
 *
 * Purpose : real-time tasks must never wait for the serial port, and the last lines
 *           before a crash must survive the reset.
 * Called by: logger_start() from console_start(); log_msg()/ESP_LOGx from any task.
 * Test    : on the board (the character handling is tested on the PC: test_logtext).
 * Spec    : sprint-steps.md step 1.3 ("logger"), 04-firmware/02-base-node.md.
 *
 * Flow:  log_msg / ESP_LOGx --copy--> queue --> logger task --> flight recorder (RTC RAM)
 *                                                          \--> UART (if level <= current)
 * Console text (echo, replies) takes the same queue as "raw" records so lines never interleave
 * mid-word. A full queue drops the new line and counts it (`log stats`); it never blocks a caller.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "core/logger.h"
#include "skynoid_core.h"
#include "skynoid_logtext.h"

#define LOG_UART      UART_NUM_0
#define REC_TEXT_MAX  160
#define FR_CAP        2048          /* flight recorder size in bytes */
#define TASK_STACK    3072
#define TASK_PRIO     2

typedef struct {
    uint8_t  raw;                   /* 0 = a log line, 1 = console text to print as is, 2 = binary frame */
    uint16_t len;
    char     text[REC_TEXT_MAX];
} log_rec_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_fr_lock;
static volatile int s_level = SKYNOID_LOG_LEVEL;
static volatile uint32_t s_dropped;

/* In RTC "noinit" RAM: survives a software or watchdog reset, random after power-on (the ring
 * header check in skl_ring_attach throws such garbage away). */
static RTC_NOINIT_ATTR uint8_t s_fr_buf[FR_CAP];
static RTC_NOINIT_ATTR skl_ring_t s_fr;

static void post(const log_rec_t *r, TickType_t wait)
{
    if (s_queue == NULL) { return; }
    if (xQueueSend(s_queue, r, wait) != pdTRUE) { s_dropped++; }
}

/* ---- producers (any task) --------------------------------------------------------- */
void log_msg(int level, const char *fmt, ...)
{
    if (s_queue == NULL || level > 3) { return; }
    char body[LOG_TEXT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    log_rec_t r;
    r.raw = 0;
    r.len = (uint16_t)lt_format(r.text, sizeof r.text, level, (uint32_t)(esp_timer_get_time() / 1000), body);
    post(&r, 0);
}

void logger_write_raw(const char *text, size_t len)
{
    while (s_queue != NULL && len > 0) {
        log_rec_t r;
        r.raw = 1;
        r.len = (uint16_t)((len < REC_TEXT_MAX) ? len : REC_TEXT_MAX);
        memcpy(r.text, text, r.len);
        post(&r, pdMS_TO_TICKS(20));
        text += r.len;
        len -= r.len;
    }
}

bool logger_write_bin(const uint8_t *data, size_t len)
{
    if (s_queue == NULL || data == NULL || len == 0 || len > LOGGER_BIN_MAX) { return false; }
    log_rec_t r;
    r.raw = 2;
    r.len = (uint16_t)len;
    memcpy(r.text, data, len);
    if (xQueueSend(s_queue, &r, pdMS_TO_TICKS(20)) != pdTRUE) { s_dropped++; return false; }
    return true;
}

/* ESP_LOGx lands here (installed with esp_log_set_vprintf). The text already starts with
 * "I (123) tag: ..." so it is queued as it is. */
static int log_hook(const char *fmt, va_list ap)
{
    log_rec_t r;
    r.raw = 0;
    int n = vsnprintf(r.text, sizeof r.text, fmt, ap);
    if (n < 0) { return n; }
    if (n >= (int)sizeof r.text) { n = (int)sizeof r.text - 1; }
    r.len = (uint16_t)lt_trim_eol(r.text, (size_t)n);
    r.text[r.len] = '\0';
    if (r.len > 0) { post(&r, 0); }
    return n;
}

/* ---- consumer: the logger task ---------------------------------------------------------- */
static void uart_out(const char *text, size_t len, char *prev)
{
    char buf[2 * REC_TEXT_MAX + 2];
    const size_t n = lt_to_crlf(buf, text, len, prev);
    uart_write_bytes(LOG_UART, buf, n);
}

static void logger_task(void *arg)
{
    (void)arg;
    char prev = 0;
    log_rec_t r;
    for (;;) {
        if (xQueueReceive(s_queue, &r, portMAX_DELAY) != pdTRUE) { continue; }
        if (r.raw == 2) { uart_write_bytes(LOG_UART, r.text, r.len); continue; }      /* binary: untouched */
        if (r.raw) { uart_out(r.text, r.len, &prev); continue; }

        if (xSemaphoreTake(s_fr_lock, pdMS_TO_TICKS(10)) == pdTRUE) {      /* record everything */
            skl_ring_put(&s_fr, s_fr_buf, FR_CAP, (const uint8_t *)r.text, r.len);
            skl_ring_put(&s_fr, s_fr_buf, FR_CAP, (const uint8_t *)"\n", 1);
            xSemaphoreGive(s_fr_lock);
        }
        int lvl = lt_level_from_text(r.text);
        if (lvl < 0) { lvl = 2; }                                           /* unknown text counts as info */
        if (lvl <= s_level) {
            uart_out(r.text, r.len, &prev);
            uart_out("\n", 1, &prev);
        }
    }
}

/* ---- control ---------------------------------------------------------------------------------- */
void logger_start(void)
{
    s_fr_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(LOGGER_RING_RECORDS, sizeof(log_rec_t));
    skl_ring_attach(&s_fr, FR_CAP);
    xTaskCreate(logger_task, "logger", TASK_STACK, NULL, TASK_PRIO, NULL);
    esp_log_set_vprintf(log_hook);                                          /* ESP_LOGx now goes through us */
    LOGI("logger started, recorder holds %u bytes from before this boot", (unsigned)s_fr.len);
}

void logger_set_level(int level) { if (level >= 0 && level <= 3) { s_level = level; } }
int logger_get_level(void) { return s_level; }
uint32_t logger_dropped(void) { return s_dropped; }

size_t logger_recorder_read(uint8_t *out, size_t cap)
{
    size_t n = 0;
    if (xSemaphoreTake(s_fr_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        n = skl_ring_read(&s_fr, s_fr_buf, FR_CAP, out, cap);
        xSemaphoreGive(s_fr_lock);
    }
    return n;
}
