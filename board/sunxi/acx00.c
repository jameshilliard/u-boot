// SPDX-License-Identifier: GPL-2.0+
/*
 * Board selection of the ACx00 companion and its optional I2C path.
 * Kept separate from the upstream PHY driver.
 *
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <dm.h>
#include <dm/device-internal.h>
#include <nvmem.h>
#include <phy.h>
#include <linux/bitops.h>
#include <linux/libfdt.h>
#include <xpowers-acx00.h>

#define ACX00_EPHY_CONFIG_VARIANT_AC300 BIT(8)

enum acx00_variant {
	ACX00_VARIANT_AC200,
	ACX00_VARIANT_AC300,
};

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


int board_phy_config(struct phy_device *phydev)
{
	struct ofnode_phandle_args args;
	enum acx00_variant variant;
	bool has_configuration;
	u16 configuration;
	ofnode package;
	int ret;

	if (!ofnode_valid(phydev->node))
		goto configure;
	package = ofnode_get_parent(phydev->node);
	if (!ofnode_valid(package) ||
	    (!ofnode_device_is_compatible(package, "x-powers,acx00-ephy-package") &&
	     !ofnode_device_is_compatible(package, "x-powers,ac200-ephy-package")))
		goto configure;

	ret = acx00_ephy_select_variant(package, &variant, &configuration,
					&has_configuration);
	if (ret)
		return ret;
	if (variant == ACX00_VARIANT_AC200) {
		ret = ofnode_parse_phandle_with_args(package, "x-powers,ac200",
						     NULL, 0, 0, &args);
		if (ret)
			return ret;
		ret = acx00_ephy_enable_path(args.node);
		if (ret)
			return ret;
	}
configure:
	return phydev->drv->config ? phydev->drv->config(phydev) : 0;
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
