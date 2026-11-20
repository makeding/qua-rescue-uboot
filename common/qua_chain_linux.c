// SPDX-License-Identifier: GPL-2.0
/*
 * One-shot RAM handoff from the rescue AArch32 console to the untouched
 * vendor AArch64 U-Boot image already staged by the original first stage.
 * Nothing here writes persistent storage.  All failure paths return to the
 * rescue console before cache cleanup and before the first-stage image is
 * overwritten by the second ARM trampoline.
 */
#include <common.h>
#include <environment.h>
#include <fdt_support.h>
#include <asm/u-boot-arm.h>
#include <configs/qua_trial_values.h>

#define QUA_KERNEL_ADDR          0x26000000UL
#define QUA_INITRD_ADDR          0x30000000UL
#define QUA_DTB_ADDR             0x23000000UL
#define QUA_BASE_ENV_ADDR        0x24000000UL
#define QUA_STAGE2_ADDR          0x01500000UL
#define QUA_STAGE2_DEST          0x00021000UL
#define QUA_STAGE2_COPY_BYTES    0x000b2000UL
#define QUA_STAGE2_STUB_ADDR     0x01f02000UL
#define QUA_ENV_EXPORT_ADDR      0x10000000UL
#define QUA_GOLDEN_MAGIC1_ADDR   0x000d3000UL
#define QUA_GOLDEN_MAGIC2_ADDR   0x000d3004UL
#define QUA_GOLDEN_MAGIC1        0x676f6c64UL
#define QUA_GOLDEN_MAGIC2        0x626f6f74UL
#define QUA_DTB_EXPANDED_BYTES   0x00010000UL

static void __attribute__((noreturn)) qua_enter_stage2(void)
{
	void (*stub)(void) = (void (*)(void))QUA_STAGE2_STUB_ADDR;

	/* This is the vendor goldenboot boundary: caches/MMU are torn down
	 * before the RAM-resident stub overwrites this first-stage image. */
	{
		extern void qua_usb_console_shutdown(void);
		qua_usb_console_shutdown();
	}
	cleanup_before_bootcode();
	asm volatile("bx %0" : : "r" (stub) : "memory");
	__builtin_unreachable();
}

void qua_chain_linux_trial(void)
{
	void *dtb = (void *)QUA_DTB_ADDR;
	int ret;

	printf("QUA_CHAIN trial nonce=%s\n", QUA_TRIAL_NONCE);

	if (QUA_STAGE2_BYTES != 0x00100000UL ||
	    QUA_STAGE2_BYTES < QUA_STAGE2_COPY_BYTES) {
		printf("QUA_CHAIN invalid stage2 length %lx\n",
		       (ulong)QUA_STAGE2_BYTES);
		return;
	}

	if (QUA_BASE_ENV_BYTES != sizeof(env_t)) {
		printf("QUA_CHAIN invalid baseline environment size %lx\n",
		       (ulong)QUA_BASE_ENV_BYTES);
		return;
	}

	/* The vendor second stage uses more environment fields than this trial
	 * changes.  Restore the verified pre-trial image first, then override
	 * only the one-shot fields below. env_import validates the environment's
	 * own CRC even when the environment is generated for each device. */
	if (env_import((const char *)QUA_BASE_ENV_ADDR, 1) != 1) {
		puts("QUA_CHAIN baseline environment import failed\n");
		return;
	}

	ret = fdt_check_header(dtb);
	if (ret) {
		printf("QUA_CHAIN DTB header fail %s\n", fdt_strerror(ret));
		return;
	}

	ret = fdt_open_into(dtb, dtb, QUA_DTB_EXPANDED_BYTES);
	if (ret) {
		printf("QUA_CHAIN DTB expand fail %s\n", fdt_strerror(ret));
		return;
	}

#ifdef QUA_MAINTENANCE_INITRD
	if (setenv("bootcmd", "env set fdt_high ffffffffffffffff;booti 0x26000000 0x30000000 0x23000000") ||
#else
	if (setenv("bootcmd", "env set fdt_high ffffffffffffffff;booti 0x26000000 - 0x23000000") ||
#endif
	    setenv("bootdelay", "0") ||
	    setenv("fdt_loadaddr", "23000000")) {
		puts("QUA_CHAIN volatile environment setup failed\n");
		return;
	}

	ret = fdt_chosen(dtb, 1);
	if (ret) {
		printf("QUA_CHAIN fdt_chosen fail %s\n", fdt_strerror(ret));
		return;
	}

	memset((void *)QUA_ENV_EXPORT_ADDR, 0, sizeof(env_t));
	if (env_export_uboot64((env_t *)QUA_ENV_EXPORT_ADDR)) {
		puts("QUA_CHAIN environment export failed\n");
		return;
	}

	*(volatile u32 *)QUA_GOLDEN_MAGIC1_ADDR = QUA_GOLDEN_MAGIC1;
	*(volatile u32 *)QUA_GOLDEN_MAGIC2_ADDR = QUA_GOLDEN_MAGIC2;
	printf("QUA_CHAIN verified; entering staged vendor U-Boot64\n");
	qua_enter_stage2();
}
