/*
 * console_uart.c - the console task: USB bytes in, line editor, engine, text out.
 *
 * Purpose : let a person type commands in a terminal (picocom -b 460800 /dev/ttyUSB0).
 * Called by: app_main() calls console_start() once.
 * Test    : on the board (the logic it uses is tested on the PC: test_text, test_wire,
 *           test_console, test_logtext). See docs/steps/step-1.3b-console-engine.md.
 * Spec    : sprint-steps.md step 1.3, protocol-v0.md section 9 ("silent until spoken to").
 *
 * Every received byte goes through the wire parser first. Text bytes (outside a frame)
 * go to the line editor; valid binary frames go to link_on_frame() (step 1.4).
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
#include "core/logger.h"
#include "link/link.h"
#include "skynoid_console.h"
#include "skynoid_text.h"
#include "skynoid_wire.h"

#define CON_UART     UART_NUM_0
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
    skw_parser_t parser;
    skw_frame_t frame;
    skt_line_t line;
    console_out_t out = { .write = out_write, .ctx = NULL };
    uint8_t rx[64];

    skw_parser_init(&parser);
    skt_line_init(&line);
    put("skynoid console ready, type help\r\n");

    for (;;) {
        const int n = uart_read_bytes(CON_UART, rx, sizeof rx, pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            const skw_event_t ev = skw_parser_feed(&parser, rx[i], &frame);
            if (ev == SKW_EV_FRAME) { link_on_frame(&frame); continue; }                /* step 1.4 */
            if (ev != SKW_EV_TEXT) { continue; }                                        /* bad frame: counted by the parser */
            const char c = (char)parser.text_byte;
            switch (skt_line_feed(&line, parser.text_byte)) {
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
        link_tick();                         /* 500 ms without PING ends the session (runs every <= 20 ms) */
    }
}

void console_start(void)
{
    console_register_system();
    console_register_log();
    link_init();

    const uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CON_UART, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CON_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(CON_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    logger_start();                         /* from here on, every UART write goes through the logger */
    xTaskCreate(console_task, "console", TASK_STACK, NULL, TASK_PRIO, NULL);
}
