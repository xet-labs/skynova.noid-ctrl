/*
 * chan.c - installs the UART driver(s) for the link and console channels from the build-time macros.
 *
 * Purpose : one place that turns UART_LINK_* / UART_CONSOLE_* into real UART drivers, so the logger, the
 *           console task and the link task never hard-code a UART number again.
 * Called by: console_start() calls chan_init() before logger_start(); logger_task writes with chan_write();
 *           console_task / link_task read with chan_read().
 * Test    : the rules are tested on the PC (test_chan); this file is driver glue and is tested on the board.
 * Spec    : docs/steps/step-1.7-uart-channels.md.
 *
 * ESP-IDF calls are the same three the old console code used (uart_driver_install, uart_param_config,
 * uart_set_pin). UART0 keeps its pins (GPIO1/3, the USB bridge), so uart_set_pin is not called for it.
 */
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_err.h"

#include "app_config.h"
#include "core/chan.h"

#define FREE_RX_BUF 256            /* UART0 left to nobody: a small driver, needs more than the 128 byte FIFO */

static skc_uart_t s_link;
static skc_uart_t s_console;
static char s_desc[112];

static void install(int num, int32_t baud, int tx, int rx, int rx_buf, int tx_buf)
{
    const uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    const uart_port_t port = (uart_port_t)num;
    ESP_ERROR_CHECK(uart_driver_install(port, rx_buf, tx_buf, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &cfg));
    if (num == 0) {                                  /* the USB bridge stays on GPIO1/3 */
        ESP_ERROR_CHECK(uart_set_pin(port, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    } else {
        ESP_ERROR_CHECK(uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }
}

void chan_init(void)
{
    s_link = (skc_uart_t){ .num = UART_LINK_NUM, .baud = UART_LINK_BAUD,
                           .tx_pin = UART_LINK_TX_PIN, .rx_pin = UART_LINK_RX_PIN };
    s_console = (skc_uart_t){ .num = UART_CONSOLE_NUM, .baud = UART_CONSOLE_BAUD,
                              .tx_pin = UART_CONSOLE_TX_PIN, .rx_pin = UART_CONSOLE_RX_PIN };

    const skc_err_t err = skc_validate(&s_link, &s_console, CHAN_CONSOLE_BAUD_GIVEN != 0);
    if (err != SKC_OK) {                             /* the build checks should have stopped this */
        printf("chan: bad UART settings: %s\n", skc_err_text(err));
        abort();
    }

    install(s_link.num, s_link.baud, s_link.tx_pin, s_link.rx_pin, UART_RX_BUF, UART_TX_BUF);
    if (!skc_shared(&s_link, &s_console)) {
        install(s_console.num, s_console.baud, s_console.tx_pin, s_console.rx_pin, UART_RX_BUF, UART_TX_BUF);
    }
#if CHAN_UART0_FREE
    /* Nobody uses the USB UART: say so on purpose. ROM boot text, panic dumps and the first printf still
     * leave GPIO1 at this baud (default 115200 = the ESP-IDF console setting in sdkconfig). */
    install(0, UART0_FREE_BAUD, PIN_UART0_TX, PIN_UART0_RX, FREE_RX_BUF, 0);
#endif
    skc_describe(s_desc, sizeof s_desc, &s_link, &s_console);
}

bool chan_shared(void)
{
    return skc_shared(&s_link, &s_console);
}

int chan_read(skc_ch_t ch, uint8_t *buf, size_t cap, uint32_t wait_ms)
{
    const int num = (ch == SKC_CH_LINK) ? s_link.num : s_console.num;
    return uart_read_bytes((uart_port_t)num, buf, (uint32_t)cap, pdMS_TO_TICKS(wait_ms));
}

void chan_write(skc_ch_t ch, const void *data, size_t len)
{
    const int num = (ch == SKC_CH_LINK) ? s_link.num : s_console.num;
    uart_write_bytes((uart_port_t)num, data, len);
}

const char *chan_describe(void)
{
    return (s_desc[0] != '\0') ? s_desc : "channels not started";
}
