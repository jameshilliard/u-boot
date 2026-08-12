// SPDX-License-Identifier: GPL-2.0+
/*
 * X-Powers AC200/AC300 Ethernet PHY driver
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (c) 2022 Arm Ltd. (Andre Przywara <andre.przywara@arm.com>)
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <clk.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <malloc.h>
#include <nvmem.h>
#include <phy.h>
#include <power/regulator.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/libfdt.h>
#include <asm/unaligned.h>
#include <xpowers-ac200.h>
#include <xpowers-acx00.h>

#define ACX00_EPHY_ID				0x00441400

#define ACX00_EPHY_CONFIG_CALIBRATION_MASK	GENMASK(3, 0)
#define ACX00_EPHY_CONFIG_VARIANT_AC300		BIT(8)
#define ACX00_EPHY_CONFIG_CALIBRATION_LOW	BIT(9)

#define ACX00_EPHY_FALLBACK_CLK_RATE		24000000

#define ACX00_PAGE_SELECT_REG			0x1f
#define ACX00_PAGE_SELECT_MASK			GENMASK(12, 8)
#define ACX00_PAGE_0				0
#define ACX00_PAGE_1				1
#define ACX00_PAGE_2				2
#define ACX00_PAGE_6				6
#define ACX00_PAGE_8				8

#define ACX00_PAGE0_GLOBAL_CONFIG_REG		0x13
#define ACX00_PAGE0_XMII_RX_CLOCK_INVERT	BIT(12)
#define ACX00_PAGE0_MDI_MODE_MASK		GENMASK(1, 0)
#define ACX00_PAGE0_MDI_MODE_AUTO		2

#define ACX00_PAGE1_APS_CONTROL_REG		0x12
#define ACX00_PAGE1_APS_DISABLED_4S_VALUE	0x4824
#define ACX00_PAGE1_UAPS_CONTROL_REG		0x13
#define ACX00_PAGE1_UAPS_ENABLE			BIT(15)
#define ACX00_PAGE1_INTELLIGENT_EEE_CONTROL_REG	0x17
#define ACX00_PAGE1_INTELLIGENT_EEE_ENABLE	BIT(3)

#define ACX00_PAGE2_TX_DATA_CONTROL_REG		0x18
#define ACX00_PAGE2_10BT_FIR_SELECT_MASK		GENMASK(14, 12)

#define ACX00_PAGE6_ADC_CONTROL_REG		0x10
#define ACX00_PAGE6_ADC_CONTROL_LOW_CAL_VALUE	0x5523
#define ACX00_PAGE6_AFE_RX_CONTROL_REG		0x13
#define ACX00_PAGE6_AFE_RX_CONTROL_VALUE		0xf000
#define ACX00_PAGE6_AFE_EQ_RX_DETECT_CONTROL_REG	0x14
#define AC200_PAGE6_AFE_EQ_RX_DETECT_VALUE	0x708f
#define AC300_PAGE6_AFE_EQ_RX_DETECT_VALUE	0x708b
#define ACX00_PAGE6_AFE_EQ_RX_DETECT_LOW_CAL_VALUE 0x7809
#define ACX00_PAGE6_TX_LEVEL_REG			0x15
#define ACX00_PAGE6_TX_LEVEL_DEFAULT_VALUE	0x1530
#define ACX00_PAGE6_TX_LEVEL_LOW_CAL_VALUE	0x3533

#define ACX00_PAGE8_AFE_CONTROL_REG		0x18
#define ACX00_PAGE8_AFE_CONTROL_VALUE		0x00bc
#define ACX00_PAGE8_AUTO_CAL_CONTROL_REG		0x1d
#define ACX00_PAGE8_AUTO_CAL_LOW_VALUE		0x0844

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

enum acx00_variant {
	ACX00_VARIANT_AC200,
	ACX00_VARIANT_AC300,
};

struct acx00_ephy_priv {
	struct phy_device *phydev;
	struct udevice *control;
	struct udevice *vcc;
	struct clk clk;
	enum acx00_variant variant;
	u16 control_config;
	u16 ac300_sys_control;
	bool use_low_calibration_tuning;
	bool xmii_rx_clock_inverted;
	bool clk_acquired;
	bool clk_enabled;
	bool ac300_package_known;
	bool ac300_internal_dldo;
	bool supply_enabled;
	bool powered;
};

static int acx00_ephy_read_page(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MDIO_DEVAD_NONE, ACX00_PAGE_SELECT_REG);
	if (ret < 0)
		return ret;

	return FIELD_GET(ACX00_PAGE_SELECT_MASK, ret);
}

static int acx00_ephy_write_page(struct phy_device *phydev, int page)
{
	return phy_write(phydev, MDIO_DEVAD_NONE, ACX00_PAGE_SELECT_REG,
			 FIELD_PREP(ACX00_PAGE_SELECT_MASK, page));
}

static int acx00_ephy_modify_paged(struct phy_device *phydev, int page,
				   int reg, u16 mask, u16 set)
{
	int oldpage;
	int ret;

	oldpage = phy_select_page(phydev, page);
	if (oldpage < 0)
		return phy_restore_page(phydev, oldpage, 0);

	ret = phy_modify(phydev, MDIO_DEVAD_NONE, reg, mask, set);

	return phy_restore_page(phydev, oldpage, ret);
}

static int acx00_ephy_read_configuration(ofnode package, u16 *configuration,
					 bool *present)
{
	struct nvmem_cell cell;
	u8 buf[sizeof(*configuration)] = {};
	int ret;

	*present = ofnode_read_bool(package, "nvmem-cells");
	if (!*present) {
		*configuration = 0;
		return 0;
	}

	ret = nvmem_cell_get_by_name_nodev(package, "configuration", &cell);
	if (ret)
		return ret;
	if (!cell.size || cell.size > sizeof(buf))
		return -ERANGE;

	ret = nvmem_cell_read(&cell, buf, cell.size);
	if (ret)
		return ret;

	*configuration = buf[0];
	if (cell.size == sizeof(buf))
		*configuration |= (u16)buf[1] << 8;
	*configuration &= GENMASK(9, 0);

	return 0;
}

static int acx00_ephy_select_variant(ofnode package,
				     enum acx00_variant *variant,
				     u16 *configuration, bool *present)
{
	bool selectable;
	bool fixed_ac300;
	int ret;

	selectable = ofnode_device_is_compatible(package,
						 "x-powers,acx00-ephy-package");
	fixed_ac300 = ofnode_device_is_compatible(package,
						  "x-powers,ac300-ephy-package");
	if (!selectable && !fixed_ac300 &&
	    !ofnode_device_is_compatible(package,
					 "x-powers,ac200-ephy-package"))
		return -ENODEV;

	ret = acx00_ephy_read_configuration(package, configuration, present);
	if (ret)
		return ret;
	if ((selectable || fixed_ac300) && !*present)
		return -EINVAL;

	if (selectable)
		*variant = *configuration & ACX00_EPHY_CONFIG_VARIANT_AC300 ?
			ACX00_VARIANT_AC300 : ACX00_VARIANT_AC200;
	else
		*variant = fixed_ac300 ? ACX00_VARIANT_AC300 :
			ACX00_VARIANT_AC200;

	if (!selectable && *present &&
	    !!(*configuration & ACX00_EPHY_CONFIG_VARIANT_AC300) !=
	    (*variant == ACX00_VARIANT_AC300))
		return -EINVAL;

	return 0;
}

static bool acx00_ephy_node_needs_probe(ofnode node)
{
	const char *status;

	status = ofnode_read_string(node, "status");

	return status && !strcmp(status, "fail-needs-probe");
}

static int acx00_ephy_enable_path(ofnode node)
{
	char path[256];
	ofnode parent;
	int ret;

	parent = ofnode_get_parent(node);
	if (ofnode_valid(parent)) {
		ret = acx00_ephy_enable_path(parent);
		if (ret)
			return ret;
	}

	if (ofnode_is_enabled(node))
		return 0;
	if (!acx00_ephy_node_needs_probe(node))
		return -ENODEV;

	ret = ofnode_get_path(node, path, sizeof(path));
	if (ret)
		return ret;

	return dev_enable_by_path(path);
}

static ulong acx00_ephy_get_clock_rate(ofnode node, struct clk *clk,
				       bool *acquired)
{
	ulong rate;
	int ret;

	*acquired = false;
	ret = clk_get_by_index_nodev(node, 0, clk);
	if (ret)
		return ACX00_EPHY_FALLBACK_CLK_RATE;
	*acquired = true;

	rate = clk_get_rate(clk);
	if (IS_ERR_VALUE(rate))
		return ACX00_EPHY_FALLBACK_CLK_RATE;

	return rate;
}

static int acx00_ephy_get_regulator(ofnode package, struct udevice **vcc)
{
	struct ofnode_phandle_args args;
	int ret;

	ret = ofnode_parse_phandle_with_args(package, "vcc-supply", NULL, 0,
					     0, &args);
	if (ret)
		return ret;

	return uclass_get_device_by_ofnode(UCLASS_REGULATOR, args.node, vcc);
}

static u16 ac200_ephy_config(const struct acx00_ephy_priv *priv)
{
	return priv->control_config |
		(priv->phydev->interface == PHY_INTERFACE_MODE_RMII ?
		 AC200_EPHY_RMII_SEL : 0) |
		FIELD_PREP(AC200_EPHY_PHY_ADDR_MASK, priv->phydev->addr);
}

static int ac200_ephy_power_on(struct acx00_ephy_priv *priv)
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

static int ac200_ephy_power_off(struct acx00_ephy_priv *priv)
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

static int ac200_ephy_init(struct acx00_ephy_priv *priv, ofnode package,
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

	ret = acx00_ephy_enable_path(args.node);
	if (ret)
		return ret;

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

static int ac300_ephy_power_off(struct acx00_ephy_priv *priv)
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

static int ac300_ephy_power_on(struct acx00_ephy_priv *priv)
{
	u8 package_status;
	int sys_control;
	int ret;

	if (priv->powered)
		return 0;
	if (priv->clk_acquired && !priv->clk_enabled) {
		ret = clk_enable(&priv->clk);
		if (ret && ret != -ENOSYS)
			return ret;
		if (!ret)
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

static int ac300_ephy_init(struct acx00_ephy_priv *priv, ofnode package,
			   u8 calibration)
{
	ulong clock_rate;
	u8 code;

	if (priv->phydev->addr > FIELD_MAX(AC300_PACKAGE_PHY_ADDR_MASK))
		return -EINVAL;

	clock_rate = acx00_ephy_get_clock_rate(package, &priv->clk, &priv->clk_acquired);
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

static int acx00_ephy_power_off(struct acx00_ephy_priv *priv)
{
	int supply_ret = 0;
	int ret;

	if (priv->variant == ACX00_VARIANT_AC300)
		ret = ac300_ephy_power_off(priv);
	else
		ret = ac200_ephy_power_off(priv);

	if (priv->supply_enabled) {
		supply_ret = regulator_set_enable_if_allowed(priv->vcc, false);
		if (!supply_ret)
			priv->supply_enabled = false;
	}

	return ret ? ret : supply_ret;
}

static int acx00_ephy_power_on(struct acx00_ephy_priv *priv)
{
	int ret;

	if (!priv->supply_enabled) {
		ret = regulator_set_enable_if_allowed(priv->vcc, true);
		if (ret)
			return ret;
		priv->supply_enabled = true;
		mdelay(10);
	}

	if (priv->variant == ACX00_VARIANT_AC300)
		ret = ac300_ephy_power_on(priv);
	else
		ret = ac200_ephy_power_on(priv);
	if (ret)
		acx00_ephy_power_off(priv);

	return ret;
}

static int acx00_ephy_vendor_config(struct acx00_ephy_priv *priv)
{
	struct phy_device *phydev = priv->phydev;
	u16 afe_eq_rx_detect;
	u16 tx_level;
	u16 global_config;
	int oldpage;
	int ret = 0;

	afe_eq_rx_detect = priv->variant == ACX00_VARIANT_AC300 ?
		AC300_PAGE6_AFE_EQ_RX_DETECT_VALUE :
		AC200_PAGE6_AFE_EQ_RX_DETECT_VALUE;
	tx_level = ACX00_PAGE6_TX_LEVEL_DEFAULT_VALUE;
	global_config = FIELD_PREP(ACX00_PAGE0_MDI_MODE_MASK,
				   ACX00_PAGE0_MDI_MODE_AUTO);
	if (priv->xmii_rx_clock_inverted)
		global_config |= ACX00_PAGE0_XMII_RX_CLOCK_INVERT;

	ret = acx00_ephy_modify_paged(phydev, ACX00_PAGE_0,
				      ACX00_PAGE0_GLOBAL_CONFIG_REG,
				      ACX00_PAGE0_XMII_RX_CLOCK_INVERT |
				      ACX00_PAGE0_MDI_MODE_MASK,
				      global_config);
	if (ret)
		return ret;

	if (priv->variant == ACX00_VARIANT_AC300 &&
	    priv->use_low_calibration_tuning) {
		afe_eq_rx_detect =
			ACX00_PAGE6_AFE_EQ_RX_DETECT_LOW_CAL_VALUE;
		tx_level = ACX00_PAGE6_TX_LEVEL_LOW_CAL_VALUE;
	}

	oldpage = phy_select_page(phydev, ACX00_PAGE_1);
	if (oldpage < 0)
		goto out_restore_page;

	ret = phy_write(phydev, MDIO_DEVAD_NONE,
			ACX00_PAGE1_APS_CONTROL_REG,
			ACX00_PAGE1_APS_DISABLED_4S_VALUE);
	if (ret)
		goto out_restore_page;
	ret = phy_modify(phydev, MDIO_DEVAD_NONE,
			 ACX00_PAGE1_UAPS_CONTROL_REG,
			 ACX00_PAGE1_UAPS_ENABLE, 0);
	if (ret)
		goto out_restore_page;
	ret = phy_modify(phydev, MDIO_DEVAD_NONE,
			 ACX00_PAGE1_INTELLIGENT_EEE_CONTROL_REG,
			 ACX00_PAGE1_INTELLIGENT_EEE_ENABLE, 0);
	if (ret)
		goto out_restore_page;

	ret = acx00_ephy_write_page(phydev, ACX00_PAGE_2);
	if (ret)
		goto out_restore_page;
	ret = phy_modify(phydev, MDIO_DEVAD_NONE,
			 ACX00_PAGE2_TX_DATA_CONTROL_REG,
			 ACX00_PAGE2_10BT_FIR_SELECT_MASK, 0);
	if (ret)
		goto out_restore_page;

	ret = acx00_ephy_write_page(phydev, ACX00_PAGE_6);
	if (ret)
		goto out_restore_page;
	ret = phy_write(phydev, MDIO_DEVAD_NONE,
			ACX00_PAGE6_AFE_EQ_RX_DETECT_CONTROL_REG,
			afe_eq_rx_detect);
	if (ret)
		goto out_restore_page;
	ret = phy_write(phydev, MDIO_DEVAD_NONE,
			ACX00_PAGE6_AFE_RX_CONTROL_REG,
			ACX00_PAGE6_AFE_RX_CONTROL_VALUE);
	if (ret)
		goto out_restore_page;
	if (priv->variant == ACX00_VARIANT_AC300 &&
	    priv->use_low_calibration_tuning) {
		ret = phy_write(phydev, MDIO_DEVAD_NONE,
				ACX00_PAGE6_ADC_CONTROL_REG,
				ACX00_PAGE6_ADC_CONTROL_LOW_CAL_VALUE);
		if (ret)
			goto out_restore_page;
	}
	ret = phy_write(phydev, MDIO_DEVAD_NONE,
			ACX00_PAGE6_TX_LEVEL_REG, tx_level);
	if (ret)
		goto out_restore_page;

	ret = acx00_ephy_write_page(phydev, ACX00_PAGE_8);
	if (ret)
		goto out_restore_page;
	if (priv->variant == ACX00_VARIANT_AC300 &&
	    priv->use_low_calibration_tuning) {
		ret = phy_write(phydev, MDIO_DEVAD_NONE,
				ACX00_PAGE8_AUTO_CAL_CONTROL_REG,
				ACX00_PAGE8_AUTO_CAL_LOW_VALUE);
		if (ret)
			goto out_restore_page;
	}
	ret = phy_write(phydev, MDIO_DEVAD_NONE,
			ACX00_PAGE8_AFE_CONTROL_REG,
			ACX00_PAGE8_AFE_CONTROL_VALUE);

out_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

static int acx00_ephy_full_config(struct acx00_ephy_priv *priv)
{
	int ret;

	ret = acx00_ephy_power_on(priv);
	if (ret)
		return ret;

	ret = phy_reset(priv->phydev);
	if (ret)
		goto err_power_off;

	ret = acx00_ephy_vendor_config(priv);
	if (ret)
		goto err_power_off;

	ret = genphy_config(priv->phydev);
	if (ret)
		goto err_power_off;

	return 0;

err_power_off:
	acx00_ephy_power_off(priv);

	return ret;
}

static int acx00_ephy_init_package(struct phy_device *phydev,
				   struct acx00_ephy_priv *priv)
{
	ofnode package;
	bool has_configuration;
	u16 configuration;
	u32 base_addr;
	u8 calibration;
	int ret;

	package = ofnode_get_parent(phydev->node);
	if (!ofnode_valid(package))
		return -EINVAL;

	ret = ofnode_read_u32(package, "reg", &base_addr);
	if (ret || base_addr != phydev->addr)
		return -EINVAL;

	ret = acx00_ephy_select_variant(package, &priv->variant,
					&configuration,
					&has_configuration);
	if (ret)
		return ret;

	ret = acx00_ephy_get_regulator(package, &priv->vcc);
	if (ret)
		return ret;

	calibration = FIELD_GET(ACX00_EPHY_CONFIG_CALIBRATION_MASK,
				configuration);
	priv->use_low_calibration_tuning =
		priv->variant == ACX00_VARIANT_AC300 &&
		configuration & ACX00_EPHY_CONFIG_CALIBRATION_LOW;
	priv->xmii_rx_clock_inverted =
		ofnode_read_bool(package, "x-powers,xmii-rx-clock-inverted");

	if (priv->variant == ACX00_VARIANT_AC300)
		return ac300_ephy_init(priv, package, calibration);

	return ac200_ephy_init(priv, package, has_configuration, calibration);
}

static int acx00_ephy_config(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv;
	ofnode package;
	int ret;

	package = ofnode_get_parent(phydev->node);
	if (!ofnode_valid(package) ||
	    (!ofnode_device_is_compatible(package,
					 "x-powers,ac200-ephy-package") &&
	     !ofnode_device_is_compatible(package,
					  "x-powers,ac300-ephy-package") &&
	     !ofnode_device_is_compatible(package,
					  "x-powers,acx00-ephy-package")))
		return genphy_config(phydev);

	if (phydev->interface != PHY_INTERFACE_MODE_MII &&
	    phydev->interface != PHY_INTERFACE_MODE_RMII)
		return -EINVAL;

	if (phydev->priv)
		return acx00_ephy_full_config(phydev->priv);

	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	priv->phydev = phydev;

	ret = acx00_ephy_init_package(phydev, priv);
	if (ret)
		goto err_free;

	phydev->priv = priv;
	ret = acx00_ephy_full_config(priv);
	if (ret)
		goto err_free;

	return 0;

err_free:
	phydev->priv = NULL;
	free(priv);

	return ret;
}

static int acx00_ephy_startup(struct phy_device *phydev)
{
	struct acx00_ephy_priv *priv = phydev->priv;
	int ret;

	if (!priv)
		return genphy_startup(phydev);

	if (!priv->powered) {
		ret = acx00_ephy_full_config(priv);
		if (ret)
			return ret;
	}

	return genphy_startup(phydev);
}

static ofnode acx00_ephy_find_package(void)
{
	ofnode package;

	package = ofnode_by_compatible(ofnode_null(),
				       "x-powers,acx00-ephy-package");
	if (ofnode_valid(package))
		return package;

	package = ofnode_by_compatible(ofnode_null(),
				       "x-powers,ac200-ephy-package");
	if (ofnode_valid(package))
		return package;

	return ofnode_by_compatible(ofnode_null(),
				    "x-powers,ac300-ephy-package");
}

static int acx00_ephy_fdt_find_package(const void *fdt)
{
	int node;

	node = fdt_node_offset_by_compatible(fdt, -1,
					     "x-powers,acx00-ephy-package");
	if (node >= 0)
		return node;

	return fdt_node_offset_by_compatible(fdt, -1,
					     "x-powers,ac200-ephy-package");
}

int xpowers_acx00_fixup_fdt(void *fdt)
{
	enum acx00_variant variant;
	const fdt32_t *property;
	bool has_configuration;
	u16 configuration;
	ofnode package;
	const char *status;
	int package_offset;
	int ac200_offset;
	int length;
	int ret;

	package = acx00_ephy_find_package();
	if (!ofnode_valid(package))
		return -ENOENT;

	ret = acx00_ephy_select_variant(package, &variant, &configuration,
					&has_configuration);
	if (ret)
		return ret;
	if (variant != ACX00_VARIANT_AC200)
		return 0;

	package_offset = acx00_ephy_fdt_find_package(fdt);
	if (package_offset < 0)
		return package_offset;

	property = fdt_getprop(fdt, package_offset, "x-powers,ac200",
			       &length);
	if (!property || length < sizeof(*property))
		return -EINVAL;

	ac200_offset = fdt_node_offset_by_phandle(fdt,
						  fdt32_to_cpu(*property));
	if (ac200_offset < 0)
		return ac200_offset;

	while (ac200_offset > 0) {
		status = fdt_getprop(fdt, ac200_offset, "status", NULL);
		if (status && !strcmp(status, "fail-needs-probe")) {
			ret = fdt_setprop_string(fdt, ac200_offset,
						 "status", "okay");
			if (ret)
				return ret;
		} else if (status && strcmp(status, "okay") &&
			   strcmp(status, "ok")) {
			return -ENODEV;
		}

		ac200_offset = fdt_parent_offset(fdt, ac200_offset);
	}

	return 0;
}

U_BOOT_PHY_DRIVER(xpowers_acx00) = {
	.name = "X-Powers AC200/AC300 EPHY",
	.uid = ACX00_EPHY_ID,
	.mask = 0xfffffff0,
	.features = PHY_BASIC_FEATURES,
	.config = acx00_ephy_config,
	.startup = acx00_ephy_startup,
	.shutdown = genphy_shutdown,
	.read_page = acx00_ephy_read_page,
	.write_page = acx00_ephy_write_page,
};
