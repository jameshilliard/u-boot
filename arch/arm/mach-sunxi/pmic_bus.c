// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2015 Hans de Goede <hdegoede@redhat.com>
 *
 * Sunxi PMIC bus access helpers
 *
 * The axp152 & axp209 use an i2c bus, the axp221 uses the p2wi bus and the
 * axp223 uses the rsb bus, these functions abstract this.
 */

#include <axp_pmic.h>
#include <dm.h>
#include <dm/ofnode.h>
#include <dm/uclass.h>
#include <errno.h>
#include <asm/arch/p2wi.h>
#include <asm/arch/rsb.h>
#include <i2c.h>
#include <power/pmic.h>
#include <asm/arch/pmic_bus.h>

#define AXP221_CHIP_ADDR		0x68

#if CONFIG_IS_ENABLED(PMIC_AXP)
static struct udevice *pmic;
#endif

#if !CONFIG_IS_ENABLED(PMIC_AXP) && CONFIG_IS_ENABLED(DM_I2C)
static int pmic_bus_get_i2c_bus(struct udevice **busp, ofnode *nodep)
{
	static const char * const compatibles[] = {
#if defined(CONFIG_AXP152_POWER)
		"x-powers,axp152",
#endif
#if defined(CONFIG_AXP209_POWER)
		"x-powers,axp202",
		"x-powers,axp209",
#endif
#if defined(CONFIG_AXP221_POWER)
		"x-powers,axp221",
		"x-powers,axp223",
#endif
#if defined(CONFIG_AXP305_POWER)
		"x-powers,axp305",
#endif
#if defined(CONFIG_AXP313_POWER)
		"x-powers,axp313a",
		"x-powers,axp323",
#endif
#if defined(CONFIG_AXP318W_POWER)
		"x-powers,axp318w",
#endif
#if defined(CONFIG_AXP717_POWER)
		"x-powers,axp717",
#endif
#if defined(CONFIG_AXP803_POWER)
		"x-powers,axp803",
#endif
#if defined(CONFIG_AXP809_POWER)
		"x-powers,axp809",
		"x-powers,axp806",
#endif
#if defined(CONFIG_AXP818_POWER)
		"x-powers,axp818",
		"x-powers,axp813",
#endif
	};
	struct udevice *bus;
	ofnode node;
	int i, ret;

	for (i = 0, node = ofnode_null(); i < ARRAY_SIZE(compatibles); i++) {
		node = ofnode_by_compatible(ofnode_null(), compatibles[i]);
		if (ofnode_valid(node))
			break;
	}

	if (!ofnode_valid(node))
		return -ENODEV;

	ret = uclass_get_device_by_ofnode(UCLASS_I2C, ofnode_get_parent(node),
					  &bus);
	if (ret)
		return ret;

	*busp = bus;
	if (nodep)
		*nodep = node;

	return 0;
}

static int pmic_bus_get_i2c_chip(struct udevice **devp)
{
	struct udevice *bus;
	ofnode node;
	u32 addr;
	int ret;

	ret = pmic_bus_get_i2c_bus(&bus, &node);
	if (ret)
		return ret;

	ret = ofnode_read_u32(node, "reg", &addr);
	if (ret)
		return ret;

	return i2c_get_chip(bus, addr, 1, devp);
}

static int pmic_bus_init_i2c(void)
{
	struct udevice *bus;

	return pmic_bus_get_i2c_bus(&bus, NULL);
}
#else
static int __maybe_unused pmic_bus_init_i2c(void)
{
	return -ENODEV;
}
#endif

int pmic_bus_init(void)
{
	/* This cannot be 0 because it is used in SPL before BSS is ready */
	static int needs_init = 1;
	int ret = 0;

	if (!needs_init)
		return 0;

#if CONFIG_IS_ENABLED(PMIC_AXP)
	ret = uclass_get_device_by_driver(UCLASS_PMIC, DM_DRIVER_GET(axp_pmic),
					  &pmic);
#else
	if (IS_ENABLED(CONFIG_SYS_I2C_SUN6I_P2WI)) {
		p2wi_init();
		ret = p2wi_change_to_p2wi_mode(AXP221_CHIP_ADDR,
					       AXP_PMIC_MODE_REG,
					       AXP_PMIC_MODE_P2WI);
	} else if (IS_ENABLED(CONFIG_SYS_I2C_SUN8I_RSB)) {
		ret = rsb_init();
		if (ret)
			return ret;

		ret = rsb_set_device_address(AXP_PMIC_PRI_DEVICE_ADDR,
					     AXP_PMIC_PRI_RUNTIME_ADDR);
	} else if (CONFIG_IS_ENABLED(DM_I2C)) {
		ret = pmic_bus_init_i2c();
	}
#endif

	needs_init = ret;

	return ret;
}

int pmic_bus_read(u8 reg, u8 *data)
{
#if CONFIG_IS_ENABLED(PMIC_AXP)
	return pmic_read(pmic, reg, data, 1);
#else
#if CONFIG_IS_ENABLED(DM_I2C)
	struct udevice *dev;
	int ret;
#endif

	if (IS_ENABLED(CONFIG_SYS_I2C_SUN6I_P2WI))
		return p2wi_read(reg, data);
	if (IS_ENABLED(CONFIG_SYS_I2C_SUN8I_RSB))
		return rsb_read(AXP_PMIC_PRI_RUNTIME_ADDR, reg, data);

#if CONFIG_IS_ENABLED(DM_I2C)
	ret = pmic_bus_get_i2c_chip(&dev);
	if (ret)
		return ret;

	return dm_i2c_read(dev, reg, data, 1);
#else
	return i2c_read(CONFIG_AXP_I2C_ADDRESS, reg, 1, data, 1);
#endif
#endif
}

int pmic_bus_write(u8 reg, u8 data)
{
#if CONFIG_IS_ENABLED(PMIC_AXP)
	return pmic_write(pmic, reg, &data, 1);
#else
#if CONFIG_IS_ENABLED(DM_I2C)
	struct udevice *dev;
	int ret;
#endif

	if (IS_ENABLED(CONFIG_SYS_I2C_SUN6I_P2WI))
		return p2wi_write(reg, data);
	if (IS_ENABLED(CONFIG_SYS_I2C_SUN8I_RSB))
		return rsb_write(AXP_PMIC_PRI_RUNTIME_ADDR, reg, data);

#if CONFIG_IS_ENABLED(DM_I2C)
	ret = pmic_bus_get_i2c_chip(&dev);
	if (ret)
		return ret;

	return dm_i2c_write(dev, reg, &data, 1);
#else
	return i2c_write(CONFIG_AXP_I2C_ADDRESS, reg, 1, &data, 1);
#endif
#endif
}

int pmic_bus_setbits(u8 reg, u8 bits)
{
	int ret;
	u8 val;

	ret = pmic_bus_read(reg, &val);
	if (ret)
		return ret;

	if ((val & bits) == bits)
		return 0;

	val |= bits;
	return pmic_bus_write(reg, val);
}

int pmic_bus_clrbits(u8 reg, u8 bits)
{
	int ret;
	u8 val;

	ret = pmic_bus_read(reg, &val);
	if (ret)
		return ret;

	if (!(val & bits))
		return 0;

	val &= ~bits;
	return pmic_bus_write(reg, val);
}
