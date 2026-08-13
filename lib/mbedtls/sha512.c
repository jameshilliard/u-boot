// SPDX-License-Identifier: GPL-2.0+
/*
 * Hash shim layer on MbedTLS Crypto library
 *
 * Copyright (c) 2024 Linaro Limited
 * Author: Raymond Mao <raymond.mao@linaro.org>
 */
#ifndef USE_HOSTCC
#include <cyclic.h>
#endif /* USE_HOSTCC */
#include <compiler.h>
#include <u-boot/sha512.h>

void sha384_starts(sha512_context *ctx)
{
	mbedtls_sha512_init(ctx);
	mbedtls_sha512_starts(ctx, 1);
}

void
sha384_update(sha512_context *ctx, const uint8_t *input, uint32_t length)
{
	mbedtls_sha512_update(ctx, input, length);
}

void sha384_finish(sha512_context *ctx, uint8_t digest[SHA384_SUM_LEN])
{
	mbedtls_sha512_finish(ctx, digest);
	mbedtls_sha512_free(ctx);
}

void sha384_csum_wd(const unsigned char *input, unsigned int length,
		    unsigned char *output, unsigned int chunk_sz)
{
	mbedtls_sha512(input, length, output, 1);
}

void sha512_starts(sha512_context *ctx)
{
	mbedtls_sha512_init(ctx);
	mbedtls_sha512_starts(ctx, 0);
}

void
sha512_update(sha512_context *ctx, const uint8_t *input, uint32_t length)
{
	mbedtls_sha512_update(ctx, input, length);
}

void sha512_finish(sha512_context *ctx, uint8_t digest[SHA512_SUM_LEN])
{
	mbedtls_sha512_finish(ctx, digest);
	mbedtls_sha512_free(ctx);
}

void sha512_csum_wd(const unsigned char *input, unsigned int ilen,
		    unsigned char *output, unsigned int chunk_sz)
{
	sha512_context ctx;

	sha512_starts(&ctx);

	if (IS_ENABLED(CONFIG_HW_WATCHDOG) || IS_ENABLED(CONFIG_WATCHDOG)) {
		const unsigned char *curr = input;
		const unsigned char *end = input + ilen;
		int chunk;

		while (curr < end) {
			chunk = end - curr;
			if (chunk > chunk_sz)
				chunk = chunk_sz;
			sha512_update(&ctx, curr, chunk);
			curr += chunk;
			schedule();
		}
	} else {
		sha512_update(&ctx, input, ilen);
	}

	sha512_finish(&ctx, output);
}
