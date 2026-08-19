/*
 * http reader limits.
 *
 * hio_htrd_t accumulates a header block into a growable buffer and has no way
 * of knowing the block will ever end. these exercise the caps that stop a peer
 * from turning one connection into unbounded memory - the octet cap, the line
 * cap, their defaults, and the ability to lift either deliberately.
 *
 * no sockets and no event loop: bytes go straight into hio_htrd_feed(), so
 * every case is deterministic and instantaneous.
 */

#include <hio-htrd.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>

static hio_t* g_hio = HIO_NULL;
static int g_peeked;      /* complete header blocks seen */

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_ERROR | HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static int r_peek (hio_htrd_t* htrd, hio_htre_t* re) { g_peeked++; return 0; }
static int r_poke (hio_htrd_t* htrd, hio_htre_t* re) { return 0; }
static int r_push (hio_htrd_t* htrd, hio_htre_t* re, const hio_bch_t* d, hio_oow_t l) { return 0; }

static hio_htrd_recbs_t g_recbs = { r_peek, r_poke, r_push };

static hio_htrd_t* new_reader (void)
{
	hio_htrd_t* htrd = hio_htrd_open(g_hio, 0);
	if (htrd) hio_htrd_setrecbs (htrd, &g_recbs);
	return htrd;
}

/* feed a buffer in slices, so a limit that only holds within one feed rather
 * than across a whole block shows up as a pass here and a failure in
 * production. returns the hio_htrd_feed() result of the first slice that
 * failed, or 0 if all of it went in. */
static int feed_in_slices (hio_htrd_t* htrd, const hio_bch_t* p, hio_oow_t len, hio_oow_t slice)
{
	hio_oow_t off = 0;
	while (off < len)
	{
		hio_oow_t n = len - off;
		hio_oow_t rem = 0;
		if (n > slice) n = slice;
		if (hio_htrd_feed(htrd, &p[off], n, &rem) <= -1) return -1;
		off += n;
	}
	return 0;
}

/* ------------------------------------------------------------------ */

static void test_defaults (void)
{
	hio_htrd_t* htrd;
	hio_htrd_lim_t lim;

	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 3); return; }

	hio_htrd_getlimit (htrd, &lim);
	OK (lim.hdrsize == HIO_HTRD_DFL_HDRSIZE, "a new reader is capped on header octets by default");
	OK (lim.hdrcount == HIO_HTRD_DFL_HDRCOUNT, "a new reader is capped on header lines by default");

	lim.hdrsize = 4096;
	lim.hdrcount = 7;
	hio_htrd_setlimit (htrd, &lim);
	HIO_MEMSET (&lim, 0, HIO_SIZEOF(lim));
	hio_htrd_getlimit (htrd, &lim);
	OK (lim.hdrsize == 4096 && lim.hdrcount == 7, "the caps read back as set");

	hio_htrd_close (htrd);
}

static void test_ordinary_request_passes (void)
{
	/* the caps must be invisible to anything reasonable */
	static const hio_bch_t req[] =
		"GET /some/path HTTP/1.1\r\n"
		"Host: example.org\r\n"
		"User-Agent: whatever/1.0\r\n"
		"Accept: */*\r\n"
		"\r\n";
	hio_htrd_t* htrd;

	g_peeked = 0;
	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 2); return; }

	OK (feed_in_slices(htrd, req, HIO_SIZEOF(req) - 1, 7) == 0,
	    "an ordinary request feeds without tripping the default caps");
	OK (g_peeked == 1, "and its header block is reported complete");

	hio_htrd_close (htrd);
}

static void test_hdrsize_cap (void)
{
	hio_htrd_t* htrd;
	hio_htrd_lim_t lim;
	hio_bch_t big[4096];
	hio_oow_t i;
	int fed;

	g_peeked = 0;
	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 3); return; }

	lim.hdrsize = 512;
	lim.hdrcount = 0;   /* isolate the octet cap */
	hio_htrd_setlimit (htrd, &lim);

	/* one header line long enough to blow the cap on its own, and no
	 * terminating blank line - the shape of the attack. */
	memcpy (big, "GET / HTTP/1.1\r\nX-Pad: ", 23);
	for (i = 23; i < HIO_COUNTOF(big); i++) big[i] = 'A';

	fed = feed_in_slices(htrd, big, HIO_COUNTOF(big), 64);
	OK (fed <= -1, "a header block past the octet cap is rejected");
	OK (hio_htrd_geterrnum(htrd) == HIO_HTRD_ETOOBIG, "and reports HIO_HTRD_ETOOBIG");
	OK (g_peeked == 0, "no header block is reported complete");

	hio_htrd_close (htrd);
}

static void test_hdrsize_cap_spans_feeds (void)
{
	/* the cap has to hold across feeds. a peer that sends the block a few
	 * octets at a time never exceeds it within any single feed, which is
	 * precisely how a per-feed check would be defeated. */
	hio_htrd_t* htrd;
	hio_htrd_lim_t lim;
	hio_bch_t big[4096];
	hio_oow_t i;

	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 1); return; }

	lim.hdrsize = 512;
	lim.hdrcount = 0;
	hio_htrd_setlimit (htrd, &lim);

	memcpy (big, "GET / HTTP/1.1\r\nX-Pad: ", 23);
	for (i = 23; i < HIO_COUNTOF(big); i++) big[i] = 'A';

	OK (feed_in_slices(htrd, big, HIO_COUNTOF(big), 1) <= -1,
	    "the octet cap holds when the block arrives one octet per feed");

	hio_htrd_close (htrd);
}

static void test_hdrcount_cap (void)
{
	/* many tiny headers stay well under any octet cap while still making the
	 * header table expensive. */
	hio_htrd_t* htrd;
	hio_htrd_lim_t lim;
	hio_becs_t buf;
	hio_oow_t i;
	int fed;

	g_peeked = 0;
	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 3); return; }

	lim.hdrsize = 0;    /* isolate the line cap */
	lim.hdrcount = 8;
	hio_htrd_setlimit (htrd, &lim);

	if (hio_becs_init(&buf, g_hio, 0) <= -1) { skip ("buffer init failed", 3); hio_htrd_close(htrd); return; }
	hio_becs_cat (&buf, "GET / HTTP/1.1\r\n");
	for (i = 0; i < 64; i++) hio_becs_fcat (&buf, "X-%zu: v\r\n", i);
	hio_becs_cat (&buf, "\r\n");

	fed = feed_in_slices(htrd, HIO_BECS_PTR(&buf), HIO_BECS_LEN(&buf), 9);
	OK (fed <= -1, "a header block past the line cap is rejected");
	OK (hio_htrd_geterrnum(htrd) == HIO_HTRD_ETOOBIG, "and reports HIO_HTRD_ETOOBIG");
	OK (g_peeked == 0, "no header block is reported complete");

	hio_becs_fini (&buf);
	hio_htrd_close (htrd);
}

static void test_caps_can_be_lifted (void)
{
	/* 0 means off. a caller who knows their peer can say so. */
	hio_htrd_t* htrd;
	hio_htrd_lim_t lim;
	hio_becs_t buf;
	hio_oow_t i;

	g_peeked = 0;
	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 1); return; }

	lim.hdrsize = 0;
	lim.hdrcount = 0;
	hio_htrd_setlimit (htrd, &lim);

	if (hio_becs_init(&buf, g_hio, 0) <= -1) { skip ("buffer init failed", 1); hio_htrd_close(htrd); return; }
	hio_becs_cat (&buf, "GET / HTTP/1.1\r\n");
	/* past both defaults: 400 lines and well over 64KB of value */
	for (i = 0; i < 400; i++) hio_becs_fcat (&buf, "X-%zu: %0500d\r\n", i, (int)i);
	hio_becs_cat (&buf, "\r\n");

	OK (feed_in_slices(htrd, HIO_BECS_PTR(&buf), HIO_BECS_LEN(&buf), 512) == 0 && g_peeked == 1,
	    "a block past both defaults feeds fine once the caps are lifted");

	hio_becs_fini (&buf);
	hio_htrd_close (htrd);
}

static void test_counters_reset_between_blocks (void)
{
	/* a keep-alive connection feeds many blocks through one reader, and each
	 * request has to get its own budget - otherwise the nth request on a
	 * connection is rejected for the size of the ones before it.
	 *
	 * the zeroing that makes this work is clear_feed()'s memset of fed.s
	 * between requests, not the explicit reset at the end of a header block;
	 * removing the latter alone does not fail this case. it is kept there
	 * anyway to stay in step with the plen reset beside it. */
	hio_htrd_t* htrd;
	hio_htrd_lim_t lim;
	static const hio_bch_t req[] =
		"GET /x HTTP/1.1\r\n"
		"Host: example.org\r\n"
		"Accept: */*\r\n"
		"\r\n";
	int i, all_ok = 1;

	g_peeked = 0;
	htrd = new_reader();
	if (!htrd) { skip ("reader creation failed", 1); return; }

	/* tight enough that two blocks' worth would not fit under either cap */
	lim.hdrsize = 128;
	lim.hdrcount = 6;
	hio_htrd_setlimit (htrd, &lim);

	for (i = 0; i < 20; i++)
	{
		if (feed_in_slices(htrd, req, HIO_SIZEOF(req) - 1, 5) <= -1) { all_ok = 0; break; }
	}

	OK (all_ok && g_peeked == 20,
	    "twenty requests on one reader each get a fresh octet and line budget");

	hio_htrd_close (htrd);
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;

	no_plan ();

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_LOG, 1, &errinf);
	if (!g_hio)
	{
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	test_defaults ();
	test_ordinary_request_passes ();
	test_hdrsize_cap ();
	test_hdrsize_cap_spans_feeds ();
	test_hdrcount_cap ();
	test_caps_can_be_lifted ();
	test_counters_reset_between_blocks ();

	hio_close (g_hio);
	return exit_status();
}
