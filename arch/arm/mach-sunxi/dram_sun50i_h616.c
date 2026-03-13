// SPDX-License-Identifier: GPL-2.0+
/*
 * sun50i H616 platform dram controller driver
 *
 * While controller is very similar to that in H6, PHY is completely
 * unknown. That's why this driver has plenty of magic numbers. Some
 * meaning was nevertheless deduced from strings found in boot0 and
 * known meaning of some dram parameters.
 * This driver supports DDR3, LPDDR3 and LPDDR4 memory. There is no
 * DDR4 support yet.
 *
 * (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 */
#include <init.h>
#include <log.h>
#include <asm/io.h>

#define H616_GPIO_SELECT_ENABLED	IS_ENABLED(CONFIG_SUNXI_DRAM_H616_GPIO_SELECT)

#if H616_GPIO_SELECT_ENABLED
#include <asm-generic/gpio.h>
#include <asm/global_data.h>
#include <errno.h>
#include <sunxi_gpio.h>
#include <linux/libfdt.h>
#endif
#include <asm/arch/clock.h>
#include <asm/arch/dram.h>
#include <asm/arch/dram_dw_helpers.h>
#include <asm/arch/cpu.h>
#include <asm/arch/prcm.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#define H616_DRAM_STRAP_GPIO_COUNT	4

#if H616_GPIO_SELECT_ENABLED
DECLARE_GLOBAL_DATA_PTR;
#endif

enum {
	MBUS_QOS_LOWEST = 0,
	MBUS_QOS_LOW,
	MBUS_QOS_HIGH,
	MBUS_QOS_HIGHEST
};

static void mbus_configure_port(u8 port,
				bool bwlimit,
				bool priority,
				u8 qos,
				u8 waittime,
				u8 acs,
				u16 bwl0,
				u16 bwl1,
				u16 bwl2)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	const u32 cfg0 = ( (bwlimit ? (1 << 0) : 0)
			   | (priority ? (1 << 1) : 0)
			   | ((qos & 0x3) << 2)
			   | ((waittime & 0xf) << 4)
			   | ((acs & 0xff) << 8)
			   | (bwl0 << 16) );
	const u32 cfg1 = ((u32)bwl2 << 16) | (bwl1 & 0xffff);

	debug("MBUS port %d cfg0 %08x cfg1 %08x\n", port, cfg0, cfg1);
	writel_relaxed(cfg0, &mctl_com->master[port].cfg0);
	writel_relaxed(cfg1, &mctl_com->master[port].cfg1);
}

#define MBUS_CONF(port, priority, qos, acs, bwl0, bwl1, bwl2)	\
	mbus_configure_port(port, true, priority, \
			    MBUS_QOS_ ## qos, 0, acs, bwl0, bwl1, bwl2)

static void mctl_set_master_priority(void)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;

	/* enable bandwidth limit windows and set windows size 1us */
	writel(399, &mctl_com->tmr);
	writel(BIT(16), &mctl_com->bwcr);

	MBUS_CONF(0, false, HIGHEST, 0,  256,  128,  100);
	MBUS_CONF(1, false,    HIGH, 0, 1536, 1400,  256);
	MBUS_CONF(2, false, HIGHEST, 0,  512,  256,   96);
	MBUS_CONF(3, false,    HIGH, 0,  256,  100,   80);
	MBUS_CONF(4, false,    HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(5, false,    HIGH, 2,  100,   64,   32);
	MBUS_CONF(6, false,    HIGH, 2,  100,   64,   32);
	MBUS_CONF(8, false,    HIGH, 0,  256,  128,   64);
	MBUS_CONF(11, false,    HIGH, 0,  256,  128,  100);
	MBUS_CONF(14, false,    HIGH, 0, 1024,  256,   64);
	MBUS_CONF(16, false, HIGHEST, 6, 8192, 2800, 2400);
	MBUS_CONF(21, false, HIGHEST, 6, 2048,  768,  512);
	MBUS_CONF(22, false,    HIGH, 0,  256,  128,  100);
	MBUS_CONF(25, true,  HIGHEST, 0,  100,   64,   32);
	MBUS_CONF(26, false,    HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(37, false,    HIGH, 0,  256,  128,   64);
	MBUS_CONF(38, false,    HIGH, 2,  100,   64,   32);
	MBUS_CONF(39, false,    HIGH, 2, 8192, 5500, 5000);
	MBUS_CONF(40, false,    HIGH, 2,  100,   64,   32);

	dmb();
}

static void mctl_sys_init(u32 clk_rate)
{
	void * const ccm = (void *)SUNXI_CCM_BASE;
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	/* Put all DRAM-related blocks to reset state */
	clrbits_le32(ccm + CCU_H6_MBUS_CFG, MBUS_ENABLE);
	clrbits_le32(ccm + CCU_H6_MBUS_CFG, MBUS_RESET);
	clrbits_le32(ccm + CCU_H6_DRAM_GATE_RESET, BIT(GATE_SHIFT));
	udelay(5);
	clrbits_le32(ccm + CCU_H6_DRAM_GATE_RESET, BIT(RESET_SHIFT));
	clrbits_le32(ccm + CCU_H6_PLL5_CFG, CCM_PLL_CTRL_EN);
	clrbits_le32(ccm + CCU_H6_DRAM_CLK_CFG, DRAM_MOD_RESET);

	udelay(5);

	/* Set PLL5 rate to doubled DRAM clock rate */
	writel(CCM_PLL_CTRL_EN | CCM_PLL_LOCK_EN | CCM_PLL_OUT_EN |
	       CCM_PLL5_CTRL_N(clk_rate * 2 / 24), ccm + CCU_H6_PLL5_CFG);
	mctl_await_completion(ccm + CCU_H6_PLL5_CFG,
			      CCM_PLL_LOCK, CCM_PLL_LOCK);

	/* Configure DRAM mod clock */
	writel(DRAM_CLK_SRC_PLL5, ccm + CCU_H6_DRAM_CLK_CFG);
	writel(BIT(RESET_SHIFT), ccm + CCU_H6_DRAM_GATE_RESET);
	udelay(5);
	setbits_le32(ccm + CCU_H6_DRAM_GATE_RESET, BIT(GATE_SHIFT));

	/* Disable all channels */
	writel(0, &mctl_com->maer0);
	writel(0, &mctl_com->maer1);
	writel(0, &mctl_com->maer2);

	/* Configure MBUS and enable DRAM mod reset */
	setbits_le32(ccm + CCU_H6_MBUS_CFG, MBUS_RESET);
	setbits_le32(ccm + CCU_H6_MBUS_CFG, MBUS_ENABLE);

	clrbits_le32(&mctl_com->unk_0x500, BIT(25));

	setbits_le32(ccm + CCU_H6_DRAM_CLK_CFG, DRAM_MOD_RESET);
	udelay(5);

	/* Unknown hack, which enables access of mctl_ctl regs */
	writel(0x8000, &mctl_ctl->clken);
}

static void mctl_set_addrmap(const struct dram_config *config)
{
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u8 cols = config->cols;
	u8 rows = config->rows;
	u8 ranks = config->ranks;

	if (!config->bus_full_width)
		cols -= 1;

	/* Ranks */
	if (ranks == 2)
		mctl_ctl->addrmap[0] = rows + cols - 3;
	else
		mctl_ctl->addrmap[0] = 0x1F;

	/* Banks, hardcoded to 8 banks now */
	mctl_ctl->addrmap[1] = (cols - 2) | (cols - 2) << 8 | (cols - 2) << 16;

	/* Columns */
	mctl_ctl->addrmap[2] = 0;
	switch (cols) {
	case 7:
		mctl_ctl->addrmap[3] = 0x1F1F1F00;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 8:
		mctl_ctl->addrmap[3] = 0x1F1F0000;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 9:
		mctl_ctl->addrmap[3] = 0x1F000000;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 10:
		mctl_ctl->addrmap[3] = 0;
		mctl_ctl->addrmap[4] = 0x1F1F;
		break;
	case 11:
		mctl_ctl->addrmap[3] = 0;
		mctl_ctl->addrmap[4] = 0x1F00;
		break;
	case 12:
		mctl_ctl->addrmap[3] = 0;
		mctl_ctl->addrmap[4] = 0;
		break;
	default:
		panic("Unsupported DRAM configuration: column number invalid\n");
	}

	/* Rows */
	mctl_ctl->addrmap[5] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
	switch (rows) {
	case 13:
		mctl_ctl->addrmap[6] = (cols - 3) | 0x0F0F0F00;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 14:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | 0x0F0F0000;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 15:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | 0x0F000000;
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 16:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
		mctl_ctl->addrmap[7] = 0x0F0F;
		break;
	case 17:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
		mctl_ctl->addrmap[7] = (cols - 3) | 0x0F00;
		break;
	case 18:
		mctl_ctl->addrmap[6] = (cols - 3) | ((cols - 3) << 8) | ((cols - 3) << 16) | ((cols - 3) << 24);
		mctl_ctl->addrmap[7] = (cols - 3) | ((cols - 3) << 8);
		break;
	default:
		panic("Unsupported DRAM configuration: row number invalid\n");
	}

	/* Bank groups, DDR4 only */
	mctl_ctl->addrmap[8] = 0x3F3F;
}

#define MASK_BYTE(reg, nr) (((reg) >> ((nr) * 8)) & 0x1f)
static void mctl_phy_configure_odt(const struct dram_para *para,
				   const struct h616_dram_phy_cfg *phy_cfg)
{
	uint32_t val_lo, val_hi;

	/*
	 * This part should be applicable to all memory types, but is
	 * usually found in LPDDR4 bootloaders. Therefore, we will leave
	 * only for this type of memory.
	 */
	if (phy_cfg->set_lpddr4_dx_odt_mode) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x390, BIT(5), BIT(4));
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x3d0, BIT(5), BIT(4));
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x410, BIT(5), BIT(4));
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x450, BIT(5), BIT(4));
	}

	val_lo = para->dx_dri;
	val_hi = phy_cfg->dx_dri_hi;
	writel_relaxed(MASK_BYTE(val_lo, 0), SUNXI_DRAM_PHY0_BASE + 0x388);
	writel_relaxed(MASK_BYTE(val_hi, 0), SUNXI_DRAM_PHY0_BASE + 0x38c);
	writel_relaxed(MASK_BYTE(val_lo, 1), SUNXI_DRAM_PHY0_BASE + 0x3c8);
	writel_relaxed(MASK_BYTE(val_hi, 1), SUNXI_DRAM_PHY0_BASE + 0x3cc);
	writel_relaxed(MASK_BYTE(val_lo, 2), SUNXI_DRAM_PHY0_BASE + 0x408);
	writel_relaxed(MASK_BYTE(val_hi, 2), SUNXI_DRAM_PHY0_BASE + 0x40c);
	writel_relaxed(MASK_BYTE(val_lo, 3), SUNXI_DRAM_PHY0_BASE + 0x448);
	writel_relaxed(MASK_BYTE(val_hi, 3), SUNXI_DRAM_PHY0_BASE + 0x44c);

	val_lo = para->ca_dri;
	val_hi = para->ca_dri;
	writel_relaxed(MASK_BYTE(val_lo, 0), SUNXI_DRAM_PHY0_BASE + 0x340);
	writel_relaxed(MASK_BYTE(val_hi, 0), SUNXI_DRAM_PHY0_BASE + 0x344);
	writel_relaxed(MASK_BYTE(val_lo, 1), SUNXI_DRAM_PHY0_BASE + 0x348);
	writel_relaxed(MASK_BYTE(val_hi, 1), SUNXI_DRAM_PHY0_BASE + 0x34c);

	val_lo = phy_cfg->dx_odt_lo;
	val_hi = phy_cfg->dx_odt_hi;
	writel_relaxed(MASK_BYTE(val_lo, 0), SUNXI_DRAM_PHY0_BASE + 0x380);
	writel_relaxed(MASK_BYTE(val_hi, 0), SUNXI_DRAM_PHY0_BASE + 0x384);
	writel_relaxed(MASK_BYTE(val_lo, 1), SUNXI_DRAM_PHY0_BASE + 0x3c0);
	writel_relaxed(MASK_BYTE(val_hi, 1), SUNXI_DRAM_PHY0_BASE + 0x3c4);
	writel_relaxed(MASK_BYTE(val_lo, 2), SUNXI_DRAM_PHY0_BASE + 0x400);
	writel_relaxed(MASK_BYTE(val_hi, 2), SUNXI_DRAM_PHY0_BASE + 0x404);
	writel_relaxed(MASK_BYTE(val_lo, 3), SUNXI_DRAM_PHY0_BASE + 0x440);
	writel_relaxed(MASK_BYTE(val_hi, 3), SUNXI_DRAM_PHY0_BASE + 0x444);

	dmb();
}

static bool mctl_phy_write_leveling(const struct h616_dram_phy_cfg *phy_cfg,
				    const struct dram_config *config)
{
	bool result = true;
	u32 val;

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0, 0x80);

	writel(phy_cfg->write_leveling_reg0c, SUNXI_DRAM_PHY0_BASE + 0xc);
	writel(phy_cfg->write_leveling_reg10, SUNXI_DRAM_PHY0_BASE + 0x10);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

	if (config->bus_full_width)
		val = 0xf;
	else
		val = 3;

	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x188), val, val);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

	val = readl(SUNXI_DRAM_PHY0_BASE + 0x258);
	if (val == 0 || val == 0x3f)
		result = false;
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x25c);
	if (val == 0 || val == 0x3f)
		result = false;
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x318);
	if (val == 0 || val == 0x3f)
		result = false;
	val = readl(SUNXI_DRAM_PHY0_BASE + 0x31c);
	if (val == 0 || val == 0x3f)
		result = false;

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0);

	if (config->ranks == 2) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0, 0x40);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);

		if (config->bus_full_width)
			val = 0xf;
		else
			val = 3;

		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x188), val, val);

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 4);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0xc0);

	return result;
}

static bool mctl_phy_read_calibration(const struct dram_config *config)
{
	bool result = true;
	u32 val, tmp;

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x20);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

	if (config->bus_full_width)
		val = 0xf;
	else
		val = 3;

	while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val) {
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20) {
			result = false;
			break;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	if (config->ranks == 2) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30, 0x10);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);

		while ((readl(SUNXI_DRAM_PHY0_BASE + 0x184) & val) != val) {
			if (readl(SUNXI_DRAM_PHY0_BASE + 0x184) & 0x20) {
				result = false;
				break;
			}
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 1);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 0x30);

	val = readl(SUNXI_DRAM_PHY0_BASE + 0x274) & 7;
	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x26c) & 7;
	if (val < tmp)
		val = tmp;
	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x32c) & 7;
	if (val < tmp)
		val = tmp;
	tmp = readl(SUNXI_DRAM_PHY0_BASE + 0x334) & 7;
	if (val < tmp)
		val = tmp;
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x38, 0x7, (val + 2) & 7);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x20);

	return result;
}

static bool mctl_phy_read_training(const struct h616_dram_phy_cfg *phy_cfg,
				   const struct dram_config *config)
{
	u32 val1, val2, *ptr1, *ptr2;
	bool result = true;
	int i;

	if (phy_cfg->clear_read_training_regs) {
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x800);
		writel(0, SUNXI_DRAM_PHY0_BASE + 0x81c);
	}

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3, 2);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x804, 0x3f, 0xf);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x808, 0x3f, 0xf);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0xa04, 0x3f, 0xf);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0xa08, 0x3f, 0xf);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 6);
	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 1);

	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x840), 0xc, 0xc);
	if (readl(SUNXI_DRAM_PHY0_BASE + 0x840) & 3)
		result = false;

	if (config->bus_full_width) {
		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa40), 0xc, 0xc);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0xa40) & 3)
			result = false;
	}

	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x898);
	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x850);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}
	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8bc);
	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x874);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}

	if (config->bus_full_width) {
		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa98);
		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa50);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}

		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xabc);
		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa74);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 3);

	if (config->ranks == 2) {
		/* maybe last parameter should be 1? */
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3, 2);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 6);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 1);

		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x840), 0xc, 0xc);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x840) & 3)
			result = false;

		if (config->bus_full_width) {
			mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xa40), 0xc, 0xc);
			if (readl(SUNXI_DRAM_PHY0_BASE + 0xa40) & 3)
				result = false;
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 3);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 3);

	return result;
}

static bool mctl_phy_write_training(const struct dram_config *config)
{
	u32 val1, val2, *ptr1, *ptr2;
	bool result = true;
	int i;

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x134);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x138);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x19c);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x1a0);

	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc, 8);

	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
	setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x20);

	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8e0), 3, 3);
	if (readl(SUNXI_DRAM_PHY0_BASE + 0x8e0) & 0xc)
		result = false;

	if (config->bus_full_width) {
		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xae0), 3, 3);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0xae0) & 0xc)
			result = false;
	}

	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x938);
	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8f0);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}
	ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x95c);
	ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x914);
	for (i = 0; i < 9; i++) {
		val1 = readl(&ptr1[i]);
		val2 = readl(&ptr2[i]);
		if (val1 - val2 <= 6)
			result = false;
	}

	if (config->bus_full_width) {
		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xb38);
		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xaf0);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}
		ptr1 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xb5c);
		ptr2 = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xb14);
		for (i = 0; i < 9; i++) {
			val1 = readl(&ptr1[i]);
			val2 = readl(&ptr2[i]);
			if (val1 - val2 <= 6)
				result = false;
		}
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x60);

	if (config->ranks == 2) {
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc, 4);

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x20);

		mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x8e0), 3, 3);
		if (readl(SUNXI_DRAM_PHY0_BASE + 0x8e0) & 0xc)
			result = false;

		if (config->bus_full_width) {
			mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0xae0), 3, 3);
			if (readl(SUNXI_DRAM_PHY0_BASE + 0xae0) & 0xc)
				result = false;
		}

		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x60);
	}

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x198, 0xc);

	return result;
}

static void mctl_phy_bit_delay_compensation(const struct dram_para *para,
					    const struct h616_dram_phy_cfg *phy_cfg)
{
	u32 *ptr, val;
	int i;

	if (para->tpr10 & TPR10_DX_BIT_DELAY1) {
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 8, 8);
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 0x10);
		if (phy_cfg->clear_phy_ctl_0x4_80)
			clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x4, 0x80);

		if (para->tpr10 & BIT(30))
			val = para->tpr11 & 0x3f;
		else
			val = (para->tpr11 & 0xf) << 1;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x484);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 15) & 0x1e;
		else
			val = (para->tpr11 >> 15) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x4d0);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x590);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x4cc);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x58c);

		if (para->tpr10 & BIT(30))
			val = (para->tpr11 >> 8) & 0x3f;
		else
			val = (para->tpr11 >> 3) & 0x1e;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x4d8);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 19) & 0x1e;
		else
			val = (para->tpr11 >> 19) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x524);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x5e4);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x520);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x5e0);

		if (para->tpr10 & BIT(30))
			val = (para->tpr11 >> 16) & 0x3f;
		else
			val = (para->tpr11 >> 7) & 0x1e;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x604);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 23) & 0x1e;
		else
			val = (para->tpr11 >> 23) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x650);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x710);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x64c);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x70c);

		if (para->tpr10 & BIT(30))
			val = (para->tpr11 >> 24) & 0x3f;
		else
			val = (para->tpr11 >> 11) & 0x1e;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x658);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 27) & 0x1e;
		else
			val = (para->tpr11 >> 27) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x6a4);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x764);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x6a0);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x760);

		dmb();

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 1);
	}

	if (para->tpr10 & TPR10_DX_BIT_DELAY0) {
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x190, 4);

		if (para->tpr10 & BIT(30))
			val = para->tpr12 & 0x3f;
		else
			val = (para->tpr12 & 0xf) << 1;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x480);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en << 1) & 0x1e;
		else
			val = (para->tpr12 >> 15) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x528);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x5e8);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x4c8);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x588);

		if (para->tpr10 & BIT(30))
			val = (para->tpr12 >> 8) & 0x3f;
		else
			val = (para->tpr12 >> 3) & 0x1e;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x4d4);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 3) & 0x1e;
		else
			val = (para->tpr12 >> 19) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x52c);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x5ec);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x51c);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x5dc);

		if (para->tpr10 & BIT(30))
			val = (para->tpr12 >> 16) & 0x3f;
		else
			val = (para->tpr12 >> 7) & 0x1e;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x600);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 7) & 0x1e;
		else
			val = (para->tpr12 >> 23) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x6a8);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x768);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x648);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x708);

		if (para->tpr10 & BIT(30))
			val = (para->tpr12 >> 24) & 0x3f;
		else
			val = (para->tpr12 >> 11) & 0x1e;

		ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x654);
		for (i = 0; i < 9; i++) {
			writel_relaxed(val, ptr);
			writel_relaxed(val, ptr + 0x30);
			ptr += 2;
		}

		if (para->tpr10 & BIT(30))
			val = (para->odt_en >> 11) & 0x1e;
		else
			val = (para->tpr12 >> 27) & 0x1e;

		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x6ac);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x76c);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x69c);
		writel_relaxed(val, SUNXI_DRAM_PHY0_BASE + 0x75c);

		dmb();

		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x54, 0x80);
	}
}

static void
mctl_phy_ca_bit_delay_compensation(const struct h616_dram_backend *backend,
				   const struct dram_para *para,
				   const struct dram_config *config)
{
	u32 val;
	u32 *ptr;
	int i;

	if (para->tpr0 & BIT(30))
		val = (para->tpr0 >> 7) & 0x3e;
	else
		val = (para->tpr10 >> 3) & 0x1e;

	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0x780);
	for (i = 0; i < 32; i++)
		writel(val, &ptr[i]);

	val = (para->tpr10 << 1) & 0x1e;
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x7d8);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x7dc);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x7e0);
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x7f4);

	val = (para->tpr10 >> 7) & 0x1e;
	backend->ca_bit_delay_compensation(para, config, val);
}

static bool mctl_phy_init(const struct h616_dram_backend *backend,
			  const struct dram_para *para,
			  const struct dram_config *config)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	struct h616_dram_phy_cfg phy_cfg;
	const u8 *phy_init = backend->get_phy_init();
	u32 val, *ptr;
	int i;

	backend->get_phy_cfg(para, &phy_cfg);

	if (phy_cfg.clear_phy_ctl_0x4_80)
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x4,0x80);

	if (config->bus_full_width)
		val = 0xf;
	else
		val = 3;
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x3c, 0xf, val);

	writel(phy_cfg.training_reg14, SUNXI_DRAM_PHY0_BASE + 0x14);
	writel(phy_cfg.training_reg14, SUNXI_DRAM_PHY0_BASE + 0x35c);
	writel(phy_cfg.training_reg14, SUNXI_DRAM_PHY0_BASE + 0x368);
	writel(phy_cfg.training_reg14, SUNXI_DRAM_PHY0_BASE + 0x374);

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x18);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x360);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x36c);
	writel(0, SUNXI_DRAM_PHY0_BASE + 0x378);

	writel(phy_cfg.training_reg1c, SUNXI_DRAM_PHY0_BASE + 0x1c);
	writel(phy_cfg.training_reg1c, SUNXI_DRAM_PHY0_BASE + 0x364);
	writel(phy_cfg.training_reg1c, SUNXI_DRAM_PHY0_BASE + 0x370);
	writel(phy_cfg.training_reg1c, SUNXI_DRAM_PHY0_BASE + 0x37c);

	ptr = (u32 *)(SUNXI_DRAM_PHY0_BASE + 0xc0);
	for (i = 0; i < H616_PHY_INIT_LEN; i++)
		writel(phy_init[i], &ptr[i]);

	if (para->tpr10 & TPR10_CA_BIT_DELAY)
		mctl_phy_ca_bit_delay_compensation(backend, para, config);

	writel(phy_cfg.tpr6_val, SUNXI_DRAM_PHY0_BASE + 0x3dc);
	writel(phy_cfg.tpr6_val, SUNXI_DRAM_PHY0_BASE + 0x45c);

	mctl_phy_configure_odt(para, &phy_cfg);
	clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 4, 0x7, phy_cfg.phy_mode);

	if (para->clk <= 672)
		writel(0xf, SUNXI_DRAM_PHY0_BASE + 0x20);
	if (para->clk > 500) {
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x144, BIT(7));
		clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 0xe0);
	} else {
		setbits_le32(SUNXI_DRAM_PHY0_BASE + 0x144, BIT(7));
		clrsetbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 0xe0, 0x20);
	}

	clrbits_le32(&mctl_com->unk_0x500, 0x200);
	udelay(1);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x14c, 8);

	mctl_await_completion((u32 *)(SUNXI_DRAM_PHY0_BASE + 0x180), 4, 4);

	udelay(1000);

	writel(0x37, SUNXI_DRAM_PHY0_BASE + 0x58);

	writel(0, &mctl_ctl->swctl);
	setbits_le32(&mctl_ctl->dfimisc, 1);

	/* start DFI init */
	setbits_le32(&mctl_ctl->dfimisc, 0x20);
	writel(1, &mctl_ctl->swctl);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	/* poll DFI init complete */
	mctl_await_completion(&mctl_ctl->dfistat, 1, 1);
	writel(0, &mctl_ctl->swctl);
	clrbits_le32(&mctl_ctl->dfimisc, 0x20);

	clrbits_le32(&mctl_ctl->pwrctl, 0x20);
	writel(1, &mctl_ctl->swctl);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);
	mctl_await_completion(&mctl_ctl->statr, 3, 1);

	udelay(200);

	writel(0, &mctl_ctl->swctl);
	clrbits_le32(&mctl_ctl->dfimisc, 1);

	writel(1, &mctl_ctl->swctl);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);

	backend->program_mode_registers(para, mctl_ctl);

	writel(0, SUNXI_DRAM_PHY0_BASE + 0x54);

	writel(0, &mctl_ctl->swctl);
	clrbits_le32(&mctl_ctl->rfshctl3, 1);
	writel(1, &mctl_ctl->swctl);

	if (para->tpr10 & TPR10_WRITE_LEVELING) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_write_leveling(&phy_cfg, config))
				break;
		if (i == 5) {
			debug("write leveling failed!\n");
			return false;
		}
	}

	if (para->tpr10 & TPR10_READ_CALIBRATION) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_read_calibration(config))
				break;
		if (i == 5) {
			debug("read calibration failed!\n");
			return false;
		}
	}

	if (para->tpr10 & TPR10_READ_TRAINING) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_read_training(&phy_cfg, config))
				break;
		if (i == 5) {
			debug("read training failed!\n");
			return false;
		}
	}

	if (para->tpr10 & TPR10_WRITE_TRAINING) {
		for (i = 0; i < 5; i++)
			if (mctl_phy_write_training(config))
				break;
		if (i == 5) {
			debug("write training failed!\n");
			return false;
		}
	}

	mctl_phy_bit_delay_compensation(para, &phy_cfg);

	clrbits_le32(SUNXI_DRAM_PHY0_BASE + 0x60, 4);

	return true;
}

static bool mctl_ctrl_init(const struct h616_dram_backend *backend,
			   const struct dram_para *para,
			   const struct dram_config *config)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;
	u32 reg_val;

	clrsetbits_le32(&mctl_com->unk_0x500, BIT(24), 0x200);
	writel(0x8000, &mctl_ctl->clken);

	setbits_le32(&mctl_com->unk_0x008, 0xff00);

	if (backend->set_com_ctl_0x50)
		writel(1, SUNXI_DRAM_COM_BASE + 0x50);
	clrsetbits_le32(&mctl_ctl->sched[0], 0xff00, 0x3000);

	writel(0, &mctl_ctl->hwlpctl);

	setbits_le32(&mctl_com->unk_0x008, 0xff00);

	reg_val = MSTR_ACTIVE_RANKS(config->ranks);
	reg_val |= backend->mstr_flags;
	if (config->bus_full_width)
		reg_val |= MSTR_BUSWIDTH_FULL;
	else
		reg_val |= MSTR_BUSWIDTH_HALF;
	writel(BIT(31) | BIT(30) | reg_val, &mctl_ctl->mstr);

	if (config->ranks == 2)
		writel(0x0303, &mctl_ctl->odtmap);
	else
		writel(0x0201, &mctl_ctl->odtmap);

	reg_val = backend->odtcfg;
	writel(reg_val, &mctl_ctl->odtcfg);
	writel(reg_val, &mctl_ctl->unk_0x2240);
	writel(reg_val, &mctl_ctl->unk_0x3240);
	writel(reg_val, &mctl_ctl->unk_0x4240);

	writel(BIT(31), &mctl_com->cr);

	mctl_set_addrmap(config);

	backend->set_timing_params(para);

	writel(0, &mctl_ctl->pwrctl);

	setbits_le32(&mctl_ctl->dfiupd[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->zqctl[0], BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x2180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x3180, BIT(31) | BIT(30));
	setbits_le32(&mctl_ctl->unk_0x4180, BIT(31) | BIT(30));

	setbits_le32(&mctl_ctl->rfshctl3, BIT(0));
	clrbits_le32(&mctl_ctl->dfimisc, BIT(0));

	writel(0, &mctl_com->maer0);
	writel(0, &mctl_com->maer1);
	writel(0, &mctl_com->maer2);

	writel(0x20, &mctl_ctl->pwrctl);
	setbits_le32(&mctl_ctl->clken, BIT(8));

	clrsetbits_le32(&mctl_com->unk_0x500, BIT(24), 0x300);
	udelay(1);
	/* this write seems to enable PHY MMIO region */
	setbits_le32(&mctl_com->unk_0x500, BIT(24));
	udelay(1);

	if (!mctl_phy_init(backend, para, config))
		return false;

	writel(0, &mctl_ctl->swctl);
	clrbits_le32(&mctl_ctl->rfshctl3, BIT(0));

	setbits_le32(&mctl_com->unk_0x014, BIT(31));
	writel(0xffffffff, &mctl_com->maer0);
	writel(0x7ff, &mctl_com->maer1);
	writel(0xffff, &mctl_com->maer2);

	writel(1, &mctl_ctl->swctl);
	mctl_await_completion(&mctl_ctl->swstat, 1, 1);

	return true;
}

static bool h616_mctl_core_init(const struct dram_para *para,
				const struct h616_dram_backend *backend,
				const struct dram_config *config)
{
	mctl_sys_init(para->clk);

	return mctl_ctrl_init(backend, para, config);
}

bool mctl_core_init(const struct dram_para *para,
		    const struct dram_config *config)
{
	return h616_mctl_core_init(para, h616_get_dram_backend(para), config);
}

#if H616_GPIO_SELECT_ENABLED
static int h616_fdt_read_u32(const void *blob, int node, const char *prop_name,
			     u32 *val)
{
	const fdt32_t *prop;
	int len;

	prop = fdt_getprop(blob, node, prop_name, &len);
	if (!prop || len != sizeof(*prop))
		return -EINVAL;

	*val = fdt32_to_cpu(*prop);

	return 0;
}

static int h616_fdt_read_phandle_node(const void *blob, int node,
				      const char *prop_name, int *target_node)
{
	const fdt32_t *prop;
	int len;

	prop = fdt_getprop(blob, node, prop_name, &len);
	if (!prop)
		return -ENOENT;
	if (len != sizeof(*prop))
		return -EINVAL;

	*target_node = fdt_node_offset_by_phandle(blob, fdt32_to_cpu(*prop));

	return *target_node < 0 ? *target_node : 0;
}

static int h616_get_strap_gpio(u32 bank, u32 pin)
{
	if (pin >= SUNXI_GPIOS_PER_BANK)
		return -EINVAL;

	switch (bank) {
	case SUNXI_GPIO_A:
		return SUNXI_GPA(pin);
	case SUNXI_GPIO_B:
		return SUNXI_GPB(pin);
	case SUNXI_GPIO_C:
		return SUNXI_GPC(pin);
	case SUNXI_GPIO_D:
		return SUNXI_GPD(pin);
	case SUNXI_GPIO_E:
		return SUNXI_GPE(pin);
	case SUNXI_GPIO_F:
		return SUNXI_GPF(pin);
	case SUNXI_GPIO_G:
		return SUNXI_GPG(pin);
	case SUNXI_GPIO_H:
		return SUNXI_GPH(pin);
	case SUNXI_GPIO_I:
		return SUNXI_GPI(pin);
	case SUNXI_GPIO_L:
		return SUNXI_GPL(pin);
	case SUNXI_GPIO_M:
		return SUNXI_GPM(pin);
	case SUNXI_GPIO_N:
		return SUNXI_GPN(pin);
	default:
		return -EINVAL;
	}
}

static int h616_fdt_get_gpio_spec(const void *blob, int node,
				  const char *prop_name, int index,
				  u32 *bank, u32 *pin)
{
	const fdt32_t *prop;
	int entries, gpio_node, i, len, pos;
	u32 cells, phandle;

	prop = fdt_getprop(blob, node, prop_name, &len);
	if (!prop)
		return -ENOENT;
	if (len % sizeof(*prop))
		return -EINVAL;

	entries = len / sizeof(*prop);
	for (i = 0, pos = 0; pos < entries; i++) {
		phandle = fdt32_to_cpu(prop[pos++]);
		if (!phandle)
			break;

		gpio_node = fdt_node_offset_by_phandle(blob, phandle);
		if (gpio_node < 0)
			return gpio_node;
		if (h616_fdt_read_u32(blob, gpio_node, "#gpio-cells", &cells))
			return -EINVAL;
		if (cells < 2 || pos + cells > entries)
			return -EINVAL;

		if (i == index) {
			*bank = fdt32_to_cpu(prop[pos]);
			*pin = fdt32_to_cpu(prop[pos + 1]);
			return 0;
		}

		pos += cells;
	}

	return -ENOENT;
}

static int h616_parse_dram_para(const void *blob, int node,
				struct dram_para *para)
{
	int params_node;
	int ret;
	u32 val;

	params_node = node;
	ret = h616_fdt_read_phandle_node(blob, node, "allwinner,dram-params",
					 &params_node);
	if (ret && ret != -ENOENT)
		return ret;

	if (h616_fdt_read_u32(blob, params_node, "allwinner,dram-clk",
			      &para->clk))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,dram-type", &val))
		return -EINVAL;

	switch (val) {
	case SUNXI_DRAM_TYPE_DDR3:
	case SUNXI_DRAM_TYPE_LPDDR3:
	case SUNXI_DRAM_TYPE_LPDDR4:
		para->type = val;
		break;
	default:
		return -EINVAL;
	}

	if (h616_fdt_read_u32(blob, params_node, "allwinner,dx-odt",
			      &para->dx_odt))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,dx-dri",
			      &para->dx_dri))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,ca-dri",
			      &para->ca_dri))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,odt-en",
			      &para->odt_en))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,tpr0",
			      &para->tpr0))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,tpr2",
			      &para->tpr2))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,tpr6",
			      &para->tpr6))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,tpr10",
			      &para->tpr10))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,tpr11",
			      &para->tpr11))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, params_node, "allwinner,tpr12",
			      &para->tpr12))
		return -EINVAL;

	return 0;
}

static int h616_load_dram_profile(u32 profile_id, struct dram_para *para)
{
	const void *blob = gd->fdt_blob;
	int node, profiles;
	u32 reg;

	profiles = fdt_path_offset(blob, "/dram-profiles");
	if (profiles < 0)
		return profiles;

	for (node = fdt_first_subnode(blob, profiles);
	     node >= 0;
	     node = fdt_next_subnode(blob, node)) {
		if (h616_fdt_read_u32(blob, node, "reg", &reg))
			continue;
		if (reg != profile_id)
			continue;

		return h616_parse_dram_para(blob, node, para);
	}

	return -ENOENT;
}

static int h616_get_dram_profile_id(u32 *profile_id)
{
	const void *blob = gd->fdt_blob;
	int gpio, i, profiles, ret, value;
	u32 bank, pin;

	profiles = fdt_path_offset(blob, "/dram-profiles");
	if (profiles < 0)
		return profiles;

	*profile_id = 0;
	for (i = 0; i < H616_DRAM_STRAP_GPIO_COUNT; i++) {
		ret = h616_fdt_get_gpio_spec(blob, profiles,
					     "allwinner,dram-coding-gpios",
					     i, &bank, &pin);
		if (ret == -ENOENT)
			return i ? 0 : -ENOENT;
		if (ret)
			return ret;

		gpio = h616_get_strap_gpio(bank, pin);
		if (gpio < 0)
			return gpio;

		ret = gpio_request(gpio, "h616_dram_sel");
		if (ret)
			return ret;

		ret = gpio_direction_input(gpio);
		if (ret) {
			gpio_free(gpio);
			return ret;
		}

		value = gpio_get_value(gpio);
		gpio_free(gpio);
		if (value < 0)
			return value;

		*profile_id |= !!value << i;
	}

	return 0;
}

static void h616_get_dram_para(struct dram_para *para)
{
	u32 profile_id;
	int ret;

	ret = h616_get_dram_profile_id(&profile_id);
	if (ret)
		panic("H616 GPIO DRAM profile selection could not determine a profile\n");

	ret = h616_load_dram_profile(profile_id, para);
	if (ret)
		panic("H616 GPIO DRAM profile selection failed to load profile %u\n",
		      profile_id);
}
#else
static enum sunxi_dram_type h616_get_fixed_dram_type(void)
{
#ifdef CONFIG_SUNXI_DRAM_H616_DDR3_1333
	return SUNXI_DRAM_TYPE_DDR3;
#elif defined(CONFIG_SUNXI_DRAM_H616_LPDDR3)
	return SUNXI_DRAM_TYPE_LPDDR3;
#elif defined(CONFIG_SUNXI_DRAM_H616_LPDDR4)
	return SUNXI_DRAM_TYPE_LPDDR4;
#endif
	panic("No fixed H616 DRAM type selected\n");
}

static void h616_get_dram_para(struct dram_para *para)
{
	*para = (struct dram_para) {
		.clk = CONFIG_DRAM_CLK,
		.type = h616_get_fixed_dram_type(),
		.dx_odt = CONFIG_DRAM_SUNXI_DX_ODT,
		.dx_dri = CONFIG_DRAM_SUNXI_DX_DRI,
		.ca_dri = CONFIG_DRAM_SUNXI_CA_DRI,
		.odt_en = CONFIG_DRAM_SUNXI_ODT_EN,
		.tpr0 = CONFIG_DRAM_SUNXI_TPR0,
		.tpr2 = CONFIG_DRAM_SUNXI_TPR2,
		.tpr6 = CONFIG_DRAM_SUNXI_TPR6,
		.tpr10 = CONFIG_DRAM_SUNXI_TPR10,
		.tpr11 = CONFIG_DRAM_SUNXI_TPR11,
		.tpr12 = CONFIG_DRAM_SUNXI_TPR12,
	};
}
#endif

unsigned long sunxi_dram_init(void)
{
	void *const prcm = (void *)SUNXI_PRCM_BASE;
	struct dram_para para;
	struct dram_config config;
	unsigned long size;

	h616_get_dram_para(&para);

	setbits_le32(prcm + CCU_PRCM_RES_CAL_CTRL, BIT(8));
	clrbits_le32(prcm + CCU_PRCM_OHMS240, 0x3f);

	mctl_auto_detect_rank_width(&para, &config);
	mctl_auto_detect_dram_size(&para, &config);

	mctl_core_init(&para, &config);

	size = mctl_calc_size(&config);

	mctl_set_master_priority();

	return size;
};
