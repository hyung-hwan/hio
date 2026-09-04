/*
 * the dhcpv6 message layer.
 *
 * dhcpv6 relaying is not a header field but a containment: a relay agent
 * wraps the message it received whole, as the value of a RELAY-MSG option
 * inside the message it sends on. so every accessor here takes a relay level,
 * and the interesting failures are all about that nesting - an inner message
 * whose length disagrees with the option carrying it, an edit at one level
 * that has to grow or shrink the option at every level outside it, and a
 * packet that claims to nest more deeply than the protocol allows, which is
 * what turns a recursive walker into a stack overflow.
 *
 * like t-017 this layer is pure - a buffer in, a buffer out - so all of it is
 * reachable without a socket.
 */

#include <hio-dhcp.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>

#define BUFCAPA 2048

static hio_uint8_t g_buf[BUFCAPA];

/* ------------------------------------------------------------------ */
/* builders                                                          */

static void init_msg (hio_dhcp6_pktbuf_t* pkt, hio_uint8_t mtype)
{
	OK (hio_dhcp6_init_pktbuf(pkt, g_buf, BUFCAPA) == 0, "pktbuf initialised");
	pkt->hdr->msgtype = mtype;
	pkt->hdr->transid[0] = 0x11;
	pkt->hdr->transid[1] = 0x22;
	pkt->hdr->transid[2] = 0x33;
}

/* write a message header and options into a plain buffer by hand, returning
 * how many octets were used. the edit functions are not involved, so a test
 * of them starts from something they did not build. */
static hio_oow_t put_hdr (hio_uint8_t* b, hio_uint8_t mtype)
{
	b[0] = mtype;
	b[1] = 0xaa; b[2] = 0xbb; b[3] = 0xcc;
	return 4;
}

static hio_oow_t put_relay_hdr (hio_uint8_t* b, hio_uint8_t mtype, hio_uint8_t hop)
{
	HIO_MEMSET (b, 0, HIO_SIZEOF(hio_dhcp6_relay_hdr_t));
	b[0] = mtype;
	b[1] = hop;
	b[2 + 15] = 0x01;       /* link address, last octet */
	b[2 + 16 + 15] = 0x02;  /* peer address, last octet */
	return HIO_SIZEOF(hio_dhcp6_relay_hdr_t);
}

static hio_oow_t put_opt (hio_uint8_t* b, hio_uint16_t code, const void* data, hio_uint16_t dlen)
{
	hio_uint16_t v;
	v = hio_hton16(code); HIO_MEMCPY (&b[0], &v, 2);
	v = hio_hton16(dlen); HIO_MEMCPY (&b[2], &v, 2);
	if (dlen > 0) HIO_MEMCPY (&b[4], data, dlen);
	return 4 + dlen;
}

/* an option's data, read out of the packed header without assuming the
 * payload is aligned for a wider load */
static hio_uint16_t opt_code (const hio_dhcp6_opt_hdr_t* o) { return hio_ntoh16(o->code); }
static hio_uint16_t opt_len (const hio_dhcp6_opt_hdr_t* o) { return hio_ntoh16(o->len); }
static const hio_uint8_t* opt_data (const hio_dhcp6_opt_hdr_t* o) { return (const hio_uint8_t*)(o + 1); }

/* ------------------------------------------------------------------ */

static void test_layout (void)
{
	/* the two headers are on the wire exactly as rfc 3315 draws them, so
	 * their sizes are not free to differ from these */
	OK (HIO_SIZEOF(hio_dhcp6_pkt_hdr_t) == 4, "message header is 4 octets");
	OK (HIO_SIZEOF(hio_dhcp6_relay_hdr_t) == 34, "relay header is 34 octets");
	OK (HIO_SIZEOF(hio_dhcp6_opt_hdr_t) == 4, "option header is 4 octets");
}

static void test_init_pktbuf (void)
{
	hio_dhcp6_pktbuf_t pkt;

	OK (hio_dhcp6_init_pktbuf(&pkt, g_buf, BUFCAPA) == 0, "init succeeds with room");
	OK (pkt.len == HIO_SIZEOF(hio_dhcp6_pkt_hdr_t), "a fresh packet is just a header");
	OK (pkt.capa == BUFCAPA, "capacity is recorded");
	OK (pkt.hdr->msgtype == 0, "the header is cleared");

	/* a buffer too small for even the fixed header cannot hold a message */
	OK (hio_dhcp6_init_pktbuf(&pkt, g_buf, 3) <= -1, "init refuses a buffer under 4 octets");
}

static void test_append_and_find (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	static const hio_uint8_t duid[10] = { 0,1,0,1,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f };
	hio_uint16_t elapsed = hio_hton16(0x0102);

	init_msg (&pkt, HIO_DHCP6_MSG_SOLICIT);

	o = hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_CLIENTID, duid, HIO_SIZEOF(duid));
	OK (o != HIO_NULL, "clientid appended");
	OK (pkt.len == 4 + 4 + 10, "the packet grew by header plus data");

	o = hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_ELAPSED_TIME, &elapsed, 2);
	OK (o != HIO_NULL, "elapsed time appended");
	OK (pkt.len == 4 + 14 + 6, "and again");

	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL, "clientid found");
	OK (opt_len(o) == 10, "with the length it was given");
	OK (memcmp(opt_data(o), duid, 10) == 0, "and the data it was given");

	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_ELAPSED_TIME);
	OK (o != HIO_NULL, "elapsed time found");
	OK (opt_len(o) == 2, "two octets of it");

	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (o == HIO_NULL, "an option that was never added is not found");
}

static void test_byte_order (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	const hio_uint8_t* raw;

	/* both fields of a dhcpv6 option header are 16 bit and network order, so
	 * the octets on the wire are fixed regardless of this machine's */
	init_msg (&pkt, HIO_DHCP6_MSG_SOLICIT);
	o = hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_IA_PD, "ab", 2);
	OK (o != HIO_NULL, "option 25 added");

	raw = (const hio_uint8_t*)o;
	OK (raw[0] == 0x00 && raw[1] == 25, "the code is big endian on the wire");
	OK (raw[2] == 0x00 && raw[3] == 0x02, "so is the length");
	OK (opt_code(o) == HIO_DHCP6_OPT_IA_PD, "and reads back in host order");
}

static void test_walk_every_option (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	int n = 0;
	int codes[8];

	init_msg (&pkt, HIO_DHCP6_MSG_REQUEST);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_CLIENTID, "c", 1);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_SERVERID, "ss", 2);
	/* a zero-length option is legal - rapid commit is defined as exactly that */
	o = hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_RAPID_COMMIT, HIO_NULL, 0);
	OK (o != HIO_NULL, "a zero-length option is accepted");
	OK (opt_len(o) == 0, "and carries no data");

	o = HIO_NULL;
	while ((o = hio_dhcp6_get_option_after((hio_dhcp6_pktinf_t*)&pkt, 0, o)))
	{
		if (n < (int)HIO_COUNTOF(codes)) codes[n] = opt_code(o);
		n++;
		if (n > (int)HIO_COUNTOF(codes)) break; /* a walker that does not advance */
	}

	OK (n == 3, "the walk visits every option once");
	OK (codes[0] == HIO_DHCP6_OPT_CLIENTID, "in the order they were appended (1)");
	OK (codes[1] == HIO_DHCP6_OPT_SERVERID, "in the order they were appended (2)");
	OK (codes[2] == HIO_DHCP6_OPT_RAPID_COMMIT, "in the order they were appended (3)");
}

static void test_find_after (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* a;
	hio_dhcp6_opt_hdr_t* b;

	/* several options may share a code - two IA_NAs for two addresses - so
	 * finding one has to be able to continue past it */
	init_msg (&pkt, HIO_DHCP6_MSG_REQUEST);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_IA_NA, "one", 3);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_ELAPSED_TIME, "xx", 2);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_IA_NA, "two", 3);

	a = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_IA_NA);
	OK (a != HIO_NULL && memcmp(opt_data(a), "one", 3) == 0, "the first of two IA_NAs");

	b = hio_dhcp6_find_option_after((hio_dhcp6_pktinf_t*)&pkt, 0, a, HIO_DHCP6_OPT_IA_NA);
	OK (b != HIO_NULL && memcmp(opt_data(b), "two", 3) == 0, "and the second, skipping the option between");

	OK (hio_dhcp6_find_option_after((hio_dhcp6_pktinf_t*)&pkt, 0, b, HIO_DHCP6_OPT_IA_NA) == HIO_NULL,
	    "and no third");
}

/* ------------------------------------------------------------------ */
/* relay nesting                                                     */

/* RELAY-FORW { RELAY-MSG: RELAY-FORW { RELAY-MSG: SOLICIT { CLIENTID } } },
 * built by hand so the nesting under test is not the nesting the edit
 * functions produce. returns the total length. */
static hio_oow_t build_two_level_relay (hio_uint8_t* b)
{
	hio_uint8_t inner[64];
	hio_uint8_t mid[128];
	hio_oow_t ilen = 0, mlen = 0, olen = 0;

	ilen += put_hdr(&inner[ilen], HIO_DHCP6_MSG_SOLICIT);
	ilen += put_opt(&inner[ilen], HIO_DHCP6_OPT_CLIENTID, "innerduid", 9);

	mlen += put_relay_hdr(&mid[mlen], HIO_DHCP6_MSG_RELAYFORW, 0);
	mlen += put_opt(&mid[mlen], HIO_DHCP6_OPT_INTERFACE_ID, "eth1", 4);
	mlen += put_opt(&mid[mlen], HIO_DHCP6_OPT_RELAY_MESSAGE, inner, (hio_uint16_t)ilen);

	olen += put_relay_hdr(&b[olen], HIO_DHCP6_MSG_RELAYFORW, 1);
	olen += put_opt(&b[olen], HIO_DHCP6_OPT_RELAY_MESSAGE, mid, (hio_uint16_t)mlen);
	olen += put_opt(&b[olen], HIO_DHCP6_OPT_INTERFACE_ID, "eth0", 4);

	return olen;
}

static void test_relay_level (void)
{
	hio_dhcp6_pktinf_t pkt;
	hio_dhcp6_pkt_hdr_t* h;

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = build_two_level_relay(g_buf);

	OK (hio_dhcp6_get_relay_level(&pkt) == 2, "two relay agents means level 2");

	h = hio_dhcp6_get_pkt_hdr(&pkt, 0);
	OK (h != HIO_NULL && h->msgtype == HIO_DHCP6_MSG_RELAYFORW, "level 0 is the outermost relay");

	h = hio_dhcp6_get_pkt_hdr(&pkt, 1);
	OK (h != HIO_NULL && h->msgtype == HIO_DHCP6_MSG_RELAYFORW, "level 1 is the inner relay");
	OK (h != HIO_NULL && ((hio_dhcp6_relay_hdr_t*)h)->hopcount == 0, "with its own hop count");

	h = hio_dhcp6_get_pkt_hdr(&pkt, 2);
	OK (h != HIO_NULL && h->msgtype == HIO_DHCP6_MSG_SOLICIT, "level 2 is the client's own message");

	h = hio_dhcp6_get_pkt_hdr(&pkt, -1);
	OK (h != HIO_NULL && h->msgtype == HIO_DHCP6_MSG_SOLICIT, "a negative level means the innermost");

	/* a level the packet does not nest to is an error. returning the
	 * innermost instead would have a caller read one message believing it
	 * was another. */
	OK (hio_dhcp6_get_pkt_hdr(&pkt, 3) == HIO_NULL, "a level past the nesting is refused");

	/* a plain message is level 0 and nothing else */
	pkt.len = put_hdr(g_buf, HIO_DHCP6_MSG_SOLICIT);
	OK (hio_dhcp6_get_relay_level(&pkt) == 0, "an unrelayed message is level 0");
	h = hio_dhcp6_get_pkt_hdr(&pkt, 0);
	OK (h != HIO_NULL && h->msgtype == HIO_DHCP6_MSG_SOLICIT, "and level 0 is itself");
	OK (hio_dhcp6_get_pkt_hdr(&pkt, 1) == HIO_NULL, "asking it for level 1 fails");
}

static void test_find_per_level (void)
{
	hio_dhcp6_pktinf_t pkt;
	hio_dhcp6_opt_hdr_t* o;

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = build_two_level_relay(g_buf);

	/* the two relay levels each carry an INTERFACE_ID, and they differ. this
	 * is the case that catches a find that ignores its level argument. */
	o = hio_dhcp6_find_option(&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (o != HIO_NULL && opt_len(o) == 4 && memcmp(opt_data(o), "eth0", 4) == 0,
	    "level 0 finds the outer relay's interface id");

	o = hio_dhcp6_find_option(&pkt, 1, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (o != HIO_NULL && opt_len(o) == 4 && memcmp(opt_data(o), "eth1", 4) == 0,
	    "level 1 finds the inner relay's, not the outer one's");

	o = hio_dhcp6_find_option(&pkt, 2, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "innerduid", 9) == 0,
	    "level 2 finds the client's own option");

	o = hio_dhcp6_find_option(&pkt, -1, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "innerduid", 9) == 0,
	    "and so does the innermost level");

	/* the client id is at level 2 only */
	OK (hio_dhcp6_find_option(&pkt, 0, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "the client id is not at level 0");
	OK (hio_dhcp6_find_option(&pkt, 1, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "nor at level 1");
	OK (hio_dhcp6_find_option(&pkt, 3, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "and a level past the nesting finds nothing rather than the innermost");
}

static void test_malformed_relay (void)
{
	hio_dhcp6_pktinf_t pkt;
	hio_uint8_t inner[32];
	hio_oow_t ilen = 0, olen = 0;
	hio_uint16_t v;

	/* a RELAY-MSG whose length runs past the end of the packet carrying it */
	ilen += put_hdr(&inner[ilen], HIO_DHCP6_MSG_SOLICIT);
	ilen += put_opt(&inner[ilen], HIO_DHCP6_OPT_CLIENTID, "duid", 4);

	olen += put_relay_hdr(&g_buf[olen], HIO_DHCP6_MSG_RELAYFORW, 0);
	olen += put_opt(&g_buf[olen], HIO_DHCP6_OPT_RELAY_MESSAGE, inner, (hio_uint16_t)ilen);

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = olen;
	OK (hio_dhcp6_get_relay_level(&pkt) == 1, "the well-formed version nests once");

	/* overstate the inner length by one octet */
	v = hio_hton16((hio_uint16_t)(ilen + 1));
	HIO_MEMCPY (&g_buf[HIO_SIZEOF(hio_dhcp6_relay_hdr_t) + 2], &v, 2);
	OK (hio_dhcp6_get_relay_level(&pkt) == 0, "a relay message running past the packet is not followed");
	OK (hio_dhcp6_find_option(&pkt, 1, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "and nothing is read out of it");

	/* a relay with no RELAY-MSG option at all relays nothing */
	olen = put_relay_hdr(g_buf, HIO_DHCP6_MSG_RELAYFORW, 0);
	olen += put_opt(&g_buf[olen], HIO_DHCP6_OPT_INTERFACE_ID, "eth0", 4);
	pkt.len = olen;
	OK (hio_dhcp6_get_relay_level(&pkt) == 0, "a relay carrying nothing is level 0");
	OK (hio_dhcp6_find_option(&pkt, -1, HIO_DHCP6_OPT_INTERFACE_ID) != HIO_NULL,
	    "its own options are still readable");

	/* a relay header truncated mid-address */
	pkt.len = HIO_SIZEOF(hio_dhcp6_relay_hdr_t) - 1;
	OK (hio_dhcp6_get_relay_level(&pkt) == 0, "a truncated relay header is not followed");
	OK (hio_dhcp6_find_option(&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID) == HIO_NULL,
	    "and yields no options");

	/* an option whose length exceeds what is left */
	olen = put_hdr(g_buf, HIO_DHCP6_MSG_SOLICIT);
	olen += put_opt(&g_buf[olen], HIO_DHCP6_OPT_CLIENTID, "duid", 4);
	pkt.len = olen - 1; /* one octet of the value is gone */
	OK (hio_dhcp6_find_option(&pkt, 0, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "an option whose value is cut short is not returned");
}

static void test_nesting_bound (void)
{
	hio_dhcp6_pktinf_t pkt;
	hio_oow_t total;
	int i;
	const int levels = HIO_DHCP6_MAX_RELAY_LEVEL + 8;
	hio_oow_t hdr_at[HIO_DHCP6_MAX_RELAY_LEVEL + 8];

	/* a packet that nests further than the protocol permits. the traversals
	 * are recursive, so without the level cap this is a stack overflow driven
	 * by an attacker's packet rather than a parse failure.
	 *
	 * it is built outside in: write every relay header first, then close the
	 * RELAY-MSG lengths from the inside out. */
	HIO_MEMSET (g_buf, 0, BUFCAPA);
	total = 0;
	for (i = 0; i < levels; i++)
	{
		hio_uint16_t v;
		hdr_at[i] = total;
		total += put_relay_hdr(&g_buf[total], HIO_DHCP6_MSG_RELAYFORW, 0);
		/* the RELAY-MSG header; its length is filled in below */
		v = hio_hton16(HIO_DHCP6_OPT_RELAY_MESSAGE);
		HIO_MEMCPY (&g_buf[total], &v, 2);
		total += 4;
	}
	total += put_hdr(&g_buf[total], HIO_DHCP6_MSG_SOLICIT);
	/* an option down at the bottom. a traversal that followed the nesting all
	 * the way would reach it; one that stops at the cap must not. */
	total += put_opt(&g_buf[total], HIO_DHCP6_OPT_CLIENTID, "toodeep", 7);

	for (i = levels - 1; i >= 0; i--)
	{
		hio_oow_t opt_at = hdr_at[i] + HIO_SIZEOF(hio_dhcp6_relay_hdr_t);
		hio_uint16_t v = hio_hton16((hio_uint16_t)(total - (opt_at + 4)));
		HIO_MEMCPY (&g_buf[opt_at + 2], &v, 2);
	}

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = total;

	OK (total <= BUFCAPA, "the deep packet fits the buffer");
	OK (hio_dhcp6_get_relay_level(&pkt) == HIO_DHCP6_MAX_RELAY_LEVEL,
	    "the nesting is reported no deeper than the protocol allows");

	/* and the walkers stop there too rather than following it down */
	OK (hio_dhcp6_get_pkt_hdr(&pkt, HIO_DHCP6_MAX_RELAY_LEVEL) != HIO_NULL,
	    "the last permitted level is reachable");
	OK (hio_dhcp6_get_pkt_hdr(&pkt, HIO_DHCP6_MAX_RELAY_LEVEL + 1) == HIO_NULL,
	    "one past it is not");
	OK (hio_dhcp6_find_option(&pkt, HIO_DHCP6_MAX_RELAY_LEVEL + 1, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "and a find past it does not recurse");
	OK (hio_dhcp6_find_option(&pkt, levels, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "the option at the bottom of an over-deep packet is not reached");
	OK (hio_dhcp6_find_option(&pkt, -1, HIO_DHCP6_OPT_CLIENTID) == HIO_NULL,
	    "nor is it reached by asking for the innermost");
}

/* a relay message that relays nothing is as deep as its packet goes, so
 * asking for the innermost level has to mean that one - the same answer
 * hio_dhcp6_get_pkt_hdr() gives - rather than nothing at all. otherwise the
 * two disagree about what a malformed relay's innermost message is. */
static void test_innermost_of_a_broken_chain (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_pkt_hdr_t* h;
	hio_dhcp6_opt_hdr_t* o;
	hio_oow_t olen;

	olen = put_relay_hdr(g_buf, HIO_DHCP6_MSG_RELAYFORW, 0);
	olen += put_opt(&g_buf[olen], HIO_DHCP6_OPT_INTERFACE_ID, "eth0", 4);
	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = olen;
	pkt.capa = BUFCAPA;

	h = hio_dhcp6_get_pkt_hdr((hio_dhcp6_pktinf_t*)&pkt, -1);
	OK (h != HIO_NULL && h->msgtype == HIO_DHCP6_MSG_RELAYFORW, "the innermost message is the relay itself");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, -1, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (o != HIO_NULL, "and a find at the innermost level agrees");

	/* and so does an edit */
	o = hio_dhcp6_insert_option_at(&pkt, -1, HIO_NULL, 1, HIO_DHCP6_OPT_PREFERENCE, "p", 1);
	OK (o != HIO_NULL, "an insert at the innermost level lands there");
	OK (pkt.len == olen + 5, "the packet grew");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_PREFERENCE);
	OK (o != HIO_NULL, "which for this packet is level 0");
	OK (hio_dhcp6_delete_option_at(&pkt, -1, o, 1) == 0, "and a delete at the innermost level too");
	OK (pkt.len == olen, "leaving it as it was");

	/* a definite level the packet does not have is still refused */
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_INTERFACE_ID) == HIO_NULL,
	    "but naming level 1 does not settle for level 0");
	OK (hio_dhcp6_insert_option_at(&pkt, 1, HIO_NULL, 1, HIO_DHCP6_OPT_PREFERENCE, "p", 1) == HIO_NULL,
	    "and neither does an insert");
	OK (pkt.len == olen, "which changed nothing");
}

/* ------------------------------------------------------------------ */
/* editing                                                           */

static void test_insert_at_position (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* first;
	hio_dhcp6_opt_hdr_t* o;
	int n = 0;
	int codes[8];

	init_msg (&pkt, HIO_DHCP6_MSG_SOLICIT);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_SERVERID, "sid", 3);
	first = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (first != HIO_NULL, "an option to insert before");

	/* inserting at an existing option's position puts the new one first and
	 * shifts that one along */
	o = hio_dhcp6_insert_option_at(&pkt, 0, first, 1, HIO_DHCP6_OPT_CLIENTID, "cid", 3);
	OK (o != HIO_NULL, "inserted at a position rather than appended");
	OK (o == first, "the new option occupies the old one's place");

	o = HIO_NULL;
	while ((o = hio_dhcp6_get_option_after((hio_dhcp6_pktinf_t*)&pkt, 0, o)))
	{
		if (n < (int)HIO_COUNTOF(codes)) codes[n] = opt_code(o);
		if (++n > (int)HIO_COUNTOF(codes)) break;
	}
	OK (n == 2, "both options are present");
	OK (codes[0] == HIO_DHCP6_OPT_CLIENTID, "the inserted one comes first");
	OK (codes[1] == HIO_DHCP6_OPT_SERVERID, "and the shifted one after it");

	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "sid", 3) == 0, "the shifted option kept its data");
}

static void test_insert_limits (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	static hio_uint8_t big[BUFCAPA];

	init_msg (&pkt, HIO_DHCP6_MSG_SOLICIT);

	/* the buffer bounds the packet: header plus option header plus data */
	OK (hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_USER_CLASS, big, BUFCAPA) == HIO_NULL,
	    "an option larger than the buffer is refused");
	OK (pkt.len == 4, "and the packet is left as it was");

	OK (hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_USER_CLASS, big, BUFCAPA - 4 - 4) != HIO_NULL,
	    "one that exactly fills the buffer is accepted");
	OK (pkt.len == BUFCAPA, "and fills it");

	OK (hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_PREFERENCE, "p", 1) == HIO_NULL,
	    "nothing more fits");
	OK (pkt.len == BUFCAPA, "and the failed insert changed nothing");

	/* an option length field is 16 bits, so data past 65535 cannot be
	 * described - the length would wrap and the option would claim to carry
	 * a different amount than was copied in */
	init_msg (&pkt, HIO_DHCP6_MSG_SOLICIT);
	pkt.capa = 0x20000; /* pretend to a buffer large enough to reach the real limit */
	o = hio_dhcp6_insert_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_USER_CLASS, big, HIO_DHCP6_MAX_OPT_DLEN + 1);
	OK (o == HIO_NULL, "data past what a 16-bit length can express is refused");
	OK (pkt.len == 4, "and nothing was written");
}

static void test_insert_bad_position (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* first;
	hio_uint8_t* p;

	init_msg (&pkt, HIO_DHCP6_MSG_SOLICIT);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_SERVERID, "server", 6);
	first = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (first != HIO_NULL, "a starting packet with one option");

	/* a position one octet into an option is not where an option begins, and
	 * with checking asked for it is refused rather than splicing the packet
	 * into nonsense */
	p = (hio_uint8_t*)first + 1;
	OK (hio_dhcp6_insert_option_at(&pkt, 0, (hio_dhcp6_opt_hdr_t*)p, 1, HIO_DHCP6_OPT_CLIENTID, "c", 1) == HIO_NULL,
	    "a position inside an option is refused");
	OK (pkt.len == 4 + 10, "and the packet is untouched");

	/* as is one outside the packet altogether */
	p = (hio_uint8_t*)pkt.hdr + pkt.len + 8;
	OK (hio_dhcp6_insert_option_at(&pkt, 0, (hio_dhcp6_opt_hdr_t*)p, 1, HIO_DHCP6_OPT_CLIENTID, "c", 1) == HIO_NULL,
	    "a position past the packet is refused");
	OK (pkt.len == 4 + 10, "and that leaves it untouched too");

	/* the position just past the last option is the append point, and is
	 * allowed */
	p = (hio_uint8_t*)pkt.hdr + pkt.len;
	OK (hio_dhcp6_insert_option_at(&pkt, 0, (hio_dhcp6_opt_hdr_t*)p, 1, HIO_DHCP6_OPT_CLIENTID, "c", 1) != HIO_NULL,
	    "the position just after the last option is the append point");
	OK (pkt.len == 4 + 10 + 5, "and the append took");
}

static void test_delete (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	hio_oow_t before;

	init_msg (&pkt, HIO_DHCP6_MSG_REQUEST);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_CLIENTID, "cid", 3);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_ELAPSED_TIME, "xx", 2);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_SERVERID, "sid", 3);
	before = pkt.len;

	/* deleting from the middle closes the gap */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_ELAPSED_TIME);
	OK (o != HIO_NULL, "the option to delete is there");
	OK (hio_dhcp6_delete_option_at(&pkt, 0, o, 1) == 0, "deleted");
	OK (pkt.len == before - 6, "the packet shrank by header plus data");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_ELAPSED_TIME) == HIO_NULL,
	    "and it is gone");

	/* the options either side survived intact, which a wrong move length
	 * would not leave true */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL && opt_len(o) == 3 && memcmp(opt_data(o), "cid", 3) == 0, "the option before it is intact");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (o != HIO_NULL && opt_len(o) == 3 && memcmp(opt_data(o), "sid", 3) == 0, "and the one after it moved down whole");

	/* deleting the last one leaves a message with no options */
	OK (hio_dhcp6_delete_option_at(&pkt, 0, o, 1) == 0, "the last option deleted");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL, "the first is still there");
	OK (hio_dhcp6_delete_option_at(&pkt, 0, o, 1) == 0, "and it deletes too");
	OK (pkt.len == 4, "leaving just the message header");
	OK (hio_dhcp6_get_option_after((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_NULL) == HIO_NULL, "with no options at all");

	OK (hio_dhcp6_delete_option_at(&pkt, 0, HIO_NULL, 1) <= -1, "deleting nothing is refused");
}

static void test_delete_bad_position (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	hio_uint8_t* p;
	hio_uint16_t v;

	init_msg (&pkt, HIO_DHCP6_MSG_REQUEST);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_CLIENTID, "cid", 3);
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL, "one option present");

	/* the append point is not an option, so there is nothing there to delete */
	p = (hio_uint8_t*)pkt.hdr + pkt.len;
	OK (hio_dhcp6_delete_option_at(&pkt, 0, (hio_dhcp6_opt_hdr_t*)p, 1) <= -1,
	    "deleting at the append point is refused");
	OK (pkt.len == 4 + 7, "and the packet is unchanged");

	/* a position past the packet is refused with checking off as well as on:
	 * the move length is derived from it, and an unchecked one underflows */
	p = (hio_uint8_t*)pkt.hdr + pkt.len + 16;
	OK (hio_dhcp6_delete_option_at(&pkt, 0, (hio_dhcp6_opt_hdr_t*)p, 0) <= -1,
	    "a position past the packet is refused even unchecked");
	OK (pkt.len == 4 + 7, "and still unchanged");

	/* nor may the option's own length field reach past the packet */
	v = hio_hton16(4000);
	HIO_MEMCPY ((hio_uint8_t*)o + 2, &v, 2);
	OK (hio_dhcp6_delete_option_at(&pkt, 0, o, 0) <= -1,
	    "an option claiming more data than the packet holds is refused");
	OK (pkt.len == 4 + 7, "and changes nothing");
}

static void test_replace (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	hio_oow_t before;

	init_msg (&pkt, HIO_DHCP6_MSG_REPLY);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_CLIENTID, "cid", 3);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_PREFERENCE, "p", 1);
	hio_dhcp6_insert_option_at (&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_SERVERID, "sid", 3);
	before = pkt.len;

	/* same size: nothing moves */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_PREFERENCE);
	o = hio_dhcp6_replace_option_at(&pkt, 0, o, 1, HIO_DHCP6_OPT_PREFERENCE, "q", 1);
	OK (o != HIO_NULL, "replaced with the same length");
	OK (pkt.len == before, "the packet is the same size");
	OK (opt_data(o)[0] == 'q', "with the new value");

	/* larger: the tail shifts up */
	o = hio_dhcp6_replace_option_at(&pkt, 0, o, 1, HIO_DHCP6_OPT_PREFERENCE, "abcdefgh", 8);
	OK (o != HIO_NULL, "replaced with something longer");
	OK (pkt.len == before + 7, "the packet grew by the difference");
	OK (opt_len(o) == 8 && memcmp(opt_data(o), "abcdefgh", 8) == 0, "and holds the longer value");

	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "sid", 3) == 0, "the option after it shifted up whole");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "cid", 3) == 0, "the one before it did not move");

	/* smaller: the tail shifts down */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_PREFERENCE);
	o = hio_dhcp6_replace_option_at(&pkt, 0, o, 1, HIO_DHCP6_OPT_PREFERENCE, HIO_NULL, 0);
	OK (o != HIO_NULL, "replaced with nothing at all");
	OK (pkt.len == before - 1, "the packet shrank");
	OK (opt_len(o) == 0, "and the option carries no data");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_SERVERID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "sid", 3) == 0, "with the tail intact again");

	/* the code may change too, not only the value */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_PREFERENCE);
	o = hio_dhcp6_replace_option_at(&pkt, 0, o, 1, HIO_DHCP6_OPT_RAPID_COMMIT, HIO_NULL, 0);
	OK (o != HIO_NULL, "replaced with a different code");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_PREFERENCE) == HIO_NULL,
	    "the old code is gone");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RAPID_COMMIT) != HIO_NULL,
	    "and the new one is there");

	/* a replacement that will not fit is refused, and changes nothing */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_CLIENTID);
	before = pkt.len;
	OK (hio_dhcp6_replace_option_at(&pkt, 0, o, 1, HIO_DHCP6_OPT_CLIENTID, g_buf, BUFCAPA) == HIO_NULL,
	    "a replacement past the buffer is refused");
	OK (pkt.len == before, "and the packet is unchanged");
	OK (hio_dhcp6_replace_option_at(&pkt, 0, HIO_NULL, 1, HIO_DHCP6_OPT_CLIENTID, "x", 1) == HIO_NULL,
	    "replacing nothing is refused");
}

/* ------------------------------------------------------------------ */
/* editing through a relay chain                                     */

static void test_edit_inner_level (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	hio_dhcp6_opt_hdr_t* rm0;
	hio_dhcp6_opt_hdr_t* rm1;
	hio_oow_t before;
	hio_uint16_t rm0_len, rm1_len;

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = build_two_level_relay(g_buf);
	pkt.capa = BUFCAPA;
	before = pkt.len;

	rm0 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	rm1 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_RELAY_MESSAGE);
	OK (rm0 != HIO_NULL && rm1 != HIO_NULL, "both relay message options found");
	rm0_len = opt_len(rm0);
	rm1_len = opt_len(rm1);

	/* appending inside the client's own message has to grow the RELAY-MSG
	 * option at each of the two levels enclosing it, or the packet stops
	 * describing itself */
	o = hio_dhcp6_insert_option_at(&pkt, 2, HIO_NULL, 1, HIO_DHCP6_OPT_ELAPSED_TIME, "\x00\x05", 2);
	OK (o != HIO_NULL, "an option appended at level 2");
	OK (pkt.len == before + 6, "the whole packet grew by header plus data");

	rm0 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	rm1 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_RELAY_MESSAGE);
	OK (rm0 != HIO_NULL && opt_len(rm0) == rm0_len + 6, "the outer relay option grew to match");
	OK (rm1 != HIO_NULL && opt_len(rm1) == rm1_len + 6, "and so did the inner one");

	/* which is to say the nesting still parses, and everything is still
	 * where it was */
	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 2, "the packet still nests twice");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 2, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "innerduid", 9) == 0, "the client id survived");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 2, HIO_DHCP6_OPT_ELAPSED_TIME);
	OK (o != HIO_NULL && opt_len(o) == 2, "and the new option is at level 2");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_ELAPSED_TIME) == HIO_NULL,
	    "not at level 0");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "eth1", 4) == 0, "the inner relay's own option is intact");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "eth0", 4) == 0, "and so is the outer relay's");

	/* deleting it again puts every length back */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 2, HIO_DHCP6_OPT_ELAPSED_TIME);
	OK (hio_dhcp6_delete_option_at(&pkt, 2, o, 1) == 0, "and deleted from level 2");
	OK (pkt.len == before, "the packet is its original size");
	rm0 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	rm1 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_RELAY_MESSAGE);
	OK (rm0 != HIO_NULL && opt_len(rm0) == rm0_len, "the outer relay option shrank back");
	OK (rm1 != HIO_NULL && opt_len(rm1) == rm1_len, "and so did the inner one");
	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 2, "and it still nests twice");

	/* the round trip put every octet back, not merely every length */
	{
		static hio_uint8_t fresh[BUFCAPA];
		hio_oow_t flen = build_two_level_relay(fresh);
		OK (flen == pkt.len && memcmp(fresh, g_buf, flen) == 0,
		    "insert then delete leaves the packet byte for byte as it was");
	}
}

static void test_edit_middle_level (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	hio_dhcp6_opt_hdr_t* rm0;
	hio_uint16_t rm0_len, rm1_len;
	hio_oow_t before;

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = build_two_level_relay(g_buf);
	pkt.capa = BUFCAPA;
	before = pkt.len;

	rm0_len = opt_len(hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE));
	rm1_len = opt_len(hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_RELAY_MESSAGE));

	/* an edit at level 1 grows the option outside it but not the one inside */
	o = hio_dhcp6_insert_option_at(&pkt, 1, HIO_NULL, 1, HIO_DHCP6_OPT_VENDOR_VSI, "vs", 2);
	OK (o != HIO_NULL, "an option appended at level 1");
	OK (pkt.len == before + 6, "the packet grew");

	rm0 = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	OK (rm0 != HIO_NULL && opt_len(rm0) == rm0_len + 6, "the enclosing relay option grew");
	OK (opt_len(hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_RELAY_MESSAGE)) == rm1_len,
	    "the one it encloses did not");

	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_VENDOR_VSI) != HIO_NULL,
	    "the new option reads back at level 1");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 2, HIO_DHCP6_OPT_CLIENTID) != HIO_NULL,
	    "and the client's message is still reachable through it");

	/* replacing at level 0 shifts everything nested inside, so the whole
	 * chain has to come out the other side */
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID);
	o = hio_dhcp6_replace_option_at(&pkt, 0, o, 1, HIO_DHCP6_OPT_INTERFACE_ID, "eth0.100", 8);
	OK (o != HIO_NULL, "the outermost interface id replaced with a longer one");
	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 2, "the packet still nests twice");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 2, HIO_DHCP6_OPT_CLIENTID);
	OK (o != HIO_NULL && memcmp(opt_data(o), "innerduid", 9) == 0, "and the innermost option is intact");
}

/* an edit that cannot be completed must leave the packet exactly as it was.
 * checking the lengths is not enough: an implementation that shifts the octets
 * first and only then discovers the enclosing option cannot describe the
 * result reports the failure with a packet already spliced. */
static hio_uint8_t g_snap[BUFCAPA];
static hio_oow_t g_snap_len;

static void snapshot (const hio_dhcp6_pktbuf_t* pkt)
{
	g_snap_len = pkt->len;
	HIO_MEMCPY (g_snap, pkt->hdr, pkt->len);
}

static int unchanged (const hio_dhcp6_pktbuf_t* pkt)
{
	return pkt->len == g_snap_len && memcmp(g_snap, pkt->hdr, g_snap_len) == 0;
}

static void test_relay_length_bound (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* rm;
	hio_dhcp6_opt_hdr_t* o;
	hio_uint16_t v;

	/* understate what the outer relay option carries, then remove something
	 * from inside it. the removal cannot be subtracted from a length that
	 * small, and an implementation that subtracted anyway would wrap the
	 * field round to just under 65536.
	 *
	 * the position and the level are taken while the packet is still
	 * consistent, since corrupting the length is what makes level 1
	 * unreachable through a lookup - which is why the edit is unchecked. */
	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = build_two_level_relay(g_buf);
	pkt.capa = BUFCAPA;

	rm = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (rm != HIO_NULL && o != HIO_NULL, "a relay chain to edit inside");
	v = hio_hton16(2);
	HIO_MEMCPY ((hio_uint8_t*)rm + 2, &v, 2);
	snapshot (&pkt);

	OK (hio_dhcp6_delete_option_at(&pkt, 1, o, 0) <= -1,
	    "a delete larger than the enclosing option claims to hold is refused");
	OK (opt_len(rm) == 2, "the length is not wrapped below zero");
	OK (unchanged(&pkt), "and nothing was moved");

	/* a replacement that shrinks is the same problem */
	OK (hio_dhcp6_replace_option_at(&pkt, 1, o, 0, HIO_DHCP6_OPT_INTERFACE_ID, HIO_NULL, 0) == HIO_NULL,
	    "a replacement shrinking past it is refused too");
	OK (opt_len(rm) == 2, "with the length still unwrapped");
	OK (unchanged(&pkt), "and the packet untouched");
}

/* the other direction needs a real packet of very nearly the size an option
 * length can express - a relay option cannot be overstated and still be
 * traversed, so the only way to reach the top of the range is to get there
 * honestly. */
#define HUGE_INNER_DLEN (65500)
#define HUGE_CAPA (HUGE_INNER_DLEN + 512)
static hio_uint8_t g_huge[HUGE_CAPA];
static hio_uint8_t g_huge_snap[HUGE_CAPA];

static void test_enclosing_length_ceiling (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* rm;
	hio_dhcp6_opt_hdr_t* o;
	hio_uint8_t* inner;
	hio_oow_t ilen, olen, snap_len;

	/* RELAY-FORW { RELAY-MSG: SOLICIT { USER-CLASS of 65500 octets } } */
	inner = &g_huge[HIO_SIZEOF(hio_dhcp6_relay_hdr_t) + 4];
	HIO_MEMSET (inner, 0x5a, HUGE_INNER_DLEN + 8);
	ilen = put_hdr(inner, HIO_DHCP6_MSG_SOLICIT);
	ilen += put_opt(&inner[ilen], HIO_DHCP6_OPT_USER_CLASS, HIO_NULL, 0);
	{
		hio_uint16_t v = hio_hton16(HUGE_INNER_DLEN);
		HIO_MEMCPY (&inner[ilen - 2], &v, 2);
		ilen += HUGE_INNER_DLEN;
	}
	OK (ilen <= HIO_DHCP6_MAX_OPT_DLEN, "the inner message fits an option length");

	olen = put_relay_hdr(g_huge, HIO_DHCP6_MSG_RELAYFORW, 0);
	{
		hio_uint16_t v;
		v = hio_hton16(HIO_DHCP6_OPT_RELAY_MESSAGE); HIO_MEMCPY (&g_huge[olen], &v, 2);
		v = hio_hton16((hio_uint16_t)ilen); HIO_MEMCPY (&g_huge[olen + 2], &v, 2);
		olen += 4 + ilen;
	}
	/* the relay's own option, after the one it relays. an insert inside the
	 * relayed message has to shift this along, so a refused insert that moved
	 * the octets anyway is visible. */
	olen += put_opt(&g_huge[olen], HIO_DHCP6_OPT_INTERFACE_ID, "eth0", 4);

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_huge;
	pkt.len = olen;
	pkt.capa = HUGE_CAPA;

	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 1, "the large packet nests once");
	rm = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	OK (rm != HIO_NULL && opt_len(rm) == ilen, "and its relay option is at the top of the range");
	o = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_USER_CLASS);
	OK (o != HIO_NULL && opt_len(o) == HUGE_INNER_DLEN, "the large option inside it reads back");

	snap_len = pkt.len;
	HIO_MEMCPY (g_huge_snap, g_huge, snap_len);

	/* the buffer has room; the enclosing option's length field does not */
	OK (pkt.len + 4 + 64 <= pkt.capa, "the buffer could hold another option");
	OK (hio_dhcp6_insert_option_at(&pkt, 1, HIO_NULL, 1, HIO_DHCP6_OPT_PREFERENCE, g_huge, 64) == HIO_NULL,
	    "but an insert the enclosing relay option could not describe is refused");
	OK (opt_len(rm) == ilen, "its length did not wrap");
	OK (pkt.len == snap_len && memcmp(g_huge_snap, g_huge, snap_len) == 0,
	    "and not one octet was moved");

	/* and a replacement that grows past the same ceiling */
	OK (hio_dhcp6_replace_option_at(&pkt, 1, o, 1, HIO_DHCP6_OPT_USER_CLASS, g_huge, HUGE_INNER_DLEN + 64) == HIO_NULL,
	    "a replacement past the ceiling is refused as well");
	OK (opt_len(rm) == ilen, "with its length still intact");
	OK (pkt.len == snap_len && memcmp(g_huge_snap, g_huge, snap_len) == 0, "and the packet unmoved");

	/* one that stays inside it goes through, so the ceiling is a limit and
	 * not a blanket refusal */
	OK (hio_dhcp6_insert_option_at(&pkt, 1, HIO_NULL, 1, HIO_DHCP6_OPT_PREFERENCE, g_huge, HIO_DHCP6_MAX_OPT_DLEN - (int)ilen - 4) != HIO_NULL,
	    "an insert that exactly reaches the ceiling is accepted");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID) != HIO_NULL,
	    "with the relay's own option shifted along intact");
	rm = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	OK (rm != HIO_NULL && opt_len(rm) == HIO_DHCP6_MAX_OPT_DLEN, "filling the enclosing option exactly");
	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 1, "and the packet still parses");
}

/* 'safe' exists because a position is just an address: nothing about it says
 * which level it came from. with checking off, a caller may hand an edit at
 * one level a position belonging to another - and then the option's own size
 * has no relation to the enclosing relay option's length. this is the case
 * where subtracting one from the other runs below zero, so it is where the
 * enclosing length has to be examined before anything moves rather than after.
 */
static void test_position_from_another_level (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* rm;
	hio_dhcp6_opt_hdr_t* outer_opt;
	hio_oow_t olen;

	/* RELAY-FORW { RELAY-MSG: a bare SOLICIT header } + a 20-octet option of
	 * the relay's own. the relayed message is thus far smaller than that
	 * option, which no position taken at the inner level could be. */
	olen = put_relay_hdr(g_buf, HIO_DHCP6_MSG_RELAYFORW, 0);
	{
		hio_uint8_t inner[8];
		hio_oow_t ilen = put_hdr(inner, HIO_DHCP6_MSG_SOLICIT);
		olen += put_opt(&g_buf[olen], HIO_DHCP6_OPT_RELAY_MESSAGE, inner, (hio_uint16_t)ilen);
	}
	olen += put_opt(&g_buf[olen], HIO_DHCP6_OPT_INTERFACE_ID, "0123456789abcdef", 16);

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = olen;
	pkt.capa = BUFCAPA;

	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 1, "the packet nests once");
	rm = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_RELAY_MESSAGE);
	outer_opt = hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID);
	OK (rm != HIO_NULL && opt_len(rm) == 4, "carrying a message of four octets");
	OK (outer_opt != HIO_NULL && opt_len(outer_opt) == 16, "beside an option of twenty");
	snapshot (&pkt);

	/* deleting the outer option as though it were at level 1 would take more
	 * out of the relay option than it holds */
	OK (hio_dhcp6_delete_option_at(&pkt, 1, outer_opt, 0) <= -1,
	    "an unchecked delete at the wrong level is refused");
	OK (opt_len(rm) == 4, "the relay option's length is not wrapped below zero");
	OK (unchanged(&pkt), "and the packet is not spliced");

	/* and so would replacing it with something shorter */
	OK (hio_dhcp6_replace_option_at(&pkt, 1, outer_opt, 0, HIO_DHCP6_OPT_INTERFACE_ID, HIO_NULL, 0) == HIO_NULL,
	    "an unchecked replace at the wrong level is refused as well");
	OK (opt_len(rm) == 4, "with its length still intact");
	OK (unchanged(&pkt), "and the packet still whole");

	/* with checking on, the position is rejected for what it is */
	OK (hio_dhcp6_delete_option_at(&pkt, 1, outer_opt, 1) <= -1,
	    "and a checked delete refuses the position outright");
	OK (unchanged(&pkt), "changing nothing either way");
}

/* the end of one level's options is not the end of the packet - level 0's own
 * options come after everything nested inside it. so the position just past
 * the last option of an inner level is an append point for that level and
 * nothing else; treating it as an option there would edit the enclosing
 * message instead. */
static void test_inner_append_point (void)
{
	hio_dhcp6_pktbuf_t pkt;
	hio_dhcp6_opt_hdr_t* o;
	hio_dhcp6_opt_hdr_t* last;
	hio_uint8_t* area_end;
	hio_oow_t before;

	pkt.hdr = (hio_dhcp6_pkt_hdr_t*)g_buf;
	pkt.len = build_two_level_relay(g_buf);
	pkt.capa = BUFCAPA;

	/* walk level 1 to its last option and step past it */
	last = HIO_NULL;
	while ((o = hio_dhcp6_get_option_after((hio_dhcp6_pktinf_t*)&pkt, 1, last))) last = o;
	OK (last != HIO_NULL, "level 1 has options to walk");
	area_end = (hio_uint8_t*)last + 4 + opt_len(last);
	OK (area_end < (hio_uint8_t*)pkt.hdr + pkt.len, "and its option area ends before the packet does");

	/* level 0's own interface id sits at that address, so a delete which
	 * accepted it would take an option out of the wrong message */
	before = pkt.len;
	snapshot (&pkt);
	OK (hio_dhcp6_delete_option_at(&pkt, 1, (hio_dhcp6_opt_hdr_t*)area_end, 1) <= -1,
	    "deleting at an inner level's append point is refused");
	OK (unchanged(&pkt), "and the enclosing message keeps its option");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_INTERFACE_ID) != HIO_NULL,
	    "which is still where it was");

	/* an insert there, on the other hand, is exactly how one appends to
	 * level 1 - and it must land at level 1, not level 0 */
	o = hio_dhcp6_insert_option_at(&pkt, 1, (hio_dhcp6_opt_hdr_t*)area_end, 1, HIO_DHCP6_OPT_PREFERENCE, "p", 1);
	OK (o != HIO_NULL, "inserting there appends to level 1");
	OK (pkt.len == before + 5, "the packet grew");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 1, HIO_DHCP6_OPT_PREFERENCE) != HIO_NULL,
	    "and the new option is at level 1");
	OK (hio_dhcp6_find_option((hio_dhcp6_pktinf_t*)&pkt, 0, HIO_DHCP6_OPT_PREFERENCE) == HIO_NULL,
	    "not at level 0");
	OK (hio_dhcp6_get_relay_level((hio_dhcp6_pktinf_t*)&pkt) == 2, "and the chain still parses");
}

/* ------------------------------------------------------------------ */

int main (void)
{
	no_plan ();

	test_layout ();
	test_init_pktbuf ();
	test_append_and_find ();
	test_byte_order ();
	test_walk_every_option ();
	test_find_after ();
	test_relay_level ();
	test_find_per_level ();
	test_malformed_relay ();
	test_nesting_bound ();
	test_innermost_of_a_broken_chain ();
	test_insert_at_position ();
	test_insert_limits ();
	test_insert_bad_position ();
	test_delete ();
	test_delete_bad_position ();
	test_replace ();
	test_edit_inner_level ();
	test_edit_middle_level ();
	test_relay_length_bound ();
	test_enclosing_length_ceiling ();
	test_position_from_another_level ();
	test_inner_append_point ();

	return exit_status();
}
