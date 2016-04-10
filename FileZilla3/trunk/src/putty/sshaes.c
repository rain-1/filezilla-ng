/*
 * Modified to use the AES implementation from Nettle.
 */

#include <assert.h>
#include <stdlib.h>

#include "ssh.h"

#include <nettle/aes.h>
#include <nettle/memxor.h>

typedef struct AESContext AESContext;

struct AESContext {
    struct aes_ctx enc_ctx;
    struct aes_ctx dec_ctx;
    uint8_t iv[16];
};

static void aes_encrypt_cbc(unsigned char *blk, int len, AESContext * ctx)
{
    assert((len & 15) == 0);

    while (len > 0) {
	memxor(blk, ctx->iv, 16);
	aes_encrypt(&ctx->enc_ctx, 16, ctx->iv, blk);
	memcpy(blk, ctx->iv, 16);
	blk += 16;
	len -= 16;
    }
}

static void aes_decrypt_cbc(unsigned char *blk, int len, AESContext * ctx)
{
    uint8_t x[16];

    assert((len & 15) == 0);

    while (len > 0) {
	aes_decrypt(&ctx->dec_ctx, 16, x, blk);
	memxor(x, ctx->iv, 16);
	memcpy(ctx->iv, blk, 16);
	memcpy(blk, x, 16);
	blk += 16;
	len -= 16;
    }
}

static void aes_sdctr(unsigned char *blk, int len, AESContext *ctx)
{
    int i;
    uint8_t b[16];
    word32 tmp;

    assert((len & 15) == 0);

    while (len > 0) {
	aes_encrypt(&ctx->enc_ctx, 16, b, ctx->iv);
	memxor(blk, b, 16);
	for (i = 3; i >= 0; i--) {
	    tmp = GET_32BIT_MSB_FIRST(ctx->iv + 4 * i);
	    tmp = (tmp + 1) & 0xffffffff;
	    PUT_32BIT_MSB_FIRST(ctx->iv + 4 * i, tmp);
	    if (tmp != 0)
		break;
	}
	blk += 16;
	len -= 16;
    }
}

void *aes_make_context(void)
{
    AESContext* ctx = snew(AESContext);
    return ctx;
}

void aes_free_context(void *handle)
{
    sfree(handle);
}

void aes128_key(void *handle, unsigned char *key)
{
    AESContext *ctx = (AESContext *)handle;
    aes_set_encrypt_key(&ctx->enc_ctx, 16, key);
    aes_invert_key(&ctx->dec_ctx, &ctx->enc_ctx);
}

void aes192_key(void *handle, unsigned char *key)
{
    AESContext *ctx = (AESContext *)handle;
    aes_set_encrypt_key(&ctx->enc_ctx, 24, key);
    aes_invert_key(&ctx->dec_ctx, &ctx->enc_ctx);
}

void aes256_key(void *handle, unsigned char *key)
{
    AESContext *ctx = (AESContext *)handle;
    aes_set_encrypt_key(&ctx->enc_ctx, 32, key);
    aes_invert_key(&ctx->dec_ctx, &ctx->enc_ctx);
}

void aes_iv(void *handle, unsigned char *iv)
{
    AESContext *ctx = (AESContext *)handle;
    memcpy(ctx->iv, iv, 16);
}

void aes_ssh2_sdctr(void *handle, unsigned char *blk, int len)
{
    AESContext *ctx = (AESContext *)handle;
    aes_sdctr(blk, len, ctx);
}

void aes_ssh2_encrypt_blk(void *handle, unsigned char *blk, int len)
{
    AESContext *ctx = (AESContext *)handle;
    aes_encrypt_cbc(blk, len, ctx);
}

void aes_ssh2_decrypt_blk(void *handle, unsigned char *blk, int len)
{
    AESContext *ctx = (AESContext *)handle;
    aes_decrypt_cbc(blk, len, ctx);
}

void aes256_encrypt_pubkey(unsigned char *key, unsigned char *blk, int len)
{
    AESContext ctx;
    aes256_key(&ctx, key);
    memset(ctx.iv, 0, sizeof(ctx.iv));
    aes_encrypt_cbc(blk, len, &ctx);
    smemclr(&ctx, sizeof(ctx));
}

void aes256_decrypt_pubkey(unsigned char *key, unsigned char *blk, int len)
{
    AESContext ctx;
    aes256_key(&ctx, key);
    memset(ctx.iv, 0, sizeof(ctx.iv));
    aes_decrypt_cbc(blk, len, &ctx);
    smemclr(&ctx, sizeof(ctx));
}

static const struct ssh2_cipher ssh_aes128_ctr = {
    aes_make_context, aes_free_context, aes_iv, aes128_key,
    aes_ssh2_sdctr, aes_ssh2_sdctr, NULL, NULL,
    "aes128-ctr",
    16, 128, 16, 0, "AES-128 SDCTR",
    NULL
};

static const struct ssh2_cipher ssh_aes192_ctr = {
    aes_make_context, aes_free_context, aes_iv, aes192_key,
    aes_ssh2_sdctr, aes_ssh2_sdctr, NULL, NULL,
    "aes192-ctr",
    16, 192, 24, 0, "AES-192 SDCTR",
    NULL
};

static const struct ssh2_cipher ssh_aes256_ctr = {
    aes_make_context, aes_free_context, aes_iv, aes256_key,
    aes_ssh2_sdctr, aes_ssh2_sdctr, NULL, NULL,
    "aes256-ctr",
    16, 256, 32, 0, "AES-256 SDCTR",
    NULL
};

static const struct ssh2_cipher ssh_aes128 = {
    aes_make_context, aes_free_context, aes_iv, aes128_key,
    aes_ssh2_encrypt_blk, aes_ssh2_decrypt_blk, NULL, NULL,
    "aes128-cbc",
    16, 128, 16, SSH_CIPHER_IS_CBC, "AES-128 CBC",
    NULL
};

static const struct ssh2_cipher ssh_aes192 = {
    aes_make_context, aes_free_context, aes_iv, aes192_key,
    aes_ssh2_encrypt_blk, aes_ssh2_decrypt_blk, NULL, NULL,
    "aes192-cbc",
    16, 192, 24, SSH_CIPHER_IS_CBC, "AES-192 CBC",
    NULL
};

static const struct ssh2_cipher ssh_aes256 = {
    aes_make_context, aes_free_context, aes_iv, aes256_key,
    aes_ssh2_encrypt_blk, aes_ssh2_decrypt_blk, NULL, NULL,
    "aes256-cbc",
    16, 256, 32, SSH_CIPHER_IS_CBC, "AES-256 CBC",
    NULL
};

static const struct ssh2_cipher ssh_rijndael_lysator = {
    aes_make_context, aes_free_context, aes_iv, aes256_key,
    aes_ssh2_encrypt_blk, aes_ssh2_decrypt_blk, NULL, NULL,
    "rijndael-cbc@lysator.liu.se",
    16, 256, 32, SSH_CIPHER_IS_CBC, "AES-256 CBC",
    NULL
};

static const struct ssh2_cipher *const aes_list[] = {
    &ssh_aes256_ctr,
    &ssh_aes256,
    &ssh_rijndael_lysator,
    &ssh_aes192_ctr,
    &ssh_aes192,
    &ssh_aes128_ctr,
    &ssh_aes128,
};

const struct ssh2_ciphers ssh2_aes = {
    sizeof(aes_list) / sizeof(*aes_list),
    aes_list
};
