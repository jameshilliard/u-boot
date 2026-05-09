// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner BSP raw NAND secure-storage support.
 *
 * The BSP stores redundant secure-storage blocks immediately after the
 * U-Boot proper area. The blocks are identified by an OOB marker and
 * their pages are read through the normal NAND ECC/randomizer path.
 */

#ifdef CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR
#include <env.h>
#include <net.h>
#include <u-boot/crc.h>
#endif
#include <asm/unaligned.h>
#include <memalign.h>
#include <nand.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "nand_secure_storage.h"

/*
 * The BSP defines MAX_SECURE_STORAGE_BLOCK_NUM as 50 and starts scanning at
 * uboot_next_block. The U-Boot range below follows the BSP
 * geometry-dependent defaults used when no stored NAND partition table is
 * present.
 */
#define SS_SCAN_END_BLOCK		50

#define SS_SMALL_NAND_MAX_BLOCK_SIZE	SZ_1M
#define SS_SMALL_NAND_MIN_BLOCK_SIZE	SZ_128K
#define SS_SMALL_NAND_ALWAYS_BLOCK_SIZE	SZ_512K
#define SS_SMALL_NAND_UBOOT_START	8
#define SS_SMALL_NAND_UBOOT_SIZE	SZ_4M
#define SS_MEDIUM_NAND_MAX_BLOCK_SIZE	SZ_2M
#define SS_BIG_NAND_UBOOT_START		4
#define SS_BIG_NAND_1M_UBOOT_BLOCKS	20
#define SS_BIG_NAND_2M_UBOOT_BLOCKS	10
#define SS_BIG_NAND_MIN_UBOOT_BLOCKS	8

/*
 * The BSP selects the 128-page SPL-copy geometry for H616/sun50iw9.
 * Only unrelated SUN8IW18 and SUN50IW11 BSP SoC families use the
 * 256-page variant.
 */
#define SS_SPL_PAGE_CNT_PER_COPY	128

#define SS_OOB_MAGIC_OFF		1
#define SS_OOB_CHECKSUM_OFF		3
#define SS_OOB_MAGIC			0xaa5c
#define SS_OOB_MAGIC_SIZE		(SS_OOB_MAGIC_OFF + sizeof(u16))
#define SS_OOB_CHECKSUM_SIZE		(SS_OOB_CHECKSUM_OFF + sizeof(u32))

#ifdef CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR
#define SS_ITEM_SIZE			SZ_4K
#define SS_EMPTY_SUM			0x1234
#define SS_ITEMS_PER_BLOCK		32
#define SS_MAP_MAX_ENTRIES		(SS_ITEMS_PER_BLOCK - 1)
#define SS_NAME_LEN			64

#define SS_STORE_MAGIC			0x17253948
#define SS_STORE_MAX_LEN		0xc00
#endif

struct ss_info {
	struct mtd_info *mtd;
	u32 first_block;
	u32 last_block;
	u32 marker_count;
	u32 page_size;
	u32 oob_size;
	u32 erase_size;
	u32 pages_per_block;
	u32 spl_copy_blocks;
	u32 spl_copy_count;
	u32 uboot_start_block;
	u32 uboot_next_block;
};

#ifdef CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR
struct ss_object {
	u32 magic;
	u32 id;
	char name[SS_NAME_LEN];
	u32 re_encrypt;
	u32 version;
	u32 write_protect;
	u32 reserved[3];
	u32 actual_len;
	u8 data[SS_STORE_MAX_LEN];
	u32 crc;
} __packed;

static u32 ss_get_le32(const void *buf, size_t offset)
{
	return get_unaligned_le32((const u8 *)buf + offset);
}
#endif

static bool ss_oob_has_magic(const u8 *oob)
{
	return get_unaligned_be16(oob + SS_OOB_MAGIC_OFF) == SS_OOB_MAGIC;
}

static int ss_read_ecc_page(struct ss_info *ss, u32 block, u32 page, void *buf,
			    u8 *oob)
{
	struct mtd_oob_ops ops = {};
	loff_t offs = (loff_t)block * ss->erase_size +
		      (loff_t)page * ss->page_size;
	int ret;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.len = ss->page_size;
	ops.ooblen = ss->oob_size;
	ops.datbuf = buf;
	ops.oobbuf = oob;

	ret = mtd_read_oob(ss->mtd, offs, &ops);
	if (ret && !mtd_is_bitflip(ret))
		return ret;

	if (ops.retlen != ss->page_size || ops.oobretlen != ss->oob_size)
		return -EIO;

	return 0;
}

#ifdef CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR
static u32 ss_page_sum(const void *buf, size_t len)
{
	const u8 *bytes = buf;
	u32 sum = SS_EMPTY_SUM;
	size_t i;

	for (i = 0; i < len / sizeof(u32); i++)
		sum += get_unaligned_le32(bytes + i * sizeof(u32));

	return sum;
}

static int ss_read_page(struct ss_info *ss, u32 block, u32 page, void *buf,
			u8 *oob, u32 len)
{
	u32 sum;
	int ret;

	ret = ss_read_ecc_page(ss, block, page, buf, oob);
	if (ret)
		return ret;

	if (!ss_oob_has_magic(oob))
		return -ENOENT;

	sum = ss_page_sum(buf, len);
	if (sum != get_unaligned_be32(oob + SS_OOB_CHECKSUM_OFF))
		return -EBADMSG;
	if (sum == SS_EMPTY_SUM)
		return -ENODATA;

	return 0;
}

static int ss_read_replicated_page(struct ss_info *ss, u32 block, u32 page,
				   void *buf, u8 *oob, u32 len)
{
	int ret = -ENOENT;

	for (; page < ss->pages_per_block; page += SS_ITEMS_PER_BLOCK) {
		ret = ss_read_page(ss, block, page, buf, oob, len);
		if (!ret)
			return 0;
	}

	return ret;
}
#endif

static bool ss_is_secure_block(struct ss_info *ss, u32 block, void *pagebuf,
			       u8 *oob)
{
	if (ss_read_ecc_page(ss, block, 0, pagebuf, oob))
		return false;

	return ss_oob_has_magic(oob);
}

static void ss_default_uboot_range(struct ss_info *ss)
{
	u32 block_size = ss->erase_size;
	u32 uboot_blocks;

	if (ss->erase_size <= SS_SMALL_NAND_ALWAYS_BLOCK_SIZE ||
	    (ss->erase_size <= SS_SMALL_NAND_MAX_BLOCK_SIZE &&
	     ss->pages_per_block <= 128)) {
		if (block_size < SS_SMALL_NAND_MIN_BLOCK_SIZE)
			block_size = SS_SMALL_NAND_MIN_BLOCK_SIZE;
		block_size = roundup_pow_of_two(block_size);

		ss->uboot_start_block = SS_SMALL_NAND_UBOOT_START;
		uboot_blocks = SS_SMALL_NAND_UBOOT_SIZE / block_size;
	} else if (ss->erase_size <= SS_SMALL_NAND_MAX_BLOCK_SIZE) {
		ss->uboot_start_block = SS_BIG_NAND_UBOOT_START;
		uboot_blocks = SS_BIG_NAND_1M_UBOOT_BLOCKS;
	} else if (ss->erase_size <= SS_MEDIUM_NAND_MAX_BLOCK_SIZE) {
		ss->uboot_start_block = SS_BIG_NAND_UBOOT_START;
		uboot_blocks = SS_BIG_NAND_2M_UBOOT_BLOCKS;
	} else {
		ss->uboot_start_block = SS_BIG_NAND_UBOOT_START;
		uboot_blocks = SS_BIG_NAND_MIN_UBOOT_BLOCKS;
	}

	ss->uboot_next_block = ss->uboot_start_block + uboot_blocks;
}

static void ss_default_spl_layout(struct ss_info *ss)
{
	ss->spl_copy_blocks =
		DIV_ROUND_UP(SS_SPL_PAGE_CNT_PER_COPY, ss->pages_per_block);

	/*
	 * The BSP write loop silently stops once the next SPL copy would
	 * overlap U-Boot proper. Keep the same floor division here.
	 */
	if (ss->spl_copy_blocks)
		ss->spl_copy_count =
			ss->uboot_start_block / ss->spl_copy_blocks;
}

static int ss_scan_blocks(struct ss_info *ss)
{
	struct mtd_info *mtd;
	u64 total_blocks;
	u32 block, end_block;
	int ret;

	nand_init();

	mtd = get_nand_dev_by_index(0);
	if (!mtd)
		return -ENODEV;
	if (!mtd->writesize || !mtd->erasesize || !mtd->oobsize)
		return -EINVAL;
	if (mtd_mod_by_ws(mtd->erasesize, mtd))
		return -EINVAL;
	if (mtd->oobsize < SS_OOB_MAGIC_SIZE)
		return -EINVAL;
	if (IS_ENABLED(CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR) &&
	    mtd->oobsize < SS_OOB_CHECKSUM_SIZE)
		return -EINVAL;
	if (!nand_is_slc(mtd_to_nand(mtd)))
		return -EOPNOTSUPP;

	memset(ss, 0, sizeof(*ss));
	ss->mtd = mtd;
	ss->page_size = mtd->writesize;
	ss->oob_size = mtd->oobsize;
	ss->erase_size = mtd->erasesize;
	ss->pages_per_block = mtd_div_by_ws(mtd->erasesize, mtd);
	ss_default_uboot_range(ss);
	ss_default_spl_layout(ss);

	ALLOC_CACHE_ALIGN_BUFFER(u8, pagebuf, ss->page_size);
	ALLOC_CACHE_ALIGN_BUFFER(u8, oob, ss->oob_size);

	total_blocks = mtd_div_by_eb(mtd->size, mtd);
	end_block = min_t(u64, total_blocks, SS_SCAN_END_BLOCK);

	if (ss->uboot_next_block >= end_block)
		return -ENOENT;

	for (block = ss->uboot_next_block; block < end_block; block++) {
		ret = mtd_block_isbad(mtd, (loff_t)block * ss->erase_size);
		if (ret > 0)
			continue;
		if (ret < 0)
			return ret;
		if (!ss_is_secure_block(ss, block, pagebuf, oob))
			continue;

		if (!ss->marker_count)
			ss->first_block = block;
		ss->last_block = block;
		ss->marker_count++;
	}

	if (!ss->marker_count)
		return -ENOENT;

	return 0;
}

static int ss_find_blocks(struct ss_info *ss)
{
	static struct ss_info cached_ss;
	static bool cached;
	int ret;

	if (cached) {
		*ss = cached_ss;
		return 0;
	}

	ret = ss_scan_blocks(ss);
	if (ret)
		return ret;

	cached_ss = *ss;
	cached = true;

	return 0;
}

int sunxi_nand_secure_storage_locate(struct sunxi_nand_secure_storage_info *info)
{
	struct ss_info ss;
	int ret;

	if (!info)
		return -EINVAL;

	ret = ss_find_blocks(&ss);
	if (ret)
		return ret;

	memset(info, 0, sizeof(*info));
	info->mtd_name = ss.mtd->name;
	info->mtd_size = ss.mtd->size;
	info->spl_copy_size = (u64)ss.spl_copy_blocks * ss.erase_size;
	info->spl_copy_count = ss.spl_copy_count;
	info->uboot_offset = (u64)ss.uboot_start_block * ss.erase_size;
	info->uboot_size = (u64)(ss.uboot_next_block - ss.uboot_start_block) *
			   ss.erase_size;
	info->secure_offset = (u64)ss.first_block * ss.erase_size;
	info->secure_size = (u64)(ss.last_block - ss.first_block + 1) *
			    ss.erase_size;
	info->secure_end = (u64)(ss.last_block + 1) * ss.erase_size;

	return 0;
}

#ifdef CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR
static int ss_read_item(struct ss_info *ss, u32 item, void *buf)
{
	u32 pages_per_item = SS_ITEM_SIZE / ss->page_size;
	u32 page_len = pages_per_item == 1 ? SS_ITEM_SIZE : ss->page_size;
	u32 page;
	int ret;

	if (ss->page_size > SS_ITEM_SIZE || SS_ITEM_SIZE % ss->page_size)
		return -EINVAL;
	if ((item + 1) * pages_per_item > ss->pages_per_block)
		return -EINVAL;

	ALLOC_CACHE_ALIGN_BUFFER(u8, oob, ss->oob_size);

	for (page = 0; page < pages_per_item; page++) {
		u8 *dst = (u8 *)buf + page * ss->page_size;
		u32 raw_page = item * pages_per_item + page;

		ret = ss_read_replicated_page(ss, ss->first_block, raw_page,
					      dst, oob, page_len);
		if (ret && ss->last_block != ss->first_block)
			ret = ss_read_replicated_page(ss, ss->last_block,
						      raw_page, dst, oob,
						      page_len);
		if (ret)
			return ret;
	}

	return 0;
}

static bool ss_object_name_matches(const struct ss_object *object,
				   const char *key)
{
	size_t key_len = strlen(key);
	size_t name_len = strnlen(object->name, SS_NAME_LEN);

	return name_len == key_len && !memcmp(object->name, key, key_len);
}

static int ss_find_item(const char *map, const char *key, u32 *item)
{
	size_t key_len = strlen(key);
	u32 pos = 0;
	u32 nr = 1;

	if (key_len >= SS_NAME_LEN)
		return -ENAMETOOLONG;

	while (pos < SS_ITEM_SIZE && map[pos]) {
		const char *entry = map + pos;
		const char *colon;
		size_t entry_len = strnlen(entry, SS_ITEM_SIZE - pos);

		if (entry_len == SS_ITEM_SIZE - pos)
			return -EINVAL;
		if (nr > SS_MAP_MAX_ENTRIES)
			return -ENOSPC;

		colon = memchr(entry, ':', entry_len);
		if (!colon || colon == entry || colon + 1 == entry + entry_len)
			return -EINVAL;

		if (colon - entry == key_len && !memcmp(entry, key, key_len)) {
			*item = nr;
			return 0;
		}

		pos += entry_len + 1;
		nr++;
	}

	return -ENOENT;
}

static int ss_get_payload(const void *item, const char *key, const u8 **payload,
			  u32 *len)
{
	const struct ss_object *object = item;
	u32 actual_len, stored_crc, crc;

	if (ss_get_le32(item, offsetof(struct ss_object, magic)) !=
	    SS_STORE_MAGIC)
		return -EINVAL;

	stored_crc = ss_get_le32(item, offsetof(struct ss_object, crc));
	crc = crc32(0, item, offsetof(struct ss_object, crc));
	if (crc != stored_crc)
		return -EBADMSG;
	if (!ss_object_name_matches(object, key))
		return -ENOENT;

	actual_len = ss_get_le32(item, offsetof(struct ss_object, actual_len));
	if (actual_len > SS_STORE_MAX_LEN)
		return -EINVAL;

	*payload = ((const struct ss_object *)item)->data;
	*len = actual_len;

	return 0;
}

static bool ss_parse_mac(const u8 *buf, u32 len, u8 *mac)
{
	char text[ARP_HLEN_ASCII + 1];
	size_t text_len;

	text_len = strnlen((const char *)buf, len);
	if (!text_len || text_len > ARP_HLEN_ASCII)
		return false;

	memcpy(text, buf, text_len);
	text[text_len] = '\0';
	string_to_enetaddr(text, mac);

	return is_valid_ethaddr(mac);
}

static int ss_read_mac(const char *key, u8 *mac)
{
	struct ss_info ss;
	const u8 *payload;
	u32 item, len;
	int ret;

	ret = ss_find_blocks(&ss);
	if (ret)
		return ret;

	ALLOC_CACHE_ALIGN_BUFFER(u8, itembuf, SS_ITEM_SIZE);

	ret = ss_read_item(&ss, 0, itembuf);
	if (ret)
		return ret;

	ret = ss_find_item((const char *)itembuf, key, &item);
	if (ret)
		return ret;

	ret = ss_read_item(&ss, item, itembuf);
	if (ret)
		return ret;

	ret = ss_get_payload(itembuf, key, &payload, &len);
	if (ret)
		return ret;

	return ss_parse_mac(payload, len, mac) ? 0 : -EINVAL;
}

void sunxi_nand_secure_storage_setup_ethaddr(void)
{
	const char *key = CONFIG_SUNXI_NAND_SECURE_STORAGE_ETHADDR_KEY;
	static bool attempted;
	u8 mac[ARP_HLEN];
	int ret;

	if (attempted || !key[0] || eth_env_get_enetaddr("ethaddr", mac))
		return;

	attempted = true;

	ret = ss_read_mac(key, mac);
	if (ret) {
		printf("Secure storage: failed to read key '%s' (%d)\n",
		       key, ret);
		return;
	}

	eth_env_set_enetaddr("ethaddr", mac);
	printf("Secure storage mac: %pM\n", mac);
}
#endif
