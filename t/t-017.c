/*
 * the dhcpv4 message layer.
 *
 * this is the part both the server and the client stand on, so it is tested on
 * its own first: it is pure - a buffer in, a buffer out, no sockets and no
 * timing - and every one of its failure modes is reachable from a unit test.
 *
 * the cases that matter most are the ones a hand-written parser gets wrong:
 * an option payload carries no alignment, a length field that disagrees with
 * the option's definition is a malformed packet rather than a value to
 * salvage, and a hardware address length is an index into a fixed field that a
 * hostile packet can overrun.
 */

#include <hio-dhcp.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFCAPA 512

static hio_uint8_t g_buf[BUFCAPA];

/* the walker takes no context argument, so a count has to live out here */
static int g_walked;
static int g_walk_codes[32];

static int count_opt (hio_dhcp4_opt_hdr_t* opt)
{
	if (g_walked < (int)HIO_COUNTOF(g_walk_codes)) g_walk_codes[g_walked] = opt->code;
	g_walked++;
	return 1; /* non-zero continues; 0 would stop the walk after the first */
}

/* a request as a client would send it, for the reply and client-id cases */
static void make_request (hio_dhcp4_pktbuf_t* pkt, hio_uint8_t mtype)
{
	static const hio_uint8_t mac[6] = { 0x02, 0x00, 0xde, 0xad, 0xbe, 0xef };

	hio_dhcp4_init_pktbuf (pkt, g_buf, BUFCAPA);
	pkt->hdr->op = HIO_DHCP4_OP_BOOTREQUEST;
	pkt->hdr->htype = HIO_DHCP4_HTYPE_ETHERNET;
	pkt->hdr->hlen = 6;
	HIO_MEMCPY (pkt->hdr->chaddr, mac, 6);
	pkt->hdr->xid = hio_hton32(0x11223344);
	pkt->hdr->flags = hio_hton16(0x8000); /* broadcast */
	pkt->hdr->giaddr = hio_hton32(0x0a000001);
	hio_dhcp4_add_option_u8 (pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, mtype);
}

static void as_pktinf (hio_dhcp4_pktinf_t* inf, const hio_dhcp4_pktbuf_t* pkt)
{
	inf->hdr = pkt->hdr;
	inf->len = pkt->len;
}

/* ------------------------------------------------------------------ */

static void test_cookie_and_layout (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_uint32_t cookie;

	OK (hio_dhcp4_init_pktbuf(&pkt, g_buf, BUFCAPA) == 0,
	    "a packet buffer initialises to just the fixed header");
	OK (pkt.len == HIO_SIZEOF(hio_dhcp4_pkt_hdr_t),
	    "and its length is the header alone until an option is added");

	OK (hio_dhcp4_add_option_u8(&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_DISCOVER) == 0,
	    "an option can be added");

	/* the cookie has to be there, and in network order, or nothing downstream
	 * will recognise the packet as dhcp at all */
	HIO_MEMCPY (&cookie, &g_buf[HIO_SIZEOF(hio_dhcp4_pkt_hdr_t)], 4);
	OK (cookie == HIO_CONST_HTON32(HIO_DHCP4_MAGIC_COOKIE),
	    "and adding the first one writes the magic cookie ahead of it");

	/* header + cookie + (code,len,value) */
	OK (pkt.len == HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) + 4 + 3,
	    "and the length accounts for the cookie, the option header and its payload");
}

static void test_byte_order (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	const hio_uint8_t* p;
	hio_uint8_t l;
	hio_uint16_t v16;
	hio_uint32_t v32;

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	OK (hio_dhcp4_add_option_u16(&pkt, HIO_DHCP4_OPT_MAX_SIZE, 0x0102) == 0 &&
	    hio_dhcp4_add_option_u32(&pkt, HIO_DHCP4_OPT_LEASE_TIME, 0x01020304) == 0,
	    "16- and 32-bit options can be added");
	as_pktinf (&inf, &pkt);

	/* on the wire, most significant octet first - the whole reason these
	 * helpers exist rather than callers writing the bytes themselves */
	OK (hio_dhcp4_get_option_data(&inf, HIO_DHCP4_OPT_MAX_SIZE, &p, &l) == 0 &&
	    l == 2 && p[0] == 0x01 && p[1] == 0x02,
	    "and a 16-bit option is written most significant octet first");
	OK (hio_dhcp4_get_option_data(&inf, HIO_DHCP4_OPT_LEASE_TIME, &p, &l) == 0 &&
	    l == 4 && p[0] == 0x01 && p[1] == 0x02 && p[2] == 0x03 && p[3] == 0x04,
	    "and so is a 32-bit one");

	OK (hio_dhcp4_get_option_u16(&inf, HIO_DHCP4_OPT_MAX_SIZE, &v16) == 0 && v16 == 0x0102,
	    "and reading it back gives the host-order value again");
	OK (hio_dhcp4_get_option_u32(&inf, HIO_DHCP4_OPT_LEASE_TIME, &v32) == 0 && v32 == 0x01020304,
	    "for both widths");
}

static void test_unaligned_payload (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint32_t v32;

	/* an option payload starts wherever the options before it ended, so its
	 * alignment is whatever happens to fall out. a one-octet option ahead of
	 * the 32-bit one guarantees the latter is misaligned - which is the case
	 * that reading through a cast gets wrong, silently on x86 and fatally
	 * elsewhere. */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_ACK);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_IP_TTL, 64);
	hio_dhcp4_add_option_u32 (&pkt, HIO_DHCP4_OPT_LEASE_TIME, 0xDEADBEEF);
	as_pktinf (&inf, &pkt);

	OK (hio_dhcp4_get_option_u32(&inf, HIO_DHCP4_OPT_LEASE_TIME, &v32) == 0 && v32 == 0xDEADBEEF,
	    "a 32-bit option is read correctly from an unaligned offset");
}

static void test_wrong_width_is_refused (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t two[2] = { 0x12, 0x34 };
	hio_uint32_t v32;
	hio_uint16_t v16;

	/* a lease time is four octets by definition. two is a malformed packet,
	 * and the answer to that is a refusal rather than whichever two octets
	 * happen to follow in the buffer. */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option (&pkt, HIO_DHCP4_OPT_LEASE_TIME, two, 2);
	as_pktinf (&inf, &pkt);

	OK (hio_dhcp4_get_option_u32(&inf, HIO_DHCP4_OPT_LEASE_TIME, &v32) <= -1,
	    "an option whose length disagrees with its definition is refused");
	OK (hio_dhcp4_get_option_u16(&inf, HIO_DHCP4_OPT_LEASE_TIME, &v16) == 0 && v16 == 0x1234,
	    "and is readable at the width it actually has");

	/* an option that is not there is not an error to be papered over either */
	OK (hio_dhcp4_get_option_u32(&inf, HIO_DHCP4_OPT_T1, &v32) <= -1,
	    "and an absent option reports absence rather than a value");
}

static void test_msg_type (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t mtype;

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_REQUEST);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_msg_type(&inf, &mtype) == 0 && mtype == HIO_DHCP4_MSG_REQUEST,
	    "the message type is read from option 53");

	/* no option 53 is what distinguishes bootp from dhcp, so it must not be
	 * mistaken for a message type of zero */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_IP_TTL, 64);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_msg_type(&inf, &mtype) <= -1,
	    "a packet without one is refused rather than read as type zero");

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, 0);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_msg_type(&inf, &mtype) <= -1, "and nor is a type of zero accepted");

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, 99);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_msg_type(&inf, &mtype) <= -1, "nor one this implementation does not know");
}

static void test_client_id (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	const hio_uint8_t* p;
	hio_uint8_t l;
	static const hio_uint8_t cid[5] = { 0xff, 'a', 'b', 'c', 'd' };

	/* with no option 61, the hardware address is the identity */
	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_client_id(&inf, &p, &l) == 0 && l == 6 &&
	    p[0] == 0x02 && p[5] == 0xef,
	    "with no client-id option, the hardware address identifies the client");

	/* with one, it wins - and it is opaque, so it is returned as it came */
	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	hio_dhcp4_add_option (&pkt, HIO_DHCP4_OPT_CLIENT_ID, (void*)cid, 5);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_client_id(&inf, &p, &l) == 0 && l == 5 && HIO_MEMCMP(p, cid, 5) == 0,
	    "and a client-id option takes precedence over it");

	/* a packet may claim a hardware address longer than the field that holds
	 * it. trusting hlen there is how a parser reads past the header. */
	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	pkt.hdr->hlen = 200;
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_get_client_id(&inf, &p, &l) <= -1,
	    "and a hardware address longer than the field is refused rather than trusted");
}

static void test_check_pkt (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;

	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_check_pkt(&inf) == 0, "a well-formed packet passes the shape check");

	/* short of the fixed header there is nothing to read at all */
	as_pktinf (&inf, &pkt);
	inf.len = HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) - 1;
	OK (hio_dhcp4_check_pkt(&inf) <= -1, "one shorter than the fixed header is refused");

	/* and with no room for the cookie there are no options */
	as_pktinf (&inf, &pkt);
	inf.len = HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) + 2;
	OK (hio_dhcp4_check_pkt(&inf) <= -1, "and one with no room for the cookie is refused");

	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	g_buf[HIO_SIZEOF(hio_dhcp4_pkt_hdr_t)] ^= 0xFF; /* break the cookie */
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_check_pkt(&inf) <= -1, "and one whose cookie is wrong is refused");

	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	pkt.hdr->hlen = 200;
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_check_pkt(&inf) <= -1, "and one claiming an oversized hardware address is refused");
}

static void test_reply_echoes_the_request (void)
{
	hio_dhcp4_pktbuf_t req, rep;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t reqbuf[BUFCAPA], repbuf[BUFCAPA];
	hio_dhcp4_pktbuf_t tmp;

	/* build the request in its own buffer so the reply does not overwrite it */
	make_request (&tmp, HIO_DHCP4_MSG_DISCOVER);
	HIO_MEMCPY (reqbuf, g_buf, tmp.len);
	req = tmp;
	req.hdr = (hio_dhcp4_pkt_hdr_t*)reqbuf;
	as_pktinf (&inf, &req);

	OK (hio_dhcp4_init_reply_pktbuf(&rep, repbuf, BUFCAPA, &inf) == 0,
	    "a reply can be started from a request");
	OK (rep.hdr->op == HIO_DHCP4_OP_BOOTREPLY, "and is marked as a reply");

	/* these four are what let the reply reach the right client and be
	 * recognised by it */
	OK (rep.hdr->xid == req.hdr->xid, "and echoes the transaction id");
	OK (rep.hdr->flags == req.hdr->flags, "and the broadcast flag");
	OK (rep.hdr->giaddr == req.hdr->giaddr, "and the relay address");
	OK (rep.hdr->hlen == req.hdr->hlen && rep.hdr->htype == req.hdr->htype &&
	    HIO_MEMCMP(rep.hdr->chaddr, req.hdr->chaddr, req.hdr->hlen) == 0,
	    "and the client's hardware address");
}

static void test_option_editing (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t v8;

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_OFFER);
	hio_dhcp4_add_option_u32 (&pkt, HIO_DHCP4_OPT_LEASE_TIME, 3600);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_IP_TTL, 64);
	as_pktinf (&inf, &pkt);

	g_walked = 0;
	OK (hio_dhcp4_walk_options(&inf, count_opt) == 0,
	    "the options of a packet that fills its buffer walk to the end");
	OK (g_walked == 3 && g_walk_codes[0] == HIO_DHCP4_OPT_MESSAGE_TYPE &&
	    g_walk_codes[2] == HIO_DHCP4_OPT_IP_TTL,
	    "walking the options visits each one in the order written");

	OK (hio_dhcp4_delete_option(&pkt, HIO_DHCP4_OPT_LEASE_TIME) == 0,
	    "an option can be deleted");
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_find_option(&inf, HIO_DHCP4_OPT_LEASE_TIME) == HIO_NULL,
	    "and is then no longer found");
	OK (hio_dhcp4_get_option_u8(&inf, HIO_DHCP4_OPT_IP_TTL, &v8) == 0 && v8 == 64,
	    "while the options around it survive intact");
}

/* the one-octet options. PADDING and END carry no length field, and a
 * traversal that does not account for that goes wrong in two separate ways -
 * both of which every conforming packet reaches, because every conforming
 * packet ends with an END option. */
static void test_one_octet_options (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;

	/* a packet whose final octet is a lone END. this is what this library's
	 * own reply builder emits, so a walk that rejects it rejects almost
	 * everything. */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_ACK);
	hio_dhcp4_add_option_u32 (&pkt, HIO_DHCP4_OPT_LEASE_TIME, 3600);
	hio_dhcp4_add_option (&pkt, HIO_DHCP4_OPT_END, HIO_NULL, 0);
	as_pktinf (&inf, &pkt);

	g_walked = 0;
	OK (hio_dhcp4_walk_options(&inf, count_opt) == 0 && g_walked == 2,
	    "a packet ending in a lone END option walks cleanly");

	/* a padding octet between two options. padding is one octet, so advancing
	 * by two lands the cursor inside the following option and everything read
	 * after it is misaligned. */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_ACK);
	hio_dhcp4_add_option (&pkt, HIO_DHCP4_OPT_PADDING, HIO_NULL, 0);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_IP_TTL, 64);
	hio_dhcp4_add_option_u32 (&pkt, HIO_DHCP4_OPT_LEASE_TIME, 3600);
	hio_dhcp4_add_option (&pkt, HIO_DHCP4_OPT_END, HIO_NULL, 0);
	as_pktinf (&inf, &pkt);

	g_walked = 0;
	OK (hio_dhcp4_walk_options(&inf, count_opt) == 0 && g_walked == 3 &&
	    g_walk_codes[0] == HIO_DHCP4_OPT_MESSAGE_TYPE &&
	    g_walk_codes[1] == HIO_DHCP4_OPT_IP_TTL &&
	    g_walk_codes[2] == HIO_DHCP4_OPT_LEASE_TIME,
	    "and interior padding is stepped over one octet at a time");

	/* and find_option must agree with the walk about the same packet - they
	 * are two traversals of one format, and they disagreed before */
	{
		hio_uint32_t lt = 0;
		OK (hio_dhcp4_get_option_u32(&inf, HIO_DHCP4_OPT_LEASE_TIME, &lt) == 0 && lt == 3600,
		    "and find_option reads the option after the padding too");
	}
}

static void test_overlong_option_still_refused (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t* lenfield;

	/* the bounds check above was loosened by one octet to stop it rejecting
	 * the last option of a well-formed packet. this is the other side of that:
	 * a length that genuinely runs past the end must still be refused, or the
	 * loosening would have opened a read past the buffer. */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_ACK);
	hio_dhcp4_add_option_u32 (&pkt, HIO_DHCP4_OPT_LEASE_TIME, 3600);
	as_pktinf (&inf, &pkt);

	/* overstate the last option's length by one */
	lenfield = &g_buf[pkt.len - 4 - 1];
	OK (*lenfield == 4, "the length field is where the test expects it");
	*lenfield = 5;

	g_walked = 0;
	OK (hio_dhcp4_walk_options(&inf, count_opt) <= -1,
	    "an option length that runs past the end of the packet is still refused");
}

static void test_capacity (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_uint8_t small[HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) + 8];
	hio_uint8_t payload[16];

	OK (hio_dhcp4_init_pktbuf(&pkt, small, HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) - 1) <= -1,
	    "a buffer too small for the header is refused outright");

	/* room for the cookie and a little else, so a large option must not be
	 * written past the end - the failure a fixed-size datagram buffer makes
	 * reachable from the network */
	HIO_MEMSET (payload, 'x', HIO_SIZEOF(payload));
	OK (hio_dhcp4_init_pktbuf(&pkt, small, HIO_SIZEOF(small)) == 0,
	    "a buffer with room for the header alone initialises");
	OK (hio_dhcp4_add_option(&pkt, HIO_DHCP4_OPT_HOST_NAME, payload, HIO_SIZEOF(payload)) <= -1,
	    "and an option that would not fit is refused rather than overrunning it");
	OK (pkt.len <= HIO_SIZEOF(small), "leaving the length within the buffer");
}

/* ------------------------------------------------------------------ */
/* the relay agent information option, RFC 3046                       */

/* option 82 carries suboptions inside its own value, in the same
 * code/length/value shape as options themselves but one level down. what makes
 * it worth testing is the arithmetic: adding a suboption grows an option whose
 * length field holds only 255, inside a packet buffer that is also finite -
 * two limits that a length computed in an octet cannot express. */

static void test_relay_suboptions (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t* v;
	hio_uint8_t vlen = 0;
	static const hio_uint8_t circuit[4] = { 'e', 't', 'h', '0' };
	static const hio_uint8_t remote[3] = { 'r', 'i', 'd' };

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_DISCOVER);

	OK (hio_dhcp4_add_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, circuit, 4) == 0,
	    "a relay suboption can be added to a packet with no relay option");
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_find_option(&inf, HIO_DHCP4_OPT_RELAY) != HIO_NULL,
	    "and the relay option comes into being to hold it");

	v = hio_dhcp4_find_relay_suboption_value(&inf, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &vlen);
	OK (v && vlen == 4 && HIO_MEMCMP(v, circuit, 4) == 0,
	    "and it is found again with its value intact");

	OK (hio_dhcp4_add_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_REMOTE_ID, remote, 3) == 0,
	    "a second suboption is appended to the option that exists");
	as_pktinf (&inf, &pkt);
	v = hio_dhcp4_find_relay_suboption_value(&inf, HIO_DHCP4_OPT_RELAY_REMOTE_ID, &vlen);
	OK (v && vlen == 3 && HIO_MEMCMP(v, remote, 3) == 0, "and is found by its own code");
	v = hio_dhcp4_find_relay_suboption_value(&inf, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &vlen);
	OK (v && vlen == 4 && HIO_MEMCMP(v, circuit, 4) == 0, "while the first is still there");

	OK (hio_dhcp4_delete_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID) == 0,
	    "one suboption can be deleted");
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_find_relay_suboption_value(&inf, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &vlen) == HIO_NULL,
	    "and is then gone");
	v = hio_dhcp4_find_relay_suboption_value(&inf, HIO_DHCP4_OPT_RELAY_REMOTE_ID, &vlen);
	OK (v && vlen == 3 && HIO_MEMCMP(v, remote, 3) == 0, "while the other survives it");

	/* deleting the last one takes the option with it rather than leaving an
	 * empty option behind for something else to trip over */
	OK (hio_dhcp4_delete_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_REMOTE_ID) == 0,
	    "the last suboption can be deleted too");
	as_pktinf (&inf, &pkt);
	OK (hio_dhcp4_find_option(&inf, HIO_DHCP4_OPT_RELAY) == HIO_NULL,
	    "and the now-empty relay option goes with it");

	{
		hio_uint8_t mt = 0;
		OK (hio_dhcp4_get_msg_type(&inf, &mt) == 0 && mt == HIO_DHCP4_MSG_DISCOVER,
		    "and the options around it are undisturbed by the shuffling");
	}

	OK (hio_dhcp4_delete_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID) <= -1,
	    "deleting a suboption that is not there reports failure");
}

static void test_relay_suboption_limits (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t big[255];
	hio_uint8_t small[8];

	HIO_MEMSET (big, 'x', HIO_SIZEOF(big));
	HIO_MEMSET (small, 'y', HIO_SIZEOF(small));

	/* a suboption costs two octets of header on top of its value, and an
	 * option length field holds 255 - so 254 does not fit. computed in an
	 * octet that sum wraps, and a huge suboption becomes a zero-length option:
	 * worse than a refusal, because nothing reports it. */
	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	OK (hio_dhcp4_add_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, big, 254) <= -1,
	    "a suboption too large for an option length field is refused");
	OK (hio_dhcp4_add_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, big, 253) == 0,
	    "and the largest one that does fit is accepted");

	/* the same arithmetic one level up: appending to an option already at its
	 * limit would take its length past 255 */
	OK (hio_dhcp4_add_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_REMOTE_ID, small, 8) <= -1,
	    "a suboption that would take the option past 255 octets is refused");
	as_pktinf (&inf, &pkt);
	{
		hio_dhcp4_opt_hdr_t* o = hio_dhcp4_find_option(&inf, HIO_DHCP4_OPT_RELAY);
		OK (o && o->len == 255, "leaving the option at its true length, not a wrapped one");
	}

	/* and the packet buffer is a limit of its own */
	{
		hio_uint8_t tiny[HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) + 16];
		hio_dhcp4_pktbuf_t tp;
		hio_dhcp4_init_pktbuf (&tp, tiny, HIO_SIZEOF(tiny));
		hio_dhcp4_add_option_u8 (&tp, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_DISCOVER);
		OK (hio_dhcp4_add_relay_suboption(&tp, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, big, 100) <= -1,
		    "and one that would not fit the packet buffer is refused");
		OK (tp.len <= HIO_SIZEOF(tiny), "leaving the length within the buffer");
	}
}

static void test_relay_suboption_bounds (void)
{
	hio_uint8_t raw[8];
	hio_uint8_t* v;
	hio_uint8_t vlen = 99;

	/* a suboption claiming more than the enclosing option holds. the value
	 * pointer returned for it would be read that far by the caller - past the
	 * packet, when the relay option is the last one in it. */
	raw[0] = HIO_DHCP4_OPT_RELAY_CIRCUIT_ID;
	raw[1] = 200;
	raw[2] = 'a';
	raw[3] = 'b';
	v = hio_dhcp4_get_relay_suboption_value(raw, 4, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &vlen);
	OK (v == HIO_NULL, "a suboption whose length overruns its option is refused");

	raw[0] = HIO_DHCP4_OPT_RELAY_CIRCUIT_ID;
	v = hio_dhcp4_get_relay_suboption_value(raw, 1, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &vlen);
	OK (v == HIO_NULL, "and so is a truncated suboption header");

	/* the length pointer is optional - a caller may only want the value */
	raw[0] = HIO_DHCP4_OPT_RELAY_REMOTE_ID;
	raw[1] = 2;
	raw[2] = 'z'; raw[3] = 'z';
	v = hio_dhcp4_get_relay_suboption_value(raw, 4, HIO_DHCP4_OPT_RELAY_REMOTE_ID, HIO_NULL);
	OK (v != HIO_NULL && v[0] == 'z', "and the length pointer may be omitted");
}

static void test_option_value_accessor (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_dhcp4_pktinf_t inf;
	hio_uint8_t* v;
	hio_uint8_t vlen = 0;
	static const hio_bch_t name[] = "host.example";

	hio_dhcp4_init_pktbuf (&pkt, g_buf, BUFCAPA);
	hio_dhcp4_add_option_u8 (&pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_DISCOVER);
	hio_dhcp4_add_option (&pkt, HIO_DHCP4_OPT_HOST_NAME, (void*)name, HIO_SIZEOF(name) - 1);
	as_pktinf (&inf, &pkt);

	v = hio_dhcp4_get_option_value(&inf, HIO_DHCP4_OPT_HOST_NAME, &vlen);
	OK (v && vlen == HIO_SIZEOF(name) - 1 && HIO_MEMCMP(v, name, vlen) == 0,
	    "an option's value is reachable without the caller stepping over its header");
	OK (hio_dhcp4_get_option_value(&inf, HIO_DHCP4_OPT_ROOT_PATH, &vlen) == HIO_NULL,
	    "and an absent option yields nothing rather than a header pointer");
}

/* a code is one octet on the wire, but the argument carrying it is an int, and
 * it reaches the packet through a plain assignment. so a code past 255 was
 * truncated: adding 'suboption 257' wrote suboption 1 - an Agent Circuit ID
 * nobody asked for - and reported success, leaving data in the packet that the
 * code the caller passed could never find again. */
static void test_code_must_fit_an_octet (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_uint8_t olen;
	hio_oow_t before;

	/* --- the option level --- */
	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	before = pkt.len;
	OK (hio_dhcp4_add_option(&pkt, 0x101, "x", 1) <= -1, "an option code past one octet is refused");
	OK (hio_dhcp4_add_option(&pkt, -1, "x", 1) <= -1, "and a negative one");
	OK (pkt.len == before, "neither wrote anything");
	OK (hio_dhcp4_find_option((hio_dhcp4_pktinf_t*)&pkt, HIO_DHCP4_OPT_SUBNET) == HIO_NULL,
	    "and option 1 was not created by the truncation of 0x101");

	/* the ends of the octet range are legitimate codes: PADDING is 0 and
	 * END is 255, so the bound is the octet, not a narrower guess */
	OK (hio_dhcp4_add_option(&pkt, HIO_DHCP4_OPT_PADDING, HIO_NULL, 0) == 0, "padding still adds");
	OK (hio_dhcp4_add_option(&pkt, HIO_DHCP4_OPT_END, HIO_NULL, 0) == 0, "and so does END");
	OK (pkt.len == before + 2, "one octet each");

	/* --- the suboption level --- */
	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	before = pkt.len;
	OK (hio_dhcp4_add_relay_suboption(&pkt, 0x101, (const hio_uint8_t*)"secret", 6) <= -1,
	    "a suboption code past one octet is refused");
	OK (pkt.len == before, "nothing was added");
	OK (hio_dhcp4_find_option((hio_dhcp4_pktinf_t*)&pkt, HIO_DHCP4_OPT_RELAY) == HIO_NULL,
	    "no relay option was created");
	olen = 0xff;
	OK (hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &olen) == HIO_NULL,
	    "and in particular no circuit id, which 0x101 truncates to");

	/* 0 is refused on the way in as well as on the way out: a suboption
	 * stored under it could never be looked up or deleted again, since this
	 * api spends 0 as its 'no suboption wanted' sentinel */
	OK (hio_dhcp4_add_relay_suboption(&pkt, 0, (const hio_uint8_t*)"zero", 4) <= -1,
	    "a suboption code of 0 is refused when adding");
	OK (pkt.len == before, "leaving the packet as it was");

	/* and the whole legitimate range still works at both ends */
	OK (hio_dhcp4_add_relay_suboption(&pkt, 1, (const hio_uint8_t*)"a", 1) == 0, "code 1 adds");
	OK (hio_dhcp4_delete_relay_suboption(&pkt, 1) == 0, "and deletes");
	OK (hio_dhcp4_add_relay_suboption(&pkt, 255, (const hio_uint8_t*)"b", 1) == 0, "code 255 adds");
	OK (hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, 255, &olen) != HIO_NULL,
	    "and is findable by its own code");
	OK (hio_dhcp4_delete_relay_suboption(&pkt, 255) == 0, "and deletes too");
	OK (pkt.len == before, "back where it started");
}

/* dhcp4_find_option() only looks inside a relay option for a suboption when it
 * is asked for one - a code above zero - and otherwise returns the relay option
 * having written neither the value pointer nor the value length. the two
 * suboption entry points go on to use both, so a code of zero or below has to
 * be refused before the lookup rather than after it. there is no suboption 0
 * to ask for in the first place. */
static void test_suboption_code_zero (void)
{
	hio_dhcp4_pktbuf_t pkt;
	hio_uint8_t olen;
	hio_uint8_t* v;
	hio_oow_t before;

	make_request (&pkt, HIO_DHCP4_MSG_DISCOVER);
	OK (hio_dhcp4_add_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, (const hio_uint8_t*)"eth0", 4) == 0,
	    "a relay option with one suboption");
	before = pkt.len;

	/* the suboption that is there reads back */
	olen = 0xff;
	v = hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID, &olen);
	OK (v != HIO_NULL && olen == 4 && HIO_MEMCMP(v, "eth0", 4) == 0, "and it is found by its own code");

	/* code 0 is not a suboption. the relay option is present, so a lookup
	 * that forgot to check would find it and hand back a value pointer that
	 * was never written. */
	olen = 0xff;
	v = hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, 0, &olen);
	OK (v == HIO_NULL, "a suboption code of 0 finds nothing");
	OK (olen == 0xff, "and the caller's length is left alone rather than half-written");

	v = hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, -1, &olen);
	OK (v == HIO_NULL, "nor does a negative one");

	/* and nothing above an octet can name a suboption either */
	olen = 0xff;
	OK (hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, 256, &olen) == HIO_NULL,
	    "a code past one octet finds nothing");
	OK (olen == 0xff, "with the caller's length untouched");
	OK (hio_dhcp4_find_relay_suboption_value((hio_dhcp4_pktinf_t*)&pkt, 0x101, &olen) == HIO_NULL,
	    "and one that would truncate to a real code finds nothing");
	OK (hio_dhcp4_delete_relay_suboption(&pkt, 256) <= -1, "nor can such a code delete");
	OK (hio_dhcp4_delete_relay_suboption(&pkt, 0x101) <= -1, "including one that would truncate");
	OK (pkt.len == before, "and the packet is untouched by any of it");

	OK (hio_dhcp4_delete_relay_suboption(&pkt, 0) <= -1, "deleting suboption 0 is refused");
	OK (pkt.len == before, "and the packet is untouched");
	OK (hio_dhcp4_delete_relay_suboption(&pkt, -1) <= -1, "as is a negative code");
	OK (pkt.len == before, "leaving it untouched too");

	/* and the real one still deletes, so the guard did not break the path.
	 * it was the option's only suboption, so what goes is the whole relay
	 * option: its own two-octet header plus the six the suboption occupied. */
	OK (hio_dhcp4_delete_relay_suboption(&pkt, HIO_DHCP4_OPT_RELAY_CIRCUIT_ID) == 0,
	    "the suboption that is there still deletes");
	OK (pkt.len == before - 8, "taking the now-empty relay option with it");
	OK (hio_dhcp4_find_option((hio_dhcp4_pktinf_t*)&pkt, HIO_DHCP4_OPT_RELAY) == HIO_NULL,
	    "so no relay option is left");
}

/* an option area ending exactly with {OVERLOAD, len=1} and no value octet.
 * the overload branch reads that value to learn which fields are overloaded,
 * and it runs for an option the search did not match - so the read has to be
 * bounded by the option's length check, not by the match.
 *
 * the packet is heap-allocated at exactly its own size so that a read one
 * octet past it is a heap overflow rather than a quiet touch of the next
 * global, which is what let this sit unnoticed in three separate trees. */
static void test_overload_at_the_very_end (void)
{
	hio_oow_t plen = HIO_SIZEOF(hio_dhcp4_pkt_hdr_t) + 4 + 2;
	hio_uint8_t* buf = (hio_uint8_t*)malloc(plen);
	hio_uint32_t cookie = HIO_CONST_HTON32(HIO_DHCP4_MAGIC_COOKIE);
	hio_dhcp4_pktinf_t pkt;

	OK (buf != HIO_NULL, "a packet sized to the octet");
	memset (buf, 0, plen);
	HIO_MEMCPY (buf + HIO_SIZEOF(hio_dhcp4_pkt_hdr_t), &cookie, 4);
	buf[plen - 2] = HIO_DHCP4_OPT_OVERLOAD;
	buf[plen - 1] = 1; /* claims a value octet that is not there */

	pkt.hdr = (hio_dhcp4_pkt_hdr_t*)buf;
	pkt.len = plen;

	/* searching for something else reaches the overload option through the
	 * path that does not match it */
	OK (hio_dhcp4_find_option(&pkt, HIO_DHCP4_OPT_SUBNET) == HIO_NULL,
	    "an option area ending in a truncated overload yields nothing");
	OK (hio_dhcp4_find_option(&pkt, HIO_DHCP4_OPT_OVERLOAD) == HIO_NULL,
	    "and the truncated overload option is not returned either");

	/* the walker reaches it too, and must refuse the packet rather than
	 * reading the octet that is not there */
	g_walked = 0;
	OK (hio_dhcp4_walk_options(&pkt, count_opt) <= -1, "the walker refuses it");
	OK (g_walked == 0, "having visited nothing");

	free (buf);
}

/* ------------------------------------------------------------------ */

int main (void)
{
	no_plan ();

	test_cookie_and_layout ();
	test_byte_order ();
	test_unaligned_payload ();
	test_wrong_width_is_refused ();
	test_msg_type ();
	test_client_id ();
	test_check_pkt ();
	test_reply_echoes_the_request ();
	test_option_editing ();
	test_one_octet_options ();
	test_overlong_option_still_refused ();
	test_capacity ();
	test_relay_suboptions ();
	test_relay_suboption_limits ();
	test_relay_suboption_bounds ();
	test_option_value_accessor ();
	test_suboption_code_zero ();
	test_code_must_fit_an_octet ();
	test_overload_at_the_very_end ();

	return exit_status();
}
