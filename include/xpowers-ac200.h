/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __XPOWERS_AC200_H
#define __XPOWERS_AC200_H

#include <linux/types.h>

struct udevice;

int ac200_read(struct udevice *dev, unsigned int reg, u16 *value);
int ac200_write(struct udevice *dev, unsigned int reg, u16 value);
int ac200_update_bits(struct udevice *dev, unsigned int reg, u16 mask,
		      u16 value);
ulong ac200_get_clock_rate(struct udevice *dev);

#endif
