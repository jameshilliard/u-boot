/*
 * sun50i H616 DDR3-1333 timings, as programmed by Allwinner's boot0
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

static const u8 h616_ddr3_phy_init_default[H616_PHY_INIT_LEN] = {
	0x07, 0x0b, 0x02, 0x16, 0x0d, 0x0e, 0x14, 0x19,
	0x0a, 0x15, 0x03, 0x13, 0x04, 0x0c, 0x10, 0x06,
	0x0f, 0x11, 0x1a, 0x01, 0x12, 0x17, 0x00, 0x08,
	0x09, 0x05, 0x18
};

static const u8 h616_ddr3_phy_init_addr_map_1[H616_PHY_INIT_LEN] = {
	0x08, 0x02, 0x12, 0x05, 0x15, 0x17, 0x18, 0x0b,
	0x14, 0x07, 0x04, 0x13, 0x0c, 0x00, 0x16, 0x1a,
	0x0a, 0x11, 0x03, 0x10, 0x0e, 0x01, 0x0d, 0x19,
	0x06, 0x09, 0x0f
};

static const u8 *h616_ddr3_get_phy_init(void)
{
	if (IS_ENABLED(CONFIG_DRAM_SUNXI_PHY_ADDR_MAP_1))
		return h616_ddr3_phy_init_addr_map_1;

	return h616_ddr3_phy_init_default;
}

static void h616_ddr3_set_timing_params(const struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	u8 tccd		= 2;			/* JEDEC: 4nCK */
	u8 tfaw		= h616_ns_to_t(para, 50);    /* JEDEC: 30 ns w/ 1K pages */
	u8 trrd		= max(h616_ns_to_t(para, 6), 4); /* JEDEC: max(6 ns, 4nCK) */
	u8 trcd		= h616_ns_to_t(para, 15);    /* JEDEC: 13.5 ns */
	u8 trc		= h616_ns_to_t(para, 53);    /* JEDEC: 49.5 ns */
	u8 txp		= max(h616_ns_to_t(para, 6), 3); /* JEDEC: max(6 ns, 3nCK) */
	u8 trtp		= max(h616_ns_to_t(para, 8), 2); /* JEDEC: max(7.5 ns, 4nCK) */
	u8 trp		= h616_ns_to_t(para, 15);    /* JEDEC: >= 13.75 ns */
	u8 tras		= h616_ns_to_t(para, 38);    /* JEDEC >= 36 ns, <= 9*trefi */
	u16 trefi	= h616_ns_to_t(para, 7800) / 32; /* JEDEC: 7.8us@Tcase <= 85C */
	u16 trfc	= h616_ns_to_t(para, 350);   /* JEDEC: 160 ns for 2Gb */
	u16 txsr	= 4;			/* ? */

	u8 tmrw		= 0;			/* ? */
	u8 tmrd		= 4;			/* JEDEC: 4nCK */
	u8 tmod		= max(h616_ns_to_t(para, 15), 12); /* JEDEC: max(15 ns, 12nCK) */
	u8 tcke		= max(h616_ns_to_t(para, 6), 3);   /* JEDEC: max(5.625 ns, 3nCK) */
	u8 tcksrx	= max(h616_ns_to_t(para, 10), 4);  /* JEDEC: max(10 ns, 5nCK) */
	u8 tcksre	= max(h616_ns_to_t(para, 10), 4);  /* JEDEC: max(10 ns, 5nCK) */
	u8 tckesr	= tcke + 1;		/* JEDEC: tCKE(min) + 1nCK */
	u8 trasmax	= (para->clk / 2) / 15;	/* JEDEC: tREFI * 9 */
	u8 txs		= h616_ns_to_t(para, 360) / 32; /* JEDEC: max(5nCK,tRFC+10ns) */
	u8 txsdll	= 16;			/* JEDEC: 512 nCK */
	u8 txsabort	= 4;			/* ? */
	u8 txsfast	= 4;			/* ? */
	u8 tcl		= 7;			/* JEDEC: CL / 2 => 6 */
	u8 tcwl		= 5;			/* JEDEC: 8 */
	u8 t_rdata_en	= 9;			/* ? */
	u8 t_wr_lat	= 5;			/* ? */

	u8 twtp;				/* (WL + BL / 2 + tWR) / 2 */
	u8 twr2rd;				/* (WL + BL / 2 + tWTR) / 2 */
	u8 trd2wr;				/* (RL + BL / 2 + 2 - WL) / 2 */

	if (para->tpr2 & 0x100) {
		tcl = 5;
		tcwl = 4;
		t_rdata_en = 5;
		t_wr_lat = 3;
	}

	twtp   = tcl + 2 + tcwl;
	twr2rd = trtp + 2 + tcwl;
	trd2wr = tcl + 3 - tcwl;

	/* set DRAM timing */
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

	clrbits_le32(&mctl_ctl->init[0], 3 << 30);
	writel(0x420000, &mctl_ctl->init[1]);
	writel(5, &mctl_ctl->init[2]);
	writel(0x1f140004, &mctl_ctl->init[3]);
	writel(0x00200000, &mctl_ctl->init[4]);

	writel(0, &mctl_ctl->dfimisc);
	clrsetbits_le32(&mctl_ctl->rankctl, 0xff0, 0x660);

	/* Configure DFI timing */
	writel(t_wr_lat | 0x2000000 | (t_rdata_en << 16) | 0x808000,
	       &mctl_ctl->dfitmg0);
	writel(0x100202, &mctl_ctl->dfitmg1);

	/* set refresh timing */
	writel((trefi << 16) | trfc, &mctl_ctl->rfshtmg);
}

static void h616_ddr3_get_phy_cfg(const struct dram_para *para,
				  struct h616_dram_phy_cfg *phy_cfg)
{
	if (para->tpr2 & 0x100) {
		phy_cfg->training_reg14 = 9;
		phy_cfg->training_reg1c = 7;
	} else {
		phy_cfg->training_reg14 = 13;
		phy_cfg->training_reg1c = 9;
	}

	phy_cfg->write_leveling_reg0c = 4;
	phy_cfg->write_leveling_reg10 = 0x40;
	phy_cfg->dx_dri_hi = para->dx_dri;
	phy_cfg->dx_odt_lo = para->dx_odt;
	phy_cfg->dx_odt_hi = para->dx_odt;
	phy_cfg->tpr6_val = para->tpr6 & 0xff;
	phy_cfg->phy_mode = 0x0a;
	phy_cfg->clear_phy_ctl_0x4_80 = false;
	phy_cfg->set_lpddr4_dx_odt_mode = false;
	phy_cfg->clear_read_training_regs = false;
}

static void h616_ddr3_program_mode_registers(const struct dram_para *para,
					     struct sunxi_mctl_ctl_reg *mctl_ctl)
{
	u32 mr0, mr2;

	if (para->tpr2 & 0x100) {
		mr0 = 0x1b50;
		mr2 = 0x10;
	} else {
		mr0 = 0x1f14;
		mr2 = 0x20;
	}

	writel(mr0, &mctl_ctl->mrctrl1);
	writel(0x80000030, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

	writel(4, &mctl_ctl->mrctrl1);
	writel(0x80001030, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

	writel(mr2, &mctl_ctl->mrctrl1);
	writel(0x80002030, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);

	writel(0, &mctl_ctl->mrctrl1);
	writel(0x80003030, &mctl_ctl->mrctrl0);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
}

static void h616_ddr3_ca_bit_delay_compensation(const struct dram_para *para,
						const struct dram_config *config,
						u32 val)
{
	if (para->tpr2 & 1) {
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x794);
		if (config->ranks == 2) {
			val = (para->tpr10 >> 11) & 0x1e;
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x7e4);
		}
		if (para->tpr0 & BIT(31)) {
			val = (para->tpr0 << 1) & 0x3e;
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x790);
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x7b8);
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x7cc);
		}
	} else {
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x7d4);
		if (config->ranks == 2) {
			val = (para->tpr10 >> 11) & 0x1e;
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x79c);
		}
		if (para->tpr0 & BIT(31)) {
			val = (para->tpr0 << 1) & 0x3e;
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x78c);
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x7a4);
			writel(val, SUNXI_DRAM_PHY0_BASE + 0x7b8);
		}
	}
}

const struct h616_dram_backend h616_ddr3_backend = {
	.mstr_flags = MSTR_BURST_LENGTH(8) | MSTR_DEVICETYPE_DDR3 | MSTR_2TMODE,
	.odtcfg = 0x06000400,
	.set_com_ctl_0x50 = false,
	.get_phy_init = h616_ddr3_get_phy_init,
	.set_timing_params = h616_ddr3_set_timing_params,
	.get_phy_cfg = h616_ddr3_get_phy_cfg,
	.program_mode_registers = h616_ddr3_program_mode_registers,
	.ca_bit_delay_compensation = h616_ddr3_ca_bit_delay_compensation,
};
