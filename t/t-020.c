/*
 * the md5, sha-1 and sha-256 digests.
 *
 * these are pure functions - bytes in, a fixed-width digest out - so they are
 * checked against the vectors published with the algorithms rather than
 * against each other. a digest that is wrong in a way both the reader and the
 * writer agree on is exactly the failure a self-consistency test misses.
 *
 * the cases that matter beyond the published vectors are the ones the block
 * buffering gets wrong: a message that ends one octet short of the point where
 * the length no longer fits, one exactly at it, and one that is fed in pieces
 * rather than whole. the padding takes a different branch on each side of that
 * boundary and the two have to agree.
 */

#include <hio-md5.h>
#include <hio-sha1.h>
#include <hio-sha256.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* a digest is compared as text so a failure reports what it actually got */
static void tohex (hio_bch_t* out, const hio_uint8_t* d, int n)
{
	static const char* x = "0123456789abcdef";
	int i;
	for (i = 0; i < n; i++)
	{
		out[i * 2]     = x[(d[i] >> 4) & 0x0f];
		out[i * 2 + 1] = x[d[i] & 0x0f];
	}
	out[n * 2] = '\0';
}

static void ck_md5 (const void* data, hio_oow_t len, const char* want, const char* what)
{
	hio_uint8_t d[HIO_MD5_DIGEST_LEN];
	hio_bch_t got[HIO_MD5_DIGEST_LEN * 2 + 1];

	hio_md5_digest (d, data, len);
	tohex (got, d, HIO_MD5_DIGEST_LEN);
	OK (strcmp(got, want) == 0, what);
}

static void ck_sha1 (const void* data, hio_oow_t len, const char* want, const char* what)
{
	hio_uint8_t d[HIO_SHA1_DIGEST_LEN];
	hio_bch_t got[HIO_SHA1_DIGEST_LEN * 2 + 1];

	hio_sha1_digest (d, data, len);
	tohex (got, d, HIO_SHA1_DIGEST_LEN);
	OK (strcmp(got, want) == 0, what);
}

static void ck_sha256 (const void* data, hio_oow_t len, const char* want, const char* what)
{
	hio_uint8_t d[HIO_SHA256_DIGEST_LEN];
	hio_bch_t got[HIO_SHA256_DIGEST_LEN * 2 + 1];

	hio_sha256_digest (d, data, len);
	tohex (got, d, HIO_SHA256_DIGEST_LEN);
	OK (strcmp(got, want) == 0, what);
}

/* ------------------------------------------------------------------ */

/* the suite published in rfc 1321 appendix a.5, in full */
static void test_md5_vectors (void)
{
	ck_md5 ("", 0,
	        "d41d8cd98f00b204e9800998ecf8427e",
	        "md5 of the empty message");
	ck_md5 ("a", 1,
	        "0cc175b9c0f1b6a831c399e269772661",
	        "md5 of a");
	ck_md5 ("abc", 3,
	        "900150983cd24fb0d6963f7d28e17f72",
	        "md5 of abc");
	ck_md5 ("message digest", 14,
	        "f96b697d7cb7938d525a2f31aaf161d0",
	        "md5 of message digest");
	ck_md5 ("abcdefghijklmnopqrstuvwxyz", 26,
	        "c3fcd3d76192e4007dfb496cca67e13b",
	        "md5 of the lower-case alphabet");
	ck_md5 ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62,
	        "d174ab98d277d9f5a5611c2c9f419d9f",
	        "md5 of the alphabet and digits");
	ck_md5 ("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80,
	        "57edf4a22be3c955ac49da2e2107b67a",
	        "md5 of the eighty digits that span two blocks");
}

/* fips 180-4 / rfc 3174 publish these */
static void test_sha1_vectors (void)
{
	ck_sha1 ("", 0,
	         "da39a3ee5e6b4b0d3255bfef95601890afd80709",
	         "sha1 of the empty message");
	ck_sha1 ("abc", 3,
	         "a9993e364706816aba3e25717850c26c9cd0d89d",
	         "sha1 of abc");
	ck_sha1 ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
	         "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
	         "sha1 of the 56-octet message that spans two blocks");
	ck_sha1 ("The quick brown fox jumps over the lazy dog", 43,
	         "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12",
	         "sha1 of the quick brown fox");
}

static void test_sha256_vectors (void)
{
	ck_sha256 ("", 0,
	           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	           "sha256 of the empty message");
	ck_sha256 ("abc", 3,
	           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	           "sha256 of abc");
	ck_sha256 ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
	           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
	           "sha256 of the 56-octet message that spans two blocks");
}

/* the million-a vector is the one that exercises the bit counter past what a
 * single block carries, and it is the only published case that does */
static void test_long_message (void)
{
	hio_uint8_t* buf = (hio_uint8_t*)malloc(1000000);

	if (!buf)
	{
		skip ("out of memory for the million-octet vector", 3);
		return;
	}

	memset (buf, 'a', 1000000);
	ck_md5 (buf, 1000000,
	        "7707d6ae4e027c70eea2a935c2296f21",
	        "md5 of one million a");
	ck_sha1 (buf, 1000000,
	         "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
	         "sha1 of one million a");
	ck_sha256 (buf, 1000000,
	           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
	           "sha256 of one million a");
	free (buf);
}

/* the padding chooses between one block and two depending on how much room is
 * left, and 55/56 is where it switches. feeding a whole range across it catches
 * an off-by-one in either direction. */
static void test_block_boundaries (void)
{
	hio_uint8_t buf[200];
	hio_uint8_t c1b[HIO_MD5_DIGEST_LEN], c2b[HIO_MD5_DIGEST_LEN];
	hio_uint8_t d1[HIO_SHA1_DIGEST_LEN], d2[HIO_SHA1_DIGEST_LEN];
	hio_uint8_t e1[HIO_SHA256_DIGEST_LEN], e2[HIO_SHA256_DIGEST_LEN];
	hio_oow_t n;
	int md5_ok = 1, sha1_ok = 1, sha256_ok = 1;

	for (n = 0; n < HIO_COUNTOF(buf); n++) buf[n] = (hio_uint8_t)n;

	/* a one-shot digest and the same message fed one octet at a time must
	 * agree at every length, which is what says the buffering is right */
	for (n = 0; n <= HIO_COUNTOF(buf); n++)
	{
		hio_md5_ctx_t c0;
		hio_sha1_ctx_t c1;
		hio_sha256_ctx_t c2;
		hio_oow_t i;

		hio_md5_digest (c1b, buf, n);
		hio_md5_init (&c0);
		for (i = 0; i < n; i++) hio_md5_update(&c0, &buf[i], 1);
		hio_md5_final (&c0, c2b);
		if (memcmp(c1b, c2b, sizeof(c1b)) != 0) md5_ok = 0;

		hio_sha1_digest (d1, buf, n);
		hio_sha1_init (&c1);
		for (i = 0; i < n; i++) hio_sha1_update(&c1, &buf[i], 1);
		hio_sha1_final (&c1, d2);
		if (memcmp(d1, d2, sizeof(d1)) != 0) sha1_ok = 0;

		hio_sha256_digest (e1, buf, n);
		hio_sha256_init (&c2);
		for (i = 0; i < n; i++) hio_sha256_update(&c2, &buf[i], 1);
		hio_sha256_final (&c2, e2);
		if (memcmp(e1, e2, sizeof(e1)) != 0) sha256_ok = 0;
	}

	OK (md5_ok, "md5 agrees octet by octet with the one-shot at every length to 200");
	OK (sha1_ok, "sha1 agrees octet by octet with the one-shot at every length to 200");
	OK (sha256_ok, "sha256 agrees octet by octet with the one-shot at every length to 200");
}

/* an update call is allowed to carry nothing, and must leave the digest alone */
static void test_empty_update (void)
{
	hio_sha1_ctx_t c1;
	hio_uint8_t d1[HIO_SHA1_DIGEST_LEN], d2[HIO_SHA1_DIGEST_LEN];

	hio_sha1_init (&c1);
	hio_sha1_update (&c1, "", 0);
	hio_sha1_update (&c1, "abc", 3);
	hio_sha1_update (&c1, "", 0);
	hio_sha1_final (&c1, d1);
	hio_sha1_digest (d2, "abc", 3);

	OK (memcmp(d1, d2, sizeof(d1)) == 0, "a zero-length update changes nothing");
}

/* the value rfc 6455 publishes for the websocket handshake. it is the one
 * place sha-1 is not interchangeable with anything else, so it is worth
 * pinning here rather than discovering it wrong against a browser. */
static void test_websocket_accept (void)
{
	static const char key[] = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	static const hio_uint8_t want[HIO_SHA1_DIGEST_LEN] =
	{
		0xb3, 0x7a, 0x4f, 0x2c, 0xc0, 0x62, 0x4f, 0x16, 0x90, 0xf6,
		0x46, 0x06, 0xcf, 0x38, 0x59, 0x45, 0xb2, 0xbe, 0xc4, 0xea
	};
	hio_uint8_t d[HIO_SHA1_DIGEST_LEN];

	hio_sha1_digest (d, key, sizeof(key) - 1);
	OK (memcmp(d, want, sizeof(want)) == 0,
	    "sha1 of the key and guid from rfc 6455 matches the published digest");
}

/* the two digests are different widths and different algorithms; a build that
 * wired one to the other would still pass a single-algorithm check */
static void test_not_confused (void)
{
	hio_uint8_t d1[HIO_SHA1_DIGEST_LEN];
	hio_uint8_t d2[HIO_SHA256_DIGEST_LEN];

	hio_sha1_digest (d1, "abc", 3);
	hio_sha256_digest (d2, "abc", 3);

	OK (HIO_MD5_DIGEST_LEN == 16 && HIO_SHA1_DIGEST_LEN == 20 && HIO_SHA256_DIGEST_LEN == 32,
	    "the digest widths are what the specs say");
	OK (memcmp(d1, d2, HIO_SHA1_DIGEST_LEN) != 0, "and the two algorithms do not produce the same bytes");
}

/* ------------------------------------------------------------------ */

int main (void)
{
	no_plan ();

	test_md5_vectors ();
	test_sha1_vectors ();
	test_sha256_vectors ();
	test_long_message ();
	test_block_boundaries ();
	test_empty_update ();
	test_websocket_accept ();
	test_not_confused ();

	return exit_status();
}
