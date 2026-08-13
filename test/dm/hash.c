// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for driver-model hash-provider selection
 *
 * Copyright (C) 2026 James Hilliard
 */

#include <dm.h>
#include <dm/device-internal.h>
#include <dm/root.h>
#include <dm/test.h>
#include <dm/uclass-internal.h>
#include <image.h>
#include <u-boot/hash.h>
#include <u-boot/hash-checksum.h>
#include <test/test.h>
#include <test/ut.h>

static int unsupported_calls;
static int success_calls;
static int hard_error_calls;
static int unsupported_init_calls;
static int success_init_calls;
static int hard_error_init_calls;
static int update_error_calls;
static int finish_calls;
static int abort_calls;

static int hash_test_unsupported_init(struct udevice *dev,
				      enum HASH_ALGO algo, void **ctxp)
{
	unsupported_init_calls++;

	return -EOPNOTSUPP;
}

static int hash_test_success_init(struct udevice *dev, enum HASH_ALGO algo,
				  void **ctxp)
{
	success_init_calls++;
	*ctxp = dev;

	return 0;
}

static int hash_test_hard_error_init(struct udevice *dev,
				     enum HASH_ALGO algo, void **ctxp)
{
	hard_error_init_calls++;

	return -EINVAL;
}

static int hash_test_update_error_init(struct udevice *dev,
				       enum HASH_ALGO algo, void **ctxp)
{
	*ctxp = dev;

	return 0;
}

static int hash_test_progressive_update(struct udevice *dev, void *ctx,
					const void *ibuf, u32 ilen)
{
	return 0;
}

static int hash_test_update_error(struct udevice *dev, void *ctx,
				  const void *ibuf, u32 ilen)
{
	update_error_calls++;

	return -EIO;
}

static int hash_test_progressive_finish(struct udevice *dev, void *ctx,
					void *obuf)
{
	finish_calls++;

	return 0;
}

static int hash_test_progressive_abort(struct udevice *dev, void *ctx)
{
	abort_calls++;

	return 0;
}

static int hash_test_unsupported(struct udevice *dev, enum HASH_ALGO algo,
				 const void *ibuf, const uint32_t ilen,
				 void *obuf, uint32_t chunk_sz)
{
	unsupported_calls++;

	return -EOPNOTSUPP;
}

static int hash_test_success(struct udevice *dev, enum HASH_ALGO algo,
			     const void *ibuf, const uint32_t ilen,
			     void *obuf, uint32_t chunk_sz)
{
	success_calls++;
	memset(obuf, 0x5a, hash_algo_digest_size(algo));

	return 0;
}

static int hash_test_hard_error(struct udevice *dev, enum HASH_ALGO algo,
				const void *ibuf, const uint32_t ilen,
				void *obuf, uint32_t chunk_sz)
{
	hard_error_calls++;

	return -EINVAL;
}

static const struct hash_ops hash_test_unsupported_ops = {
	.hash_init = hash_test_unsupported_init,
	.hash_update = hash_test_progressive_update,
	.hash_finish = hash_test_progressive_finish,
	.hash_abort = hash_test_progressive_abort,
	.hash_digest_wd = hash_test_unsupported,
};

static const struct hash_ops hash_test_success_ops = {
	.hash_init = hash_test_success_init,
	.hash_update = hash_test_progressive_update,
	.hash_finish = hash_test_progressive_finish,
	.hash_abort = hash_test_progressive_abort,
	.hash_digest_wd = hash_test_success,
};

static const struct hash_ops hash_test_hard_error_ops = {
	.hash_init = hash_test_hard_error_init,
	.hash_update = hash_test_progressive_update,
	.hash_finish = hash_test_progressive_finish,
	.hash_abort = hash_test_progressive_abort,
	.hash_digest_wd = hash_test_hard_error,
};

static const struct hash_ops hash_test_update_error_ops = {
	.hash_init = hash_test_update_error_init,
	.hash_update = hash_test_update_error,
	.hash_finish = hash_test_progressive_finish,
	.hash_abort = hash_test_progressive_abort,
};

U_BOOT_DRIVER(hash_test_unsupported_drv) = {
	.name = "hash_test_unsupported",
	.id = UCLASS_HASH,
	.ops = &hash_test_unsupported_ops,
};

U_BOOT_DRIVER(hash_test_success_drv) = {
	.name = "hash_test_success",
	.id = UCLASS_HASH,
	.ops = &hash_test_success_ops,
};

U_BOOT_DRIVER(hash_test_hard_error_drv) = {
	.name = "hash_test_hard_error",
	.id = UCLASS_HASH,
	.ops = &hash_test_hard_error_ops,
};

U_BOOT_DRIVER(hash_test_update_error_drv) = {
	.name = "hash_test_update_error",
	.id = UCLASS_HASH,
	.ops = &hash_test_update_error_ops,
};

static int hash_test_unbind_all(void)
{
	struct udevice *dev;
	int ret;

	for (;;) {
		ret = uclass_find_first_device(UCLASS_HASH, &dev);
		if (ret || !dev)
			return ret;
		if (device_active(dev)) {
			ret = device_remove(dev, DM_REMOVE_NORMAL);
			if (ret)
				return ret;
		}
		ret = device_unbind(dev);
		if (ret)
			return ret;
	}
}

static int hash_test_bind(const struct driver *drv, const char *name)
{
	struct udevice *dev;

	return device_bind(dm_root(), drv, name, 0, ofnode_null(), &dev);
}

static int dm_test_hash_provider_selection(struct unit_test_state *uts)
{
	const struct image_region region = {
		.data = "test",
		.size = 4,
	};
	struct udevice *dev;
	u8 digest[32];
	void *ctx;
	int ret;

	ut_assertok(hash_test_unbind_all());
	ut_assertok(hash_test_bind(DM_DRIVER_GET(hash_test_unsupported_drv),
				   "hash-unsupported"));
	ut_assertok(hash_test_bind(DM_DRIVER_GET(hash_test_success_drv),
				   "hash-success"));

	unsupported_calls = 0;
	success_calls = 0;
	memset(digest, 0, sizeof(digest));
	ret = hash_digest_wd_lookup(HASH_ALGO_SHA256, "test", 4, digest, 4);
	ut_assertok(ret);
	ut_asserteq(1, unsupported_calls);
	ut_asserteq(1, success_calls);
	for (int i = 0; i < sizeof(digest); i++)
		ut_asserteq(0x5a, digest[i]);

	unsupported_init_calls = 0;
	success_init_calls = 0;
	ret = hash_init_lookup(HASH_ALGO_SHA256, &dev, &ctx);
	ut_assertok(ret);
	ut_asserteq(1, unsupported_init_calls);
	ut_asserteq(1, success_init_calls);
	ut_asserteq_str("hash-success", dev->name);
	ut_asserteq_ptr(dev, ctx);
	abort_calls = 0;
	ut_assertok(hash_abort(dev, ctx));
	ut_asserteq(1, abort_calls);

	ut_assertok(hash_test_unbind_all());
	ut_assertok(hash_test_bind(DM_DRIVER_GET(hash_test_hard_error_drv),
				   "hash-hard-error"));
	ut_assertok(hash_test_bind(DM_DRIVER_GET(hash_test_success_drv),
				   "hash-success"));

	hard_error_calls = 0;
	success_calls = 0;
	ret = hash_digest_wd_lookup(HASH_ALGO_SHA256, "test", 4, digest, 4);
	ut_asserteq(-EINVAL, ret);
	ut_asserteq(1, hard_error_calls);
	ut_asserteq(0, success_calls);

	hard_error_init_calls = 0;
	success_init_calls = 0;
	ret = hash_init_lookup(HASH_ALGO_SHA256, &dev, &ctx);
	ut_asserteq(-EINVAL, ret);
	ut_asserteq(1, hard_error_init_calls);
	ut_asserteq(0, success_init_calls);

	ut_assertok(hash_test_unbind_all());
	ut_assertok(hash_test_bind(DM_DRIVER_GET(hash_test_update_error_drv),
				   "hash-update-error"));
	update_error_calls = 0;
	finish_calls = 0;
	abort_calls = 0;
	ret = hash_calculate("sha256", &region, 1, digest);
	ut_asserteq(-EIO, ret);
	ut_asserteq(1, update_error_calls);
	ut_asserteq(0, finish_calls);
	ut_asserteq(1, abort_calls);

	return 0;
}

DM_TEST(dm_test_hash_provider_selection, UTF_SCAN_FDT);
