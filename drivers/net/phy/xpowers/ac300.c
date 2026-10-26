// SPDX-License-Identifier: GPL-2.0-only
/*
 * X-Powers AC300 Ethernet PHY package control
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (c) 2022 Arm Ltd. (Andre Przywara <andre.przywara@arm.com>)
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <clk.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "acx00.h"

#define AC300_EPHY_CONTROL_ADDR_OFFSET		16
#define AC300_EPHY_BGS_EFFUSE_OFFSET		3
#define AC300_EPHY_CLK_RATE_24_MHZ		24000000
#define AC300_EPHY_CLK_RATE_25_MHZ		25000000
#define AC300_EPHY_CLK_RATE_27_MHZ		27000000
#define AC300_SYS_CONTROL_REG			0x00
#define AC300_PACKAGE_STATUS_MASK		GENMASK(11, 8)
#define AC300_EPHY_CLK_SEL_MASK			GENMASK(7, 6)
#define AC300_EPHY_CLK_SEL_25_MHZ		0
#define AC300_EPHY_CLK_SEL_27_MHZ		1
#define AC300_EPHY_CLK_SEL_24_MHZ		2
#define AC300_EFUSE_CLK_ENABLE			BIT(5)
#define AC300_EPHY_REG_CLK_ENABLE		BIT(4)
#define AC300_CLKIN_GATING_ENABLE		BIT(2)
#define AC300_EPHY_RESET_DEASSERT		BIT(1)
#define AC300_CHIP_RESET_DEASSERT		BIT(0)
#define AC300_PACKAGE_POR_INTERNAL_DLDO		BIT(3)
#define AC300_PACKAGE_PHY_ADDR_MASK		GENMASK(2, 0)
#define AC300_SYS_BIAS1_REG			0x02
#define AC300_INTERNAL_DLDO_ENABLE		BIT(15)
#define AC300_SYS_IO_REG			0x05
#define AC300_MDIO_DRV_MASK			GENMASK(15, 14)
#define AC300_MII_DRV_MASK			GENMASK(11, 10)
#define AC300_IO_DRV_LEVEL_2			2
#define AC300_CLKIN_PAD_ENABLE			BIT(4)
#define AC300_EPHY_MII_IO_ENABLE			BIT(0)
#define AC300_EPHY_CONFIG_REG			0x06
#define AC300_EPHY_BGS_EFFUSE_MASK		GENMASK(15, 12)
#define AC300_EPHY_RMII_SEL			BIT(11)
#define AC300_EPHY_SHUTDOWN			BIT(0)

#define AC300_SYS_CONTROL_ENABLE_BITS \
	(AC300_EFUSE_CLK_ENABLE | AC300_EPHY_REG_CLK_ENABLE | \
	 AC300_CLKIN_GATING_ENABLE | AC300_EPHY_RESET_DEASSERT | \
	 AC300_CHIP_RESET_DEASSERT)

#define AC300_SYS_IO_VALUE \
	(FIELD_PREP(AC300_MDIO_DRV_MASK, AC300_IO_DRV_LEVEL_2) | \
	 FIELD_PREP(AC300_MII_DRV_MASK, AC300_IO_DRV_LEVEL_2) | \
	 AC300_CLKIN_PAD_ENABLE | AC300_EPHY_MII_IO_ENABLE)

static int ac300_ephy_read(struct acx00_ephy_priv *priv, int reg)
{
	return priv->phydev->bus->read(priv->phydev->bus,
				       priv->phydev->addr +
				       AC300_EPHY_CONTROL_ADDR_OFFSET,
				       MDIO_DEVAD_NONE, reg);
}

static int ac300_ephy_write(struct acx00_ephy_priv *priv, int reg, u16 value)
{
	return priv->phydev->bus->write(priv->phydev->bus,
					priv->phydev->addr +
					AC300_EPHY_CONTROL_ADDR_OFFSET,
					MDIO_DEVAD_NONE, reg, value);
}

static int ac300_ephy_modify(struct acx00_ephy_priv *priv, int reg,
			     u16 mask, u16 set)
{
	int value;

	value = ac300_ephy_read(priv, reg);
	if (value < 0)
		return value;

	return ac300_ephy_write(priv, reg,
				(value & ~mask) | (set & mask));
}

static u16 ac300_ephy_config(const struct acx00_ephy_priv *priv)
{
	return priv->control_config |
		(priv->phydev->interface == PHY_INTERFACE_MODE_RMII ?
		 AC300_EPHY_RMII_SEL : 0);
}

static u16 ac300_ephy_reset_value(const struct acx00_ephy_priv *priv)
{
	return !priv->ac300_package_known || !priv->ac300_internal_dldo ?
		AC300_CHIP_RESET_DEASSERT : 0;
}

int ac300_ephy_power_off(struct acx00_ephy_priv *priv)
{
	int clk_ret = 0;
	int err;
	int ret = 0;

	if (priv->powered) {
		ret = ac300_ephy_write(priv, AC300_EPHY_CONFIG_REG,
				       ac300_ephy_config(priv) |
				       AC300_EPHY_SHUTDOWN);
		err = ac300_ephy_write(priv, AC300_SYS_IO_REG, 0);
		if (!ret)
			ret = err;
		err = ac300_ephy_write(priv, AC300_SYS_CONTROL_REG,
				       ac300_ephy_reset_value(priv));
		if (!ret)
			ret = err;

		priv->powered = false;
	}

	if (priv->clk_enabled) {
		clk_ret = clk_disable(&priv->clk);
		if (!clk_ret)
			priv->clk_enabled = false;
	}

	return ret ? ret : clk_ret;
}

int ac300_ephy_power_on(struct acx00_ephy_priv *priv)
{
	u8 package_status;
	int sys_control;
	int ret;

	if (priv->powered)
		return 0;
	if (!priv->clk_enabled) {
		ret = clk_enable(&priv->clk);
		if (ret)
			return ret;
		priv->clk_enabled = true;
	}

	priv->powered = true;
	ret = ac300_ephy_write(priv, AC300_SYS_CONTROL_REG,
			       ac300_ephy_reset_value(priv));
	if (ret)
		goto err_power_off;

	ret = ac300_ephy_write(priv, AC300_SYS_CONTROL_REG,
			       AC300_EPHY_RESET_DEASSERT |
			       AC300_CHIP_RESET_DEASSERT);
	if (ret)
		goto err_power_off;

	ret = ac300_ephy_write(priv, AC300_SYS_CONTROL_REG,
			       priv->ac300_sys_control);
	if (ret)
		goto err_power_off;

	sys_control = ac300_ephy_read(priv, AC300_SYS_CONTROL_REG);
	if (sys_control < 0) {
		ret = sys_control;
		goto err_power_off;
	}

	package_status = FIELD_GET(AC300_PACKAGE_STATUS_MASK, sys_control);
	if ((~package_status & AC300_PACKAGE_PHY_ADDR_MASK) !=
	    priv->phydev->addr) {
		ret = -EINVAL;
		goto err_power_off;
	}

	priv->ac300_internal_dldo =
		package_status & AC300_PACKAGE_POR_INTERNAL_DLDO;
	priv->ac300_package_known = true;
	ret = ac300_ephy_modify(priv, AC300_SYS_BIAS1_REG,
				AC300_INTERNAL_DLDO_ENABLE,
				priv->ac300_internal_dldo ?
				AC300_INTERNAL_DLDO_ENABLE : 0);
	if (ret)
		goto err_power_off;

	ret = ac300_ephy_write(priv, AC300_SYS_IO_REG, AC300_SYS_IO_VALUE);
	if (ret)
		goto err_power_off;

	mdelay(10);
	ret = ac300_ephy_write(priv, AC300_EPHY_CONFIG_REG,
			       ac300_ephy_config(priv) | AC300_EPHY_SHUTDOWN);
	if (ret)
		goto err_power_off;

	mdelay(10);
	ret = ac300_ephy_write(priv, AC300_EPHY_CONFIG_REG,
			       ac300_ephy_config(priv));
	if (ret)
		goto err_power_off;

	return 0;

err_power_off:
	ac300_ephy_power_off(priv);

	return ret;
}

int ac300_ephy_init(struct acx00_ephy_priv *priv, ofnode package,
		    u8 calibration)
{
	ulong clock_rate;
	u8 code;
	int ret;

	if (priv->phydev->addr > FIELD_MAX(AC300_PACKAGE_PHY_ADDR_MASK))
		return -EINVAL;

	ret = clk_get_by_index_nodev(package, 0, &priv->clk);
	if (ret)
		return ret;
	clock_rate = clk_get_rate(&priv->clk);
	if (IS_ERR_VALUE(clock_rate))
		return (long)clock_rate;
	switch (clock_rate) {
	case AC300_EPHY_CLK_RATE_24_MHZ:
		priv->ac300_sys_control =
			FIELD_PREP(AC300_EPHY_CLK_SEL_MASK,
				   AC300_EPHY_CLK_SEL_24_MHZ);
		break;
	case AC300_EPHY_CLK_RATE_25_MHZ:
		priv->ac300_sys_control =
			FIELD_PREP(AC300_EPHY_CLK_SEL_MASK,
				   AC300_EPHY_CLK_SEL_25_MHZ);
		break;
	case AC300_EPHY_CLK_RATE_27_MHZ:
		priv->ac300_sys_control =
			FIELD_PREP(AC300_EPHY_CLK_SEL_MASK,
				   AC300_EPHY_CLK_SEL_27_MHZ);
		break;
	default:
		return -EINVAL;
	}
	priv->ac300_sys_control |= AC300_SYS_CONTROL_ENABLE_BITS;

	code = (calibration + AC300_EPHY_BGS_EFFUSE_OFFSET) &
		FIELD_MAX(AC300_EPHY_BGS_EFFUSE_MASK);
	priv->control_config =
		FIELD_PREP(AC300_EPHY_BGS_EFFUSE_MASK, code);

	return 0;
}
