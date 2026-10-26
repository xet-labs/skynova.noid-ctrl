/*
 * chan_cfg.h - build-time half of the channel layer (step 1.7): pin defaults and the refusals.
 *
 * What it is : app_config.h defines the UART_LINK_* / UART_CONSOLE_* / UART0_FREE_BAUD macros with #ifndef
 *              defaults and then includes this file. Here the PIN defaults are filled in (they need
 *              board_pins.h) and every bad combination stops the build with a readable #error.
 * Why #if    : an error at build time is better than a silent wrong port on the bench. The number rules
 *              are the macros of lib/skynoid_chan (SKC_*), so the PC test (test_chan) and this file agree;
 *              chan.c also runs skc_validate at boot as a second net.
 * Not here   : which UART a record leaves on (lib: skc_channel_for) and the driver code (core/chan.c).
 * Spec       : docs/steps/step-1.7-uart-channels.md.
 *
 * Do not include this file directly: include app_config.h.
 */
#ifndef CHAN_CFG_H
#define CHAN_CFG_H

#include "board/board_pins.h"
#include "skynoid_chan.h"

/* ---- derived switches (usable in #if and in C) -------------------------------------------------- */
#define CHAN_SHARED       (UART_LINK_NUM == UART_CONSOLE_NUM)     /* one port carries frames and text */
#define CHAN_UART0_FREE   (UART_LINK_NUM != 0 && UART_CONSOLE_NUM != 0)   /* nobody uses the USB UART */
#ifdef UART_CONSOLE_BAUD_IS_DEFAULT
#define CHAN_CONSOLE_BAUD_GIVEN 0        /* app_config.h used its default */
#else
#define CHAN_CONSOLE_BAUD_GIVEN 1        /* somebody passed -DUART_CONSOLE_BAUD=... */
#endif

/* ---- numbers: UART numbers and bauds ------------------------------------------------------------ */
#if !SKC_NUM_OK(UART_LINK_NUM)
#error "UART_LINK_NUM must be 0, 1 or 2 (the ESP32 has three UARTs)"
#endif
#if !SKC_NUM_OK(UART_CONSOLE_NUM)
#error "UART_CONSOLE_NUM must be 0, 1 or 2 (the ESP32 has three UARTs)"
#endif
#if !SKC_BAUD_OK(UART_LINK_BAUD)
#error "UART_LINK_BAUD must be between 1200 and 2000000"
#endif
#if !SKC_BAUD_OK(UART0_FREE_BAUD)
#error "UART0_FREE_BAUD must be between 1200 and 2000000"
#endif
#if CHAN_SHARED
#if CHAN_CONSOLE_BAUD_GIVEN && (UART_CONSOLE_BAUD != UART_LINK_BAUD)
#error "Link and console share one UART, so UART_CONSOLE_BAUD must equal UART_LINK_BAUD or be left out. Give the console its own port with -DUART_CONSOLE_NUM=<other UART>"
#endif
#else
#if !SKC_BAUD_OK(UART_CONSOLE_BAUD)
#error "UART_CONSOLE_BAUD must be between 1200 and 2000000"
#endif
#endif

/* ---- pin defaults: the pair of the chosen UART ---------------------------------------------------
 * UART0 = the USB bridge (GPIO1/3), UART2 = GPIO5/15 (board_pins.h). UART1 has NO default pair on
 * purpose (no free GPIO in role single): -1 here, and the checks below ask for explicit pins. */
#define CHAN_DEF_TX(n) ((n) == 0 ? PIN_UART0_TX : ((n) == 2 ? PIN_UART2_TX : -1))
#define CHAN_DEF_RX(n) ((n) == 0 ? PIN_UART0_RX : ((n) == 2 ? PIN_UART2_RX : -1))

#ifndef UART_LINK_TX_PIN
#define UART_LINK_TX_PIN CHAN_DEF_TX(UART_LINK_NUM)
#endif
#ifndef UART_LINK_RX_PIN
#define UART_LINK_RX_PIN CHAN_DEF_RX(UART_LINK_NUM)
#endif
#ifndef UART_CONSOLE_TX_PIN
#if CHAN_SHARED
#define UART_CONSOLE_TX_PIN UART_LINK_TX_PIN        /* same port, same wires */
#else
#define UART_CONSOLE_TX_PIN CHAN_DEF_TX(UART_CONSOLE_NUM)
#endif
#endif
#ifndef UART_CONSOLE_RX_PIN
#if CHAN_SHARED
#define UART_CONSOLE_RX_PIN UART_LINK_RX_PIN
#else
#define UART_CONSOLE_RX_PIN CHAN_DEF_RX(UART_CONSOLE_NUM)
#endif
#endif

/* ---- pins: one error code per pin (0 = fine), same order of tests as skc_validate ---------------- */
#define CHAN_ROLE_MASK (0ULL SK_ROLE_PINS(SK_OR))      /* every GPIO that belongs to a robot function */
#define CHAN_PIN_ERR(p, tx) \
    ((p) < 0 ? 1 : \
     !SKC_PIN_EXISTS(p) ? 2 : \
     SKC_PIN_FORBIDDEN(p) ? 3 : \
     SKC_PIN_IS_USB(p) ? 4 : \
     ((tx) && !SKC_PIN_CAN_OUTPUT(p)) ? 5 : \
     (((CHAN_ROLE_MASK) & SK_PINBIT((p) & 63)) != 0) ? 6 : 0)

/* UART0 must keep its fixed pair; UART1 and UART2 are checked pin by pin. */
#define CHAN_E_LTX (UART_LINK_NUM == 0 ? 0 : CHAN_PIN_ERR(UART_LINK_TX_PIN, 1))
#define CHAN_E_LRX (UART_LINK_NUM == 0 ? 0 : CHAN_PIN_ERR(UART_LINK_RX_PIN, 0))
#define CHAN_E_CTX ((CHAN_SHARED || UART_CONSOLE_NUM == 0) ? 0 : CHAN_PIN_ERR(UART_CONSOLE_TX_PIN, 1))
#define CHAN_E_CRX ((CHAN_SHARED || UART_CONSOLE_NUM == 0) ? 0 : CHAN_PIN_ERR(UART_CONSOLE_RX_PIN, 0))

#if UART_LINK_NUM == 0
#if (UART_LINK_TX_PIN != SKC_UART0_TX_PIN) || (UART_LINK_RX_PIN != SKC_UART0_RX_PIN)
#error "UART_LINK_NUM=0 is the USB bridge and must keep GPIO1 (TX) and GPIO3 (RX): do not set UART_LINK_TX_PIN or UART_LINK_RX_PIN"
#endif
#endif

#if CHAN_E_LTX == 1
#error "UART_LINK_TX_PIN is not given: UART1 has no default pins, set -DUART_LINK_TX_PIN=<gpio> -DUART_LINK_RX_PIN=<gpio> (see docs/steps/step-1.7-uart-channels.md)"
#elif CHAN_E_LTX == 2
#error "UART_LINK_TX_PIN is not an existing GPIO of this board"
#elif CHAN_E_LTX == 3
#error "UART_LINK_TX_PIN is a boot-strap or flash pin (0, 12, 6..11)"
#elif CHAN_E_LTX == 4
#error "UART_LINK_TX_PIN is GPIO1 or GPIO3, which belong to the USB bridge (UART0)"
#elif CHAN_E_LTX == 5
#error "UART_LINK_TX_PIN must be an output-capable GPIO (GPIO34..39 are input only)"
#elif CHAN_E_LTX == 6
#error "UART_LINK_TX_PIN is already used by a robot function in board_pins.h (motor, IR, buzzer, touch, I2C ...)"
#endif
#if CHAN_E_LRX == 1
#error "UART_LINK_RX_PIN is not given: UART1 has no default pins, set -DUART_LINK_TX_PIN=<gpio> -DUART_LINK_RX_PIN=<gpio> (see docs/steps/step-1.7-uart-channels.md)"
#elif CHAN_E_LRX == 2
#error "UART_LINK_RX_PIN is not an existing GPIO of this board"
#elif CHAN_E_LRX == 3
#error "UART_LINK_RX_PIN is a boot-strap or flash pin (0, 12, 6..11)"
#elif CHAN_E_LRX == 4
#error "UART_LINK_RX_PIN is GPIO1 or GPIO3, which belong to the USB bridge (UART0)"
#elif CHAN_E_LRX == 6
#error "UART_LINK_RX_PIN is already used by a robot function in board_pins.h (motor, IR, buzzer, touch, I2C ...)"
#endif
#if UART_LINK_NUM != 0 && UART_LINK_TX_PIN == UART_LINK_RX_PIN
#error "UART_LINK_TX_PIN and UART_LINK_RX_PIN must be different pins"
#endif

#if CHAN_SHARED
#if (UART_CONSOLE_TX_PIN != UART_LINK_TX_PIN) || (UART_CONSOLE_RX_PIN != UART_LINK_RX_PIN)
#error "Link and console share one UART, so they use the same pins: do not set UART_CONSOLE_TX_PIN or UART_CONSOLE_RX_PIN (or set the same values)"
#endif
#else
#if UART_CONSOLE_NUM == 0
#if (UART_CONSOLE_TX_PIN != SKC_UART0_TX_PIN) || (UART_CONSOLE_RX_PIN != SKC_UART0_RX_PIN)
#error "UART_CONSOLE_NUM=0 is the USB bridge and must keep GPIO1 (TX) and GPIO3 (RX): do not set UART_CONSOLE_TX_PIN or UART_CONSOLE_RX_PIN"
#endif
#endif
#if CHAN_E_CTX == 1
#error "UART_CONSOLE_TX_PIN is not given: UART1 has no default pins, set -DUART_CONSOLE_TX_PIN=<gpio> -DUART_CONSOLE_RX_PIN=<gpio> (see docs/steps/step-1.7-uart-channels.md)"
#elif CHAN_E_CTX == 2
#error "UART_CONSOLE_TX_PIN is not an existing GPIO of this board"
#elif CHAN_E_CTX == 3
#error "UART_CONSOLE_TX_PIN is a boot-strap or flash pin (0, 12, 6..11)"
#elif CHAN_E_CTX == 4
#error "UART_CONSOLE_TX_PIN is GPIO1 or GPIO3, which belong to the USB bridge (UART0)"
#elif CHAN_E_CTX == 5
#error "UART_CONSOLE_TX_PIN must be an output-capable GPIO (GPIO34..39 are input only)"
#elif CHAN_E_CTX == 6
#error "UART_CONSOLE_TX_PIN is already used by a robot function in board_pins.h (motor, IR, buzzer, touch, I2C ...)"
#endif
#if CHAN_E_CRX == 1
#error "UART_CONSOLE_RX_PIN is not given: UART1 has no default pins, set -DUART_CONSOLE_TX_PIN=<gpio> -DUART_CONSOLE_RX_PIN=<gpio> (see docs/steps/step-1.7-uart-channels.md)"
#elif CHAN_E_CRX == 2
#error "UART_CONSOLE_RX_PIN is not an existing GPIO of this board"
#elif CHAN_E_CRX == 3
#error "UART_CONSOLE_RX_PIN is a boot-strap or flash pin (0, 12, 6..11)"
#elif CHAN_E_CRX == 4
#error "UART_CONSOLE_RX_PIN is GPIO1 or GPIO3, which belong to the USB bridge (UART0)"
#elif CHAN_E_CRX == 6
#error "UART_CONSOLE_RX_PIN is already used by a robot function in board_pins.h (motor, IR, buzzer, touch, I2C ...)"
#endif
#if UART_CONSOLE_NUM != 0 && UART_CONSOLE_TX_PIN == UART_CONSOLE_RX_PIN
#error "UART_CONSOLE_TX_PIN and UART_CONSOLE_RX_PIN must be different pins"
#endif
/* link and console on two non-USB UARTs must not share a wire */
#if UART_LINK_NUM != 0 && UART_CONSOLE_NUM != 0
#if (UART_LINK_TX_PIN == UART_CONSOLE_TX_PIN) || (UART_LINK_TX_PIN == UART_CONSOLE_RX_PIN) || \
    (UART_LINK_RX_PIN == UART_CONSOLE_TX_PIN) || (UART_LINK_RX_PIN == UART_CONSOLE_RX_PIN)
#error "Link and console must not share a pin"
#endif
#endif
#endif /* CHAN_SHARED */

#endif /* CHAN_CFG_H */
