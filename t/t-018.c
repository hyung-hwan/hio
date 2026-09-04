/*
 * the dhcpv4 server service.
 *
 * the state machine is hio_svc_dhcs_process(), a function from a request to a
 * reply that does no I/O, so the protocol is driven here by handing it packets
 * rather than by standing up a network. that is the point of exposing it: the
 * parts of DHCP that are easy to get wrong are the state transitions and the
 * lease bookkeeping, and neither needs a socket to exercise.
 *
 * written against RFC 2131. the cases follow the transitions it defines rather
 * than the shape of the implementation.
 */

#include <hio-dhcp.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define POOL_FIRST 0x0a000064u   /* 10.0.0.100 */
#define POOL_LAST  0x0a000066u   /* 10.0.0.102 - three addresses, so exhaustion is reachable */
#define SERVER_ID  0x0a000001u   /* 10.0.0.1 */
#define NETMASK    0xffffff00u
#define ROUTER     0x0a000001u
#define LEASE_SECS 600

/* mirrors PURGE_INTERVAL_SECS in dhcp-svr.c, which is private to it */
#define PURGE_INTERVAL_SECS_FOR_TEST 60

/* a pass-through allocator that can be armed to fail once. the server records
 * a lease for every address it hands out, and a lease it fails to record is an
 * address it will not remember issuing - so what it does when an allocation
 * fails is part of the protocol behaviour, not an incidental detail. */
static int g_fail_next_alloc;
static int g_failed_allocs;

static void* fi_alloc (hio_mmgr_t* mmgr, hio_oow_t n)
{
	if (g_fail_next_alloc) { g_fail_next_alloc = 0; g_failed_allocs++; return HIO_NULL; }
	return malloc(n);
}

static void* fi_realloc (hio_mmgr_t* mmgr, void* ptr, hio_oow_t n)
{
	if (g_fail_next_alloc) { g_fail_next_alloc = 0; g_failed_allocs++; return HIO_NULL; }
	return realloc(ptr, n);
}

static void fi_free (hio_mmgr_t* mmgr, void* ptr) { free (ptr); }

static hio_mmgr_t g_fi_mmgr = { fi_alloc, fi_realloc, fi_free, HIO_NULL };

static hio_t* g_hio = HIO_NULL;
static hio_svc_dhcs_t* g_dhcs = HIO_NULL;

static hio_uint8_t g_reqbuf[576];
static hio_uint8_t g_repbuf[576];

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static int start_server (void)
{
	hio_svc_dhcs_cfg_t cfg;

	HIO_MEMSET (&cfg, 0, HIO_SIZEOF(cfg));
	if (hio_bcstrtoskad(g_hio, "0.0.0.0:0", &cfg.bind_addr) <= -1) return -1;
	cfg.server_id = SERVER_ID;
	cfg.pool_first = POOL_FIRST;
	cfg.pool_last = POOL_LAST;
	cfg.netmask = NETMASK;
	cfg.router = ROUTER;
	cfg.dns1 = 0x08080808u;
	cfg.lease_secs = LEASE_SECS;
	cfg.domain = "example.test";

	g_dhcs = hio_svc_dhcs_start(g_hio, &cfg);
	return g_dhcs? 0: -1;
}

static void stop_server (void)
{
	if (g_dhcs) { hio_svc_dhcs_stop (g_dhcs); g_dhcs = HIO_NULL; }
	hio_exec (g_hio);
}

/* build a request from one client, identified by the last octet of its mac */
static void build (hio_dhcp4_pktbuf_t* pkt, hio_uint8_t mtype, hio_uint8_t client,
                   hio_uint32_t requested, hio_uint32_t server_id, hio_uint32_t ciaddr)
{
	hio_uint8_t mac[6] = { 0x02, 0, 0, 0, 0, 0 };

	mac[5] = client;
	hio_dhcp4_init_pktbuf (pkt, g_reqbuf, HIO_SIZEOF(g_reqbuf));
	pkt->hdr->op = HIO_DHCP4_OP_BOOTREQUEST;
	pkt->hdr->htype = HIO_DHCP4_HTYPE_ETHERNET;
	pkt->hdr->hlen = 6;
	HIO_MEMCPY (pkt->hdr->chaddr, mac, 6);
	pkt->hdr->xid = hio_hton32(0xABCD0000u | client);
	pkt->hdr->ciaddr = hio_hton32(ciaddr);
	hio_dhcp4_add_option_u8 (pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, mtype);
	if (requested) hio_dhcp4_add_option_u32 (pkt, HIO_DHCP4_OPT_REQUESTED_IPADDR, requested);
	if (server_id) hio_dhcp4_add_option_u32 (pkt, HIO_DHCP4_OPT_SERVER_ID, server_id);
	hio_dhcp4_add_option (pkt, HIO_DHCP4_OPT_END, HIO_NULL, 0);
}

/* the fields a request may carry beyond what build() takes. zero throughout
 * means "as build() would have made it", so a case names only what it cares
 * about. */
struct reqx_t
{
	hio_uint32_t giaddr;         /* non-zero: the request came through a relay */
	hio_uint16_t flags;          /* host order; HIO_DHCP4_FLAG_BROADCAST */
	hio_uint32_t lease_req;      /* option 51 in the request */
	const hio_uint8_t* prl;      /* option 55 */
	hio_uint8_t prl_len;
};
typedef struct reqx_t reqx_t;

/* build(), plus the extras. the option order matters to the parameter request
 * list cases, so the extras go in before END like any other option. */
static void build_x (hio_dhcp4_pktbuf_t* pkt, hio_uint8_t mtype, hio_uint8_t client,
                     hio_uint32_t requested, hio_uint32_t server_id, hio_uint32_t ciaddr,
                     const reqx_t* x)
{
	hio_uint8_t mac[6] = { 0x02, 0, 0, 0, 0, 0 };

	mac[5] = client;
	hio_dhcp4_init_pktbuf (pkt, g_reqbuf, HIO_SIZEOF(g_reqbuf));
	pkt->hdr->op = HIO_DHCP4_OP_BOOTREQUEST;
	pkt->hdr->htype = HIO_DHCP4_HTYPE_ETHERNET;
	pkt->hdr->hlen = 6;
	HIO_MEMCPY (pkt->hdr->chaddr, mac, 6);
	pkt->hdr->xid = hio_hton32(0xABCD0000u | client);
	pkt->hdr->ciaddr = hio_hton32(ciaddr);
	pkt->hdr->giaddr = hio_hton32(x->giaddr);
	pkt->hdr->flags = hio_hton16(x->flags);
	hio_dhcp4_add_option_u8 (pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, mtype);
	if (requested) hio_dhcp4_add_option_u32 (pkt, HIO_DHCP4_OPT_REQUESTED_IPADDR, requested);
	if (server_id) hio_dhcp4_add_option_u32 (pkt, HIO_DHCP4_OPT_SERVER_ID, server_id);
	if (x->lease_req) hio_dhcp4_add_option_u32 (pkt, HIO_DHCP4_OPT_LEASE_TIME, x->lease_req);
	if (x->prl && x->prl_len > 0) hio_dhcp4_add_option (pkt, HIO_DHCP4_OPT_PARAM_REQ, (void*)x->prl, x->prl_len);
	hio_dhcp4_add_option (pkt, HIO_DHCP4_OPT_END, HIO_NULL, 0);
}

/* run one request and hand back the whole reply and its destination, which is
 * what the routing and option-list cases need to look at */
static int run_x (hio_uint8_t mtype, hio_uint8_t client, hio_uint32_t requested,
                  hio_uint32_t server_id, hio_uint32_t ciaddr, const reqx_t* x,
                  hio_dhcp4_pktinf_t* out_rep, hio_skad_t* out_dst)
{
	hio_dhcp4_pktbuf_t req, rep;
	hio_dhcp4_pktinf_t reqinf;
	hio_skad_t dst;
	int n;

	build_x (&req, mtype, client, requested, server_id, ciaddr, x);
	reqinf.hdr = req.hdr;
	reqinf.len = req.len;

	rep.hdr = (hio_dhcp4_pkt_hdr_t*)g_repbuf;
	rep.len = 0;
	rep.capa = HIO_SIZEOF(g_repbuf);

	n = hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst);
	if (out_rep) { out_rep->hdr = rep.hdr; out_rep->len = rep.len; }
	if (out_dst) *out_dst = dst;
	return n;
}

/* the destination as a host-order address and a port, for comparison */
static void dst_of (const hio_skad_t* dst, hio_uint32_t* ip, hio_uint16_t* port)
{
	hio_uint32_t v = 0;
	hio_skad_get_ipad_bytes (dst, &v, HIO_SIZEOF(v));
	*ip = hio_ntoh32(v);
	*port = hio_skad_get_port(dst);
}

/* run one request through the server. returns what process() returned, and
 * fills in the reply's message type and offered address when there is one. */
static int run (hio_uint8_t mtype, hio_uint8_t client, hio_uint32_t requested,
                hio_uint32_t server_id, hio_uint32_t ciaddr,
                hio_uint8_t* out_mtype, hio_uint32_t* out_yiaddr)
{
	hio_dhcp4_pktbuf_t req, rep;
	hio_dhcp4_pktinf_t reqinf, repinf;
	hio_skad_t dst;
	int n;

	build (&req, mtype, client, requested, server_id, ciaddr);
	reqinf.hdr = req.hdr;
	reqinf.len = req.len;

	rep.hdr = (hio_dhcp4_pkt_hdr_t*)g_repbuf;
	rep.len = 0;
	rep.capa = HIO_SIZEOF(g_repbuf);

	n = hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst);
	if (n == 1)
	{
		repinf.hdr = rep.hdr;
		repinf.len = rep.len;
		if (out_mtype) hio_dhcp4_get_msg_type (&repinf, out_mtype);
		if (out_yiaddr) *out_yiaddr = hio_ntoh32(rep.hdr->yiaddr);
	}
	return n;
}

/* ------------------------------------------------------------------ */

static void test_discover_offers (void)
{
	hio_uint8_t mt = 0;
	hio_uint32_t ip = 0;

	OK (run(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &mt, &ip) == 1,
	    "a DISCOVER is answered");
	OK (mt == HIO_DHCP4_MSG_OFFER, "and the answer is an OFFER");
	OK (ip >= POOL_FIRST && ip <= POOL_LAST, "carrying an address from the pool");

	/* the offer is recorded, or a second client discovering before the first
	 * requests would be offered the same address */
	OK (hio_svc_dhcs_getleasecount(g_dhcs) == 1, "and the offer is recorded as a lease");
}

static void test_offer_is_held_against_other_clients (void)
{
	hio_uint32_t ip1 = 0, ip2 = 0;

	run (HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, HIO_NULL, &ip1);
	run (HIO_DHCP4_MSG_DISCOVER, 2, 0, 0, 0, HIO_NULL, &ip2);

	OK (ip1 != 0 && ip2 != 0 && ip1 != ip2,
	    "two clients discovering at once are offered different addresses");
}

static void test_discover_is_idempotent (void)
{
	hio_uint32_t a = 0, b = 0;

	run (HIO_DHCP4_MSG_DISCOVER, 3, 0, 0, 0, HIO_NULL, &a);
	run (HIO_DHCP4_MSG_DISCOVER, 3, 0, 0, 0, HIO_NULL, &b);

	/* a client that discovers twice is not two clients, and must not consume
	 * two addresses out of the pool */
	OK (a != 0 && a == b, "a client discovering twice is offered the same address twice");
}

static void test_request_acks_and_binds (void)
{
	hio_uint8_t mt = 0;
	hio_uint32_t offered = 0, acked = 0;
	hio_svc_dhcs_lease_t lease;

	run (HIO_DHCP4_MSG_DISCOVER, 4, 0, 0, 0, HIO_NULL, &offered);
	OK (run(HIO_DHCP4_MSG_REQUEST, 4, offered, SERVER_ID, 0, &mt, &acked) == 1,
	    "a REQUEST naming this server is answered");
	OK (mt == HIO_DHCP4_MSG_ACK && acked == offered,
	    "with an ACK for the address that was offered");

	/* and the lease moves from offered to bound, which is what makes it
	 * survive longer than the brief offer hold */
	{
		hio_oow_t i, n = hio_svc_dhcs_getleasecount(g_dhcs);
		int found = 0;
		for (i = 0; i < n; i++)
		{
			if (hio_svc_dhcs_getlease(g_dhcs, i, &lease) == 0 &&
			    lease.ipaddr == acked && lease.state == HIO_SVC_DHCS_LEASE_BOUND) found = 1;
		}
		OK (found, "and the lease is recorded as bound");
	}
}

static void test_request_for_another_server_is_ignored (void)
{
	hio_uint32_t offered = 0;
	hio_oow_t before, after;

	run (HIO_DHCP4_MSG_DISCOVER, 5, 0, 0, 0, HIO_NULL, &offered);
	before = hio_svc_dhcs_getleasecount(g_dhcs);

	OK (run(HIO_DHCP4_MSG_REQUEST, 5, offered, SERVER_ID + 1, 0, HIO_NULL, HIO_NULL) == 0,
	    "a REQUEST that selected another server draws no reply");

	/* and the address we offered goes back to the pool at once rather than
	 * being held until the offer lapses - the client has told us it went
	 * elsewhere */
	after = hio_svc_dhcs_getleasecount(g_dhcs);
	OK (after == before - 1, "and the address offered to it is released immediately");
}

static void test_request_outside_pool_is_naked (void)
{
	hio_uint8_t mt = 0;

	OK (run(HIO_DHCP4_MSG_REQUEST, 6, 0x0b000001u, SERVER_ID, 0, &mt, HIO_NULL) == 1 &&
	    mt == HIO_DHCP4_MSG_NAK,
	    "a REQUEST for an address outside the pool is answered with a NAK");
}

static void test_request_for_someone_elses_address_is_naked (void)
{
	hio_uint8_t mt = 0;
	hio_uint32_t theirs = 0;

	/* client 7 takes an address */
	run (HIO_DHCP4_MSG_DISCOVER, 7, 0, 0, 0, HIO_NULL, &theirs);
	run (HIO_DHCP4_MSG_REQUEST, 7, theirs, SERVER_ID, 0, HIO_NULL, HIO_NULL);

	/* client 8 asks for it. handing it over would put two clients on one
	 * address, so the answer has to be no. */
	OK (run(HIO_DHCP4_MSG_REQUEST, 8, theirs, SERVER_ID, 0, &mt, HIO_NULL) == 1 &&
	    mt == HIO_DHCP4_MSG_NAK,
	    "a REQUEST for an address another client holds is answered with a NAK");
}

static void test_renewal (void)
{
	hio_uint8_t mt = 0;
	hio_uint32_t bound = 0, acked = 0;

	run (HIO_DHCP4_MSG_DISCOVER, 9, 0, 0, 0, HIO_NULL, &bound);
	run (HIO_DHCP4_MSG_REQUEST, 9, bound, SERVER_ID, 0, HIO_NULL, HIO_NULL);

	/* a renewal carries no requested-address option and no server id - just
	 * ciaddr, because the client already has the address and is only asking
	 * for more time on it */
	OK (run(HIO_DHCP4_MSG_REQUEST, 9, 0, 0, bound, &mt, &acked) == 1 &&
	    mt == HIO_DHCP4_MSG_ACK && acked == bound,
	    "a renewal identified only by ciaddr is acknowledged for the same address");
}

static void test_release_frees_the_address (void)
{
	hio_uint32_t ip = 0;
	hio_oow_t before, after;

	run (HIO_DHCP4_MSG_DISCOVER, 10, 0, 0, 0, HIO_NULL, &ip);
	run (HIO_DHCP4_MSG_REQUEST, 10, ip, SERVER_ID, 0, HIO_NULL, HIO_NULL);
	before = hio_svc_dhcs_getleasecount(g_dhcs);

	OK (run(HIO_DHCP4_MSG_RELEASE, 10, 0, 0, ip, HIO_NULL, HIO_NULL) == 0,
	    "a RELEASE draws no reply");
	after = hio_svc_dhcs_getleasecount(g_dhcs);
	OK (after == before - 1, "and gives the address back");
}

static void test_decline_takes_the_address_out_of_service (void)
{
	hio_uint32_t ip = 0;
	hio_svc_dhcs_lease_t lease;
	hio_oow_t i, n;
	int declined = 0;

	run (HIO_DHCP4_MSG_DISCOVER, 11, 0, 0, 0, HIO_NULL, &ip);
	OK (run(HIO_DHCP4_MSG_DECLINE, 11, ip, SERVER_ID, 0, HIO_NULL, HIO_NULL) == 0,
	    "a DECLINE draws no reply");

	n = hio_svc_dhcs_getleasecount(g_dhcs);
	for (i = 0; i < n; i++)
	{
		if (hio_svc_dhcs_getlease(g_dhcs, i, &lease) == 0 &&
		    lease.ipaddr == ip && lease.state == HIO_SVC_DHCS_LEASE_DECLINED) declined = 1;
	}
	OK (declined, "and the address is marked declined rather than freed");

	/* the point of that distinction: freeing it would hand the same address
	 * to the next client, which would find the same conflict */
	{
		hio_uint32_t other = 0;
		run (HIO_DHCP4_MSG_DISCOVER, 12, 0, 0, 0, HIO_NULL, &other);
		OK (other != ip, "so the next client is not offered it");
	}
}

static void test_inform_returns_options_without_a_lease (void)
{
	hio_dhcp4_pktbuf_t req, rep;
	hio_dhcp4_pktinf_t reqinf, repinf;
	hio_skad_t dst;
	hio_uint8_t mt = 0;
	hio_uint32_t nm = 0;
	hio_oow_t before;

	before = hio_svc_dhcs_getleasecount(g_dhcs);

	build (&req, HIO_DHCP4_MSG_INFORM, 13, 0, 0, 0x0a0000c8u);
	reqinf.hdr = req.hdr; reqinf.len = req.len;
	rep.hdr = (hio_dhcp4_pkt_hdr_t*)g_repbuf; rep.len = 0; rep.capa = HIO_SIZEOF(g_repbuf);

	OK (hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst) == 1, "an INFORM is answered");
	repinf.hdr = rep.hdr; repinf.len = rep.len;
	OK (hio_dhcp4_get_msg_type(&repinf, &mt) == 0 && mt == HIO_DHCP4_MSG_ACK,
	    "with an ACK");
	OK (hio_dhcp4_get_option_u32(&repinf, HIO_DHCP4_OPT_SUBNET, &nm) == 0 && nm == NETMASK,
	    "carrying the configured options");

	/* an INFORM is a client that already has an address by other means, so
	 * there is nothing to lease and yiaddr stays empty */
	OK (rep.hdr->yiaddr == 0, "but no address");
	OK (hio_dhcp4_get_option_u32(&repinf, HIO_DHCP4_OPT_LEASE_TIME, &nm) <= -1,
	    "and no lease time");
	OK (hio_svc_dhcs_getleasecount(g_dhcs) == before, "and no lease is recorded");
}

static void test_offer_and_ack_agree (void)
{
	hio_dhcp4_pktbuf_t req, rep;
	hio_dhcp4_pktinf_t reqinf, repinf;
	hio_skad_t dst;
	hio_uint32_t off_nm = 0, off_rt = 0, off_lt = 0, offered = 0;
	hio_uint32_t ack_nm = 0, ack_rt = 0, ack_lt = 0;

	/* an offer that promises something the acknowledgement then contradicts
	 * is a bug a client experiences as a misconfigured network, so the two
	 * are compared field by field */
	build (&req, HIO_DHCP4_MSG_DISCOVER, 14, 0, 0, 0);
	reqinf.hdr = req.hdr; reqinf.len = req.len;
	rep.hdr = (hio_dhcp4_pkt_hdr_t*)g_repbuf; rep.len = 0; rep.capa = HIO_SIZEOF(g_repbuf);
	if (hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst) != 1) { skip ("no offer", 1); return; }
	repinf.hdr = rep.hdr; repinf.len = rep.len;
	offered = hio_ntoh32(rep.hdr->yiaddr);
	hio_dhcp4_get_option_u32 (&repinf, HIO_DHCP4_OPT_SUBNET, &off_nm);
	hio_dhcp4_get_option_u32 (&repinf, HIO_DHCP4_OPT_ROUTER, &off_rt);
	hio_dhcp4_get_option_u32 (&repinf, HIO_DHCP4_OPT_LEASE_TIME, &off_lt);

	build (&req, HIO_DHCP4_MSG_REQUEST, 14, offered, SERVER_ID, 0);
	reqinf.hdr = req.hdr; reqinf.len = req.len;
	rep.hdr = (hio_dhcp4_pkt_hdr_t*)g_repbuf; rep.len = 0; rep.capa = HIO_SIZEOF(g_repbuf);
	if (hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst) != 1) { skip ("no ack", 1); return; }
	repinf.hdr = rep.hdr; repinf.len = rep.len;
	hio_dhcp4_get_option_u32 (&repinf, HIO_DHCP4_OPT_SUBNET, &ack_nm);
	hio_dhcp4_get_option_u32 (&repinf, HIO_DHCP4_OPT_ROUTER, &ack_rt);
	hio_dhcp4_get_option_u32 (&repinf, HIO_DHCP4_OPT_LEASE_TIME, &ack_lt);

	OK (off_nm == ack_nm && off_rt == ack_rt && off_lt == ack_lt &&
	    off_nm == NETMASK && off_rt == ROUTER && off_lt == LEASE_SECS,
	    "an OFFER and the ACK that follows it describe the same network");
}

static void test_pool_exhaustion_is_silence (void)
{
	hio_uint8_t c;
	hio_uint32_t ip = 0;
	int offers = 0;

	/* the pool holds three addresses. binding all of them and then asking
	 * for a fourth must not produce a fourth address, and must not produce
	 * an error either - another server on the segment may be able to help,
	 * so silence is the correct answer. */
	for (c = 20; c < 23; c++)
	{
		if (run(HIO_DHCP4_MSG_DISCOVER, c, 0, 0, 0, HIO_NULL, &ip) == 1 && ip != 0)
		{
			run (HIO_DHCP4_MSG_REQUEST, c, ip, SERVER_ID, 0, HIO_NULL, HIO_NULL);
			offers++;
		}
	}
	OK (offers == 3, "every address in the pool can be bound");
	OK (run(HIO_DHCP4_MSG_DISCOVER, 23, 0, 0, 0, HIO_NULL, HIO_NULL) == 0,
	    "and a client arriving at an exhausted pool is answered with silence");
}

static void test_malformed_and_foreign_packets (void)
{
	hio_dhcp4_pktbuf_t req, rep;
	hio_dhcp4_pktinf_t reqinf;
	hio_skad_t dst;

	rep.hdr = (hio_dhcp4_pkt_hdr_t*)g_repbuf; rep.len = 0; rep.capa = HIO_SIZEOF(g_repbuf);

	/* a truncated datagram. a dhcp server is exposed to whatever is on the
	 * segment, so this has to be an error it reports rather than one it
	 * reads past. */
	build (&req, HIO_DHCP4_MSG_DISCOVER, 30, 0, 0, 0);
	reqinf.hdr = req.hdr; reqinf.len = 4;
	OK (hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst) <= -1,
	    "a truncated packet is refused");

	/* a reply, not a request - this server's own words coming back at it
	 * because it is bound to a broadcast address */
	build (&req, HIO_DHCP4_MSG_OFFER, 30, 0, 0, 0);
	req.hdr->op = HIO_DHCP4_OP_BOOTREPLY;
	reqinf.hdr = req.hdr; reqinf.len = req.len;
	rep.len = 0;
	OK (hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst) == 0,
	    "and a packet marked as a reply is passed over in silence");

	/* no option 53 at all is bootp, which this server does not serve */
	hio_dhcp4_init_pktbuf (&req, g_reqbuf, HIO_SIZEOF(g_reqbuf));
	req.hdr->op = HIO_DHCP4_OP_BOOTREQUEST;
	req.hdr->htype = HIO_DHCP4_HTYPE_ETHERNET;
	req.hdr->hlen = 6;
	hio_dhcp4_add_option_u8 (&req, HIO_DHCP4_OPT_IP_TTL, 64);
	reqinf.hdr = req.hdr; reqinf.len = req.len;
	rep.len = 0;
	OK (hio_svc_dhcs_process(g_dhcs, &reqinf, &rep, &dst) == 0,
	    "and a bootp packet with no message type is passed over too");
}

static void test_config_refusals (void)
{
	hio_svc_dhcs_cfg_t cfg;
	hio_svc_dhcs_t* d;

	HIO_MEMSET (&cfg, 0, HIO_SIZEOF(cfg));
	hio_bcstrtoskad (g_hio, "0.0.0.0:0", &cfg.bind_addr);
	cfg.server_id = SERVER_ID;
	cfg.pool_first = POOL_LAST;
	cfg.pool_last = POOL_FIRST;   /* inverted */
	d = hio_svc_dhcs_start(g_hio, &cfg);
	OK (!d && hio_geterrnum(g_hio) == HIO_EINVAL, "an inverted pool is refused");
	if (d) hio_svc_dhcs_stop (d);

	/* a pool the size of the address space would be an allocation nobody
	 * intended, so the bound is a refusal rather than a clamp */
	cfg.pool_first = 0x0a000000u;
	cfg.pool_last = 0x0affffffu;
	d = hio_svc_dhcs_start(g_hio, &cfg);
	OK (!d && hio_geterrnum(g_hio) == HIO_EINVAL, "and so is one larger than the bound");
	if (d) hio_svc_dhcs_stop (d);

	/* without option 54 a client cannot tell this server's replies from
	 * another's, so it is required rather than defaulted */
	cfg.pool_first = POOL_FIRST;
	cfg.pool_last = POOL_LAST;
	cfg.server_id = 0;
	d = hio_svc_dhcs_start(g_hio, &cfg);
	OK (!d && hio_geterrnum(g_hio) == HIO_EINVAL, "and a missing server identifier is refused");
	if (d) hio_svc_dhcs_stop (d);

	/* dhcpv6 is a different protocol, not this one over another family */
	cfg.server_id = SERVER_ID;
	hio_bcstrtoskad (g_hio, "[::]:0", &cfg.bind_addr);
	d = hio_svc_dhcs_start(g_hio, &cfg);
	OK (!d, "and an ipv6 bind address is refused by the v4 server");
	if (d) hio_svc_dhcs_stop (d);
}

static void test_lease_allocation_failure (void)
{
	hio_uint8_t mt = 0;
	hio_uint32_t ip = 0;
	hio_oow_t before;
	int rc;

	before = hio_svc_dhcs_getleasecount(g_dhcs);

	/* the first allocation the DISCOVER path reaches is the copy of the
	 * client identifier that the lease will own. with that refused, the
	 * server must not offer an address: an address offered but not recorded
	 * would be handed to the next client as well. */
	g_failed_allocs = 0;
	g_fail_next_alloc = 1;
	rc = run(HIO_DHCP4_MSG_DISCOVER, 40, 0, 0, 0, &mt, &ip);
	g_fail_next_alloc = 0;

	OK (g_failed_allocs == 1, "the injected allocation failure was reached");
	OK (rc <= -1, "a DISCOVER whose lease cannot be recorded is reported as an error");
	OK (hio_svc_dhcs_getleasecount(g_dhcs) == before,
	    "and records no lease rather than a half-built one");

	/* and the server is still serving afterwards - a failed allocation is not
	 * allowed to leave it in a state where nothing works */
	OK (run(HIO_DHCP4_MSG_DISCOVER, 41, 0, 0, 0, &mt, &ip) == 1 &&
	    mt == HIO_DHCP4_MSG_OFFER && ip != 0,
	    "and the next client is served normally");
}

static void test_request_allocation_failure_naks (void)
{
	hio_uint8_t mt = 0;
	int rc;

	/* a REQUEST is different from a DISCOVER: the client is waiting for a
	 * yes or a no, and silence would leave it retrying. so when the lease
	 * cannot be recorded the answer is a NAK - which tells the client to
	 * start over - rather than an error the client never hears about. */
	g_failed_allocs = 0;
	g_fail_next_alloc = 1;
	rc = run(HIO_DHCP4_MSG_REQUEST, 42, POOL_FIRST, SERVER_ID, 0, &mt, HIO_NULL);
	g_fail_next_alloc = 0;

	OK (g_failed_allocs == 1, "the injected failure was reached on the request path");
	OK (rc == 1 && mt == HIO_DHCP4_MSG_NAK,
	    "a REQUEST whose lease cannot be recorded is answered with a NAK, not silence");
}

static void test_start_allocation_failure (void)
{
	hio_svc_dhcs_cfg_t cfg;
	hio_svc_dhcs_t* d;

	HIO_MEMSET (&cfg, 0, HIO_SIZEOF(cfg));
	hio_bcstrtoskad (g_hio, "0.0.0.0:0", &cfg.bind_addr);
	cfg.server_id = SERVER_ID;
	cfg.pool_first = POOL_FIRST;
	cfg.pool_last = POOL_LAST;
	cfg.domain = "example.test";

	/* starting the service allocates the service, a copy of the domain name
	 * and a socket. failing the first of those must not leave anything
	 * behind - which is what the leak checker over this test really decides. */
	g_failed_allocs = 0;
	g_fail_next_alloc = 1;
	d = hio_svc_dhcs_start(g_hio, &cfg);
	g_fail_next_alloc = 0;

	OK (g_failed_allocs == 1 && !d,
	    "a service whose first allocation fails does not start");
	if (d) hio_svc_dhcs_stop (d);

	/* and starting normally afterwards still works */
	d = hio_svc_dhcs_start(g_hio, &cfg);
	OK (d != HIO_NULL, "and the next attempt starts normally");
	if (d) hio_svc_dhcs_stop (d);
}

/* ------------------------------------------------------------------ */

/* each group starts from a clean server so one group's leases cannot decide
 * another group's outcome */
/* RFC 2131 section 4.3.2: a NAK to a request that arrived through a relay goes
 * back to the relay, not onto a segment this server cannot reach - and with
 * the broadcast bit set, since the client must not be unicast at an address
 * the NAK is about to tell it not to use. */
static void test_relayed_nak_returns_via_the_relay (void)
{
	reqx_t x;
	hio_dhcp4_pktinf_t rep;
	hio_skad_t dst;
	hio_uint32_t ip = 0;
	hio_uint16_t port = 0;
	hio_uint8_t mt = 0;

	HIO_MEMSET (&x, 0, HIO_SIZEOF(x));

	/* first without a relay, for the contrast */
	OK (run_x(HIO_DHCP4_MSG_REQUEST, 1, 0x0a00000Bu, 0, 0, &x, &rep, &dst) == 1,
	    "an out-of-pool request is answered");
	hio_dhcp4_get_msg_type (&rep, &mt);
	OK (mt == HIO_DHCP4_MSG_NAK, "with a nak");
	dst_of (&dst, &ip, &port);
	OK (ip == 0xFFFFFFFFu && port == HIO_DHCP4_CLIENT_PORT,
	    "broadcast to the client port when no relay is involved");

	/* now through one */
	x.giaddr = 0x0a000009u;
	OK (run_x(HIO_DHCP4_MSG_REQUEST, 2, 0x0a00000Bu, 0, 0, &x, &rep, &dst) == 1,
	    "a relayed out-of-pool request is answered too");
	hio_dhcp4_get_msg_type (&rep, &mt);
	OK (mt == HIO_DHCP4_MSG_NAK, "also with a nak");
	dst_of (&dst, &ip, &port);
	OK (ip == 0x0a000009u, "sent to the relay, not to the segment");
	OK (port == HIO_DHCP4_SERVER_PORT, "and to its server port, not the client port");
	OK ((hio_ntoh16(rep.hdr->flags) & HIO_DHCP4_FLAG_BROADCAST) != 0,
	    "with the broadcast bit set for the relay to act on");
	OK (rep.hdr->giaddr == hio_hton32(0x0a000009u), "and giaddr echoed so the relay can route it");
}

/* a client that cannot receive a unicast says so in the flags, and that
 * overrides ciaddr as the place to send the reply */
static void test_broadcast_flag_overrides_ciaddr (void)
{
	reqx_t x;
	hio_skad_t dst;
	hio_uint32_t ip = 0;
	hio_uint16_t port = 0;

	HIO_MEMSET (&x, 0, HIO_SIZEOF(x));

	/* bind an address first, so the renewal below is of something real */
	OK (run_x(HIO_DHCP4_MSG_REQUEST, 3, POOL_FIRST, 0, 0, &x, HIO_NULL, HIO_NULL) == 1,
	    "an address is bound");

	/* renewing from that address, without the flag: unicast to it */
	OK (run_x(HIO_DHCP4_MSG_REQUEST, 3, 0, 0, POOL_FIRST, &x, HIO_NULL, &dst) == 1, "the renewal is acked");
	dst_of (&dst, &ip, &port);
	OK (ip == POOL_FIRST && port == HIO_DHCP4_CLIENT_PORT, "and unicast to the address it renewed");

	/* the same renewal with the flag set: broadcast instead */
	x.flags = HIO_DHCP4_FLAG_BROADCAST;
	OK (run_x(HIO_DHCP4_MSG_REQUEST, 3, 0, 0, POOL_FIRST, &x, HIO_NULL, &dst) == 1, "and again with the flag");
	dst_of (&dst, &ip, &port);
	OK (ip == 0xFFFFFFFFu, "broadcast, because the client said it cannot take a unicast");
	OK (port == HIO_DHCP4_CLIENT_PORT, "still to the client port");
}

/* the codes a reply carries, in the order it carries them. the walker takes
 * no context argument, so the result lands here. */
static int g_codes[64];
static int g_ncodes;

static int collect_code (hio_dhcp4_opt_hdr_t* o)
{
	if (g_ncodes < (int)HIO_COUNTOF(g_codes)) g_codes[g_ncodes] = o->code;
	g_ncodes++;
	return 1; /* non-zero continues the walk */
}

static void walk_reply (const hio_dhcp4_pktinf_t* rep)
{
	g_ncodes = 0;
	hio_dhcp4_walk_options (rep, collect_code);
}

static int count_option (int code)
{
	int i, n = 0;
	for (i = 0; i < g_ncodes; i++) if (g_codes[i] == code) n++;
	return n;
}

/* where 'code' first appears in the reply, or -1 */
static int index_of_option (int code)
{
	int i;
	for (i = 0; i < g_ncodes; i++) if (g_codes[i] == code) return i;
	return -1;
}

/* RFC 2132 section 9.8: the client lists what it wants and the server returns
 * what of that it can supply. what it must send regardless - the server
 * identifier and the lease times - is not the client's to ask for. */
static void test_parameter_request_list (void)
{
	static const hio_uint8_t two[] = { HIO_DHCP4_OPT_ROUTER, HIO_DHCP4_OPT_SUBNET };
	static const hio_uint8_t dup[] = { HIO_DHCP4_OPT_ROUTER, HIO_DHCP4_OPT_ROUTER, HIO_DHCP4_OPT_ROUTER };
	static const hio_uint8_t unknown[] = { 0xF0, 0xF1 };
	/* one client throughout: a repeat discover from the same client is
	 * idempotent, so these cases exercise the option list and not the pool,
	 * which holds only three addresses. */
	reqx_t x;
	hio_dhcp4_pktinf_t rep;
	hio_uint32_t v = 0;

	/* no list: everything configured comes back */
	HIO_MEMSET (&x, 0, HIO_SIZEOF(x));
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover with no list");
	walk_reply (&rep);
	OK (count_option(HIO_DHCP4_OPT_SUBNET) == 1, "the netmask is offered");
	OK (count_option(HIO_DHCP4_OPT_ROUTER) == 1, "and the router");
	OK (count_option(HIO_DHCP4_OPT_SERVER_ID) == 1, "and the server identifier");
	OK (count_option(HIO_DHCP4_OPT_LEASE_TIME) == 1, "and the lease time");

	/* a list of two: those two, and not the rest */
	x.prl = two; x.prl_len = (hio_uint8_t)HIO_COUNTOF(two);
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover naming two options");
	walk_reply (&rep);
	OK (count_option(HIO_DHCP4_OPT_ROUTER) == 1, "the router is there");
	OK (count_option(HIO_DHCP4_OPT_SUBNET) == 1, "and the netmask");
	OK (count_option(HIO_DHCP4_OPT_DNS_SERVER) == 0, "the dns servers are not, having not been asked for");
	OK (count_option(HIO_DHCP4_OPT_DOMAIN_NAME) == 0, "nor the domain name");
	OK (count_option(HIO_DHCP4_OPT_SERVER_ID) == 1, "the server identifier comes regardless");
	OK (count_option(HIO_DHCP4_OPT_LEASE_TIME) == 1, "as does the lease time");

	/* and in the order asked: router before netmask, which is not the order
	 * the server would have chosen on its own */
	OK (index_of_option(HIO_DHCP4_OPT_ROUTER) >= 0, "the router option is found");
	OK (index_of_option(HIO_DHCP4_OPT_ROUTER) < index_of_option(HIO_DHCP4_OPT_SUBNET),
	    "and the netmask comes after it, in the order the client listed rather than the server's own");

	/* a code named three times is sent once: a reply carrying it twice is
	 * ambiguous, since clients differ on which one wins */
	x.prl = dup; x.prl_len = (hio_uint8_t)HIO_COUNTOF(dup);
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover naming one option three times");
	walk_reply (&rep);
	OK (count_option(HIO_DHCP4_OPT_ROUTER) == 1, "gets it once");

	/* codes nothing is configured for are skipped, not answered with zeros */
	x.prl = unknown; x.prl_len = (hio_uint8_t)HIO_COUNTOF(unknown);
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover naming options this server has no value for");
	walk_reply (&rep);
	OK (count_option(0xF0) == 0 && count_option(0xF1) == 0, "which are simply absent");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_SERVER_ID, &v) == 0 && v == SERVER_ID,
	    "while the reply is still usable");
}

/* a client may ask for a shorter lease than the server's, and gets it. it may
 * not ask for a longer one - the configuration is a limit. */
static void test_requested_lease_time (void)
{
	reqx_t x;
	hio_dhcp4_pktinf_t rep;
	hio_uint32_t secs = 0, t1 = 0, t2 = 0, mine = 0;
	hio_svc_dhcs_lease_t lease;
	hio_ntime_t now;
	hio_ntime_t left;

	/* nothing asked: the configured lease */
	HIO_MEMSET (&x, 0, HIO_SIZEOF(x));
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover asking for no particular lease");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_LEASE_TIME, &secs) == 0 && secs == LEASE_SECS,
	    "is offered the configured lease");

	/* shorter: honoured, and T1/T2 follow it rather than the configured one */
	x.lease_req = 60;
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover asking for a minute");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_LEASE_TIME, &secs) == 0 && secs == 60, "gets a minute");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_T1, &t1) == 0 && t1 == 30, "with T1 at half of it");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_T2, &t2) == 0 && t2 == (60 / 8) * 7, "and T2 at seven eighths");

	/* longer than allowed: capped at the configuration */
	x.lease_req = 999999;
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 1, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a discover asking for far too long");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_LEASE_TIME, &secs) == 0 && secs == LEASE_SECS,
	    "is held to the configured lease");

	/* and what is recorded matches what was advertised, which is the point of
	 * settling the duration once. the address requested is the one this
	 * client was offered, not a guess at which the pool would hand out. */
	x.lease_req = 60;
	OK (run_x(HIO_DHCP4_MSG_DISCOVER, 2, 0, 0, 0, &x, &rep, HIO_NULL) == 1, "a second client discovers");
	mine = hio_ntoh32(rep.hdr->yiaddr);
	OK (mine != 0, "and is offered an address");

	OK (run_x(HIO_DHCP4_MSG_REQUEST, 2, mine, 0, 0, &x, &rep, HIO_NULL) == 1, "then requests it, asking for a minute");
	OK (hio_dhcp4_get_option_u32(&rep, HIO_DHCP4_OPT_LEASE_TIME, &secs) == 0 && secs == 60, "and is acked for a minute");

	/* found by address, not by index: the discovers above left offered leases
	 * of their own, and index 0 is one of those */
	{
		hio_oow_t i, n = hio_svc_dhcs_getleasecount(g_dhcs);
		int found = 0;
		for (i = 0; i < n; i++)
		{
			if (hio_svc_dhcs_getlease(g_dhcs, i, &lease) == 0 &&
			    lease.ipaddr == mine && lease.state == HIO_SVC_DHCS_LEASE_BOUND) { found = 1; break; }
		}
		OK (found, "with a bound lease recorded against it");
		if (found)
		{
			hio_gettime (g_hio, &now);
			HIO_SUB_NTIME (&left, &lease.expiry, &now);
			OK (left.sec <= 60 && left.sec >= 55, "expiring a minute out, not ten - what was recorded is what the client was told");
		}
		else OK (0, "(no lease to check the expiry of)");
	}
}

/* the sweep that keeps a pool nobody exhausts from remembering every client it
 * has ever seen. fired with an explicit clock rather than waited for. */
static void test_purge_timer (void)
{
	hio_ntime_t due, ahead;
	hio_oow_t before, fired = 0;

	before = g_hio->tmr.size;
	OK (before >= 1, "starting the service scheduled a sweep");

	OK (before == 1, "exactly one, so jobs[0] below is it");

	/* fire it at exactly its due moment, not past it.
	 *
	 * hio_firetmrjobs() drains every job whose time has come, in an uncapped
	 * loop, so a handler that reschedules to a moment at or before the clock
	 * it was fired at spins there forever. firing at the due moment leaves
	 * the reschedule a hair ahead whatever the handler measures from, so the
	 * loop always exits and the interval can be inspected instead - which
	 * makes a regression a failed assertion rather than a hung suite. */
	due = g_hio->tmr.jobs[0].when;
	hio_firetmrjobs (g_hio, &due, &fired);
	OK (fired == 1, "the sweep fired once");
	OK (g_hio->tmr.size == before, "and left another scheduled behind");

	/* a full interval ahead of the clock it was fired at. a handler that
	 * measured from the real clock instead would leave one due almost
	 * immediately, which is drift in production and a spin under a simulated
	 * clock. */
	HIO_SUB_NTIME (&ahead, &g_hio->tmr.jobs[0].when, &due);
	OK (ahead.sec >= PURGE_INTERVAL_SECS_FOR_TEST - 5,
	    "the next sweep is a full interval ahead, measured from the clock the handler was given");
}

static void with_fresh_server (void (*fn)(void), int nassert)
{
	if (start_server() <= -1) { skip ("cannot start the dhcp server", nassert); return; }
	fn ();
	stop_server ();
}

int main (void)
{
	hio_errinf_t errinf;

	g_hio = hio_open(&g_fi_mmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		no_plan ();
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	no_plan ();

	with_fresh_server (test_discover_offers, 4);
	with_fresh_server (test_offer_is_held_against_other_clients, 1);
	with_fresh_server (test_discover_is_idempotent, 1);
	with_fresh_server (test_request_acks_and_binds, 3);
	with_fresh_server (test_request_for_another_server_is_ignored, 2);
	with_fresh_server (test_request_outside_pool_is_naked, 1);
	with_fresh_server (test_request_for_someone_elses_address_is_naked, 1);
	with_fresh_server (test_renewal, 1);
	with_fresh_server (test_release_frees_the_address, 2);
	with_fresh_server (test_decline_takes_the_address_out_of_service, 3);
	with_fresh_server (test_inform_returns_options_without_a_lease, 6);
	with_fresh_server (test_offer_and_ack_agree, 1);
	with_fresh_server (test_pool_exhaustion_is_silence, 2);
	with_fresh_server (test_malformed_and_foreign_packets, 3);
	with_fresh_server (test_lease_allocation_failure, 4);
	with_fresh_server (test_request_allocation_failure_naks, 2);
	with_fresh_server (test_relayed_nak_returns_via_the_relay, 8);
	with_fresh_server (test_broadcast_flag_overrides_ciaddr, 6);
	with_fresh_server (test_parameter_request_list, 18);
	with_fresh_server (test_requested_lease_time, 10);
	with_fresh_server (test_purge_timer, 3);
	test_config_refusals ();
	test_start_allocation_failure ();

	hio_close (g_hio);
	return exit_status();
}
