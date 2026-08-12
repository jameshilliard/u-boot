/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __XPOWERS_AC200_H
#define __XPOWERS_AC200_H

#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/types.h>

struct udevice;

#if CONFIG_IS_ENABLED(XPOWERS_AC200)
int ac200_read(struct udevice *dev, unsigned int reg, u16 *value);
int ac200_write(struct udevice *dev, unsigned int reg, u16 value);
int ac200_update_bits(struct udevice *dev, unsigned int reg, u16 mask,
		      u16 value);
ulong ac200_get_clock_rate(struct udevice *dev);
#else
static inline int ac200_read(struct udevice *dev, unsigned int reg,
			     u16 *value)
{
	return -ENOSYS;
}

static inline int ac200_write(struct udevice *dev, unsigned int reg,
			      u16 value)
{
	return -ENOSYS;
}

static inline int ac200_update_bits(struct udevice *dev, unsigned int reg,
				    u16 mask, u16 value)
{
	return -ENOSYS;
}

static inline ulong ac200_get_clock_rate(struct udevice *dev)
{
	return -ENOSYS;
}
#endif

#endif
