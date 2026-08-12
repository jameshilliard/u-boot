// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner Security ID NVMEM provider
 *
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <dm.h>
#include <dm/read.h>
#include <misc.h>
#include <asm/io.h>

struct sunxi_sid_data {
	unsigned int value_offset;
	unsigned int size;
};

struct sunxi_sid_priv {
	const struct sunxi_sid_data *data;
	u8 __iomem *base;
};

static int sunxi_sid_read(struct udevice *dev, int offset, void *buf, int size)
{
	struct sunxi_sid_priv *priv = dev_get_priv(dev);
	u8 *out = buf;
	int i;

	if (offset < 0 || !buf || size < 0 ||
	    offset + size > priv->data->size)
		return -EINVAL;

	for (i = 0; i < size; i++) {
		unsigned int byte_offset = offset + i;
		u32 value;

		value = readl(priv->base + priv->data->value_offset +
			      (byte_offset & ~3));
		out[i] = value >> (8 * (byte_offset & 3));
	}

	return size;
}

static int sunxi_sid_probe(struct udevice *dev)
{
	struct sunxi_sid_priv *priv = dev_get_priv(dev);

	priv->data = (const void *)dev_get_driver_data(dev);
	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	return 0;
}

static const struct misc_ops sunxi_sid_ops = {
	.read = sunxi_sid_read,
};

static const struct sunxi_sid_data sun4i_a10_sid_data = {
	.size = 0x10,
};

static const struct sunxi_sid_data sun7i_a20_sid_data = {
	.size = 0x200,
};

static const struct sunxi_sid_data sun50i_a64_sid_data = {
	.value_offset = 0x200,
	.size = 0x100,
};

static const struct sunxi_sid_data sun50i_h6_sid_data = {
	.value_offset = 0x200,
	.size = 0x200,
};

static const struct udevice_id sunxi_sid_ids[] = {
	{
		.compatible = "allwinner,sun4i-a10-sid",
		.data = (ulong)&sun4i_a10_sid_data,
	},
	{
		.compatible = "allwinner,sun7i-a20-sid",
		.data = (ulong)&sun7i_a20_sid_data,
	},
	{
		.compatible = "allwinner,sun8i-a83t-sid",
		.data = (ulong)&sun50i_a64_sid_data,
	},
	{
		.compatible = "allwinner,sun20i-d1-sid",
		.data = (ulong)&sun50i_a64_sid_data,
	},
	{
		.compatible = "allwinner,sun50i-a64-sid",
		.data = (ulong)&sun50i_a64_sid_data,
	},
	{
		.compatible = "allwinner,sun50i-h5-sid",
		.data = (ulong)&sun50i_a64_sid_data,
	},
	{
		.compatible = "allwinner,sun50i-h6-sid",
		.data = (ulong)&sun50i_h6_sid_data,
	},
	{ }
};

U_BOOT_DRIVER(sunxi_sid) = {
	.name = "sunxi_sid",
	.id = UCLASS_MISC,
	.of_match = sunxi_sid_ids,
	.probe = sunxi_sid_probe,
	.ops = &sunxi_sid_ops,
	.priv_auto = sizeof(struct sunxi_sid_priv),
};
