/* SPDX-License-Identifier: GPL-2.0 */
/* Volatile console trial: no vendor bootr, key provisioning or AV commands. */
#include <common.h>
#include <command.h>

static int do_qua_go(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	ulong address;
	ulong (*entry)(int, char * const []);
	ulong result;
	char *end;

	if (argc < 2)
		return CMD_RET_USAGE;
	address = simple_strtoul(argv[1], &end, 16);
	if (*end)
		return CMD_RET_USAGE;
	entry = (void *)address;
	printf("## Starting RAM application at %08lx ...\n", address);
	result = entry(argc - 1, argv + 1);
	printf("## RAM application returned %08lx\n", result);
	return result != 0;
}

U_BOOT_CMD(go, CONFIG_SYS_MAXARGS, 1, do_qua_go,
	"run a standalone RAM application", "address [arguments ...]");
U_BOOT_CMD(reset, 1, 0, do_reset,
	"reset the CPU to the original stored bootcode", "");

#ifdef CONFIG_QUA_CHAIN_LINUX
static int do_qua_boot(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	extern void qua_chain_linux_trial(void);
	qua_chain_linux_trial();
	return 1;
}
U_BOOT_CMD(boot, 1, 0, do_qua_boot,
	"boot the preloaded verified Linux through original U-Boot64", "");
#endif

#ifdef CONFIG_QUA_USB_CONSOLE
static int do_qua_usbstat(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	extern void qua_usb_console_debug(void);
	qua_usb_console_debug();
	return 0;
}
U_BOOT_CMD(usbstat, 1, 0, do_qua_usbstat,
	"show the USB2 rescue controller state", "");

static int do_qua_usbrecv(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	extern int qua_usb_console_recv_raw(ulong addr, ulong len, u32 *crc_out);
	ulong addr, len;
	u32 crc = 0;
	char *end;
	int ret;

	if (argc != 3)
		return CMD_RET_USAGE;
	addr = simple_strtoul(argv[1], &end, 16);
	if (*end)
		return CMD_RET_USAGE;
	len = simple_strtoul(argv[2], &end, 16);
	if (*end || !len)
		return CMD_RET_USAGE;

	printf("## Ready for raw USB download to 0x%08lx (0x%lx bytes)\n", addr, len);
	ret = qua_usb_console_recv_raw(addr, len, &crc);
	if (ret) {
		printf("## usbrecv failed: %d\n", ret);
		return 1;
	}
	printf("## Received 0x%lx bytes, crc32=%08x\n", len, crc);
	return 0;
}
U_BOOT_CMD(usbrecv, 3, 0, do_qua_usbrecv,
	"receive raw binary over USB bulk endpoint", "addr len_hex");
#endif
