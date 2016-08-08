/*
 * Modified to use the AES implementation from Nettle.
 */

#include <assert.h>
#include <stdlib.h>

#include "ssh.h"

#include <nettle/aes.h>
#include <nettle/gcm.h>
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

static void increment_iv_step32(uint8_t *iv, int i)
{
    word32 tmp;
    for (--i; i >= 0; i--) {
	tmp = GET_32BIT_MSB_FIRST(iv + 4 * i);
	tmp = (tmp + 1) & 0xffffffff;
	PUT_32BIT_MSB_FIRST(iv + 4 * i, tmp);
	if (tmp != 0)
	    break;
    }
}

static void aes_sdctr(unsigned char *blk, int len, AESContext *ctx)
{
    uint8_t b[16];

    assert((len & 15) == 0);

    while (len > 0) {
	aes_encrypt(&ctx->enc_ctx, 16, b, ctx->iv);
	memxor(blk, b, 16);
	increment_iv_step32(ctx->iv, 4);
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



/* GCM 256 */

typedef struct AES256GCMContext AES256GCMContext;
struct AES256GCMContext {
    struct gcm_aes256_ctx ctx;
    uint8_t iv[12];
};


void *aes256_gcm_make_context(void)
{
    AES256GCMContext* ctx = snew(AES256GCMContext);
    return ctx;
}


void aes256_gcm_free_context(void *handle)
{
    sfree(handle);
}


void aes256_gcm_key(void *handle, unsigned char *key)
{
    AES256GCMContext *ctx = (AES256GCMContext *)handle;
    nettle_gcm_aes256_set_key(&ctx->ctx, key);
}


void aes256_gcm_iv(void *handle, unsigned char *iv)
{
    AES256GCMContext *ctx = (AES256GCMContext *)handle;
    memcpy(ctx->iv, iv, 12);
}


void aes256_gcm_encrypt_blk(void *handle, unsigned char *blk, int len)
{
    AES256GCMContext *ctx = (AES256GCMContext *)handle;
    unsigned char adata[4];
    PUT_32BIT(adata, (unsigned int)len);
    nettle_gcm_aes256_set_iv(&ctx->ctx, 12, ctx->iv);
    nettle_gcm_aes256_update(&ctx->ctx, 4, adata);
    nettle_gcm_aes256_encrypt(&ctx->ctx, len, blk, blk);
}

void aes256_gcm_decrypt_blk(void *handle, unsigned char *blk, int len)
{
    AES256GCMContext *ctx = (AES256GCMContext *)handle;

    // Decryption was already done at MAC verification. Just increment the IV here.
    increment_iv_step32(ctx->iv + 4, 2);
}


static void *aesgcm_mac_make_context(void *ctx)
{
    return ctx;
}

static void aesgcm_mac_free_context(void *ctx)
{
    /* Not allocated, just forwarded, no need to free */
}

static void aesgcm_mac_setkey(void *ctx, unsigned char *key)
{
    /* Uses the same context as the cipher, so ignore */
}

static void aes256_gcm_mac_generate(void *handle, unsigned char *blk, int len, unsigned long seq)
{
    AES256GCMContext *ctx = (AES256GCMContext *)handle;
    nettle_gcm_aes256_digest(&ctx->ctx, 16, blk+len);
    increment_iv_step32(ctx->iv + 4, 2);
}

static int aes256_gcm_mac_verify(void *handle, unsigned char *blk, int len, unsigned long seq)
{
    int res;

    AES256GCMContext *ctx = (AES256GCMContext *)handle;

    unsigned char mac[16];

    nettle_gcm_aes256_set_iv(&ctx->ctx, 12, ctx->iv);
    unsigned char adata[4];
    PUT_32BIT(adata, (unsigned int)(len - 4));
    nettle_gcm_aes256_update(&ctx->ctx, 4, adata);
    nettle_gcm_aes256_decrypt(&ctx->ctx, len - 4, blk + 4, blk + 4);
    nettle_gcm_aes256_digest(&ctx->ctx, 16, mac);

    res = smemeq(blk+len, mac, 16);

    return res;
}

static const struct ssh_mac ssh2_aes256_gcm_mac = {
    aesgcm_mac_make_context, aesgcm_mac_free_context,
    aesgcm_mac_setkey,

    /* whole-packet operations */
    aes256_gcm_mac_generate,aes256_gcm_mac_verify,

    /* partial-packet operations, not supported */
    0,0,0,0,

    "", "", /* Not selectable individually, just part of aes256-gcm@openssh.com */
    16, 0, "AES256 GCM"
};

static const struct ssh2_cipher ssh_aes256_gcm = {
    aes256_gcm_make_context, aes256_gcm_free_context, aes256_gcm_iv, aes256_gcm_key,
    aes256_gcm_encrypt_blk, aes256_gcm_decrypt_blk, NULL, NULL,
    "aes256-gcm@openssh.com",
    16, 256, 32, 0, "AES-256 GCM",
    &ssh2_aes256_gcm_mac
};


/* GCM 128 */

typedef struct AES128GCMContext AES128GCMContext;
struct AES128GCMContext {
    struct gcm_aes128_ctx ctx;
    uint8_t iv[12];
};


void *aes128_gcm_make_context(void)
{
    AES128GCMContext* ctx = snew(AES128GCMContext);
    return ctx;
}


void aes128_gcm_free_context(void *handle)
{
    sfree(handle);
}


void aes128_gcm_key(void *handle, unsigned char *key)
{
    AES128GCMContext *ctx = (AES128GCMContext *)handle;
    nettle_gcm_aes128_set_key(&ctx->ctx, key);
}


void aes128_gcm_iv(void *handle, unsigned char *iv)
{
    AES128GCMContext *ctx = (AES128GCMContext *)handle;
    memcpy(ctx->iv, iv, 12);
}


void aes128_gcm_encrypt_blk(void *handle, unsigned char *blk, int len)
{
    AES128GCMContext *ctx = (AES128GCMContext *)handle;
    unsigned char adata[4];
    PUT_32BIT(adata, (unsigned int)len);
    nettle_gcm_aes128_set_iv(&ctx->ctx, 12, ctx->iv);
    nettle_gcm_aes128_update(&ctx->ctx, 4, adata);
    nettle_gcm_aes128_encrypt(&ctx->ctx, len, blk, blk);
}

void aes128_gcm_decrypt_blk(void *handle, unsigned char *blk, int len)
{
    AES128GCMContext *ctx = (AES128GCMContext *)handle;

    // Decryption was already done at MAC verification. Just increment the IV here.
    increment_iv_step32(ctx->iv + 4, 2);
}



static void aes128_gcm_mac_generate(void *handle, unsigned char *blk, int len, unsigned long seq)
{
    AES128GCMContext *ctx = (AES128GCMContext *)handle;
    nettle_gcm_aes128_digest(&ctx->ctx, 16, blk + len);
    increment_iv_step32(ctx->iv + 4, 2);
}

static int aes128_gcm_mac_verify(void *handle, unsigned char *blk, int len, unsigned long seq)
{
    int res;

    AES128GCMContext *ctx = (AES128GCMContext *)handle;

    unsigned char mac[16];

    nettle_gcm_aes128_set_iv(&ctx->ctx, 12, ctx->iv);
    unsigned char adata[4];
    PUT_32BIT(adata, (unsigned int)(len - 4));
    nettle_gcm_aes128_update(&ctx->ctx, 4, adata);
    nettle_gcm_aes128_decrypt(&ctx->ctx, len - 4, blk + 4, blk + 4);
    nettle_gcm_aes128_digest(&ctx->ctx, 16, mac);

    res = smemeq(blk + len, mac, 16);

    return res;
}

static const struct ssh_mac ssh2_aes128_gcm_mac = {
    aesgcm_mac_make_context, aesgcm_mac_free_context,
    aesgcm_mac_setkey,

    /* whole-packet operations */
    aes128_gcm_mac_generate, aes128_gcm_mac_verify,

    /* partial-packet operations, not supported */
    0,0,0,0,

    "", "", /* Not selectable individually, just part of aes256-gcm@openssh.com */
    16, 0, "AES128 GCM"
};


static const struct ssh2_cipher ssh_aes128_gcm = {
    aes128_gcm_make_context, aes128_gcm_free_context, aes128_gcm_iv, aes128_gcm_key,
    aes128_gcm_encrypt_blk, aes128_gcm_decrypt_blk, NULL, NULL,
    "aes128-gcm@openssh.com",
    16, 128, 16, 0, "AES-128 GCM",
    &ssh2_aes128_gcm_mac
};


static const struct ssh2_cipher *const aes_list[] = {
    &ssh_aes256_gcm,
    &ssh_aes256_ctr,
    &ssh_aes256,
    &ssh_rijndael_lysator,
    &ssh_aes192_ctr,
    &ssh_aes192,
    &ssh_aes128_gcm,
    &ssh_aes128_ctr,
    &ssh_aes128,
};

const struct ssh2_ciphers ssh2_aes = {
    sizeof(aes_list) / sizeof(*aes_list),
    aes_list
};
