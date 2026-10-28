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
void console_register_system(void);   /* version, uptime, pins (cmd_system.c) */
void console_register_log(void);      /* log level, log dump, log stats (cmd_log.c) */
void console_register_i2c(void);      /* i2c scan, i2c probe (cmd_i2c.c) */
void console_register_base(void);     /* status, batt, ir (cmd_base.c) */
void console_register_devices(void);  /* devices, selftest (cmd_devices.c) */
void console_register_servo(void);    /* servo (cmd_servo.c; locked unless built with SKYNOID_SERVO_TRIAL=1) */
void console_register_maint(void);    /* maint (cmd_maint.c; unlocks the tier 1 commands) */
void console_register_buzz(void);     /* buzz <pattern> (cmd_buzz.c; tier 1) */

#endif /* CONSOLE_UART_H */
