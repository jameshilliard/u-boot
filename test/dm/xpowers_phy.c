// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com> */

#include <dm.h>
#include <i2c.h>
#include <malloc.h>
#include <misc.h>
#include <phy.h>
#include <xpowers-ac200.h>
#include <asm/unaligned.h>
#include <dm/device-internal.h>
#include <dm/test.h>
#include <test/test.h>
#include <test/ut.h>

struct acx00_test_nvmem {
	u16 configuration;
	int error;
};

static int acx00_test_nvmem_read(struct udevice *dev, int offset, void *buf,
				 int size)
{
	struct acx00_test_nvmem *priv = dev_get_priv(dev);
	u8 data[3];

	if (priv->error)
		return priv->error;
	if (offset < 0 || size < 0 || size > sizeof(data) ||
	    offset > sizeof(data) - size)
		return -EINVAL;
	put_unaligned_le16(priv->configuration, data);
	data[2] = data[0];
	memcpy(buf, data + offset, size);
	return size;
}

static const struct misc_ops acx00_test_nvmem_ops = {
	.read = acx00_test_nvmem_read,
};

static const struct udevice_id acx00_test_nvmem_ids[] = {
	{ .compatible = "sandbox,acx00-nvmem" },
	{ }
};

U_BOOT_DRIVER(acx00_test_nvmem) = {
	.name = "acx00_test_nvmem",
	.id = UCLASS_MISC,
	.of_match = acx00_test_nvmem_ids,
	.ops = &acx00_test_nvmem_ops,
	.priv_auto = sizeof(struct acx00_test_nvmem),
};

struct ac200_test_emul {
	u16 regs[0xa200 / 2];
	u8 page;
	int accesses;
	int fail_at;
};

static int ac200_test_xfer(struct udevice *dev, struct i2c_msg *msg, int nmsgs)
{
	struct ac200_test_emul *priv = dev_get_priv(dev);
	unsigned int reg;
	u16 value;

	if (++priv->accesses == priv->fail_at)
		return -EIO;
	if (!nmsgs || msg[0].flags & I2C_M_RD || !msg[0].len)
		return -EINVAL;
	reg = (priv->page << 8) | msg[0].buf[0];
	if (reg >= sizeof(priv->regs) || reg & 1)
		return -EINVAL;
	if (nmsgs == 2 && msg[0].len == 1 && msg[1].len == 2 &&
	    msg[1].flags & I2C_M_RD) {
		put_unaligned_be16(priv->regs[reg / 2], msg[1].buf);
		return 0;
	}
	if (nmsgs != 1 || msg[0].len != 3)
		return -EINVAL;
	value = get_unaligned_be16(msg[0].buf + 1);
	if (msg[0].buf[0] == 0xfe)
		priv->page = value;
	else
		priv->regs[reg / 2] = value;
	return 0;
}

static const struct dm_i2c_ops ac200_test_ops = {
	.xfer = ac200_test_xfer,
};

static const struct udevice_id ac200_test_ids[] = {
	{ .compatible = "sandbox,ac200-emul" },
	{ }
};

U_BOOT_DRIVER(ac200_test_emul) = {
	.name = "ac200_test_emul",
	.id = UCLASS_I2C_EMUL,
	.of_match = ac200_test_ids,
	.ops = &ac200_test_ops,
	.priv_auto = sizeof(struct ac200_test_emul),
};

struct acx00_test_bus {
	u16 regs[9][32];
	u16 control[32];
	u16 package_status;
	int page;
	int accesses;
	int fail_at;
	bool reset_in_powerdown;
};

static int acx00_test_read(struct mii_dev *bus, int addr, int devad, int reg)
{
	struct acx00_test_bus *priv = bus->priv;

	if (++priv->accesses == priv->fail_at)
		return -EIO;
	if (devad != MDIO_DEVAD_NONE)
		return -EINVAL;
	if (addr == 16)
		return priv->control[reg] | (reg ? 0 : priv->package_status);
	if (addr)
		return -ENODEV;
	if (reg == 0x1f)
		return priv->page << 8;
	return priv->regs[priv->page][reg];
}

static int acx00_test_write(struct mii_dev *bus, int addr, int devad, int reg,
			    u16 value)
{
	struct acx00_test_bus *priv = bus->priv;

	if (++priv->accesses == priv->fail_at)
		return -EIO;
	if (devad != MDIO_DEVAD_NONE)
		return -EINVAL;
	if (addr == 16) {
		priv->control[reg] = value;
		return 0;
	}
	if (addr)
		return -ENODEV;
	if (reg == 0x1f) {
		if (value >> 8 >= ARRAY_SIZE(priv->regs))
			return -EINVAL;
		priv->page = value >> 8;
		return 0;
	}
	if (reg == MII_BMCR && !priv->page && value & BMCR_RESET) {
		priv->reset_in_powerdown = priv->regs[0][MII_BMCR] & BMCR_PDOWN;
		value &= ~BMCR_RESET;
	}
	priv->regs[priv->page][reg] = value;
	return 0;
}

static void acx00_test_init_bus(struct mii_dev *bus, struct acx00_test_bus *priv)
{
	memset(bus, 0, sizeof(*bus));
	memset(priv, 0, sizeof(*priv));
	strcpy(bus->name, "acx00-test");
	bus->read = acx00_test_read;
	bus->write = acx00_test_write;
	bus->priv = priv;
	priv->package_status = 0xf00;
	priv->regs[0][MII_BMCR] = BMCR_PDOWN;
	priv->regs[0][MII_BMSR] = BMSR_ANEGCAPABLE | BMSR_ANEGCOMPLETE |
		BMSR_LSTATUS | BMSR_100FULL | BMSR_100HALF | BMSR_10FULL | BMSR_10HALF;
	priv->regs[0][MII_LPA] = LPA_100FULL | LPA_10FULL;
	priv->regs[1][0x13] = BIT(15);
	priv->regs[1][0x17] = BIT(3);
}

static struct phy_device *acx00_test_create(struct mii_dev *bus, const char *path)
{
	struct phy_device *phy = phy_device_create(bus, 0, 0x00441400, false);

	if (phy) {
		phy->node = ofnode_path(path);
		phy->interface = PHY_INTERFACE_MODE_RMII;
	}
	return phy;
}

static void acx00_test_free(struct phy_device *phy)
{
	/* U-Boot phylib has no remove callback; DM teardown resets the emulators. */
	phy->bus->phymap[phy->addr] = NULL;
	free(phy->priv);
	free(phy);
}

static int dm_test_acx00_ac300(struct unit_test_state *uts)
{
	struct acx00_test_bus priv;
	struct acx00_test_nvmem *config;
	struct phy_device *phy;
	struct udevice *dev;
	struct mii_dev bus;
	int i;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "acx00-nvmem", &dev));
	config = dev_get_priv(dev);
	for (i = 0; i < 4; i++) {
		acx00_test_init_bus(&bus, &priv);
		priv.package_status = i & 1 ? 0x700 : 0xf00;
		config->configuration = i & 2 ? 0x305 : 0x105;
		phy = acx00_test_create(&bus, "/acx00-test/package/ethernet-phy@0");
		ut_assertnonnull(phy);
		ut_assertok(phy_config(phy));
		ut_assert(!priv.reset_in_powerdown);
		ut_asserteq(0, priv.page);
		ut_asserteq(0x8800, priv.control[6]);
		ut_asserteq(i & 1 ? 0 : BIT(15), priv.control[2]);
		ut_asserteq(0xb7, priv.control[0]);
		ut_asserteq(0x1002, priv.regs[0][0x13]);
		ut_asserteq(0, priv.regs[1][0x13] & BIT(15));
		ut_asserteq(0, priv.regs[1][0x17] & BIT(3));
		ut_asserteq(i & 2 ? 0x7809 : 0x708b, priv.regs[6][0x14]);
		ut_asserteq(i & 2 ? 0x3533 : 0x1530, priv.regs[6][0x15]);
		ut_assertok(phy_startup(phy));
		ut_asserteq(SPEED_100, phy->speed);
		ut_assertok(phy_shutdown(phy));
		ut_asserteq(0xb7, priv.control[0]);
		ut_assertok(phy_startup(phy));
		phy->interface = PHY_INTERFACE_MODE_MII;
		ut_assertok(phy_config(phy));
		ut_asserteq(0x8000, priv.control[6]);
		acx00_test_free(phy);
	}
	return 0;
}

DM_TEST(dm_test_acx00_ac300, UTF_SCAN_FDT);

static int dm_test_acx00_clocks(struct unit_test_state *uts)
{
	const u16 controls[] = { 0xb7, 0x37, 0x77 };
	struct acx00_test_bus priv;
	struct acx00_test_nvmem *config;
	struct phy_device *phy;
	struct udevice *dev;
	struct mii_dev bus;
	ofnode package = ofnode_path("/acx00-test/package");
	ofnode clocks = ofnode_path("/acx00-test");
	u32 phandle;
	int i;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "acx00-nvmem", &dev));
	config = dev_get_priv(dev);
	config->configuration = 0x105;
	for (i = 0; i <= ARRAY_SIZE(controls); i++) {
		ut_assertok(ofnode_read_u32_index(clocks, "clocks", i, &phandle));
		ut_assertok(ofnode_write_u32(package, "clocks", phandle));
		acx00_test_init_bus(&bus, &priv);
		phy = acx00_test_create(&bus, "/acx00-test/package/ethernet-phy@0");
		ut_assertnonnull(phy);
		if (i == ARRAY_SIZE(controls)) {
			ut_asserteq(-EINVAL, phy_config(phy));
			ut_asserteq(0, priv.accesses);
		} else {
			ut_assertok(phy_config(phy));
			ut_asserteq(controls[i], priv.control[0]);
		}
		acx00_test_free(phy);
	}
	ut_assertok(ofnode_read_u32_index(clocks, "clocks", 0, &phandle));
	ut_assertok(ofnode_write_u32(package, "clocks", phandle));
	return 0;
}

DM_TEST(dm_test_acx00_clocks, UTF_SCAN_FDT);

static int dm_test_acx00_address(struct unit_test_state *uts)
{
	struct acx00_test_bus priv;
	struct acx00_test_nvmem *config;
	struct phy_device *phy;
	struct udevice *dev;
	struct mii_dev bus;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "acx00-nvmem", &dev));
	config = dev_get_priv(dev);
	config->configuration = 0x105;
	acx00_test_init_bus(&bus, &priv);
	priv.package_status = 0xe00;
	phy = acx00_test_create(&bus, "/acx00-test/package/ethernet-phy@0");
	ut_assertnonnull(phy);
	ut_asserteq(-EINVAL, phy_config(phy));
	ut_assertnull(phy->priv);
	/* The strap mismatch must be detected before touching PHY registers. */
	ut_asserteq(BMCR_PDOWN, priv.regs[0][MII_BMCR]);
	acx00_test_free(phy);
	return 0;
}

DM_TEST(dm_test_acx00_address, UTF_SCAN_FDT);

static int dm_test_acx00_invalid(struct unit_test_state *uts)
{
	struct acx00_test_bus priv;
	struct acx00_test_nvmem *config;
	struct phy_device *phy;
	struct udevice *dev;
	struct mii_dev bus;
	const char * const paths[] = {
		"/acx00-test/short-cell/ethernet-phy",
		"/acx00-test/missing-clock/ethernet-phy",
		"/acx00-test/fixed-ac300/ethernet-phy",
	};
	int i;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "acx00-nvmem", &dev));
	config = dev_get_priv(dev);
	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		acx00_test_init_bus(&bus, &priv);
		config->configuration = i == 2 ? 5 : 0x105;
		phy = acx00_test_create(&bus, paths[i]);
		ut_assertnonnull(phy);
		ut_assert(phy_config(phy) < 0);
		ut_assertnull(phy->priv);
		ut_asserteq(0, priv.accesses);
		acx00_test_free(phy);
	}
	acx00_test_init_bus(&bus, &priv);
	phy = acx00_test_create(&bus, "/acx00-test/package/ethernet-phy@0");
	ut_assertnonnull(phy);
	config->error = -EIO;
	ut_asserteq(-EIO, phy_config(phy));
	ut_asserteq(0, priv.accesses);
	config->error = 0;
	config->configuration = 0x105;
	phy->interface = PHY_INTERFACE_MODE_RGMII;
	ut_asserteq(-EINVAL, phy_config(phy));
	ut_asserteq(0, priv.accesses);
	acx00_test_free(phy);
	return 0;
}

DM_TEST(dm_test_acx00_invalid, UTF_SCAN_FDT);

static int dm_test_acx00_mdio_errors(struct unit_test_state *uts)
{
	struct acx00_test_bus priv;
	struct acx00_test_nvmem *config;
	struct phy_device *phy;
	struct udevice *dev;
	struct mii_dev bus;
	int accesses, i;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "acx00-nvmem", &dev));
	config = dev_get_priv(dev);
	config->configuration = 0x305;
	acx00_test_init_bus(&bus, &priv);
	phy = acx00_test_create(&bus, "/acx00-test/package/ethernet-phy@0");
	ut_assertnonnull(phy);
	ut_assertok(phy_config(phy));
	accesses = priv.accesses;
	acx00_test_free(phy);
	for (i = 1; i <= accesses; i++) {
		acx00_test_init_bus(&bus, &priv);
		priv.fail_at = i;
		phy = acx00_test_create(&bus, "/acx00-test/package/ethernet-phy@0");
		ut_assertnonnull(phy);
		ut_assertf(phy_config(phy) < 0, "MDIO access %d ignored its error", i);
		ut_assertnull(phy->priv);
		ut_asserteq(0, priv.control[0] & BIT(1));
		priv.fail_at = 0;
		/* Model the control reset clearing the link register page. */
		priv.page = 0;
		ut_assertok(phy_config(phy));
		acx00_test_free(phy);
	}
	return 0;
}

DM_TEST(dm_test_acx00_mdio_errors, UTF_SCAN_FDT);

static int dm_test_acx00_ac200(struct unit_test_state *uts)
{
	struct acx00_test_bus priv;
	struct acx00_test_nvmem *config;
	struct ac200_test_emul *emul;
	struct phy_device *phy;
	struct udevice *dev, *control;
	struct mii_dev bus;
	u16 value;
	int i;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_I2C_EMUL, "ac200-emul", &dev));
	emul = dev_get_priv(dev);
	emul->regs[0x8004 / 2] = 9;
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "acx00-nvmem", &dev));
	config = dev_get_priv(dev);
	config->configuration = 5;
	for (i = 0; i < 2; i++) {
		acx00_test_init_bus(&bus, &priv);
		phy = acx00_test_create(&bus, i ?
			"/acx00-test/fixed-ac200/ethernet-phy" :
			"/acx00-test/package/ethernet-phy@0");
		ut_assertnonnull(phy);
		ut_assertok(phy_config(phy));
		ut_assert(!priv.reset_in_powerdown);
		ut_asserteq(i ? 0xc804 : 0x8804, emul->regs[0x6000 / 2]);
		ut_asserteq(3, emul->regs[0x14 / 2]);
		ut_asserteq(1, emul->regs[0x16 / 2]);
		ut_asserteq(0x708f, priv.regs[6][0x14]);
		ut_assertok(phy_startup(phy));
		ut_assertok(phy_shutdown(phy));
		ut_assertok(phy_startup(phy));
		acx00_test_free(phy);
	}
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "ac200@10", &control));
	ut_assertok(ac200_write(control, 0x6000, 0x1234));
	emul->page = 0x80;
	ut_assertok(ac200_read(control, 0x6000, &value));
	ut_asserteq(0x1234, value);
	ut_asserteq(-EINVAL, ac200_write(control, 0x6001, 0));
	ut_asserteq(-EINVAL, ac200_read(control, 0xa200, &value));
	emul->fail_at = emul->accesses + 1;
	ut_asserteq(-EIO, ac200_read(control, 0x6000, &value));
	return 0;
}

DM_TEST(dm_test_acx00_ac200, UTF_SCAN_FDT);

static int dm_test_acx00_i2c_errors(struct unit_test_state *uts)
{
	struct acx00_test_bus priv;
	struct ac200_test_emul *emul;
	struct phy_device *phy;
	struct udevice *dev, *control;
	struct mii_dev bus;
	int accesses, i;

	ut_set_skip_delays(uts, true);
	ut_assertok(uclass_get_device_by_name(UCLASS_I2C_EMUL, "ac200-emul", &dev));
	emul = dev_get_priv(dev);
	acx00_test_init_bus(&bus, &priv);
	phy = acx00_test_create(&bus, "/acx00-test/fixed-ac200/ethernet-phy");
	ut_assertnonnull(phy);
	ut_assertok(phy_config(phy));
	accesses = emul->accesses;
	acx00_test_free(phy);
	ut_assertok(uclass_get_device_by_name(UCLASS_MISC, "ac200@10", &control));
	for (i = 1; i <= accesses; i++) {
		ut_assertok(device_remove(control, DM_REMOVE_NORMAL));
		memset(emul, 0, sizeof(*emul));
		emul->fail_at = i;
		acx00_test_init_bus(&bus, &priv);
		phy = acx00_test_create(&bus, "/acx00-test/fixed-ac200/ethernet-phy");
		ut_assertnonnull(phy);
		ut_assertf(phy_config(phy) < 0, "I2C access %d ignored its error", i);
		ut_assertnull(phy->priv);
		emul->fail_at = 0;
		ut_assertok(phy_config(phy));
		acx00_test_free(phy);
	}
	return 0;
}

DM_TEST(dm_test_acx00_i2c_errors, UTF_SCAN_FDT);
