/*
 * sun50i H616 LPDDR4-2133 timings, as programmed by Allwinner's boot0
 * for orangepi zero3 with the H618 and LPDDR4 memory.
 *
 * (C) Copyright 2023 Mikhail Kalashnikov <iuncuim@gmail.com>
 *   Based on H6 DDR3 timings:
 *   (C) Copyright 2020 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/arch/dram.h>
#include <asm/arch/cpu.h>
#include <linux/delay.h>

static const u8 h616_lpddr4_phy_init_default[H616_PHY_INIT_LEN] = {
	0x02, 0x00, 0x17, 0x05, 0x04, 0x19, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x01,
	0x18, 0x03, 0x1a
};

static const u8 h616_lpddr4_phy_init_addr_map_1[H616_PHY_INIT_LEN] = {
	0x03, 0x00, 0x17, 0x05, 0x02, 0x19, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x01,
	0x18, 0x04, 0x1a
};

static const u8 *h616_lpddr4_get_phy_init(void)
{
	if (IS_ENABLED(CONFIG_DRAM_SUNXI_PHY_ADDR_MAP_1))
		return h616_lpddr4_phy_init_addr_map_1;

	return h616_lpddr4_phy_init_default;
}

static void h616_lpddr4_set_timing_params(const struct dram_para *para)
{
	struct sunxi_mctl_ctl_reg * const mctl_ctl =
			(struct sunxi_mctl_ctl_reg *)SUNXI_DRAM_CTL0_BASE;

	u8 tccd		= 4;
	u8 tfaw		= h616_ns_to_t(para, 40);
	u8 trrd		= max(h616_ns_to_t(para, 10), 2);
	u8 trcd		= max(h616_ns_to_t(para, 18), 2);
	u8 trc		= h616_ns_to_t(para, 65);
	u8 txp		= max(h616_ns_to_t(para, 8), 2);
	u8 trtp		= 4;
	u8 trp		= h616_ns_to_t(para, 21);
	u8 tras		= h616_ns_to_t(para, 42);
	u16 trefi	= h616_ns_to_t(para, 3904) / 32;
	u16 trfc	= h616_ns_to_t(para, 280);
	u16 txsr	= h616_ns_to_t(para, 190);

	u8 tmrw		= max(h616_ns_to_t(para, 14), 5);
	u8 tmrd		= tmrw;
	u8 tmod		= 12;
	u8 tcke		= max(h616_ns_to_t(para, 15), 2);
	u8 tcksrx	= max(h616_ns_to_t(para, 2), 2);
	u8 tcksre	= max(h616_ns_to_t(para, 5), 2);
	u8 tckesr	= tcke;
	u8 trasmax	= (trefi * 9) / 32;
	u8 txs		= 4;
	u8 txsdll	= 16;
	u8 txsabort	= 4;
	u8 txsfast	= 4;
	u8 tcl		= 10;
	u8 tcwl		= 5;
	u8 t_rdata_en	= 17;
	u8 tphy_wrlat	= 5;

	u8 twtp		= 24;
	u8 twr2rd	= max(trrd, (u8)4) + 14;
	u8 trd2wr	= (h616_ns_to_t(para, 4) + 17) - h616_ns_to_t(para, 1);

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

	clrsetbits_le32(&mctl_ctl->init[0], 0xC0000FFF, 0x3f0);
	writel(0x01f20000, &mctl_ctl->init[1]);
	writel(0x00000d05, &mctl_ctl->init[2]);
	writel(0, &mctl_ctl->dfimisc);
	writel(0x0034001b, &mctl_ctl->init[3]);
	writel(0x00330000, &mctl_ctl->init[4]);
	writel(0x00040072, &mctl_ctl->init[6]);
	writel(0x00240009, &mctl_ctl->init[7]);

	clrsetbits_le32(&mctl_ctl->rankctl, 0xff0, 0x660);

	/* Configure DFI timing */
	writel(tphy_wrlat | 0x2000000 | (t_rdata_en << 16) | 0x808000,
	       &mctl_ctl->dfitmg0);
	writel(0x100202, &mctl_ctl->dfitmg1);

	/* set refresh timing */
	writel((trefi << 16) | trfc, &mctl_ctl->rfshtmg);
}

static void h616_lpddr4_get_phy_cfg(const struct dram_para *para,
				    struct h616_dram_phy_cfg *phy_cfg)
{
	phy_cfg->training_reg14 = 20;
	phy_cfg->training_reg1c = 10;
	phy_cfg->write_leveling_reg0c = 0x1b;
	phy_cfg->write_leveling_reg10 = 0;
	phy_cfg->dx_dri_hi = 0x04040404;
	phy_cfg->dx_odt_lo = para->dx_odt;
	phy_cfg->dx_odt_hi = 0;
	phy_cfg->tpr6_val = (para->tpr6 >> 24) & 0xff;
	phy_cfg->phy_mode = 0x0d;
	phy_cfg->clear_phy_ctl_0x4_80 = true;
	phy_cfg->set_lpddr4_dx_odt_mode = true;
	phy_cfg->clear_read_training_regs = true;
}

static void h616_lpddr4_mr_write(struct sunxi_mctl_ctl_reg *mctl_ctl,
				 u32 mrctrl1)
{
	writel(mrctrl1, &mctl_ctl->mrctrl1);
	udelay(10);
	writel(0x80000030, &mctl_ctl->mrctrl0);
	udelay(10);
	mctl_await_completion(&mctl_ctl->mrctrl0, BIT(31), 0);
}

static void h616_lpddr4_program_mode_registers(const struct dram_para *para,
					       struct sunxi_mctl_ctl_reg *mctl_ctl)
{
	(void)para;

	h616_lpddr4_mr_write(mctl_ctl, 0x0);
	h616_lpddr4_mr_write(mctl_ctl, 0x134);
	h616_lpddr4_mr_write(mctl_ctl, 0x21b);
	h616_lpddr4_mr_write(mctl_ctl, 0x333);
	h616_lpddr4_mr_write(mctl_ctl, 0x403);
	h616_lpddr4_mr_write(mctl_ctl, 0xb04);
	h616_lpddr4_mr_write(mctl_ctl, 0xc72);
	h616_lpddr4_mr_write(mctl_ctl, 0xe09);
	h616_lpddr4_mr_write(mctl_ctl, 0x1624);
}

static void h616_lpddr4_ca_bit_delay_compensation(const struct dram_para *para,
						  const struct dram_config *config,
						  u32 val)
{
	writel(val, SUNXI_DRAM_PHY0_BASE + 0x788);
	if (config->ranks == 2) {
		val = (para->tpr10 >> 11) & 0x1e;
		writel(val, SUNXI_DRAM_PHY0_BASE + 0x794);
	}
}

const struct h616_dram_backend h616_lpddr4_backend = {
	.mstr_flags = MSTR_BURST_LENGTH(16) | MSTR_DEVICETYPE_LPDDR4,
	.odtcfg = 0x04000400,
	.set_com_ctl_0x50 = true,
	.get_phy_init = h616_lpddr4_get_phy_init,
	.set_timing_params = h616_lpddr4_set_timing_params,
	.get_phy_cfg = h616_lpddr4_get_phy_cfg,
	.program_mode_registers = h616_lpddr4_program_mode_registers,
	.ca_bit_delay_compensation = h616_lpddr4_ca_bit_delay_compensation,
};
