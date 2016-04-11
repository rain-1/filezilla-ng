/*
 * SHA-256 algorithm as described at
 * 
 *   http://csrc.nist.gov/cryptval/shs.html
 */

#include "ssh.h"
#include <nettle/sha2.h>

/* ----------------------------------------------------------------------
 * Outer SHA256 algorithm: take an arbitrary length byte string,
 * convert it into 16-word blocks with the prescribed padding at
 * the end, and pass those blocks to the core SHA256 algorithm.
 */

void SHA256_Init(SHA256_State *s) {
     sha256_init(s);
}

void SHA256_Bytes(SHA256_State *s, const void *p, int len) {
    sha256_update(s, len, p);
}

void SHA256_Final(SHA256_State *s, unsigned char *digest) {
    sha256_digest(s, SHA256_DIGEST_SIZE, digest);
}

void SHA256_Simple(const void *p, int len, unsigned char *output) {
    SHA256_State s;

    SHA256_Init(&s);
    SHA256_Bytes(&s, p, len);
    SHA256_Final(&s, output);
    smemclr(&s, sizeof(s));
}

/*
 * Thin abstraction for things where hashes are pluggable.
 */

static void *putty_sha256_init(void)
{
    SHA256_State *s;

    s = snew(SHA256_State);
    SHA256_Init(s);
    return s;
}

static void *sha256_copy(const void *vold)
{
    const SHA256_State *old = (const SHA256_State *)vold;
    SHA256_State *s;

    s = snew(SHA256_State);
    *s = *old;
    return s;
}

static void sha256_free(void *handle)
{
    SHA256_State *s = handle;

    smemclr(s, sizeof(*s));
    sfree(s);
}

static void sha256_bytes(void *handle, const void *p, int len)
{
    SHA256_State *s = handle;

    SHA256_Bytes(s, p, len);
}

static void sha256_final(void *handle, unsigned char *output)
{
    SHA256_State *s = handle;

    SHA256_Final(s, output);
    sha256_free(s);
}

const struct ssh_hash ssh_sha256 = {
    putty_sha256_init, sha256_copy, sha256_bytes, sha256_final, sha256_free,
    32, "SHA-256"
};

/* ----------------------------------------------------------------------
 * The above is the SHA-256 algorithm itself. Now we implement the
 * HMAC wrapper on it.
 */

static void *sha256_make_context(void *cipher_ctx)
{
    return snewn(3, SHA256_State);
}

static void sha256_free_context(void *handle)
{
    smemclr(handle, 3 * sizeof(SHA256_State));
    sfree(handle);
}

static void sha256_key_internal(void *handle, unsigned char *key, int len)
{
    SHA256_State *keys = (SHA256_State *)handle;
    unsigned char foo[64];
    int i;

    memset(foo, 0x36, 64);
    for (i = 0; i < len && i < 64; i++)
	foo[i] ^= key[i];
    SHA256_Init(&keys[0]);
    SHA256_Bytes(&keys[0], foo, 64);

    memset(foo, 0x5C, 64);
    for (i = 0; i < len && i < 64; i++)
	foo[i] ^= key[i];
    SHA256_Init(&keys[1]);
    SHA256_Bytes(&keys[1], foo, 64);

    smemclr(foo, 64);		       /* burn the evidence */
}

static void sha256_key(void *handle, unsigned char *key)
{
    sha256_key_internal(handle, key, 32);
}

static void hmacsha256_start(void *handle)
{
    SHA256_State *keys = (SHA256_State *)handle;

    keys[2] = keys[0];		      /* structure copy */
}

static void hmacsha256_bytes(void *handle, unsigned char const *blk, int len)
{
    SHA256_State *keys = (SHA256_State *)handle;
    SHA256_Bytes(&keys[2], (void *)blk, len);
}

static void hmacsha256_genresult(void *handle, unsigned char *hmac)
{
    SHA256_State *keys = (SHA256_State *)handle;
    SHA256_State s;
    unsigned char intermediate[32];

    s = keys[2];		       /* structure copy */
    SHA256_Final(&s, intermediate);
    s = keys[1];		       /* structure copy */
    SHA256_Bytes(&s, intermediate, 32);
    SHA256_Final(&s, hmac);
}

static void sha256_do_hmac(void *handle, unsigned char *blk, int len,
			 unsigned long seq, unsigned char *hmac)
{
    unsigned char seqbuf[4];

    PUT_32BIT_MSB_FIRST(seqbuf, seq);
    hmacsha256_start(handle);
    hmacsha256_bytes(handle, seqbuf, 4);
    hmacsha256_bytes(handle, blk, len);
    hmacsha256_genresult(handle, hmac);
}

static void sha256_generate(void *handle, unsigned char *blk, int len,
			  unsigned long seq)
{
    sha256_do_hmac(handle, blk, len, seq, blk + len);
}

static int hmacsha256_verresult(void *handle, unsigned char const *hmac)
{
    unsigned char correct[32];
    hmacsha256_genresult(handle, correct);
    return smemeq(correct, hmac, 32);
}

static int sha256_verify(void *handle, unsigned char *blk, int len,
		       unsigned long seq)
{
    unsigned char correct[32];
    sha256_do_hmac(handle, blk, len, seq, correct);
    return smemeq(correct, blk + len, 32);
}

const struct ssh_mac ssh_hmac_sha256 = {
    sha256_make_context, sha256_free_context, sha256_key,
    sha256_generate, sha256_verify,
    hmacsha256_start, hmacsha256_bytes,
    hmacsha256_genresult, hmacsha256_verresult,
    "hmac-sha2-256", "hmac-sha2-256-etm@openssh.com",
    32, 32,
    "HMAC-SHA-256"
};

#ifdef TEST

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    unsigned char digest[32];
    int i, j, errors;

    struct {
	const char *teststring;
	unsigned char digest[32];
    } tests[] = {
	{ "abc", {
	    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
	    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
	    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
	    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
	} },
	{ "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", {
	    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
	    0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
	    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
	    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
	} },
    };

    errors = 0;

    for (i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
	SHA256_Simple(tests[i].teststring,
		      strlen(tests[i].teststring), digest);
	for (j = 0; j < 32; j++) {
	    if (digest[j] != tests[i].digest[j]) {
		fprintf(stderr,
			"\"%s\" digest byte %d should be 0x%02x, is 0x%02x\n",
			tests[i].teststring, j, tests[i].digest[j], digest[j]);
		errors++;
	    }
	}
    }

    printf("%d errors\n", errors);

    return 0;
}

#endif
