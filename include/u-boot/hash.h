/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2021 ASPEED Technology Inc.
 */
#ifndef _UBOOT_HASH_H
#define _UBOOT_HASH_H

enum HASH_ALGO {
	HASH_ALGO_CRC16_CCITT,
	HASH_ALGO_CRC32,
	HASH_ALGO_MD5,
	HASH_ALGO_SHA1,
	HASH_ALGO_SHA256,
	HASH_ALGO_SHA384,
	HASH_ALGO_SHA512,

	HASH_ALGO_NUM,

	HASH_ALGO_INVALID = 0xffffffff,
};

struct udevice;

/* general APIs for hash algo information */
enum HASH_ALGO hash_algo_lookup_by_name(const char *name);
ssize_t hash_algo_digest_size(enum HASH_ALGO algo);
const char *hash_algo_name(enum HASH_ALGO algo);

/* device-dependent APIs */
int hash_digest(struct udevice *dev, enum HASH_ALGO algo,
		const void *ibuf, const uint32_t ilen,
		void *obuf);
int hash_digest_wd(struct udevice *dev, enum HASH_ALGO algo,
		   const void *ibuf, const uint32_t ilen,
		   void *obuf, uint32_t chunk_sz);
/**
 * hash_digest_wd_lookup() - Hash with the first provider supporting an algorithm
 *
 * Probe each hash device in order and use the first one which supports the
 * requested algorithm. Probe failures are remembered while later providers are
 * tried. Once a provider accepts an operation, hard failures are returned
 * without trying another provider.
 *
 * @algo: Hash algorithm
 * @ibuf: Input buffer
 * @ilen: Input buffer length
 * @obuf: Output buffer
 * @chunk_sz: Watchdog scheduling interval
 * Return: 0 on success, -ENODEV if there are no providers, -EOPNOTSUPP if no
 * provider supports @algo, or another negative error from a provider
 */
int hash_digest_wd_lookup(enum HASH_ALGO algo, const void *ibuf,
			  const u32 ilen, void *obuf, u32 chunk_sz);
/**
 * hash_init_lookup() - Start hashing with the first supporting provider
 *
 * Probe each hash device in order and initialize the first one which supports
 * @algo. The selected device is returned for the matching update and finish
 * operations.
 *
 * @algo: Hash algorithm
 * @devp: Returns the selected hash device
 * @ctxp: Returns the provider's progressive-hash context
 * Return: 0 on success, -ENODEV if there are no providers, -EOPNOTSUPP if no
 * provider supports @algo, or another negative error from a provider
 */
int hash_init_lookup(enum HASH_ALGO algo, struct udevice **devp, void **ctxp);

/*
 * A successful hash_init() returns a context which must be consumed by
 * exactly one hash_finish() or hash_abort() call. Both operations release all
 * provider resources, including when they return an error.
 */
int hash_init(struct udevice *dev, enum HASH_ALGO algo, void **ctxp);
int hash_update(struct udevice *dev, void *ctx, const void *ibuf, const uint32_t ilen);
int hash_finish(struct udevice *dev, void *ctx, void *obuf);

/**
 * hash_abort() - Discard a progressive hash operation
 *
 * @dev: Hash device selected by hash_init() or hash_init_lookup()
 * @ctx: Progressive-hash context to release
 * Return: 0 on success, or a negative provider error
 */
int hash_abort(struct udevice *dev, void *ctx);

/*
 * struct hash_ops - Driver model for Hash operations
 *
 * The uclass interface is implemented by all hash devices
 * which use driver model.
 */
struct hash_ops {
	/* progressive operations */
	int (*hash_init)(struct udevice *dev, enum HASH_ALGO algo, void **ctxp);
	int (*hash_update)(struct udevice *dev, void *ctx, const void *ibuf, const uint32_t ilen);
	int (*hash_finish)(struct udevice *dev, void *ctx, void *obuf);
	int (*hash_abort)(struct udevice *dev, void *ctx);

	/* all-in-one operation */
	int (*hash_digest)(struct udevice *dev, enum HASH_ALGO algo,
			   const void *ibuf, const uint32_t ilen,
			   void *obuf);

	/* all-in-one operation with watchdog triggering every chunk_sz */
	int (*hash_digest_wd)(struct udevice *dev, enum HASH_ALGO algo,
			      const void *ibuf, const uint32_t ilen,
			      void *obuf, uint32_t chunk_sz);
};

#endif
