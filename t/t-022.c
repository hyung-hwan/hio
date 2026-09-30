/*
 * the websocket framing of rfc 6455 section 5.
 *
 * the codec is pure - octets in, octets out - so it is checked against the
 * frames printed in section 5.7 rather than against itself. those cover all
 * three length forms, a masked frame, a fragmented message and a control
 * frame, which between them is most of the format.
 *
 * the rest is what the specification forbids and an implementation has to
 * refuse rather than interpret: an opcode nobody defined, a length written in
 * a longer form than it needed, a control frame split across frames or too
 * large to fit in one, and a length whose top bit is set. a decoder that
 * accepts these is one a peer can steer.
 */

#include <hio-ws.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */

/* rfc 6455 section 5.7 - a single-frame unmasked text message */
static const hio_uint8_t f_hello[]        = { 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f };
/* the same message masked */
static const hio_uint8_t f_hello_masked[] = { 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
/* a fragmented message: "Hel" then "lo" */
static const hio_uint8_t f_frag1[]        = { 0x01, 0x03, 0x48, 0x65, 0x6c };
static const hio_uint8_t f_frag2[]        = { 0x80, 0x02, 0x6c, 0x6f };
/* an unmasked ping, and a masked pong carrying the same body */
static const hio_uint8_t f_ping[]         = { 0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f };
static const hio_uint8_t f_pong_masked[]  = { 0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
/* the headers of a 256-octet and a 65536-octet binary frame */
static const hio_uint8_t f_bin256[]       = { 0x82, 0x7e, 0x01, 0x00 };
static const hio_uint8_t f_bin64k[]       = { 0x82, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };

static void test_published_frames (void)
{
	hio_ws_frame_hdr_t h;

	OK (hio_ws_parse_frame_hdr(f_hello, sizeof(f_hello), &h) == 1 &&
	    h.fin == 1 && h.rsv == 0 && h.opcode == HIO_WS_OPCODE_TEXT &&
	    !h.masked && h.plen == 5 && h.hlen == 2,
	    "the unmasked text frame of rfc 6455 parses");

	OK (hio_ws_parse_frame_hdr(f_hello_masked, sizeof(f_hello_masked), &h) == 1 &&
	    h.fin == 1 && h.opcode == HIO_WS_OPCODE_TEXT && h.masked && h.plen == 5 && h.hlen == 6 &&
	    h.mask[0] == 0x37 && h.mask[1] == 0xfa && h.mask[2] == 0x21 && h.mask[3] == 0x3d,
	    "the masked one parses, masking key and all");

	/* and the masked payload really is the same five letters */
	{
		hio_uint8_t body[5];
		memcpy (body, &f_hello_masked[6], 5);
		hio_ws_mask (body, 5, h.mask, 0);
		OK (memcmp(body, "Hello", 5) == 0, "and its payload unmasks to what it should");
	}

	OK (hio_ws_parse_frame_hdr(f_frag1, sizeof(f_frag1), &h) == 1 &&
	    h.fin == 0 && h.opcode == HIO_WS_OPCODE_TEXT && h.plen == 3,
	    "the first fragment is a text frame that does not end the message");
	OK (hio_ws_parse_frame_hdr(f_frag2, sizeof(f_frag2), &h) == 1 &&
	    h.fin == 1 && h.opcode == HIO_WS_OPCODE_CONT && h.plen == 2,
	    "and the second is a continuation that does");

	OK (hio_ws_parse_frame_hdr(f_ping, sizeof(f_ping), &h) == 1 &&
	    h.opcode == HIO_WS_OPCODE_PING && HIO_WS_OPCODE_IS_CONTROL(h.opcode) && h.plen == 5,
	    "the ping parses and reads as a control frame");
	OK (hio_ws_parse_frame_hdr(f_pong_masked, sizeof(f_pong_masked), &h) == 1 &&
	    h.opcode == HIO_WS_OPCODE_PONG && h.masked && h.plen == 5,
	    "so does the masked pong");

	OK (hio_ws_parse_frame_hdr(f_bin256, sizeof(f_bin256), &h) == 1 &&
	    h.opcode == HIO_WS_OPCODE_BIN && h.plen == 256 && h.hlen == 4,
	    "a 256-octet frame uses the two-octet length form");
	OK (hio_ws_parse_frame_hdr(f_bin64k, sizeof(f_bin64k), &h) == 1 &&
	    h.opcode == HIO_WS_OPCODE_BIN && h.plen == 65536 && h.hlen == 10,
	    "and a 65536-octet frame the eight-octet form");
}

/* a stream splits a header wherever it likes, and every split has to read as
 * 'not yet' rather than as an error or a short frame */
static void test_partial_headers (void)
{
	static const hio_uint8_t* frames[] = { f_hello_masked, f_bin256, f_bin64k };
	static const hio_oow_t lens[] = { sizeof(f_hello_masked), sizeof(f_bin256), sizeof(f_bin64k) };
	hio_ws_frame_hdr_t h;
	int i, ok_all = 1;

	for (i = 0; i < 3; i++)
	{
		hio_oow_t n, hdrlen;

		if (hio_ws_parse_frame_hdr(frames[i], lens[i], &h) != 1) { ok_all = 0; break; }
		hdrlen = h.hlen;

		for (n = 0; n < hdrlen; n++)
		{
			if (hio_ws_parse_frame_hdr(frames[i], n, &h) != 0) { ok_all = 0; break; }
		}
		/* and the very octet that completes it is enough */
		if (hio_ws_parse_frame_hdr(frames[i], hdrlen, &h) != 1) ok_all = 0;
	}

	OK (ok_all, "every prefix of a header reads as incomplete, and the whole one as complete");
}

static void ck_refused (const hio_uint8_t* b, hio_oow_t n, int why, const char* what)
{
	hio_ws_frame_hdr_t h;
	OK (hio_ws_parse_frame_hdr(b, n, &h) == -1 && h.status == why, what);
}

static void test_refusals (void)
{
	/* opcodes 0x3-0x7 and 0xb-0xf are not assigned */
	static const hio_uint8_t bad_op[]      = { 0x83, 0x00 };
	static const hio_uint8_t bad_ctl_op[]  = { 0x8b, 0x00 };
	/* a control frame must arrive whole and must be small */
	static const hio_uint8_t frag_ctl[]    = { 0x09, 0x00 };
	static const hio_uint8_t big_ctl[]     = { 0x89, 0x7e, 0x00, 0x7e };
	/* the shortest length form that fits is the only one allowed */
	static const hio_uint8_t long_short[]  = { 0x81, 0x7e, 0x00, 0x05 };
	static const hio_uint8_t longer_short[]= { 0x81, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
	/* the length is 63 bits, so the top one being set is malformed */
	static const hio_uint8_t topbit[]      = { 0x81, 0x7f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	/* 125 is the largest a control frame may carry, and refusing it would be
	 * an off-by-one rather than a rule */
	static const hio_uint8_t ok125[]       = { 0x89, 0x7d };
	hio_ws_frame_hdr_t h;

	ck_refused (bad_op, sizeof(bad_op), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a reserved non-control opcode is refused");
	ck_refused (bad_ctl_op, sizeof(bad_ctl_op), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a reserved control opcode is refused");
	ck_refused (frag_ctl, sizeof(frag_ctl), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a fragmented control frame is refused");
	ck_refused (big_ctl, sizeof(big_ctl), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a control frame over 125 octets is refused");
	ck_refused (long_short, sizeof(long_short), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a small length written in the two-octet form is refused");
	ck_refused (longer_short, sizeof(longer_short), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a length written in the eight-octet form that the two-octet form held is refused");
	ck_refused (topbit, sizeof(topbit), HIO_WS_STATUS_PROTOCOL_ERROR,
	            "a length with its top bit set is refused");

	OK (hio_ws_parse_frame_hdr(ok125, sizeof(ok125), &h) == 1 && h.plen == 125,
	    "a control frame of exactly 125 octets is accepted");
}

/* a length of 2^32 is legal on the wire and addressable on a machine whose
 * lengths are eight octets wide, but not on one where they are four. that is
 * a different complaint from a malformed frame and earns a different close
 * code, so which answer is right depends on the machine. */
static void test_huge_length (void)
{
	static const hio_uint8_t huge[] = { 0x82, 0x7f, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
	hio_ws_frame_hdr_t h;
	int r = hio_ws_parse_frame_hdr(huge, sizeof(huge), &h);

#if (HIO_SIZEOF_OOW_T >= 8)
	OK (r == 1 && h.plen == ((hio_oow_t)1 << 32),
	    "a payload length of 2^32 is carried where lengths are eight octets wide");
#else
	OK (r == -1 && h.status == HIO_WS_STATUS_MESSAGE_TOO_BIG,
	    "a payload length of 2^32 is refused as too big where lengths are four octets wide");
#endif
}

/* the reserved bits mean whatever an extension says, so the codec reports
 * them and leaves the judgement to whoever knows what was negotiated */
static void test_reserved_bits (void)
{
	static const hio_uint8_t rsv1[] = { 0xc1, 0x00 }; /* fin + rsv1 + text */
	static const hio_uint8_t rsv_all[] = { 0xf1, 0x00 };
	hio_ws_frame_hdr_t h;

	OK (hio_ws_parse_frame_hdr(rsv1, sizeof(rsv1), &h) == 1 && h.rsv == 4,
	    "a frame with rsv1 set parses and reports it");
	OK (hio_ws_parse_frame_hdr(rsv_all, sizeof(rsv_all), &h) == 1 && h.rsv == 7,
	    "and one with all three reserved bits set");
}

/* the key repeats from the start of the payload, so a payload handled in
 * pieces has to give the same answer as one handled whole */
static void test_masking (void)
{
	static const hio_uint8_t key[4] = { 0x37, 0xfa, 0x21, 0x3d };
	hio_uint8_t whole[64], pieces[64];
	hio_oow_t i, chunk;
	int ok_all = 1;

	for (i = 0; i < sizeof(whole); i++) whole[i] = pieces[i] = (hio_uint8_t)(i * 7 + 1);

	hio_ws_mask (whole, sizeof(whole), key, 0);

	/* masking is its own inverse */
	{
		hio_uint8_t back[64];
		memcpy (back, whole, sizeof(back));
		hio_ws_mask (back, sizeof(back), key, 0);
		for (i = 0; i < sizeof(back); i++) if (back[i] != (hio_uint8_t)(i * 7 + 1)) ok_all = 0;
		OK (ok_all, "masking twice returns the original octets");
	}

	/* every chunk size has to land on the same result, which is what says the
	 * offset is carried rather than restarted */
	for (chunk = 1; chunk <= sizeof(pieces); chunk++)
	{
		hio_oow_t off = 0, done = 0;

		for (i = 0; i < sizeof(pieces); i++) pieces[i] = (hio_uint8_t)(i * 7 + 1);
		while (done < sizeof(pieces))
		{
			hio_oow_t n = sizeof(pieces) - done;
			if (n > chunk) n = chunk;
			off = hio_ws_mask(&pieces[done], n, key, off);
			done += n;
		}
		if (off != sizeof(pieces) || memcmp(whole, pieces, sizeof(whole)) != 0) { ok_all = 0; break; }
	}

	OK (ok_all, "a payload masked in pieces of any size matches one masked whole");
}

static void test_building (void)
{
	static const hio_uint8_t key[4] = { 0x37, 0xfa, 0x21, 0x3d };
	hio_uint8_t buf[HIO_WS_FRAME_HDR_MAX_LEN];
	hio_ws_frame_hdr_t h;
	int n;

	/* the frames from the specification have to come back out byte for byte */
	n = hio_ws_build_frame_hdr(buf, sizeof(buf), 1, HIO_WS_OPCODE_TEXT, HIO_NULL, 5);
	OK (n == 2 && memcmp(buf, f_hello, 2) == 0, "building the unmasked text header reproduces it");

	n = hio_ws_build_frame_hdr(buf, sizeof(buf), 1, HIO_WS_OPCODE_TEXT, key, 5);
	OK (n == 6 && memcmp(buf, f_hello_masked, 6) == 0, "and the masked one, key included");

	n = hio_ws_build_frame_hdr(buf, sizeof(buf), 0, HIO_WS_OPCODE_TEXT, HIO_NULL, 3);
	OK (n == 2 && memcmp(buf, f_frag1, 2) == 0, "and a fragment that does not end the message");

	n = hio_ws_build_frame_hdr(buf, sizeof(buf), 1, HIO_WS_OPCODE_BIN, HIO_NULL, 256);
	OK (n == 4 && memcmp(buf, f_bin256, 4) == 0, "and the two-octet length form");

	n = hio_ws_build_frame_hdr(buf, sizeof(buf), 1, HIO_WS_OPCODE_BIN, HIO_NULL, 65536);
	OK (n == 10 && memcmp(buf, f_bin64k, 10) == 0, "and the eight-octet one");

	/* every length must build into something that parses back to it, and the
	 * form must switch at exactly the right place */
	{
		static const hio_oow_t lens[] = { 0, 1, 125, 126, 127, 65535, 65536, 65537 };
		static const hio_oow_t want[] = { 2, 2, 2,   4,   4,   4,     10,    10 };
		hio_oow_t i;
		int ok_all = 1;

		for (i = 0; i < HIO_COUNTOF(lens); i++)
		{
			n = hio_ws_build_frame_hdr(buf, sizeof(buf), 1, HIO_WS_OPCODE_BIN, HIO_NULL, lens[i]);
			if (n != (int)want[i]) { ok_all = 0; break; }
			if (hio_ws_parse_frame_hdr(buf, n, &h) != 1 || h.plen != lens[i] || h.hlen != (hio_oow_t)n) { ok_all = 0; break; }
		}
		OK (ok_all, "each length builds into the shortest form and parses back unchanged");
	}

	/* a buffer that cannot hold the header is refused rather than half-filled */
	OK (hio_ws_build_frame_hdr(buf, 1, 1, HIO_WS_OPCODE_TEXT, HIO_NULL, 5) == -1,
	    "a buffer too small for the header is refused");
	OK (hio_ws_build_frame_hdr(buf, 5, 1, HIO_WS_OPCODE_TEXT, key, 5) == -1,
	    "and one that has no room for the masking key");
	OK (hio_ws_build_frame_hdr(buf, 2, 1, HIO_WS_OPCODE_TEXT, HIO_NULL, 5) == 2,
	    "while a buffer of exactly the right size is enough");
}

static void test_close_payload (void)
{
	hio_uint8_t buf[128];
	int code, n;
	const hio_bch_t* reason;
	hio_oow_t rlen;

	memset (buf, 0, sizeof(buf));

	/* closing without a body says nothing beyond the fact of closing */
	OK (hio_ws_parse_close_payload(buf, 0, &code, &reason, &rlen) == 0 &&
	    code == HIO_WS_STATUS_NO_STATUS && rlen == 0,
	    "an empty close body reports that no code was given");

	n = hio_ws_build_close_payload(buf, sizeof(buf), HIO_WS_STATUS_NORMAL, HIO_NULL);
	OK (n == 2 && buf[0] == 0x03 && buf[1] == 0xe8, "a code alone is two octets, most significant first");
	OK (hio_ws_parse_close_payload(buf, n, &code, &reason, &rlen) == 0 &&
	    code == HIO_WS_STATUS_NORMAL && rlen == 0, "and reads back");

	n = hio_ws_build_close_payload(buf, sizeof(buf), HIO_WS_STATUS_GOING_AWAY, "bye");
	OK (n == 5, "a reason follows the code");
	OK (hio_ws_parse_close_payload(buf, n, &code, &reason, &rlen) == 0 &&
	    code == HIO_WS_STATUS_GOING_AWAY && rlen == 3 && memcmp(reason, "bye", 3) == 0,
	    "and comes back with it");

	/* one octet is neither a code nor the absence of one */
	buf[0] = 0x03;
	OK (hio_ws_parse_close_payload(buf, 1, &code, &reason, &rlen) == -1,
	    "a one-octet close body is refused");

	/* a code that may not be sent must not be accepted from the wire either */
	buf[0] = 0x03; buf[1] = 0xed; /* 1005 */
	OK (hio_ws_parse_close_payload(buf, 2, &code, &reason, &rlen) == -1,
	    "a close code that stands for the absence of one is refused on the wire");

	OK (hio_ws_build_close_payload(buf, sizeof(buf), HIO_WS_STATUS_ABNORMAL, HIO_NULL) == -1,
	    "and cannot be built");

	/* the whole body rides in a control frame, so it is bounded by one */
	{
		hio_bch_t big[200];
		memset (big, 'x', sizeof(big) - 1);
		big[sizeof(big) - 1] = '\0';
		OK (hio_ws_build_close_payload(buf, sizeof(buf), HIO_WS_STATUS_NORMAL, big) == -1,
		    "a reason too long for a control frame is refused");
	}
}

static void test_status_codes (void)
{
	int ok_all = 1;
	static const int sendable[] = { 1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 3000, 3999, 4000, 4999 };
	static const int not_sendable[] = { 0, 999, 1004, 1005, 1006, 1015, 1016, 2999, 5000, 65535 };
	hio_oow_t i;

	for (i = 0; i < HIO_COUNTOF(sendable); i++)
		if (!hio_ws_status_is_sendable(sendable[i])) ok_all = 0;
	OK (ok_all, "the close codes that may be sent are accepted");

	ok_all = 1;
	for (i = 0; i < HIO_COUNTOF(not_sendable); i++)
		if (hio_ws_status_is_sendable(not_sendable[i])) ok_all = 0;
	OK (ok_all, "and the reserved and local-only ones are not");
}

/* ------------------------------------------------------------------ */

int main (void)
{
	no_plan ();

	test_published_frames ();
	test_partial_headers ();
	test_refusals ();
	test_huge_length ();
	test_reserved_bits ();
	test_masking ();
	test_building ();
	test_close_payload ();
	test_status_codes ();

	return exit_status();
}
