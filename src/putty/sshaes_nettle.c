/*
 * sshaes_nettle.c
 *
 * Uses the AES functions from Nettle to leverage hardware acceleration.
 */

#include <assert.h>
#include <stdlib.h>
#include <nettle/aes.h>

#include "ssh.h"

typedef struct AESContextNettle AESContextNettle;

struct AESContextNettle {
    struct aes256_ctx ctx;
    uint8_t iv2[16];
};

static void aes_sdctr_nettle(unsigned char *blk, int len, AESContextNettle *ctx)
{
    int i;
    uint8_t b[16];
    word32 tmp;

    assert((len & 15) == 0);

    while (len > 0) {
	aes256_encrypt(&ctx->ctx, 16, b, ctx->iv2);
	for (i = 0; i < 16; i++) {
	    blk[i] ^= b[i];
	}
	for (i = 3; i >= 0; i--) {
	    tmp = GET_32BIT_MSB_FIRST(ctx->iv2 + 4 * i);
	    tmp = (tmp + 1) & 0xffffffff;
	    PUT_32BIT_MSB_FIRST(ctx->iv2 + 4 * i, tmp);
	    if (tmp != 0)
		break;
	}
	blk += 16;
	len -= 16;
    }
}

void *aes_make_context_nettle(void)
{
    AESContextNettle* ctx = snew(AESContextNettle);
    return ctx;
}

void aes_free_context_nettle(void *handle)
{
    sfree(handle);
}

void aes256_key_nettle(void *handle, unsigned char *key)
{
    AESContextNettle *ctx = (AESContextNettle *)handle;
    aes256_set_encrypt_key(&ctx->ctx, key);
}

void aes_iv_nettle(void *handle, unsigned char *iv)
{
    AESContextNettle *ctx = (AESContextNettle *)handle;
    memcpy(ctx->iv2, iv, 16);
}

void aes_ssh2_sdctr_nettle(void *handle, unsigned char *blk, int len)
{
    AESContextNettle *ctx = (AESContextNettle *)handle;
    aes_sdctr_nettle(blk, len, ctx);
}
