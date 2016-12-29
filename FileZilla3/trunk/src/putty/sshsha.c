/*
 * SHA1 hash algorithm. Used in SSH-2 as a MAC, and the transform is
 * also used as a `stirring' function for the PuTTY random number
 * pool. Implemented directly from the specification by Simon
 * Tatham.
 */

#include "ssh.h"
#include <nettle/sha1.h>

/* ----------------------------------------------------------------------
 * Core SHA algorithm: processes 16-word blocks into a message digest.
 */

void SHATransform(word32 * digest, word32 * block)
{
    // This has different endianess than the PuTTY code, but this is fine for use in the RNG
    _nettle_sha1_compress(digest, (uint8_t *)block);
}


/* ----------------------------------------------------------------------
 * Outer SHA algorithm: take an arbitrary length byte string,
 * convert it into 16-word blocks with the prescribed padding at
 * the end, and pass those blocks to the core SHA algorithm.
 */

void SHA_Init(SHA_State *s)
{
     sha1_init(s);
}

void SHA_Bytes(SHA_State *s, const void *p, int len) {
    sha1_update(s, len, p);
}

void SHA_Final(SHA_State *s, unsigned char *digest) {
    sha1_digest(s, SHA1_DIGEST_SIZE, digest);
}

void SHA_Simple(const void *p, int len, unsigned char *output) {
    SHA_State s;

    SHA_Init(&s);
    SHA_Bytes(&s, p, len);
    SHA_Final(&s, output);
    smemclr(&s, sizeof(s));
}

/*
 * Thin abstraction for things where hashes are pluggable.
 */

static void *putty_sha1_init(void)
{
    SHA_State *s;

    s = snew(SHA_State);
    SHA_Init(s);
    return s;
}

static void *sha1_copy(const void *vold)
{
    const SHA_State *old = (const SHA_State *)vold;
    SHA_State *s;

    s = snew(SHA_State);
    *s = *old;
    return s;
}

static void sha1_free(void *handle)
{
    SHA_State *s = handle;

    smemclr(s, sizeof(*s));
    sfree(s);
}

static void sha1_bytes(void *handle, const void *p, int len)
{
    SHA_State *s = handle;

    SHA_Bytes(s, p, len);
}

static void sha1_final(void *handle, unsigned char *output)
{
    SHA_State *s = handle;

    SHA_Final(s, output);
    sha1_free(s);
}

const struct ssh_hash ssh_sha1 = {
    putty_sha1_init, sha1_copy, sha1_bytes, sha1_final, sha1_free, 20, "SHA-1"
};

/* ----------------------------------------------------------------------
 * The above is the SHA-1 algorithm itself. Now we implement the
 * HMAC wrapper on it.
 */

static void *sha1_make_context(void *cipher_ctx)
{
    return snewn(3, SHA_State);
}

static void sha1_free_context(void *handle)
{
    smemclr(handle, 3 * sizeof(SHA_State));
    sfree(handle);
}

static void sha1_key_internal(void *handle, unsigned char *key, int len)
{
    SHA_State *keys = (SHA_State *)handle;
    unsigned char foo[64];
    int i;

    memset(foo, 0x36, 64);
    for (i = 0; i < len && i < 64; i++)
	foo[i] ^= key[i];
    SHA_Init(&keys[0]);
    SHA_Bytes(&keys[0], foo, 64);

    memset(foo, 0x5C, 64);
    for (i = 0; i < len && i < 64; i++)
	foo[i] ^= key[i];
    SHA_Init(&keys[1]);
    SHA_Bytes(&keys[1], foo, 64);

    smemclr(foo, 64);		       /* burn the evidence */
}

static void sha1_key(void *handle, unsigned char *key)
{
    sha1_key_internal(handle, key, 20);
}

static void sha1_key_buggy(void *handle, unsigned char *key)
{
    sha1_key_internal(handle, key, 16);
}

static void hmacsha1_start(void *handle)
{
    SHA_State *keys = (SHA_State *)handle;

    keys[2] = keys[0];		      /* structure copy */
}

static void hmacsha1_bytes(void *handle, unsigned char const *blk, int len)
{
    SHA_State *keys = (SHA_State *)handle;
    SHA_Bytes(&keys[2], (void *)blk, len);
}

static void hmacsha1_genresult(void *handle, unsigned char *hmac)
{
    SHA_State *keys = (SHA_State *)handle;
    SHA_State s;
    unsigned char intermediate[20];

    s = keys[2];		       /* structure copy */
    SHA_Final(&s, intermediate);
    s = keys[1];		       /* structure copy */
    SHA_Bytes(&s, intermediate, 20);
    SHA_Final(&s, hmac);
}

static void sha1_do_hmac(void *handle, unsigned char *blk, int len,
			 unsigned long seq, unsigned char *hmac)
{
    unsigned char seqbuf[4];

    PUT_32BIT_MSB_FIRST(seqbuf, seq);
    hmacsha1_start(handle);
    hmacsha1_bytes(handle, seqbuf, 4);
    hmacsha1_bytes(handle, blk, len);
    hmacsha1_genresult(handle, hmac);
}

static void sha1_generate(void *handle, unsigned char *blk, int len,
			  unsigned long seq)
{
    sha1_do_hmac(handle, blk, len, seq, blk + len);
}

static int hmacsha1_verresult(void *handle, unsigned char const *hmac)
{
    unsigned char correct[20];
    hmacsha1_genresult(handle, correct);
    return smemeq(correct, hmac, 20);
}

static int sha1_verify(void *handle, unsigned char *blk, int len,
		       unsigned long seq)
{
    unsigned char correct[20];
    sha1_do_hmac(handle, blk, len, seq, correct);
    return smemeq(correct, blk + len, 20);
}

static void hmacsha1_96_genresult(void *handle, unsigned char *hmac)
{
    unsigned char full[20];
    hmacsha1_genresult(handle, full);
    memcpy(hmac, full, 12);
}

static void sha1_96_generate(void *handle, unsigned char *blk, int len,
			     unsigned long seq)
{
    unsigned char full[20];
    sha1_do_hmac(handle, blk, len, seq, full);
    memcpy(blk + len, full, 12);
}

static int hmacsha1_96_verresult(void *handle, unsigned char const *hmac)
{
    unsigned char correct[20];
    hmacsha1_genresult(handle, correct);
    return smemeq(correct, hmac, 12);
}

static int sha1_96_verify(void *handle, unsigned char *blk, int len,
		       unsigned long seq)
{
    unsigned char correct[20];
    sha1_do_hmac(handle, blk, len, seq, correct);
    return smemeq(correct, blk + len, 12);
}

void hmac_sha1_simple(void *key, int keylen, void *data, int datalen,
		      unsigned char *output) {
    SHA_State states[2];
    unsigned char intermediate[20];

    sha1_key_internal(states, key, keylen);
    SHA_Bytes(&states[0], data, datalen);
    SHA_Final(&states[0], intermediate);

    SHA_Bytes(&states[1], intermediate, 20);
    SHA_Final(&states[1], output);
}

const struct ssh_mac ssh_hmac_sha1 = {
    sha1_make_context, sha1_free_context, sha1_key,
    sha1_generate, sha1_verify,
    hmacsha1_start, hmacsha1_bytes, hmacsha1_genresult, hmacsha1_verresult,
    "hmac-sha1", "hmac-sha1-etm@openssh.com",
    20, 20,
    "HMAC-SHA1"
};

const struct ssh_mac ssh_hmac_sha1_96 = {
    sha1_make_context, sha1_free_context, sha1_key,
    sha1_96_generate, sha1_96_verify,
    hmacsha1_start, hmacsha1_bytes,
    hmacsha1_96_genresult, hmacsha1_96_verresult,
    "hmac-sha1-96", "hmac-sha1-96-etm@openssh.com",
    12, 20,
    "HMAC-SHA1-96"
};

const struct ssh_mac ssh_hmac_sha1_buggy = {
    sha1_make_context, sha1_free_context, sha1_key_buggy,
    sha1_generate, sha1_verify,
    hmacsha1_start, hmacsha1_bytes, hmacsha1_genresult, hmacsha1_verresult,
    "hmac-sha1", NULL,
    20, 16,
    "bug-compatible HMAC-SHA1"
};

const struct ssh_mac ssh_hmac_sha1_96_buggy = {
    sha1_make_context, sha1_free_context, sha1_key_buggy,
    sha1_96_generate, sha1_96_verify,
    hmacsha1_start, hmacsha1_bytes,
    hmacsha1_96_genresult, hmacsha1_96_verresult,
    "hmac-sha1-96", NULL,
    12, 16,
    "bug-compatible HMAC-SHA1-96"
};
