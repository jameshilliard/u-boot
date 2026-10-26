// SPDX-License-Identifier: GPL-2.0-only
/*
 * X-Powers AC200 Ethernet PHY package control
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (c) 2022 Arm Ltd. (Andre Przywara <andre.przywara@arm.com>)
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <dm.h>
#include <linux/bitfield.h>
#include <linux/err.h>
#include <xpowers-ac200.h>

#include "acx00.h"

#define AC200_EPHY_BPS_EFFUSE_OFFSET		3
#define AC200_EPHY_CLK_RATE_24_MHZ		24000000
#define AC200_EPHY_CLK_RATE_27_MHZ		27000000

#define AC200_SYS_EPHY_CTL0_REG			0x0014
#define AC200_EPHY_RESET_DEASSERT		BIT(0)
#define AC200_EPHY_SYSCLK_ENABLE			BIT(1)
#define AC200_SYS_EPHY_CTL1_REG			0x0016
#define AC200_EPHY_MII_IO_ENABLE			BIT(0)
#define AC200_EFUSE_EPHY_REG			0x8004
#define AC200_EPHY_CTL_REG			0x6000
#define AC200_EPHY_SHUTDOWN			BIT(0)
#define AC200_EPHY_CLK_SEL_24_MHZ		BIT(2)
#define AC200_EPHY_PHY_ADDR_MASK			GENMASK(8, 4)
#define AC200_EPHY_RMII_SEL			BIT(11)
#define AC200_EPHY_BPS_EFFUSE_MASK		GENMASK(15, 12)

static u16 ac200_ephy_config(const struct acx00_ephy_priv *priv)
{
	return priv->control_config |
		(priv->phydev->interface == PHY_INTERFACE_MODE_RMII ?
		 AC200_EPHY_RMII_SEL : 0) |
		FIELD_PREP(AC200_EPHY_PHY_ADDR_MASK, priv->phydev->addr);
}

int ac200_ephy_power_on(struct acx00_ephy_priv *priv)
{
	u16 config;
	int ret;

	if (priv->phydev->addr > FIELD_MAX(AC200_EPHY_PHY_ADDR_MASK))
		return -EINVAL;
	if (priv->powered)
		return 0;

	config = ac200_ephy_config(priv);
	/* Make a failed partial start run the complete shutdown sequence. */
	priv->powered = true;

	ret = ac200_write(priv->control, AC200_SYS_EPHY_CTL0_REG, 0);
	if (ret)
		return ret;

	ret = ac200_write(priv->control, AC200_SYS_EPHY_CTL1_REG,
			  AC200_EPHY_MII_IO_ENABLE);
	if (ret)
		return ret;

	ret = ac200_write(priv->control, AC200_EPHY_CTL_REG,
			  config | AC200_EPHY_SHUTDOWN);
	if (ret)
		return ret;

	ret = ac200_write(priv->control, AC200_SYS_EPHY_CTL0_REG,
			  AC200_EPHY_RESET_DEASSERT |
			  AC200_EPHY_SYSCLK_ENABLE);
	if (ret)
		return ret;

	return ac200_write(priv->control, AC200_EPHY_CTL_REG, config);
}

int ac200_ephy_power_off(struct acx00_ephy_priv *priv)
{
	int err;
	int ret;

	if (!priv->powered)
		return 0;

	ret = ac200_write(priv->control, AC200_EPHY_CTL_REG,
			  ac200_ephy_config(priv) | AC200_EPHY_SHUTDOWN);
	err = ac200_write(priv->control, AC200_SYS_EPHY_CTL1_REG, 0);
	if (!ret)
		ret = err;
	err = ac200_write(priv->control, AC200_SYS_EPHY_CTL0_REG, 0);
	if (!ret)
		ret = err;

	priv->powered = false;

	return ret;
}

int ac200_ephy_init(struct acx00_ephy_priv *priv, ofnode package,
		    bool has_configuration, u8 calibration)
{
	struct ofnode_phandle_args args;
	ulong clock_rate;
	u16 internal_calibration;
	u8 code;
	int ret;

	ret = ofnode_parse_phandle_with_args(package, "x-powers,ac200", NULL,
					     0, 0, &args);
	if (ret)
		return ret;

	if (!ofnode_device_is_compatible(args.node, "x-powers,ac200"))
		return -EINVAL;

	ret = uclass_get_device_by_ofnode(UCLASS_MISC, args.node,
					  &priv->control);
	if (ret)
		return ret;

	if (!has_configuration) {
		ret = ac200_read(priv->control, AC200_EFUSE_EPHY_REG,
				 &internal_calibration);
		if (ret)
			return ret;
		calibration = internal_calibration;
	}

	code = (calibration + AC200_EPHY_BPS_EFFUSE_OFFSET) &
		FIELD_MAX(AC200_EPHY_BPS_EFFUSE_MASK);
	priv->control_config =
		FIELD_PREP(AC200_EPHY_BPS_EFFUSE_MASK, code);

	clock_rate = ac200_get_clock_rate(priv->control);
	if (IS_ERR_VALUE(clock_rate))
		return (long)clock_rate;
	switch (clock_rate) {
	case AC200_EPHY_CLK_RATE_24_MHZ:
		priv->control_config |= AC200_EPHY_CLK_SEL_24_MHZ;
		break;
	case AC200_EPHY_CLK_RATE_27_MHZ:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
