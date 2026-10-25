/*
 * console_uart.h - start the text console on the USB serial port (UART0).
 *
 * console_start() registers the commands, sets UART0 to 460800 8N1, starts the logger
 * and the console task. Call it once from app_main. Step 1.4 (link) will take over the
 * receiving side of the same port; the line-by-line console then moves behind it.
 */
#ifndef CONSOLE_UART_H
#define CONSOLE_UART_H

void console_start(void);
void console_register_system(void);   /* version, uptime, status, pins (cmd_system.c) */
void console_register_log(void);      /* log level, log dump, log stats (cmd_log.c) */

#endif /* CONSOLE_UART_H */
