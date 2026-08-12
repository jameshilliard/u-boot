// SPDX-License-Identifier: GPL-2.0+
/*
 * X-Powers AC200 I2C register provider
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <i2c.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <asm/unaligned.h>
#include <xpowers-ac200.h>

#define AC200_SYS_CONTROL_REG			0x0002
#define AC200_SYS_CONTROL_CHIP_RESET_DEASSERT	BIT(0)

#define AC200_TWI_REG_ADDR_H			0xfe
#define AC200_MAX_REG				0xa1f2

struct ac200_priv {
	struct clk clk;
};

static int ac200_select_page(struct udevice *dev, unsigned int page)
{
	u8 buf[2];

	/* Select explicitly: an I2C command can also change the hardware page. */
	put_unaligned_be16(page, buf);
	return dm_i2c_write(dev, AC200_TWI_REG_ADDR_H, buf, sizeof(buf));
}

int ac200_read(struct udevice *dev, unsigned int reg, u16 *value)
{
	u8 buf[2];
	int ret;

	if (!value || reg > AC200_MAX_REG || reg & 1)
		return -EINVAL;

	ret = ac200_select_page(dev, reg >> 8);
	if (ret)
		return ret;

	ret = dm_i2c_read(dev, reg & 0xff, buf, sizeof(buf));
	if (ret)
		return ret;

	*value = get_unaligned_be16(buf);

	return 0;
}

int ac200_write(struct udevice *dev, unsigned int reg, u16 value)
{
	u8 buf[2];
	int ret;

	if (reg > AC200_MAX_REG || reg & 1)
		return -EINVAL;

	ret = ac200_select_page(dev, reg >> 8);
	if (ret)
		return ret;

	put_unaligned_be16(value, buf);

	return dm_i2c_write(dev, reg & 0xff, buf, sizeof(buf));
}

int ac200_update_bits(struct udevice *dev, unsigned int reg, u16 mask,
		      u16 value)
{
	u16 old;
	int ret;

	ret = ac200_read(dev, reg, &old);
	if (ret)
		return ret;

	value = (old & ~mask) | (value & mask);
	return old == value ? 0 : ac200_write(dev, reg, value);
}

ulong ac200_get_clock_rate(struct udevice *dev)
{
	struct ac200_priv *priv = dev_get_priv(dev);

	return clk_get_rate(&priv->clk);
}

static int ac200_probe(struct udevice *dev)
{
	struct ac200_priv *priv = dev_get_priv(dev);
	int ret;

	ret = clk_get_by_index(dev, 0, &priv->clk);
	if (ret)
		return ret;
	ret = clk_enable(&priv->clk);
	if (ret)
		return ret;

	/*
	 * No minimum delay is documented. Match the vendor driver's delay
	 * before its first register access after the input clock is enabled.
	 */
	mdelay(40);

	/* Release reset without resetting other functions of the companion IC. */
	ret = ac200_update_bits(dev, AC200_SYS_CONTROL_REG,
				AC200_SYS_CONTROL_CHIP_RESET_DEASSERT,
				AC200_SYS_CONTROL_CHIP_RESET_DEASSERT);
	if (ret)
		goto err_disable_clk;

	return 0;

err_disable_clk:
	clk_disable(&priv->clk);

	return ret;
}

static int ac200_remove(struct udevice *dev)
{
	struct ac200_priv *priv = dev_get_priv(dev);

	return clk_disable(&priv->clk);
}

static const struct udevice_id ac200_ids[] = {
	{ .compatible = "x-powers,ac200" },
	{ }
};

U_BOOT_DRIVER(xpowers_ac200) = {
	.name = "xpowers_ac200",
	.id = UCLASS_MISC,
	.of_match = ac200_ids,
	.probe = ac200_probe,
	.remove = ac200_remove,
	.priv_auto = sizeof(struct ac200_priv),
};
