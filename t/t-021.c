/*
 * base64.
 *
 * the encoder is checked against the vectors in rfc 4648 section 10, which
 * between them cover all three lengths a final group can have. the decoder is
 * checked against the same vectors read backwards, and then the two are run
 * against each other over every length up to 64 - a round trip catches a pair
 * of matching mistakes only if they are not each other's inverse, which is
 * why the published vectors come first.
 *
 * the rest is what a decoder meets in the wild and must not be casual about:
 * characters outside the alphabet, padding in the wrong place, data after the
 * padding, and a final group of one character that no encoder produces.
 */

#include <hio-utl.h>
#include <hio-sha1.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */

static void ck_enc_opt (const char* in, hio_oow_t n, const char* want, int options, const char* what)
{
	hio_bch_t out[128];
	hio_oow_t inlen = n, outlen = sizeof(out);

	OK (hio_conv_bin_to_base64(in, &inlen, out, &outlen, options) == 0 &&
	    outlen == strlen(want) && memcmp(out, want, outlen) == 0, what);
}

static void ck_enc (const char* in, const char* want, const char* what)
{
	ck_enc_opt (in, strlen(in), want, 0, what);
}

static void ck_dec_opt (const char* in, const char* want, hio_oow_t wantlen, int options, const char* what)
{
	hio_uint8_t out[128];
	hio_oow_t inlen = strlen(in), outlen = sizeof(out);

	OK (hio_conv_base64_to_bin(in, &inlen, out, &outlen, options) == 0 &&
	    outlen == wantlen && memcmp(out, want, wantlen) == 0, what);
}

static void ck_dec (const char* in, const char* want, hio_oow_t wantlen, const char* what)
{
	ck_dec_opt (in, want, wantlen, 0, what);
}

/* ------------------------------------------------------------------ */

/* rfc 4648 section 10 */
static void test_vectors (void)
{
	ck_enc ("", "", "encoding nothing yields nothing");
	ck_enc ("f", "Zg==", "one octet pads out to two characters and two pads");
	ck_enc ("fo", "Zm8=", "two octets pad out to three characters and one pad");
	ck_enc ("foo", "Zm9v", "three octets fill a group exactly");
	ck_enc ("foob", "Zm9vYg==", "four octets");
	ck_enc ("fooba", "Zm9vYmE=", "five octets");
	ck_enc ("foobar", "Zm9vYmFy", "six octets fill two groups exactly");

	ck_dec ("", "", 0, "decoding nothing yields nothing");
	ck_dec ("Zg==", "f", 1, "two pads mean one octet");
	ck_dec ("Zm8=", "fo", 2, "one pad means two octets");
	ck_dec ("Zm9v", "foo", 3, "a full group means three octets");
	ck_dec ("Zm9vYg==", "foob", 4, "four octets come back");
	ck_dec ("Zm9vYmE=", "fooba", 5, "five octets come back");
	ck_dec ("Zm9vYmFy", "foobar", 6, "six octets come back");
}

/* every octet value has to survive, and every length has to land on the right
 * one of the three final-group cases */
static void test_round_trip (void)
{
	hio_uint8_t in[64], back[128];
	hio_bch_t enc[256];
	hio_oow_t n, a, b, c;
	int ok_all = 1, ok_len = 1;

	for (n = 0; n < HIO_COUNTOF(in); n++) in[n] = (hio_uint8_t)(n * 251 + 7); /* spread over the byte range */

	for (n = 0; n <= HIO_COUNTOF(in); n++)
	{
		a = n; b = sizeof(enc);
		if (hio_conv_bin_to_base64(in, &a, enc, &b, 0) != 0 || a != n) { ok_all = 0; break; }
		if (b != HIO_BASE64_LEN(n)) ok_len = 0;

		c = sizeof(back);
		if (hio_conv_base64_to_bin(enc, &b, back, &c, 0) != 0) { ok_all = 0; break; }
		if (c != n || memcmp(in, back, n) != 0) { ok_all = 0; break; }
	}

	OK (ok_all, "every length to 64 survives an encode and a decode unchanged");
	OK (ok_len, "and the encoded length is always what HIO_BASE64_LEN says");
}

/* passing no output buffer sizes the conversion instead of performing it */
static void test_dry_run (void)
{
	hio_oow_t inlen, outlen;

	inlen = 6; outlen = 0;
	OK (hio_conv_bin_to_base64("foobar", &inlen, HIO_NULL, &outlen, 0) == 0 && outlen == 8,
	    "encoding with no buffer reports the room it would need");

	inlen = 8; outlen = 0;
	OK (hio_conv_base64_to_bin("Zm9vYmFy", &inlen, HIO_NULL, &outlen, 0) == 0 && outlen == 6,
	    "and decoding does the same");

	/* the decoded size depends on the padding, not on the input length alone,
	 * which is the reason the dry run is worth having on this side */
	inlen = 8; outlen = 0;
	OK (hio_conv_base64_to_bin("Zm9vYg==", &inlen, HIO_NULL, &outlen, 0) == 0 && outlen == 4,
	    "padding is accounted for when sizing a decode");
}

static void test_small_buffer (void)
{
	hio_bch_t enc[8];
	hio_uint8_t bin[8];
	hio_oow_t inlen, outlen;

	/* a group is written whole or not at all */
	inlen = 6; outlen = 7;
	OK (hio_conv_bin_to_base64("foobar", &inlen, enc, &outlen, 0) == -2,
	    "encoding into a buffer one character short is refused");
	OK (outlen == 4 && inlen == 3, "having written the groups that did fit, and said so");

	inlen = 8; outlen = 2;
	OK (hio_conv_base64_to_bin("Zm9vYmFy", &inlen, bin, &outlen, 0) == -2,
	    "decoding into too small a buffer is refused too");

	/* the exact size must not be refused - an off-by-one here would reject
	 * every correctly sized buffer */
	inlen = 6; outlen = 8;
	OK (hio_conv_bin_to_base64("foobar", &inlen, enc, &outlen, 0) == 0 && outlen == 8,
	    "a buffer of exactly the right size is enough");
}

static void test_illegal_input (void)
{
	hio_uint8_t bin[64];
	hio_oow_t inlen, outlen;

#define REFUSED(s, what) \
	do { \
		inlen = strlen(s); outlen = sizeof(bin); \
		OK (hio_conv_base64_to_bin(s, &inlen, bin, &outlen, 0) == -1, what); \
	} while (0)

	REFUSED ("Zm9v*mFy", "a character outside the alphabet is refused");
	REFUSED ("Zm9vY",    "a final group of one character is refused");
	REFUSED ("=",        "padding with nothing to pad is refused");
	REFUSED ("Z===",     "padding after a single character is refused");
	REFUSED ("Zm9v=",    "padding at a group boundary is refused");
	REFUSED ("Zg==Zg==", "data after the padding is refused");
	REFUSED ("Zm9v\x01Zg==", "a control character is refused rather than skipped");

#undef REFUSED
}

/* line-wrapped base64 is the common case in mail and certificates */
static void test_whitespace (void)
{
	ck_dec ("Zm9v YmFy", "foobar", 6, "a space inside the encoding is skipped");
	ck_dec ("Zm9v\r\nYmFy", "foobar", 6, "so is a line break");
	ck_dec ("  Zm9vYmFy  ", "foobar", 6, "and leading and trailing space");
	ck_dec ("Zm8\t=", "fo", 2, "even between a group and its padding");
}

/* unpadded encodings turn up often enough that refusing them would be the
 * wrong call - the length of the final group already says what it carries */
static void test_unpadded (void)
{
	ck_dec ("Zg", "f", 1, "an unpadded final group of two decodes");
	ck_dec ("Zm8", "fo", 2, "an unpadded final group of three decodes");
}

/* rfc 4648 section 5. the two alphabets agree on sixty-two of their
 * characters, so only an input using the last two tells them apart. */
static void test_url_alphabet (void)
{
	/* these three octets encode to a group containing both of the characters
	 * that differ between the alphabets, which is the whole point of picking
	 * them */
	static const char raw[] = { (char)0xfb, (char)0xff, (char)0xbf };

	ck_enc_opt (raw, 3, "+/+/", 0, "the standard alphabet uses + and /");
	ck_enc_opt (raw, 3, "-_-_", HIO_BASE64_URL, "the url alphabet uses - and _ for the same octets");

	ck_dec_opt ("-_-_", raw, 3, HIO_BASE64_URL, "and decodes them back");
	ck_dec_opt ("+/+/", raw, 3, 0, "as the standard alphabet does its own");

	/* a decoder is told which alphabet to expect, so the other one's
	 * characters are not silently taken */
	{
		hio_uint8_t bin[16];
		hio_oow_t inlen, outlen;

		inlen = 4; outlen = sizeof(bin);
		OK (hio_conv_base64_to_bin("-_-_", &inlen, bin, &outlen, 0) == -1,
		    "url characters are refused when the standard alphabet was asked for");

		inlen = 4; outlen = sizeof(bin);
		OK (hio_conv_base64_to_bin("+/+/", &inlen, bin, &outlen, HIO_BASE64_URL) == -1,
		    "and standard characters when the url alphabet was");
	}
}

/* the encoding used by jwt and friends leaves the padding off */
static void test_nopad (void)
{
	ck_enc_opt ("f", 1, "Zg", HIO_BASE64_NOPAD, "one octet unpadded is two characters");
	ck_enc_opt ("fo", 2, "Zm8", HIO_BASE64_NOPAD, "two octets unpadded is three");
	ck_enc_opt ("foo", 3, "Zm9v", HIO_BASE64_NOPAD, "a full group is unaffected by the option");
	ck_enc_opt ("foob", 4, "Zm9vYg", HIO_BASE64_NOPAD, "four octets unpadded");
	ck_enc_opt ("fooba", 5, "Zm9vYmE", HIO_BASE64_NOPAD, "five octets unpadded");

	/* both variants at once is what a jwt segment actually looks like */
	{
		static const char raw[] = { (char)0xfb, (char)0xff, (char)0xbf, (char)0x01 };
		ck_enc_opt (raw, 4, "-_-_AQ", HIO_BASE64_URL | HIO_BASE64_NOPAD,
		            "the url alphabet and no padding combine");
	}

	/* the length macros have to agree with what is actually written */
	{
		hio_bch_t out[64];
		hio_oow_t n, inlen, outlen;
		int ok_pad = 1, ok_nopad = 1;

		for (n = 0; n <= 32; n++)
		{
			inlen = n; outlen = sizeof(out);
			hio_conv_bin_to_base64 ("0123456789012345678901234567890123", &inlen, out, &outlen, 0);
			if (outlen != HIO_BASE64_LEN(n)) ok_pad = 0;

			inlen = n; outlen = sizeof(out);
			hio_conv_bin_to_base64 ("0123456789012345678901234567890123", &inlen, out, &outlen, HIO_BASE64_NOPAD);
			if (outlen != HIO_BASE64_NOPAD_LEN(n)) ok_nopad = 0;
		}

		OK (ok_pad, "HIO_BASE64_LEN matches what is written at every length to 32");
		OK (ok_nopad, "and HIO_BASE64_NOPAD_LEN does the same without padding");
	}
}

/* the whole computation a websocket server performs, with the key and the
 * answer both taken from rfc 6455 section 1.3. sha-1 and base64 are each
 * correct on their own above; this is the two of them wired together. */
static void test_websocket_accept (void)
{
	static const char key[] = "dGhlIHNhbXBsZSBub25jZQ==";
	static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	static const char want[] = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

	hio_sha1_ctx_t sha;
	hio_uint8_t digest[HIO_SHA1_DIGEST_LEN];
	hio_bch_t accept[64];
	hio_oow_t dlen = sizeof(digest), alen = sizeof(accept);
	hio_uint8_t keybin[64];
	hio_oow_t klen = sizeof(key) - 1, kblen = sizeof(keybin);

	hio_sha1_init (&sha);
	hio_sha1_update (&sha, key, sizeof(key) - 1);
	hio_sha1_update (&sha, guid, sizeof(guid) - 1);
	hio_sha1_final (&sha, digest);

	OK (hio_conv_bin_to_base64(digest, &dlen, accept, &alen, 0) == 0 &&
	    alen == sizeof(want) - 1 && memcmp(accept, want, alen) == 0,
	    "the sec-websocket-accept value of rfc 6455 comes out right");

	/* a server also decodes the key to check it is sixteen octets, which is
	 * the one thing rfc 6455 says about its contents */
	OK (hio_conv_base64_to_bin(key, &klen, keybin, &kblen, 0) == 0 && kblen == 16,
	    "and the key it came from decodes to the sixteen octets rfc 6455 requires");
}

/* ------------------------------------------------------------------ */

int main (void)
{
	no_plan ();

	test_vectors ();
	test_round_trip ();
	test_dry_run ();
	test_small_buffer ();
	test_illegal_input ();
	test_whitespace ();
	test_unpadded ();
	test_url_alphabet ();
	test_nopad ();
	test_websocket_accept ();

	return exit_status();
}
