/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CONFIG_QUASTATION_RAM_CHAIN_H
#define __CONFIG_QUASTATION_RAM_CHAIN_H
#include <configs/quastation_ram_rescue.h>
#define CONFIG_QUA_CHAIN_LINUX 1
#define CONFIG_QUA_USB_CONSOLE 1
#define CONFIG_CONSOLE_MUX 1
#include <configs/qua_trial_values.h>
#undef CONFIG_IDENT_STRING
#define CONFIG_IDENT_STRING " Qua USB2 rescue (RAM)"
#endif
