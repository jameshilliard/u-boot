/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __XPOWERS_EPHY_H
#define __XPOWERS_EPHY_H

#include <clk.h>
#include <dm/ofnode.h>
#include <phy.h>

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
	bool clk_enabled;
	bool ac300_package_known;
	bool ac300_internal_dldo;
	bool supply_enabled;
	bool powered;
};

int ac200_ephy_power_on(struct acx00_ephy_priv *priv);
int ac200_ephy_power_off(struct acx00_ephy_priv *priv);
int ac300_ephy_power_on(struct acx00_ephy_priv *priv);
int ac300_ephy_power_off(struct acx00_ephy_priv *priv);
int ac200_ephy_init(struct acx00_ephy_priv *priv, ofnode package,
		    bool has_configuration, u8 calibration);
int ac300_ephy_init(struct acx00_ephy_priv *priv, ofnode package,
		    u8 calibration);

#endif
