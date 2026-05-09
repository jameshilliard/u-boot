/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Allwinner NAND secure storage helpers.
 */

#ifndef SUNXI_NAND_SECURE_STORAGE_H
#define SUNXI_NAND_SECURE_STORAGE_H

#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/types.h>

struct sunxi_nand_secure_storage_info {
	const char *mtd_name;
	u64 mtd_size;
	u64 spl_copy_size;
	u32 spl_copy_count;
	u64 uboot_offset;
	u64 uboot_size;
	u64 secure_offset;
	u64 secure_size;
	u64 secure_end;
};

#if IS_ENABLED(CONFIG_SUNXI_NAND_SECURE_STORAGE) && !defined(CONFIG_XPL_BUILD)
int sunxi_nand_secure_storage_locate(struct sunxi_nand_secure_storage_info *info);
#else
static inline int
sunxi_nand_secure_storage_locate(struct sunxi_nand_secure_storage_info *info)
{
	return -ENODEV;
}
#endif

#if IS_ENABLED(CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR) && \
	!defined(CONFIG_XPL_BUILD)
void sunxi_nand_secure_storage_setup_ethaddr(void);
#else
static inline void sunxi_nand_secure_storage_setup_ethaddr(void) {}
#endif

#endif
