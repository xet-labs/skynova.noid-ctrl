/*
 * console_uart.c - the console task: USB bytes in, line editor, engine, text out.
 *
 * Purpose : let a person type commands in a terminal (picocom -b 460800 /dev/ttyUSB0).
 * Called by: app_main() calls console_start() once.
 * Test    : on the board (the logic it uses is tested on the PC: test_text, test_wire,
 *           test_console, test_logtext). See docs/steps/step-1.3b-console-engine.md.
 * Spec    : sprint-steps.md step 1.3, protocol-v0.md section 9 ("silent until spoken to").
 *
 * Shared mode (default, link and console on one UART): every received byte goes through the wire parser first.
 * Text bytes (outside a frame) go to the line editor; valid binary frames go to link_on_frame() (step 1.4).
 * Separate mode (-DUART_CONSOLE_NUM=<other UART>, step 1.7): this task reads only the console UART, every byte
 * is text; the link UART is read by the link task (link.c, link_start_rx).
 * This task only READS the UART. Everything it prints goes through the logger task
 * (logger_write_raw), the single writer, so lines never interleave mid-word.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_err.h"

#include "app_config.h"
#include "console/console_uart.h"
#include "core/chan.h"
#include "core/logger.h"
#include "link/link.h"
#include "skynoid_console.h"
#include "skynoid_text.h"
#include "skynoid_wire.h"

#define PROMPT       "skynoid> "
#define TASK_STACK   4096
#define TASK_PRIO    3

static void out_write(const char *text, size_t len, void *ctx)
{
    (void)ctx;
    logger_write_raw(text, len);
}

static void put(const char *s)
{
    logger_write_raw(s, strlen(s));
}

static void console_task(void *arg)
{
    (void)arg;
    skt_line_t line;
    console_out_t out = { .write = out_write, .ctx = NULL };
    uint8_t rx[64];
#if CHAN_SHARED
    skw_parser_t parser;
    skw_frame_t frame;
    skw_parser_init(&parser);
#endif
    skt_line_init(&line);
    put("skynoid console ready, type help\r\n");

    for (;;) {
#if CHAN_SHARED
        /* one port: frames and text arrive mixed; the wire parser splits them (step 1.4) */
        const int n = chan_read(SKC_CH_LINK, rx, sizeof rx, 20);
#else
        /* own console port: text only, no frame parsing; the link task reads the link UART */
        const int n = chan_read(SKC_CH_CONSOLE, rx, sizeof rx, 20);
#endif
        for (int i = 0; i < n; i++) {
#if CHAN_SHARED
            const skw_event_t ev = skw_parser_feed(&parser, rx[i], &frame);
            if (ev == SKW_EV_FRAME) { link_on_frame(&frame); continue; }                /* step 1.4 */
            if (ev != SKW_EV_TEXT) { continue; }                                        /* bad frame: counted by the parser */
            const uint8_t byte = (uint8_t)parser.text_byte;
#else
            const uint8_t byte = rx[i];
#endif
            const char c = (char)byte;
            switch (skt_line_feed(&line, byte)) {
            case SKT_EV_ECHO:     logger_write_raw(&c, 1); break;
            case SKT_EV_ERASE:    put("\b \b"); break;
            case SKT_EV_LINE:
                put("\r\n");
                console_execute(line.buf, &out);
                put(PROMPT);
                break;
            case SKT_EV_TOO_LONG:
                put("\r\nERR invalid_arg line too long\r\n" PROMPT);
                break;
            default: break;
            }
        }
#if CHAN_SHARED
        link_tick();                         /* 500 ms without PING ends the session (runs every <= 20 ms) */
#endif
    }
}

void console_start(void)
{
    console_register_system();
    console_register_log();
    console_register_i2c();
    console_register_base();
    console_register_devices();
    link_init();
    chan_init();                            /* UART drivers from UART_LINK_* / UART_CONSOLE_* (core/chan.c) */
    logger_start();                         /* from here on, every UART write goes through the logger */
    link_start_rx();                        /* own link task only when the console has its own port */
    xTaskCreate(console_task, "console", TASK_STACK, NULL, TASK_PRIO, NULL);
}
