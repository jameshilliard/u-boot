/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __XPOWERS_ACX00_H
#define __XPOWERS_ACX00_H

#include <linux/errno.h>
#include <linux/kconfig.h>

#if CONFIG_IS_ENABLED(PHY_XPOWERS_ACX00)
int xpowers_acx00_fixup_fdt(void *fdt);
#else
static inline int xpowers_acx00_fixup_fdt(void *fdt)
{
	return 0;
}
#endif

#endif
