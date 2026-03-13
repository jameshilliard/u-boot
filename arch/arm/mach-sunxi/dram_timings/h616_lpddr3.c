/*
 * sun50i H616 LPDDR3 timings, as programmed by Allwinner's boot0
 *
 * The chips are probably able to be driven by a faster clock, but boot0
 * uses a more conservative timing (as usual).
 *
 * (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 * Based on H6 DDR3 timings:
 * (C) Copyright 2018,2019 Arm Ltd.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/arch/dram.h>
#include <asm/arch/cpu.h>

static const u8 h616_lpddr3_phy_init_default[H616_PHY_INIT_LEN] = {
	0x18, 0x06, 0x00, 0x05, 0x04, 0x03, 0x09, 0x02,
	0x08, 0x01, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x07,
	0x17, 0x19, 0x1a
};

static const u8 h616_lpddr3_phy_init_addr_map_1[H616_PHY_INIT_LEN] = {
	0x18, 0x00, 0x04, 0x09, 0x06, 0x05, 0x02, 0x19,
	0x17, 0x03, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x07,
	0x08, 0x01, 0x1a
};

static const u8 *h616_lpddr3_get_phy_init(void)
{
	if (IS_ENABLED(CONFIG_DRAM_SUNXI_PHY_ADDR_MAP_1))
		return h616_lpddr3_phy_init_addr_map_1;

	return h616_lpddr3_phy_init_default;
}

static void h616_lpddr3_set_timing_params(const struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	u8 tccd		= 2;
	u8 tfaw		= h616_ns_to_t(para, 50);
	u8 trrd		= max(h616_ns_to_t(para, 6), 4);
	u8 trcd		= h616_ns_to_t(para, 24);
	u8 trc		= h616_ns_to_t(para, 70);
	u8 txp		= max(h616_ns_to_t(para, 8), 3);
	u8 trtp		= max(h616_ns_to_t(para, 8), 2);
	u8 trp		= h616_ns_to_t(para, 27);
	u8 tras		= h616_ns_to_t(para, 41);
	u16 trefi	= h616_ns_to_t(para, 7800) / 64;
	u16 trfc	= h616_ns_to_t(para, 210);
	u16 txsr	= 88;

	u8 tmrw		= 5;
	u8 tmrd		= 5;
	u8 tmod		= max(h616_ns_to_t(para, 15), 12);
	u8 tcke		= max(h616_ns_to_t(para, 6), 3);
	u8 tcksrx	= max(h616_ns_to_t(para, 12), 4);
	u8 tcksre	= max(h616_ns_to_t(para, 12), 4);
	u8 tckesr	= tcke + 2;
	u8 trasmax	= (para->clk / 2) / 16;
	u8 txs		= h616_ns_to_t(para, 360) / 32;
	u8 txsdll	= 16;
	u8 txsabort	= 4;
	u8 txsfast	= 4;
	u8 tcl		= 7;
	u8 tcwl		= 4;
	u8 t_rdata_en	= 12;
	u8 t_wr_lat	= 6;

	u8 twtp		= 16;
	u8 twr2rd	= trtp + 9;
	u8 trd2wr	= 13;

	/* DRAM timing grabbed from tvbox with LPDDR3 memory */
	writel((twtp << 24) | (tfaw << 16) | (trasmax << 8) | tras,
	       &mctl_ctl->dramtmg[0]);
	writel((txp << 16) | (trtp << 8) | trc, &mctl_ctl->dramtmg[1]);
	writel((tcwl << 24) | (tcl << 16) | (trd2wr << 8) | twr2rd,
	       &mctl_ctl->dramtmg[2]);
	writel((tmrw << 20) | (tmrd << 12) | tmod, &mctl_ctl->dramtmg[3]);
	writel((trcd << 24) | (tccd << 16) | (trrd << 8) | trp,
	       &mctl_ctl->dramtmg[4]);
	writel((tcksrx << 24) | (tcksre << 16) | (tckesr << 8) | tcke,
	       &mctl_ctl->dramtmg[5]);
	/* Value suggested by ZynqMP manual and used by libdram */
	writel((txp + 2) | 0x02020000, &mctl_ctl->dramtmg[6]);
	writel((txsfast << 24) | (txsabort << 16) | (txsdll << 8) | txs,
	       &mctl_ctl->dramtmg[8]);
	writel(0x00020208, &mctl_ctl->dramtmg[9]);
	writel(0xE0C05, &mctl_ctl->dramtmg[10]);
	writel(0x440C021C, &mctl_ctl->dramtmg[11]);
	writel(8, &mctl_ctl->dramtmg[12]);
	writel(0xA100002, &mctl_ctl->dramtmg[13]);
	writel(txsr, &mctl_ctl->dramtmg[14]);

	writel(0x4f0112, &mctl_ctl->init[0]);
	writel(0x420000, &mctl_ctl->init[1]);
	writel(0xd05, &mctl_ctl->init[2]);
	writel(0x83001c, &mctl_ctl->init[3]);
	writel(0x00010000, &mctl_ctl->init[4]);

	writel(0, &mctl_ctl->dfimisc);
	clrsetbits_le32(&mctl_ctl->rankctl, 0xff0, 0x660);

	/* Configure DFI timing */
	writel(t_wr_lat | 0x2000000 | (t_rdata_en << 16) | 0x808000,
	       &mctl_ctl->dfitmg0);
	writel(0x100202, &mctl_ctl->dfitmg1);

	/* set refresh timing */
	writel((trefi << 16) | trfc, &mctl_ctl->rfshtmg);
}

static void h616_lpddr3_get_phy_cfg(const struct dram_para *para,
				    struct h616_dram_phy_cfg *phy_cfg)
{
	if (para->tpr2 & 0x100) {
		phy_cfg->training_reg14 = 12;
		phy_cfg->training_reg1c = 6;
	} else {
		phy_cfg->training_reg14 = 14;
		phy_cfg->training_reg1c = 8;
	}

	phy_cfg->write_leveling_reg0c = 4;
	phy_cfg->write_leveling_reg10 = 0x40;
	phy_cfg->dx_dri_hi = para->dx_dri;
	phy_cfg->dx_odt_lo = 0;
	phy_cfg->dx_odt_hi = para->dx_odt;
	phy_cfg->tpr6_val = (para->tpr6 >> 16) & 0xff;
	phy_cfg->phy_mode = 0x0b;
	phy_cfg->clear_phy_ctl_0x4_80 = false;
	phy_cfg->set_lpddr4_dx_odt_mode = false;
	phy_cfg->clear_read_training_regs = false;
}

static void h616_lpddr3_program_mode_registers(const struct dram_para *para,
					       struct sunxi_mctl_ctl_reg *mctl_ctl)
{
	(void)para;

	/* MR0 is read-only */
	/* MR1: nWR=14, BL8 */
	writel(0x183, &mctl_ctl->mrctrl1);
	writel(0x800000f0, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

	/* MR2: no WR leveling, WL set A, use nWR>9, nRL=14/nWL=8 */
	writel(0x21c, &mctl_ctl->mrctrl1);
	writel(0x800000f0, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

	/* MR3: 34.3 Ohm pull-up/pull-down resistor */
	writel(0x301, &mctl_ctl->mrctrl1);
	writel(0x800000f0, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
}

static void h616_lpddr3_ca_bit_delay_compensation(const struct dram_para *para,
						  const struct dram_config *config,
						  u32 val)
{
	if (para->tpr2 & 1) {
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x7a0);
		if (config->ranks == 2) {
			val = (para->tpr10 >> 11) & 0x1e;
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x79c);
		}
	} else {
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x7e8);
		if (config->ranks == 2) {
			val = (para->tpr10 >> 11) & 0x1e;
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x7f8);
		}
	}
}

const struct h616_dram_backend h616_lpddr3_backend = {
	.mstr_flags = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_LPDDR3,
	.odtcfg = 0x09020400,
	.set_com_ctl_0x50 = false,
	.get_phy_init = h616_lpddr3_get_phy_init,
	.set_timing_params = h616_lpddr3_set_timing_params,
	.get_phy_cfg = h616_lpddr3_get_phy_cfg,
	.program_mode_registers = h616_lpddr3_program_mode_registers,
	.ca_bit_delay_compensation = h616_lpddr3_ca_bit_delay_compensation,
};
