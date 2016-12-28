/*
 * SHA-512 algorithm as described at
 * 
 *   http://csrc.nist.gov/cryptval/shs.html
 *
 * Modifications made for SHA-384 also
 */

#include "ssh.h"
#include <nettle/sha2.h>

/* ----------------------------------------------------------------------
 * Outer SHA512 algorithm: take an arbitrary length byte string,
 * convert it into 16-doubleword blocks with the prescribed padding
 * at the end, and pass those blocks to the core SHA512 algorithm.
 */

void SHA512_Init(SHA512_State *s) {
    sha512_init(s);
}

void SHA384_Init(SHA512_State *s) {
    sha384_init(s);
}

void SHA512_Bytes(SHA512_State *s, const void *p, int len) {
    sha512_update(s, len, p);
}

void SHA512_Final(SHA512_State *s, unsigned char *digest) {
    sha512_digest(s, SHA512_DIGEST_SIZE, digest);
}

void SHA384_Final(SHA512_State *s, unsigned char *digest) {
    sha384_digest(s, SHA384_DIGEST_SIZE, digest);
}

void SHA512_Simple(const void *p, int len, unsigned char *output) {
    SHA512_State s;

    SHA512_Init(&s);
    SHA512_Bytes(&s, p, len);
    SHA512_Final(&s, output);
    smemclr(&s, sizeof(s));
}

void SHA384_Simple(const void *p, int len, unsigned char *output) {
    SHA512_State s;

    SHA384_Init(&s);
    SHA512_Bytes(&s, p, len);
    SHA384_Final(&s, output);
    smemclr(&s, sizeof(s));
}

/*
 * Thin abstraction for things where hashes are pluggable.
 */

static void *putty_sha512_init(void)
{
    SHA512_State *s;

    s = snew(SHA512_State);
    SHA512_Init(s);
    return s;
}

static void *sha512_copy(const void *vold)
{
    const SHA512_State *old = (const SHA512_State *)vold;
    SHA512_State *s;

    s = snew(SHA512_State);
    *s = *old;
    return s;
}

static void sha512_free(void *handle)
{
    SHA512_State *s = handle;

    smemclr(s, sizeof(*s));
    sfree(s);
}

static void sha512_bytes(void *handle, const void *p, int len)
{
    SHA512_State *s = handle;

    SHA512_Bytes(s, p, len);
}

static void sha512_final(void *handle, unsigned char *output)
{
    SHA512_State *s = handle;

    SHA512_Final(s, output);
    sha512_free(s);
}

const struct ssh_hash ssh_sha512 = {
    putty_sha512_init, sha512_copy, sha512_bytes, sha512_final, sha512_free,
    64, "SHA-512"
};

static void *putty_sha384_init(void)
{
    SHA512_State *s;

    s = snew(SHA512_State);
    SHA384_Init(s);
    return s;
}

static void sha384_final(void *handle, unsigned char *output)
{
    SHA512_State *s = handle;

    SHA384_Final(s, output);
    smemclr(s, sizeof(*s));
    sfree(s);
}

const struct ssh_hash ssh_sha384 = {
    putty_sha384_init, sha512_copy, sha512_bytes, sha384_final, sha512_free,
    48, "SHA-384"
};

#ifdef TEST

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void) {
    unsigned char digest[64];
    int i, j, errors;

    struct {
	const char *teststring;
	unsigned char digest512[64];
    } tests[] = {
	{ "abc", {
	    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
            0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
            0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
            0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
            0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
            0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
            0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
            0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f,
	} },
	{ "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
	"hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", {
	    0x8e, 0x95, 0x9b, 0x75, 0xda, 0xe3, 0x13, 0xda,
            0x8c, 0xf4, 0xf7, 0x28, 0x14, 0xfc, 0x14, 0x3f,
            0x8f, 0x77, 0x79, 0xc6, 0xeb, 0x9f, 0x7f, 0xa1,
            0x72, 0x99, 0xae, 0xad, 0xb6, 0x88, 0x90, 0x18,
            0x50, 0x1d, 0x28, 0x9e, 0x49, 0x00, 0xf7, 0xe4,
            0x33, 0x1b, 0x99, 0xde, 0xc4, 0xb5, 0x43, 0x3a,
            0xc7, 0xd3, 0x29, 0xee, 0xb6, 0xdd, 0x26, 0x54,
            0x5e, 0x96, 0xe5, 0x5b, 0x87, 0x4b, 0xe9, 0x09,
	} },
	{ NULL, {
	    0xe7, 0x18, 0x48, 0x3d, 0x0c, 0xe7, 0x69, 0x64,
	    0x4e, 0x2e, 0x42, 0xc7, 0xbc, 0x15, 0xb4, 0x63,
	    0x8e, 0x1f, 0x98, 0xb1, 0x3b, 0x20, 0x44, 0x28,
	    0x56, 0x32, 0xa8, 0x03, 0xaf, 0xa9, 0x73, 0xeb,
	    0xde, 0x0f, 0xf2, 0x44, 0x87, 0x7e, 0xa6, 0x0a,
	    0x4c, 0xb0, 0x43, 0x2c, 0xe5, 0x77, 0xc3, 0x1b,
	    0xeb, 0x00, 0x9c, 0x5c, 0x2c, 0x49, 0xaa, 0x2e,
	    0x4e, 0xad, 0xb2, 0x17, 0xad, 0x8c, 0xc0, 0x9b, 
	} },
    };

    errors = 0;

    for (i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
	if (tests[i].teststring) {
	    SHA512_Simple(tests[i].teststring,
			  strlen(tests[i].teststring), digest);
	} else {
	    SHA512_State s;
	    int n;
	    SHA512_Init(&s);
	    for (n = 0; n < 1000000 / 40; n++)
		SHA512_Bytes(&s, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			     40);
	    SHA512_Final(&s, digest);
	}
	for (j = 0; j < 64; j++) {
	    if (digest[j] != tests[i].digest512[j]) {
		fprintf(stderr,
			"\"%s\" digest512 byte %d should be 0x%02x, is 0x%02x\n",
			tests[i].teststring, j, tests[i].digest512[j],
			digest[j]);
		errors++;
	    }
	}

    }

    printf("%d errors\n", errors);

    return 0;
}

#endif
