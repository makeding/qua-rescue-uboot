/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QUA_USB_CONSOLE_H
#define QUA_USB_CONSOLE_H
int qua_usb_console_init(void);
void qua_usb_console_poll(void);
int qua_usb_console_tstc(void);
int qua_usb_console_getc(void);
void qua_usb_console_putc(const char c);
int qua_usb_console_connected(void);
int qua_usb_console_opened(void);
void qua_usb_console_shutdown(void);
#endif
