/* SPDX-License-Identifier: GPL-2.0 */
/* Polled PIO CDC-ACM console for Qua's dedicated RTD1295 USB2 UDC. */
#include <common.h>
#include <asm/io.h>
#include <errno.h>
#include "qua_usb_console.h"

/* RAM boot owns USB before any host/gadget is running; DDR is untouched. */
#define QUA_BIT(n) (1U << (n))
#define UDC 0x981e0000UL
#define PHY 0x98013800UL
#define GAHBCFG 0x008
#define GUSBCFG 0x00c
#define GRSTCTL 0x010
#define GINTSTS 0x014
#define GINTMSK 0x018
#define GRXSTSP 0x020
#define GRXFSIZ 0x024
#define GNPTXFSIZ 0x028
#define GSNPSID 0x040
#define DCFG 0x800
#define DCTL 0x804
#define DSTS 0x808
#define DIEPMSK 0x810
#define DOEPMSK 0x814
#define DAINTMSK 0x81c
#define DIEPCTL(n) (0x900 + (n) * 0x20)
#define DIEPINT(n) (0x908 + (n) * 0x20)
#define DIEPTSIZ(n) (0x910 + (n) * 0x20)
#define DOEPCTL(n) (0xb00 + (n) * 0x20)
#define DOEPINT(n) (0xb08 + (n) * 0x20)
#define DOEPTSIZ(n) (0xb10 + (n) * 0x20)
#define FIFO(n) (0x1000 + (n) * 0x1000)

#define GAHBCFG_GLBLINT QUA_BIT(0)
#define GRSTCTL_CSFTRST QUA_BIT(0)
#define GRSTCTL_RXFFLSH QUA_BIT(4)
#define GRSTCTL_TXFFLSH QUA_BIT(5)
#define GRSTCTL_TXFNUM_ALL (0x10 << 6)
#define GRSTCTL_AHBIDLE QUA_BIT(31)
#define GUSBCFG_PHYIF16 QUA_BIT(3)
#define GUSBCFG_TRDT_5 (5 << 10)
#define GUSBCFG_FORCEDEV QUA_BIT(30)
#define GUSBCFG_FORCEHOST QUA_BIT(29)
#define GINT_RXFLVL QUA_BIT(4)
#define GINT_USBRST QUA_BIT(12)
#define GINT_ENUMDONE QUA_BIT(13)
#define GINT_IEPINT QUA_BIT(18)
#define GINT_OEPINT QUA_BIT(19)
#define DCTL_SFTDISCON QUA_BIT(1)
#define DCTL_CGNPINNAK QUA_BIT(8)
#define DCTL_CGOUTNAK QUA_BIT(10)
#define DCFG_ADDR_MASK (0x7f << 4)
#define DEPCTL_EPENA QUA_BIT(31)
#define DEPCTL_CNAK QUA_BIT(26)
#define DEPCTL_TYPE_BULK (2 << 18)
#define DEPCTL_TYPE_INT (3 << 18)
#define DEPCTL_USBACTEP QUA_BIT(15)
#define DEPCTL_MPS_MASK 0x7ff
#define DIEPTSIZ_PKTCNT(n) ((n) << 19)
#define DOEPTSIZ_SUPCNT(n) ((n) << 29)
#define EP0 0
#define BULK_EP 1
#define NOTIFY_EP 2
#define RING_SIZE 4096

static volatile void *udc;
static u8 rx_ring[RING_SIZE], tx_ring[RING_SIZE];
static unsigned int rx_head, rx_count, tx_head, tx_count, bulk_mps = 512;
static bool configured, dtr, line_coding_out;
static u32 initial_id, reset_events, enum_events, setup_events;
enum ep0_phase { EP0_SETUP, EP0_IN_DATA, EP0_OUT_STATUS, EP0_OUT_DATA, EP0_IN_STATUS };
static enum ep0_phase ep0_phase;
static const u8 *ep0_in_data;
static unsigned int ep0_in_left;
static bool ep0_in_zlp;

static const u8 device_desc[] = {
	18, 1, 0, 2, 2, 0, 0, 64, 0x25, 0x05, 0xa7, 0xa4, 0, 1, 1, 2, 3, 1,
};
/* Standard 67-byte ACM configuration: notification EP2, bulk IN/OUT EP1. */
static u8 config_desc[] = {
	9,2,67,0,2,1,0,0xc0,1, 9,4,0,0,1,2,2,1,0,
	5,0x24,0,0x10,1, 5,0x24,1,0,1, 4,0x24,2,2, 5,0x24,6,0,1,
	7,5,0x82,3,8,0,16, 9,4,1,0,2,10,0,0,0,
	7,5,0x01,2,0,2,0, 7,5,0x81,2,0,2,0,
};
static const u8 language_desc[] = { 4,3,9,4 };
static const u8 product_desc[] = { 18,3,'Q',0,'u',0,'a',0,' ',0,'S',0,'t',0,'a',0,'t',0 };
static const u8 serial_desc[] = { 24,3,'U',0,'S',0,'B',0,' ',0,'C',0,'o',0,'n',0,'s',0,'o',0,'l',0,'e',0 };
static const u8 qualifier_desc[] = { 10,6,0,2,2,0,0,64,1,0 };
static u8 line_coding[] = { 0,0xc2,1,0,0,0,8 };

static const u8 identity_desc[] = {
 20,3,'Q',0,'u',0,'a',0,'R',0,'e',0,'s',0,'c',0,'u',0,'e',0
};

static u32 rd(u32 off) { return readl((void *)(udc + off)); }
static void wr(u32 off, u32 value) { writel(value, (void *)(udc + off)); }

static void flush_fifos(void)
{
	unsigned int n = 100000;
	wr(GRSTCTL, GRSTCTL_TXFNUM_ALL | GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH);
	while ((rd(GRSTCTL) & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH)) && --n)
		;
}

static void arm_ep0_setup(void)
{
	ep0_phase = EP0_SETUP;
	wr(DOEPTSIZ(EP0), 8 | DIEPTSIZ_PKTCNT(1) | DOEPTSIZ_SUPCNT(1));
	wr(DOEPCTL(EP0), DEPCTL_USBACTEP | DEPCTL_EPENA | DEPCTL_CNAK);
}

static void arm_ep0_data(unsigned int bytes)
{
	wr(DOEPTSIZ(EP0), bytes | DIEPTSIZ_PKTCNT(1));
	wr(DOEPCTL(EP0), rd(DOEPCTL(EP0)) | DEPCTL_EPENA | DEPCTL_CNAK);
}

static void arm_bulk_out(void)
{
	wr(DOEPTSIZ(BULK_EP), bulk_mps | DIEPTSIZ_PKTCNT(1));
	wr(DOEPCTL(BULK_EP), rd(DOEPCTL(BULK_EP)) | DEPCTL_EPENA | DEPCTL_CNAK);
}

static bool in_busy(unsigned int ep)
{
	return rd(DIEPCTL(ep)) & DEPCTL_EPENA;
}

/* PIO: queue the endpoint before filling its FIFO, as the vendor driver does. */
static void start_in(unsigned int ep, const u8 *data, unsigned int bytes)
{
	unsigned int words = (bytes + 3) / 4;
	unsigned int mps = ep == EP0 ? 64 : bulk_mps;
	unsigned int packets = bytes ? (bytes + mps - 1) / mps : 1;
	unsigned int i;

	wr(DIEPTSIZ(ep), bytes | DIEPTSIZ_PKTCNT(packets));
	wr(DIEPCTL(ep), rd(DIEPCTL(ep)) | DEPCTL_EPENA | DEPCTL_CNAK);
	for (i = 0; i < words; ++i) {
		u32 word = 0;
		unsigned int j;
		for (j = 0; j < 4 && i * 4 + j < bytes; ++j)
			word |= (u32)data[i * 4 + j] << (8 * j);
		wr(FIFO(ep), word);
	}
}

static void ep0_status(void)
{
	ep0_phase = EP0_IN_STATUS;
	start_in(EP0, NULL, 0);
}

static void ep0_next_in(void)
{
	unsigned int bytes = ep0_in_left > 64 ? 64 : ep0_in_left;
	if (!bytes)
		ep0_in_zlp = false;
	start_in(EP0, ep0_in_data, bytes);
	ep0_in_data += bytes;
	ep0_in_left -= bytes;
}

static void ep0_reply(const u8 *data, unsigned int bytes, unsigned int requested)
{
	ep0_in_data = data;
	ep0_in_left = bytes;
	ep0_in_zlp = bytes < requested && !(bytes % 64);
	ep0_phase = EP0_IN_DATA;
	ep0_next_in();
}

static void ep0_in_complete(void)
{
	if (ep0_phase == EP0_IN_STATUS) {
		arm_ep0_setup();
	} else if (ep0_phase == EP0_IN_DATA) {
		if (ep0_in_left || ep0_in_zlp) {
			ep0_next_in();
		} else {
			ep0_phase = EP0_OUT_STATUS;
			arm_ep0_data(64);
		}
	}
}

static void configure_bulk_mps(void)
{
	/* DSTS.ENUMSPD == 0 means high-speed. */
	bulk_mps = (rd(DSTS) & 0x6) ? 64 : 512;
	config_desc[57] = bulk_mps & 0xff;
	config_desc[58] = bulk_mps >> 8;
	config_desc[64] = bulk_mps & 0xff;
	config_desc[65] = bulk_mps >> 8;
}

static void configure_data_eps(void)
{
	wr(DIEPCTL(BULK_EP), QUA_BIT(28) | DEPCTL_USBACTEP | DEPCTL_TYPE_BULK |
	   (bulk_mps & DEPCTL_MPS_MASK));
	wr(DOEPCTL(BULK_EP), QUA_BIT(28) | DEPCTL_USBACTEP | DEPCTL_TYPE_BULK |
	   (bulk_mps & DEPCTL_MPS_MASK));
	wr(DIEPCTL(NOTIFY_EP), DEPCTL_USBACTEP | DEPCTL_TYPE_INT | 8);
	arm_bulk_out();
}

static void setup_request(const u8 *s)
{
	u16 value = s[2] | ((u16)s[3] << 8);
	u16 length = s[6] | ((u16)s[7] << 8);
	const u8 *data = NULL;
	unsigned int bytes = 0;
	static u8 config;
	static const u8 zero[] = { 0, 0 };

	++setup_events;
	line_coding_out = false;
	wr(DIEPCTL(0), rd(DIEPCTL(0)) & ~QUA_BIT(21));
	wr(DOEPCTL(0), rd(DOEPCTL(0)) & ~QUA_BIT(21));
	if ((s[0] & 0x60) == 0) {
		switch (s[1]) {
		case 0: data = zero; bytes = sizeof(zero); break; /* GET_STATUS */
		case 5: /* SET_ADDRESS, applied before status as DWC2 permits. */
			wr(DCFG, (rd(DCFG) & ~DCFG_ADDR_MASK) | (value << 4));
			ep0_status(); return;
		case 6: /* GET_DESCRIPTOR */
			switch (value >> 8) {
			case 1: data = device_desc; bytes = sizeof(device_desc); break;
			case 2: data = config_desc; bytes = sizeof(config_desc); break;
			case 3:
				if ((value & 0xff) == 0) { data = language_desc; bytes = sizeof(language_desc); }
				if ((value & 0xff) == 1) { data = product_desc; bytes = sizeof(product_desc); }
				if ((value & 0xff) == 2) { data = serial_desc; bytes = sizeof(serial_desc); }
				if ((value & 0xff) == 3) { data = identity_desc; bytes = sizeof(identity_desc); }
				break;
			case 6: data = qualifier_desc; bytes = sizeof(qualifier_desc); break;
			}
			break;
		case 8: config = configured ? 1 : 0; data = &config; bytes = 1; break;
		case 9:
			configured = value == 1;
			dtr = false;
			if (configured)
				configure_data_eps();
			ep0_status(); return;
		}
	}
	if ((s[0] & 0x7f) == 0x21) {
		switch (s[1]) {
		case 0x20: line_coding_out = true; ep0_phase = EP0_OUT_DATA; arm_ep0_data(7); return;
		case 0x21: data = line_coding; bytes = sizeof(line_coding); break;
		case 0x22: dtr = configured && !!(value & 1); ep0_status(); return;
		}
	}
	if (!data) {
		if (!length) ep0_status();
		else { wr(DIEPCTL(0), rd(DIEPCTL(0)) | QUA_BIT(21)); arm_ep0_setup(); }
		return;
	}
	if (bytes > length)
		bytes = length;
	ep0_reply(data, bytes, length);
}

static void discard_fifo(unsigned int ep, unsigned int bytes)
{
	while (bytes) { (void)rd(FIFO(0)); bytes = bytes > 4 ? bytes - 4 : 0; }
}

static void receive_bulk(unsigned int bytes)
{
	unsigned int i;
	for (i = 0; i < (bytes + 3) / 4; ++i) {
		u32 word = rd(FIFO(0));
		unsigned int j;
		for (j = 0; j < 4 && i * 4 + j < bytes; ++j)
			if (rx_count < RING_SIZE) {
				rx_ring[(rx_head + rx_count) % RING_SIZE] = word >> (8 * j);
				++rx_count;
			}
	}
}

static void service_tx(void)
{
	u8 packet[512];
	unsigned int bytes = 0;
	if (!configured || !dtr || !tx_count || in_busy(BULK_EP))
		return;
	while (tx_count && bytes < bulk_mps) {
		packet[bytes++] = tx_ring[tx_head];
		tx_head = (tx_head + 1) % RING_SIZE;
		--tx_count;
	}
	start_in(BULK_EP, packet, bytes);
}

void qua_usb_console_poll(void)
{
	u32 status;
	if (!udc)
		return;
	status = rd(GINTSTS);
	if (status & GINT_USBRST) {
		++reset_events;
		wr(GINTSTS, GINT_USBRST);
		configured = dtr = line_coding_out = false;
		rx_head = rx_count = tx_head = tx_count = 0;
		wr(DCFG, 0);
		wr(DIEPINT(0), 0xffffffff); wr(DOEPINT(0), 0xffffffff);
		wr(DAINTMSK, 0x00030007);
		wr(DIEPMSK, 0x0f);
		wr(DOEPMSK, 0x0e);
		wr(DIEPCTL(EP0), DEPCTL_USBACTEP);
		arm_ep0_setup();
	}
	if (status & GINT_ENUMDONE) { ++enum_events; wr(GINTSTS, GINT_ENUMDONE); configure_bulk_mps(); arm_ep0_setup(); }
	if (status & GINT_IEPINT) {
		u32 pending = rd(DIEPINT(0));
		wr(DIEPINT(0), pending);
		if (pending & QUA_BIT(0)) ep0_in_complete();
		wr(DIEPINT(1), rd(DIEPINT(1)));
		wr(DIEPINT(2), rd(DIEPINT(2)));
	}
	if (status & GINT_OEPINT) {
		wr(DOEPINT(0), rd(DOEPINT(0)));
		wr(DOEPINT(1), rd(DOEPINT(1)));
	}
	while (rd(GINTSTS) & GINT_RXFLVL) {
		u32 p = rd(GRXSTSP);
		unsigned int ep = p & 0xf, bytes = (p >> 4) & 0x7ff, state = (p >> 17) & 0xf;
		if (state == 6 && ep == EP0) {
			u8 setup[8]; u32 a = rd(FIFO(EP0)), b = rd(FIFO(EP0));
			memcpy(setup, &a, 4); memcpy(setup + 4, &b, 4); setup_request(setup);
		} else if (state == 2 && ep == BULK_EP) {
			receive_bulk(bytes); arm_bulk_out();
		} else if (state == 2 && ep == EP0 && line_coding_out) {
			unsigned int i;
			for (i = 0; i < (bytes + 3) / 4; ++i) {
				u32 word = rd(FIFO(0)); unsigned int j;
				for (j = 0; j < 4 && i * 4 + j < bytes; ++j)
					if (i * 4 + j < sizeof(line_coding)) line_coding[i * 4 + j] = word >> (8 * j);
			}
			line_coding_out = false; ep0_status();
		} else if (state == 2 && ep == EP0 && ep0_phase == EP0_OUT_STATUS) {
			discard_fifo(ep, bytes); arm_ep0_setup();
		} else {
			discard_fifo(ep, bytes);
		}
	}
	service_tx();
}

int qua_usb_console_init(void)
{
	u32 value;
	unsigned int timeout = 100000;
	/* Port the accepted Qua shared USB clock/reset sequence. At this RAM
	 * boot boundary Linux is stopped and no USB host has been started.
	 * Keep every non-USB CRT bit unchanged. */
	{
		volatile void *reset1 = (void *)0x98000000;
		volatile void *reset2 = (void *)0x98000004;
		volatile void *clock = (void *)0x9800000c;
		u32 phy_mask = QUA_BIT(8) | QUA_BIT(9) | QUA_BIT(2);
		writel(readl((void *)reset2) & ~QUA_BIT(3), (void *)reset2);
		writel(readl((void *)reset1) & ~(QUA_BIT(6) | phy_mask), (void *)reset1);
		writel(readl((void *)reset2) & ~QUA_BIT(3), (void *)reset2);
		writel(readl((void *)reset1) | phy_mask, (void *)reset1);
		writel(readl((void *)reset2) | QUA_BIT(3), (void *)reset2);
		mdelay(2);
		writel(readl((void *)clock) | QUA_BIT(4), (void *)clock);
		writel(readl((void *)clock) & ~QUA_BIT(4), (void *)clock);
		mdelay(1);
		writel(readl((void *)reset1) | QUA_BIT(6), (void *)reset1);
		mdelay(1);
		writel(readl((void *)clock) | QUA_BIT(4), (void *)clock);
		mdelay(2);
	}
	udc = (volatile void *)UDC;
	/* The dedicated USB2 PHY clock/mux must be enabled before reading ID. */
	value = readl((void *)(PHY + 0x0c)); writel(value | QUA_BIT(0), (void *)(PHY + 0x0c)); mdelay(100);
	value = readl((void *)(PHY + 0x2c)); writel(value | QUA_BIT(0), (void *)(PHY + 0x2c)); mdelay(100);
	value = readl((void *)(PHY + 0x40)); writel(value | QUA_BIT(0), (void *)(PHY + 0x40));
	value = readl((void *)(PHY + 0x44)); writel(value | 0x180, (void *)(PHY + 0x44)); mdelay(100);
	initial_id = rd(GSNPSID);
	if (initial_id != 0x4f54310a) { udc = NULL; return -ENODEV; }
	while (!(rd(GRSTCTL) & GRSTCTL_AHBIDLE) && --timeout) ;
	if (!timeout) { udc = NULL; return -ETIMEDOUT; }
	wr(GRSTCTL, GRSTCTL_CSFTRST);
	while ((rd(GRSTCTL) & GRSTCTL_CSFTRST) && --timeout) ;
	if (!timeout) { udc = NULL; return -ETIMEDOUT; }
	/* Matches the verified Linux PIO register state: PHY16, TRDT=5, HS, no DMA. */
	value = rd(GUSBCFG) & ~(GUSBCFG_FORCEHOST | (0xf << 10));
	wr(GUSBCFG, value | GUSBCFG_FORCEDEV | GUSBCFG_PHYIF16 | GUSBCFG_TRDT_5);
	wr(GRXFSIZ, 512); wr(GNPTXFSIZ, (256 << 16) | 512); flush_fifos();
	wr(DCFG, 0); wr(GINTSTS, 0xffffffff); wr(DIEPMSK, 0x0f); wr(DOEPMSK, 0x0e);
	wr(DAINTMSK, 0x00030007);
	wr(GINTMSK, GINT_USBRST | GINT_ENUMDONE | GINT_RXFLVL | GINT_IEPINT | GINT_OEPINT);
	wr(GAHBCFG, GAHBCFG_GLBLINT); wr(DIEPCTL(EP0), DEPCTL_USBACTEP); arm_ep0_setup();
	wr(DCTL, (rd(DCTL) & ~DCTL_SFTDISCON) | DCTL_CGNPINNAK | DCTL_CGOUTNAK);
	configured = dtr = line_coding_out = false; rx_head = rx_count = tx_head = tx_count = 0;
	return 0;
}

int qua_usb_console_tstc(void) { qua_usb_console_poll(); return rx_count != 0; }
int qua_usb_console_getc(void)
{
	u8 byte;
	while (!qua_usb_console_tstc()) ;
	byte = rx_ring[rx_head]; rx_head = (rx_head + 1) % RING_SIZE; --rx_count;
	return byte;
}
void qua_usb_console_putc(const char byte)
{
	unsigned int wait = 2000;
	if (!configured || !dtr) return;
	while (tx_count == RING_SIZE && dtr && --wait) {
		qua_usb_console_poll();
		udelay(10);
	}
	if (!dtr || tx_count == RING_SIZE) return;
	tx_ring[(tx_head + tx_count) % RING_SIZE] = byte; ++tx_count; qua_usb_console_poll();
}
int qua_usb_console_connected(void) { return configured; }
int qua_usb_console_opened(void) { return configured && dtr; }
void qua_usb_console_shutdown(void)
{
	if (!udc) return;
	wr(GINTMSK, 0); wr(DCTL, rd(DCTL) | DCTL_SFTDISCON);
	udc = NULL; configured = dtr = false;
}

void qua_usb_console_debug(void)
{
	printf("USB2 initial_id=%08x active=%d reset=%u enum=%u setup=%u configured=%d dtr=%d\n",
	       initial_id, udc != NULL, reset_events, enum_events, setup_events, configured, dtr);
	if (udc)
		printf("USB2 gusbcfg=%08x grstctl=%08x gintsts=%08x gintmsk=%08x dctl=%08x dsts=%08x ep0in=%08x ep0out=%08x\n",
		       rd(GUSBCFG), rd(GRSTCTL), rd(GINTSTS), rd(GINTMSK), rd(DCTL), rd(DSTS), rd(DIEPCTL(0)), rd(DOEPCTL(0)));
}
