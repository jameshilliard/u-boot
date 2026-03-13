// SPDX-License-Identifier: GPL-2.0+
/*
 * sun50i H616 DRAM backend dispatcher
 *
 * Select the appropriate H616 DRAM backend at compile time for fixed-profile
 * builds and at runtime when GPIO profile selection is enabled.
 */

#include <asm/arch/dram.h>
#include <vsprintf.h>

const struct h616_dram_backend *h616_get_dram_backend(const struct dram_para *para)
{
	if (IS_ENABLED(CONFIG_SUNXI_DRAM_H616_GPIO_SELECT)) {
		switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR3:
			return &h616_ddr3_backend;
		case SUNXI_DRAM_TYPE_LPDDR3:
			return &h616_lpddr3_backend;
		case SUNXI_DRAM_TYPE_LPDDR4:
			return &h616_lpddr4_backend;
		case SUNXI_DRAM_TYPE_DDR4:
		default:
			panic("Unsupported H616 DRAM type: %u\n", para->type);
		}
	}

	if (IS_ENABLED(CONFIG_SUNXI_DRAM_H616_DDR3_1333))
		return &h616_ddr3_backend;
	if (IS_ENABLED(CONFIG_SUNXI_DRAM_H616_LPDDR3))
		return &h616_lpddr3_backend;
	if (IS_ENABLED(CONFIG_SUNXI_DRAM_H616_LPDDR4))
		return &h616_lpddr4_backend;

	panic("No H616 DRAM backend selected\n");
	return NULL;
}
