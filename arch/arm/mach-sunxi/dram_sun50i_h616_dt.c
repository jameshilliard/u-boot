// SPDX-License-Identifier: GPL-2.0+
/*
 * H616 DRAM parameter loading from the device tree
 */

#include <errno.h>
#include <vsprintf.h>
#include <asm/global_data.h>
#include <asm/arch/dram.h>
#include <linux/libfdt.h>

DECLARE_GLOBAL_DATA_PTR;

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

static int h616_get_dram_type(u32 val, enum sunxi_dram_type *type)
{
	switch (val) {
	case SUNXI_DRAM_TYPE_DDR3:
	case SUNXI_DRAM_TYPE_LPDDR3:
	case SUNXI_DRAM_TYPE_LPDDR4:
		*type = val;
		return 0;
	default:
		return -EINVAL;
	}
}

static int h616_parse_dram_para(const void *blob, int node,
				struct dram_para *para)
{
	u32 val;

	if (h616_fdt_read_u32(blob, node, "allwinner,dram-clk", &para->clk))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,dram-type", &val))
		return -EINVAL;
	if (h616_get_dram_type(val, &para->type))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,dx-odt", &para->dx_odt))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,dx-dri", &para->dx_dri))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,ca-dri", &para->ca_dri))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,odt-en", &para->odt_en))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,tpr0", &para->tpr0))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,tpr2", &para->tpr2))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,tpr6", &para->tpr6))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,tpr10", &para->tpr10))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,tpr11", &para->tpr11))
		return -EINVAL;
	if (h616_fdt_read_u32(blob, node, "allwinner,tpr12", &para->tpr12))
		return -EINVAL;

	return 0;
}

void h616_get_dram_para_dt(struct dram_para *para)
{
	const void *blob = gd->fdt_blob;
	int node, profiles, ret;

	profiles = fdt_path_offset(blob, "/dram-profiles");
	if (profiles < 0)
		panic("H616 DT DRAM profile selection failed: %d\n", profiles);

	node = fdt_subnode_offset(blob, profiles, "default");
	if (node < 0)
		panic("H616 DT DRAM profile selection failed: %d\n", node);

	ret = h616_parse_dram_para(blob, node, para);
	if (ret)
		panic("H616 DT DRAM profile selection failed: %d\n", ret);
}
