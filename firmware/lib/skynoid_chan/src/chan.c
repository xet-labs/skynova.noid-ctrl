/*
 * chan.c - pure channel rules: validate a link/console combination, route a record, describe it.
 * See skynoid_chan.h. No ESP-IDF, no globals, no allocation.
 */
#include <stdio.h>

#include "skynoid_chan.h"

bool skc_shared(const skc_uart_t *link, const skc_uart_t *console)
{
    return link != NULL && console != NULL && link->num == console->num;
}

static skc_err_t check_pin(int pin, bool is_tx)
{
    if (pin < 0) { return SKC_ERR_PIN_MISSING; }
    if (!SKC_PIN_EXISTS(pin)) { return SKC_ERR_PIN_RANGE; }
    if (SKC_PIN_FORBIDDEN(pin)) { return SKC_ERR_PIN_FORBIDDEN; }
    if (SKC_PIN_IS_USB(pin)) { return SKC_ERR_PIN_USB; }
    if (is_tx && !SKC_PIN_CAN_OUTPUT(pin)) { return SKC_ERR_TX_INPUT_ONLY; }
    return SKC_OK;
}

/* One channel on its own: number, baud, pins. */
static skc_err_t check_uart(const skc_uart_t *u)
{
    if (!SKC_NUM_OK(u->num)) { return SKC_ERR_NUM; }
    if (!SKC_BAUD_OK(u->baud)) { return SKC_ERR_BAUD; }
    if (u->num == 0) {
        return (u->tx_pin == SKC_UART0_TX_PIN && u->rx_pin == SKC_UART0_RX_PIN) ? SKC_OK : SKC_ERR_UART0_PINS;
    }
    skc_err_t e = check_pin(u->tx_pin, true);
    if (e != SKC_OK) { return e; }
    e = check_pin(u->rx_pin, false);
    if (e != SKC_OK) { return e; }
    return (u->tx_pin == u->rx_pin) ? SKC_ERR_PIN_SAME : SKC_OK;
}

skc_err_t skc_validate(const skc_uart_t *link, const skc_uart_t *console, bool console_baud_given)
{
    if (link == NULL || console == NULL) { return SKC_ERR_NUM; }
    skc_err_t e = check_uart(link);
    if (e != SKC_OK) { return e; }

    if (skc_shared(link, console)) {
        if (console_baud_given && console->baud != link->baud) { return SKC_ERR_SHARED_BAUD; }
        if (console->tx_pin != link->tx_pin || console->rx_pin != link->rx_pin) { return SKC_ERR_SHARED_PINS; }
        return SKC_OK;
    }

    e = check_uart(console);
    if (e != SKC_OK) { return e; }
    if (link->num == 0 || console->num == 0) { return SKC_OK; }       /* UART0 pins are fixed and distinct */
    if (link->tx_pin == console->tx_pin || link->tx_pin == console->rx_pin ||
        link->rx_pin == console->tx_pin || link->rx_pin == console->rx_pin) {
        return SKC_ERR_PIN_SHARED;
    }
    return SKC_OK;
}

const char *skc_err_text(skc_err_t err)
{
    switch (err) {
    case SKC_OK:               return "ok";
    case SKC_ERR_NUM:          return "UART number must be 0, 1 or 2";
    case SKC_ERR_BAUD:         return "baud must be 1200..2000000";
    case SKC_ERR_UART0_PINS:   return "UART0 is the USB bridge and must keep GPIO1 (TX) and GPIO3 (RX)";
    case SKC_ERR_PIN_MISSING:  return "UART1 and UART2 need both pins given (TX and RX)";
    case SKC_ERR_PIN_RANGE:    return "pin is not an existing GPIO on this board";
    case SKC_ERR_PIN_FORBIDDEN: return "pin is a boot-strap or flash pin (0, 12, 6..11)";
    case SKC_ERR_PIN_USB:      return "GPIO1 and GPIO3 belong to the USB bridge (UART0)";
    case SKC_ERR_TX_INPUT_ONLY: return "TX pin must be an output-capable GPIO (not 34..39)";
    case SKC_ERR_PIN_SAME:     return "TX and RX must be different pins";
    case SKC_ERR_PIN_SHARED:   return "link and console must not share a pin";
    case SKC_ERR_SHARED_BAUD:  return "link and console share one UART, so UART_CONSOLE_BAUD must equal UART_LINK_BAUD or be left out";
    case SKC_ERR_SHARED_PINS:  return "link and console share one UART, so they must use the same pins";
    default:                   return "unknown channel error";
    }
}

skc_ch_t skc_channel_for(bool shared, int kind)
{
    if (shared) { return SKC_CH_LINK; }
    return (kind == SKC_REC_BIN) ? SKC_CH_LINK : SKC_CH_CONSOLE;
}

size_t skc_describe(char *out, size_t cap, const skc_uart_t *link, const skc_uart_t *console)
{
    if (out == NULL || cap == 0) { return 0; }
    out[0] = '\0';
    if (link == NULL || console == NULL) { return 0; }
    int n;
    if (skc_shared(link, console)) {
        n = snprintf(out, cap, "link uart%d@%ld tx%d rx%d; console shared", link->num, (long)link->baud,
                     link->tx_pin, link->rx_pin);
    } else {
        n = snprintf(out, cap, "link uart%d@%ld tx%d rx%d; console uart%d@%ld tx%d rx%d", link->num,
                     (long)link->baud, link->tx_pin, link->rx_pin, console->num, (long)console->baud,
                     console->tx_pin, console->rx_pin);
    }
    if (n < 0) { out[0] = '\0'; return 0; }
    return ((size_t)n < cap) ? (size_t)n : cap - 1;
}
