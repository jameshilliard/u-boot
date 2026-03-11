// SPDX-License-Identifier: GPL-2.0+
/*
 * sun50i H616 DRAM timing dispatcher
 *
 * Build all H616 timing backends and select the appropriate one at runtime.
 */

#include <asm/arch/dram.h>
#include <vsprintf.h>

void mctl_set_timing_params(const struct dram_para *para)
{
	switch (para->type) {
	case SUNXI_DRAM_TYPE_DDR3:
		h616_ddr3_set_timing_params(para);
		break;
	case SUNXI_DRAM_TYPE_LPDDR3:
		h616_lpddr3_set_timing_params(para);
		break;
	case SUNXI_DRAM_TYPE_LPDDR4:
		h616_lpddr4_set_timing_params(para);
		break;
	case SUNXI_DRAM_TYPE_DDR4:
	default:
		panic("Unsupported H616 DRAM type: %u\n", para->type);
	}
}
