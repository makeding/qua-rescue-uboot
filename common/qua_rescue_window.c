// SPDX-License-Identifier: GPL-2.0
#include <common.h>
#include <stdio_dev.h>
#include <iomux.h>
#include <asm/io.h>

extern int qua_usb_console_init(void);
extern void qua_usb_console_poll(void);
extern int qua_usb_console_tstc(void);
extern int qua_usb_console_getc(void);
extern void qua_usb_console_putc(char c);
extern int qua_usb_console_opened(void);

static void qua_usb_puts(const char *text)
{
	while (*text)
		qua_usb_console_putc(*text++);
}

int qua_rescue_window(void)
{
	struct stdio_dev usb;
	ulong start;
	int ready, status;

	/* GPIO19 drives host VBUS for this USB2 socket. The PC supplies it
	 * in peripheral mode; preserve every unrelated GPIO bit. */
	writel(readl((void *)0x9801b110) & ~(1U << 19), (void *)0x9801b110);
	writel(readl((void *)0x9801b100) | (1U << 19), (void *)0x9801b100);
	status = qua_usb_console_init();
	ready = status == 0;
	printf("Qua USB2 initialization: %d\n", status);
	if (ready) {
		memset(&usb, 0, sizeof(usb));
		strcpy(usb.name, "usbacm");
		usb.flags = DEV_FLAGS_INPUT | DEV_FLAGS_OUTPUT;
		usb.tstc = qua_usb_console_tstc;
		usb.getc = qua_usb_console_getc;
		usb.putc = qua_usb_console_putc;
		usb.puts = qua_usb_puts;
		if (!stdio_register(&usb)) {
			iomux_doenv(stdin, "serial,usbacm");
			iomux_doenv(stdout, "serial,usbacm");
			iomux_doenv(stderr, "serial,usbacm");
		}
	}
	printf("Qua rescue: 3 seconds; open USB console or press a key to stop.\n");
	start = get_timer(0);
	while (get_timer(start) < 3000) {
		if (ready) {
			qua_usb_console_poll();
			if (qua_usb_console_opened()) {
				puts("Qua USB rescue console. Type boot to start Linux.\n");
				return 1;
			}
		}
		if (tstc()) {
			(void)getc();
			puts("Qua rescue interrupted. Type boot to start Linux.\n");
			return 1;
		}
		udelay(1000);
	}
	puts("Qua rescue timeout; starting Linux.\n");
	return 0;
}
