/* SPDX-License-Identifier: GPL-2.0 */
/* The vendor tree predates GCC's major-version header split after GCC 4. */
#include <linux/compiler-gcc4.h>
#ifndef __cold
#define __cold __attribute__((__cold__))
#endif
#ifndef unreachable
#define unreachable() __builtin_unreachable()
#endif
#ifndef __compiletime_warning
#define __compiletime_warning(message) __attribute__((warning(message)))
#define __compiletime_error(message) __attribute__((error(message)))
#endif
