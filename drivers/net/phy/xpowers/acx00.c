// SPDX-License-Identifier: GPL-2.0-only
/*
 * X-Powers AC200/AC300 Ethernet PHY driver
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (c) 2022 Arm Ltd. (Andre Przywara <andre.przywara@arm.com>)
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <dm.h>
#include <malloc.h>
#include <nvmem.h>
#include <phy.h>
#include <power/regulator.h>
#include <linux/bitfield.h>
#include <linux/delay.h>

#include "acx00.h"

#define ACX00_EPHY_ID				0x00441400

#define ACX00_EPHY_CONFIG_CALIBRATION_MASK	GENMASK(3, 0)
#define ACX00_EPHY_CONFIG_VARIANT_AC300		BIT(8)
#define ACX00_EPHY_CONFIG_CALIBRATION_LOW	BIT(9)

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
					 bool *present, size_t min_len)
{
	struct nvmem_cell cell;
	u8 buf[sizeof(*configuration)] = {};
	int ret;

	*present = ofnode_stringlist_search(package, "nvmem-cell-names",
					   "configuration") >= 0;
	if (!*present) {
		*configuration = 0;
		return 0;
	}

	ret = nvmem_cell_get_by_name_nodev(package, "configuration", &cell);
	if (ret)
		return ret;
	if (cell.size < min_len || cell.size > sizeof(buf))
		return cell.size > sizeof(buf) ? -ERANGE : -EINVAL;

	ret = nvmem_cell_read(&cell, buf, cell.size);
	if (ret)
		return ret;

	*configuration = buf[0];
	if (cell.size == sizeof(buf))
		*configuration |= (u16)buf[1] << 8;

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

	ret = acx00_ephy_read_configuration(package, configuration, present,
					    selectable || fixed_ac300 ? 2 : 1);
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

	/* Reset can be acknowledged in power-down without restarting the PHY. */
	ret = phy_modify(priv->phydev, MDIO_DEVAD_NONE, MII_BMCR, BMCR_PDOWN, 0);
	if (ret)
		goto err_power_off;

	ret = phy_reset(priv->phydev);
	if (ret)
		goto err_power_off;

	ret = acx00_ephy_vendor_config(priv);
	if (ret)
		goto err_power_off;

	ret = genphy_config_aneg(priv->phydev);
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

	if (!ofnode_valid(phydev->node))
		return genphy_config(phydev);
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

	if (phydev->priv) {
		/* Apply a newly selected MII/RMII interface before resetting. */
		ret = acx00_ephy_power_off(phydev->priv);
		if (ret)
			return ret;
		return acx00_ephy_full_config(phydev->priv);
	}

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

U_BOOT_PHY_DRIVER(xpowers_acx00) = {
	.name = "X-Powers AC200/AC300 EPHY",
	.uid = ACX00_EPHY_ID,
	.mask = 0xfffffff0,
	.features = PHY_BASIC_FEATURES,
	.config = acx00_ephy_config,
	.startup = acx00_ephy_startup,
	/* The MAC may need the PHY's RMII clock before its next startup call. */
	.shutdown = genphy_shutdown,
	.read_page = acx00_ephy_read_page,
	.write_page = acx00_ephy_write_page,
};
