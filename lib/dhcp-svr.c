/*
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * DHCPv4 server service.
 *
 * Written against RFC 2131 (the protocol) and RFC 2132 (the options). Not
 * derived from any GPL implementation: hio is BSD-licensed, so a port of one
 * would have relicensed this file.
 *
 * The state machine is hio_svc_dhcs_process(), which is a function from a
 * request to a reply and takes no part in any I/O. The read callback below is
 * a thin wrapper over it. That split is deliberate - it is what lets the
 * protocol be tested by feeding it packets rather than by standing up a
 * network, and it keeps the socket handling out of the part that is easy to
 * get subtly wrong.
 */

#include <hio-dhcp.h>
#include <hio-sck.h>
#include "hio-prv.h"

/* a reply never needs more than this. the fixed header plus the options this
 * server ever sends comes to well under 576, which RFC 2131 gives as the
 * smallest datagram a client must accept. */
#define REPLY_BUFSIZE (576)

/* how long an OFFER holds an address before another client may be given it.
 * short on purpose: it exists only to stop two clients discovering at the same
 * moment from being offered the same address. */
#define OFFER_HOLD_SECS (10)

/* how often expired leases are swept.
 *
 * a lease is not reclaimed the instant it expires - nothing needs it to be -
 * so this is a housekeeping interval rather than a deadline. it exists because
 * reclaiming only on exhaustion means a pool that is never exhausted keeps
 * every lease it has ever issued, and a long-running server's memory grows
 * with the number of clients it has ever seen rather than with the number it
 * currently serves. */
#define PURGE_INTERVAL_SECS (60)

struct hio_svc_dhcs_t
{
	HIO_SVC_HEADER;

	int stopping;
	hio_dev_sck_t* sck;
	hio_svc_dhcs_cfg_t cfg;

	/* the domain is copied because the caller is not asked to keep its
	 * string alive for the life of the service */
	hio_bch_t* domain;

	/* active leases. a flat array scanned linearly: a dhcp pool is bounded
	 * and small, and this avoids a second structure to keep consistent with
	 * the first. the cost is O(n) per lookup, which is the honest trade and
	 * not one that matters at pool sizes this serves. */
	hio_svc_dhcs_lease_t* leases;
	hio_oow_t nleases;
	hio_oow_t leases_capa;

	/* the periodic sweep. HIO_TMRIDX_INVALID when none is scheduled, which is
	 * the case while the service is stopping. */
	hio_tmridx_t purge_tmridx;
};

/* the extension on the socket, so the read callback can find its service */
struct dhcs_sck_xtn_t
{
	hio_svc_dhcs_t* dhcs;
};
typedef struct dhcs_sck_xtn_t dhcs_sck_xtn_t;

/* ------------------------------------------------------------------------- */
/* leases                                                                    */
/* ------------------------------------------------------------------------- */

static void free_lease_at (hio_svc_dhcs_t* dhcs, hio_oow_t i)
{
	hio_t* hio = dhcs->hio;

	if (dhcs->leases[i].cid) hio_freemem(hio, dhcs->leases[i].cid);

	/* the order of leases carries no meaning, so the last one fills the gap
	 * rather than shifting everything down */
	dhcs->nleases--;
	if (i != dhcs->nleases) dhcs->leases[i] = dhcs->leases[dhcs->nleases];
	HIO_MEMSET(&dhcs->leases[dhcs->nleases], 0, HIO_SIZEOF(dhcs->leases[0]));
}

hio_oow_t hio_svc_dhcs_purgeexpiredleases (hio_svc_dhcs_t* dhcs)
{
	hio_t* hio = dhcs->hio;
	hio_ntime_t now;
	hio_oow_t i, n = 0;

	hio_gettime(hio, &now);

	i = 0;
	while (i < dhcs->nleases)
	{
		/* a declined address is not reclaimed on a timer. the client told us
		 * something is already using it, and nothing since then says
		 * otherwise. */
		if (dhcs->leases[i].state != HIO_SVC_DHCS_LEASE_DECLINED &&
		    HIO_CMP_NTIME(&dhcs->leases[i].expiry, &now) <= 0)
		{
			free_lease_at(dhcs, i);
			n++;
			/* free_lease_at moved a different lease into this slot, so the
			 * index is not advanced */
			continue;
		}
		i++;
	}

	return n;
}

static hio_svc_dhcs_lease_t* find_lease_by_cid (hio_svc_dhcs_t* dhcs, const hio_uint8_t* cid, hio_uint8_t cidlen)
{
	hio_oow_t i;

	for (i = 0; i < dhcs->nleases; i++)
	{
		if (dhcs->leases[i].cidlen == cidlen && dhcs->leases[i].cid &&
		    HIO_MEMCMP(dhcs->leases[i].cid, cid, cidlen) == 0) return &dhcs->leases[i];
	}

	return HIO_NULL;
}

static hio_svc_dhcs_lease_t* find_lease_by_ip (hio_svc_dhcs_t* dhcs, hio_uint32_t ipaddr)
{
	hio_oow_t i;

	for (i = 0; i < dhcs->nleases; i++)
	{
		if (dhcs->leases[i].ipaddr == ipaddr) return &dhcs->leases[i];
	}

	return HIO_NULL;
}

/* room for one more lease. the array grows by doubling; a failure here is
 * reported rather than papered over, because the alternative is handing out an
 * address the server will not remember having handed out. */
static int ensure_lease_room (hio_svc_dhcs_t* dhcs)
{
	hio_t* hio = dhcs->hio;
	hio_svc_dhcs_lease_t* tmp;
	hio_oow_t newcapa;

	if (dhcs->nleases < dhcs->leases_capa) return 0;

	newcapa = (dhcs->leases_capa <= 0)? 16: dhcs->leases_capa * 2;

	/* the pool bounds the number of leases that can ever be live, so a
	 * capacity beyond it means something has gone wrong in the accounting */
	if (newcapa > HIO_SVC_DHCS_MAX_POOL_SIZE) newcapa = HIO_SVC_DHCS_MAX_POOL_SIZE;
	if (newcapa <= dhcs->leases_capa)
	{
		hio_seterrbfmt(hio, HIO_ENOCAPA, "dhcp lease table full at %zu entries", dhcs->leases_capa);
		return -1;
	}

	tmp = (hio_svc_dhcs_lease_t*)hio_reallocmem(hio, dhcs->leases, HIO_SIZEOF(*tmp) * newcapa);
	if (HIO_UNLIKELY(!tmp)) return -1; /* hio_reallocmem has set the error */

	/* the new tail is zeroed so a partially filled array never presents a
	 * stale cid pointer as if it were live */
	HIO_MEMSET(&tmp[dhcs->leases_capa], 0, HIO_SIZEOF(*tmp) * (newcapa - dhcs->leases_capa));

	dhcs->leases = tmp;
	dhcs->leases_capa = newcapa;
	return 0;
}

/* record a lease, or update the one this client already holds. returns the
 * lease, or HIO_NULL with the error set - which the caller must treat as "no
 * address granted" rather than ignoring. */
static hio_svc_dhcs_lease_t* put_lease (
	hio_svc_dhcs_t* dhcs, const hio_uint8_t* cid, hio_uint8_t cidlen,
	hio_uint32_t ipaddr, int state, hio_uint32_t secs)
{
	hio_t* hio = dhcs->hio;
	hio_svc_dhcs_lease_t* lease;
	hio_ntime_t now, dur;

	lease = find_lease_by_cid(dhcs, cid, cidlen);
	if (!lease)
	{
		hio_uint8_t* cidcopy;

		/* the identifier is variable length and belongs to the packet, which
		 * is gone as soon as this returns, so it is copied */
		cidcopy = (hio_uint8_t*)hio_allocmem(hio, cidlen);
		if (HIO_UNLIKELY(!cidcopy)) return HIO_NULL;

		if (ensure_lease_room(dhcs) <= -1)
		{
			hio_freemem(hio, cidcopy);
			return HIO_NULL;
		}

		HIO_MEMCPY(cidcopy, cid, cidlen);

		lease = &dhcs->leases[dhcs->nleases++];
		lease->cid = cidcopy;
		lease->cidlen = cidlen;
	}

	lease->ipaddr = ipaddr;
	lease->state = state;

	hio_gettime(hio, &now);
	HIO_INIT_NTIME(&dur, (hio_ntime_sec_t)secs, 0);
	HIO_ADD_NTIME(&lease->expiry, &now, &dur);

	return lease;
}

/* is this address one this server may hand out, and is it free for this
 * client? a lease the same client already holds does not count as taken. */
static int addr_available_for (hio_svc_dhcs_t* dhcs, hio_uint32_t ipaddr, const hio_uint8_t* cid, hio_uint8_t cidlen)
{
	hio_svc_dhcs_lease_t* held;

	if (ipaddr < dhcs->cfg.pool_first || ipaddr > dhcs->cfg.pool_last) return 0;

	held = find_lease_by_ip(dhcs, ipaddr);
	if (!held) return 1;

	if (held->state == HIO_SVC_DHCS_LEASE_DECLINED) return 0;

	return (held->cidlen == cidlen && held->cid && HIO_MEMCMP(held->cid, cid, cidlen) == 0);
}

/* choose an address for a client: the one it already has, else the one it
 * asked for if that is free, else the lowest free one. 0 if the pool is
 * exhausted. */
static hio_uint32_t select_addr (
	hio_svc_dhcs_t* dhcs, const hio_uint8_t* cid, hio_uint8_t cidlen, hio_uint32_t requested)
{
	hio_svc_dhcs_lease_t* mine;
	hio_uint32_t ip;
	int purged = 0;

	mine = find_lease_by_cid(dhcs, cid, cidlen);
	if (mine && mine->state != HIO_SVC_DHCS_LEASE_DECLINED &&
	    mine->ipaddr >= dhcs->cfg.pool_first && mine->ipaddr <= dhcs->cfg.pool_last)
	{
		/* giving a returning client the same address is not merely tidy - it
		 * is what lets it keep using the address across a restart */
		return mine->ipaddr;
	}

	if (requested != 0 && addr_available_for(dhcs, requested, cid, cidlen)) return requested;

again:
	for (ip = dhcs->cfg.pool_first; ip <= dhcs->cfg.pool_last; ip++)
	{
		if (addr_available_for(dhcs, ip, cid, cidlen)) return ip;
		if (ip == 0xFFFFFFFFu) break; /* the loop counter would wrap */
	}

	if (!purged)
	{
		/* nothing free. expired leases are only reclaimed when the pool runs
		 * dry, so that a client returning after its lease lapsed still tends
		 * to get its old address back. */
		purged = 1;
		if (hio_svc_dhcs_purgeexpiredleases(dhcs) > 0) goto again;
	}

	return 0;
}

hio_oow_t hio_svc_dhcs_getleasecount (hio_svc_dhcs_t* dhcs)
{
	return dhcs->nleases;
}

int hio_svc_dhcs_getlease (hio_svc_dhcs_t* dhcs, hio_oow_t index, hio_svc_dhcs_lease_t* lease)
{
	if (index >= dhcs->nleases)
	{
		hio_seterrbfmt(dhcs->hio, HIO_ENOENT, "no lease at index %zu", index);
		return -1;
	}

	*lease = dhcs->leases[index];
	return 0;
}

/* ------------------------------------------------------------------------- */
/* reply construction                                                        */
/* ------------------------------------------------------------------------- */

/* every option this server offers with an address. kept in one place so an
 * OFFER and an ACK cannot drift apart, which is a class of bug where a client
 * accepts an offer and then finds the acknowledgement described something
 * else. */
/* emit the one option 'code' names, if this server has a value for it.
 *
 * a code nothing is configured for is silently skipped: RFC 2131 asks the
 * server for the parameters it can supply and says nothing about the rest. */
static int add_one_option (hio_svc_dhcs_t* dhcs, hio_dhcp4_pktbuf_t* rep, int code)
{
	const hio_svc_dhcs_cfg_t* cfg = &dhcs->cfg;

	switch (code)
	{
		case HIO_DHCP4_OPT_SUBNET:
			/* a zero here means "not configured". offering 0.0.0.0 as a
			 * netmask or router would be worse than saying nothing at all. */
			if (cfg->netmask == 0) return 0;
			return hio_dhcp4_add_option_uint32(rep, code, cfg->netmask);

		case HIO_DHCP4_OPT_ROUTER:
			if (cfg->router == 0) return 0;
			return hio_dhcp4_add_option_uint32(rep, code, cfg->router);

		case HIO_DHCP4_OPT_DNS_SERVER:
		{
			/* the dns option carries a list, so both servers go in one
			 * option rather than in two a client may only read the first of */
			hio_uint32_t both[2];
			hio_oow_t n = 1;
			if (cfg->dns1 == 0) return 0;
			both[0] = hio_hton32(cfg->dns1);
			if (cfg->dns2 != 0) { both[1] = hio_hton32(cfg->dns2); n = 2; }
			return hio_dhcp4_add_option(rep, code, both, (hio_uint8_t)(n * 4));
		}

		case HIO_DHCP4_OPT_DOMAIN_NAME:
		{
			hio_oow_t len;
			if (!dhcs->domain) return 0;
			len = hio_count_bcstr(dhcs->domain);
			/* an option payload length is one octet, so a longer name cannot
			 * be expressed. it is dropped rather than truncated into a
			 * different domain name. */
			if (len == 0 || len > 255) return 0;
			return hio_dhcp4_add_option(rep, code, dhcs->domain, (hio_uint8_t)len);
		}

		default:
			return 0;
	}
}

/* the options that go with a reply.
 *
 * 'lease_secs' of zero means the reply carries no lease - an ACK to an INFORM,
 * where the client has its address by other means.
 *
 * a client lists the parameters it wants in option 55, and gets them in the
 * order it asked, which is what RFC 2132 section 9.8 describes. a request
 * without that option gets everything configured, since there is nothing to
 * narrow the reply down to. the server identifier and the lease times are sent
 * either way: RFC 2131 requires them and the reply is unusable without them,
 * so they are not the client's to ask for or decline. */
static int add_config_options (hio_svc_dhcs_t* dhcs, const hio_dhcp4_pktinf_t* req, hio_dhcp4_pktbuf_t* rep, hio_uint32_t lease_secs)
{
	static const hio_uint8_t everything[] =
	{
		HIO_DHCP4_OPT_SUBNET,
		HIO_DHCP4_OPT_ROUTER,
		HIO_DHCP4_OPT_DNS_SERVER,
		HIO_DHCP4_OPT_DOMAIN_NAME
	};
	const hio_uint8_t* list = HIO_NULL;
	hio_uint8_t listlen = 0;
	hio_uint8_t sent[32]; /* one bit per code, so nothing is emitted twice */
	hio_uint8_t i;

	if (hio_dhcp4_add_option_uint32(rep, HIO_DHCP4_OPT_SERVER_ID, dhcs->cfg.server_id) <= -1) return -1;

	if (lease_secs > 0)
	{
		if (hio_dhcp4_add_option_uint32(rep, HIO_DHCP4_OPT_LEASE_TIME, lease_secs) <= -1) return -1;
		/* T1 and T2 at the conventional fractions of the lease. a client that
		 * ignores them falls back to the same fractions itself, but sending
		 * them lets the server move renewal earlier if it wants to. */
		if (hio_dhcp4_add_option_uint32(rep, HIO_DHCP4_OPT_T1, lease_secs / 2) <= -1) return -1;
		if (hio_dhcp4_add_option_uint32(rep, HIO_DHCP4_OPT_T2, (lease_secs / 8) * 7) <= -1) return -1;
	}

	if (hio_dhcp4_get_option_data(req, HIO_DHCP4_OPT_PARAM_REQ, &list, &listlen) <= -1 || listlen == 0)
	{
		list = everything;
		listlen = (hio_uint8_t)HIO_COUNTOF(everything);
	}

	HIO_MEMSET (sent, 0, HIO_SIZEOF(sent));
	for (i = 0; i < listlen; i++)
	{
		hio_uint8_t code = list[i];
		/* a client may name a code twice. emitting the option twice leaves
		 * the reply ambiguous - clients differ on whether the first or the
		 * last wins - so each is emitted once. */
		if (sent[code >> 3] & (1 << (code & 7))) continue;
		sent[code >> 3] |= (hio_uint8_t)(1 << (code & 7));
		if (add_one_option(dhcs, rep, code) <= -1) return -1;
	}

	return 0;
}

/* how long a lease to grant.
 *
 * a client may name the duration it wants in option 51. it is honoured up to
 * the configured lease time and no further - the configuration is the server's
 * limit, not a suggestion - and a client asking for nothing, or for more than
 * is allowed, gets the configured value. */
static hio_uint32_t lease_secs_for (hio_svc_dhcs_t* dhcs, const hio_dhcp4_pktinf_t* req)
{
	hio_uint32_t cfg_secs = (dhcs->cfg.lease_secs > 0)? dhcs->cfg.lease_secs: HIO_SVC_DHCS_DFL_LEASE_SECS;
	hio_uint32_t want = 0;

	if (hio_dhcp4_get_option_uint32(req, HIO_DHCP4_OPT_LEASE_TIME, &want) <= -1) return cfg_secs;
	if (want == 0 || want > cfg_secs) return cfg_secs;
	return want;
}

/* where a reply goes, per RFC 2131 section 4.1.
 *
 * the awkward case is a client that has no address yet and did not set the
 * broadcast flag: strictly the reply should be unicast to an address the client
 * does not yet answer ARP for, which needs a hand-built link-layer frame. this
 * server broadcasts instead. that reaches every compliant client - the flag
 * exists for clients that cannot receive a unicast - and costs a broadcast on
 * the segment for the ones that could have taken it directly. */
/* an address in network order becomes an hio_ip4ad_t by copy. the header
 * fields it comes from are not aligned to that type, so a cast would be an
 * unaligned access of exactly the kind the message layer avoids. */
static void set_ip4_skad (hio_skad_t* dst, hio_uint16_t port, hio_uint32_t netorder_addr)
{
	hio_ip4ad_t ad;
	HIO_MEMCPY (ad.v, &netorder_addr, HIO_SIZEOF(ad.v));
	hio_skad_init_for_ip4 (dst, port, &ad);
}

static void reply_dstaddr (const hio_dhcp4_pktinf_t* req, hio_skad_t* dst)
{
	if (req->hdr->giaddr != 0)
	{
		/* it came through a relay, so it goes back the same way - to the
		 * relay's server port, not the client's */
		set_ip4_skad (dst, HIO_DHCP4_SERVER_PORT, req->hdr->giaddr);
		return;
	}

	if (req->hdr->ciaddr != 0 && !(hio_ntoh16(req->hdr->flags) & HIO_DHCP4_FLAG_BROADCAST))
	{
		/* the client already has an address and is reachable at it. the flag
		 * is what overrides that: a client that sets it is saying it cannot
		 * take a unicast, whatever ciaddr says, so the reply is broadcast. */
		set_ip4_skad (dst, HIO_DHCP4_CLIENT_PORT, req->hdr->ciaddr);
		return;
	}

	set_ip4_skad (dst, HIO_DHCP4_CLIENT_PORT, hio_hton32(0xFFFFFFFFu));
}

/* ------------------------------------------------------------------------- */
/* the state machine                                                         */
/* ------------------------------------------------------------------------- */

static int make_nak (hio_svc_dhcs_t* dhcs, const hio_dhcp4_pktinf_t* req, hio_dhcp4_pktbuf_t* rep, hio_skad_t* dst)
{
	if (hio_dhcp4_init_reply_pktbuf(rep, rep->hdr, rep->capa, req) <= -1) return -1;
	if (hio_dhcp4_add_option_uint8(rep, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_NAK) <= -1) return -1;
	if (hio_dhcp4_add_option_uint32(rep, HIO_DHCP4_OPT_SERVER_ID, dhcs->cfg.server_id) <= -1) return -1;
	if (hio_dhcp4_add_option(rep, HIO_DHCP4_OPT_END, HIO_NULL, 0) <= -1) return -1;

	if (req->hdr->giaddr != 0)
	{
		/* a relayed request is answered through the relay, a NAK included -
		 * the client is on a segment this server cannot reach directly. the
		 * broadcast flag is set so the relay puts it on that segment as a
		 * broadcast rather than trying to unicast to an address the NAK is
		 * about to tell the client it may not use. RFC 2131 section 4.3.2. */
		rep->hdr->flags = hio_hton16((hio_uint16_t)(hio_ntoh16(rep->hdr->flags) | HIO_DHCP4_FLAG_BROADCAST));
		set_ip4_skad (dst, HIO_DHCP4_SERVER_PORT, req->hdr->giaddr);
		return 1;
	}

	/* a NAK says the client's notion of its address is wrong, so it must not
	 * be sent to that address - it goes to the broadcast address so the
	 * client hears it whatever it currently believes */
	set_ip4_skad (dst, HIO_DHCP4_CLIENT_PORT, hio_hton32(0xFFFFFFFFu));
	return 1;
}

int hio_svc_dhcs_process (hio_svc_dhcs_t* dhcs, const hio_dhcp4_pktinf_t* req, hio_dhcp4_pktbuf_t* rep, hio_skad_t* dstaddr)
{
	hio_t* hio = dhcs->hio;
	hio_uint8_t mtype;
	const hio_uint8_t* cid;
	hio_uint8_t cidlen;
	hio_uint32_t requested = 0, server_id = 0;
	hio_uint32_t lease_secs;
	hio_svc_dhcs_lease_t* lease;
	hio_uint32_t yiaddr;
	void* repbuf = rep->hdr;
	hio_oow_t repcapa = rep->capa;

	if (hio_dhcp4_check_pkt(req) <= -1)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "malformed dhcp packet");
		return -1;
	}

	/* a reply is only ever made to a request. a packet already marked as a
	 * reply is another server's, seen because this one is bound to a
	 * broadcast address. */
	if (req->hdr->op != HIO_DHCP4_OP_BOOTREQUEST) return 0;

	if (hio_dhcp4_get_msg_type(req, &mtype) <= -1)
	{
		/* no option 53 means bootp, which this server does not answer */
		return 0;
	}

	if (hio_dhcp4_get_client_id(req, &cid, &cidlen) <= -1)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "dhcp request with no usable client identifier");
		return -1;
	}

	hio_dhcp4_get_option_uint32(req, HIO_DHCP4_OPT_REQUESTED_IPADDR, &requested);
	hio_dhcp4_get_option_uint32(req, HIO_DHCP4_OPT_SERVER_ID, &server_id);

	/* settled once, so that the duration recorded against the lease and the
	 * duration advertised in the reply cannot drift apart */
	lease_secs = lease_secs_for(dhcs, req);

	switch (mtype)
	{
		case HIO_DHCP4_MSG_DISCOVER:
			yiaddr = select_addr(dhcs, cid, cidlen, requested);
			if (yiaddr == 0)
			{
				/* nothing to offer. silence is the correct answer - another
				 * server on the segment may be able to help. */
				HIO_INFO1(hio, "DHCS(%p) - no address available to offer\n", dhcs);
				return 0;
			}

			/* the offer is recorded so that a second client discovering
			 * before this one requests is not offered the same address */
			lease = put_lease(dhcs, cid, cidlen, yiaddr, HIO_SVC_DHCS_LEASE_OFFERED, OFFER_HOLD_SECS);
			if (HIO_UNLIKELY(!lease)) return -1; /* out of memory - grant nothing */

			if (hio_dhcp4_init_reply_pktbuf(rep, repbuf, repcapa, req) <= -1) return -1;
			rep->hdr->yiaddr = hio_hton32(yiaddr);
			rep->hdr->siaddr = hio_hton32(dhcs->cfg.server_id);
			if (hio_dhcp4_add_option_uint8(rep, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_OFFER) <= -1) return -1;
			if (add_config_options(dhcs, req, rep, lease_secs) <= -1) return -1;
			if (hio_dhcp4_add_option(rep, HIO_DHCP4_OPT_END, HIO_NULL, 0) <= -1) return -1;
			reply_dstaddr(req, dstaddr);
			return 1;

		case HIO_DHCP4_MSG_REQUEST:
			/* a REQUEST carrying a server identifier is the client announcing
			 * which offer it took. if that was not ours, the address it names
			 * is not ours to confirm or deny. */
			if (server_id != 0 && server_id != dhcs->cfg.server_id)
			{
				/* and any address we offered it is released, rather than held
				 * until the offer times out */
				lease = find_lease_by_cid(dhcs, cid, cidlen);
				if (lease && lease->state == HIO_SVC_DHCS_LEASE_OFFERED)
				{
					hio_oow_t i = (hio_oow_t)(lease - dhcs->leases);
					free_lease_at(dhcs, i);
				}
				return 0;
			}

			/* which address is being requested: the option if present, else
			 * ciaddr for a client renewing one it already holds */
			yiaddr = (requested != 0)? requested: hio_ntoh32(req->hdr->ciaddr);
			if (yiaddr == 0) return make_nak(dhcs, req, rep, dstaddr);

			if (!addr_available_for(dhcs, yiaddr, cid, cidlen))
			{
				/* outside the pool, or held by somebody else. the client must
				 * be told so it starts over rather than using it. */
				HIO_INFO2(hio, "DHCS(%p) - refusing request for unavailable address %u\n", dhcs, (unsigned int)yiaddr);
				return make_nak(dhcs, req, rep, dstaddr);
			}

			lease = put_lease(dhcs, cid, cidlen, yiaddr, HIO_SVC_DHCS_LEASE_BOUND, lease_secs);
			if (HIO_UNLIKELY(!lease))
			{
				/* the lease could not be recorded, so it must not be
				 * acknowledged - an address the server will not remember
				 * issuing is worse than no address */
				HIO_INFO1(hio, "DHCS(%p) - unable to record lease. sending nak\n", dhcs);
				return make_nak(dhcs, req, rep, dstaddr);
			}

			if (hio_dhcp4_init_reply_pktbuf(rep, repbuf, repcapa, req) <= -1) return -1;
			rep->hdr->yiaddr = hio_hton32(yiaddr);
			rep->hdr->siaddr = hio_hton32(dhcs->cfg.server_id);
			/* ciaddr is echoed so a renewing client can match the reply */
			rep->hdr->ciaddr = req->hdr->ciaddr;
			if (hio_dhcp4_add_option_uint8(rep, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_ACK) <= -1) return -1;
			if (add_config_options(dhcs, req, rep, lease_secs) <= -1) return -1;
			if (hio_dhcp4_add_option(rep, HIO_DHCP4_OPT_END, HIO_NULL, 0) <= -1) return -1;
			reply_dstaddr(req, dstaddr);
			return 1;

		case HIO_DHCP4_MSG_RELEASE:
			/* the client is done with it. a release is not answered. */
			lease = find_lease_by_cid(dhcs, cid, cidlen);
			if (lease) free_lease_at(dhcs, (hio_oow_t)(lease - dhcs->leases));
			return 0;

		case HIO_DHCP4_MSG_DECLINE:
			/* the client found the address already in use. it is marked
			 * rather than freed, or the next client would be handed the same
			 * address and discover the same conflict. */
			yiaddr = (requested != 0)? requested: hio_ntoh32(req->hdr->ciaddr);
			if (yiaddr != 0)
			{
				lease = find_lease_by_ip(dhcs, yiaddr);
				if (lease) lease->state = HIO_SVC_DHCS_LEASE_DECLINED;
				else
				{
					/* not currently leased, but still not to be handed out */
					lease = put_lease(dhcs, cid, cidlen, yiaddr, HIO_SVC_DHCS_LEASE_DECLINED, 0);
					if (HIO_UNLIKELY(!lease))
					{
						/* nothing can be done about it, and there is nothing
						 * to reply to a DECLINE anyway - so this is logged
						 * and the address may be offered again later */
						HIO_INFO1(hio, "DHCS(%p) - unable to record a declined address\n", dhcs);
					}
				}
			}
			return 0;

		case HIO_DHCP4_MSG_INFORM:
			/* the client has an address by other means and wants only the
			 * options. no lease is involved, and yiaddr stays zero. */
			if (hio_dhcp4_init_reply_pktbuf(rep, repbuf, repcapa, req) <= -1) return -1;
			rep->hdr->ciaddr = req->hdr->ciaddr;
			if (hio_dhcp4_add_option_uint8(rep, HIO_DHCP4_OPT_MESSAGE_TYPE, HIO_DHCP4_MSG_ACK) <= -1) return -1;
			if (add_config_options(dhcs, req, rep, 0) <= -1) return -1;
			if (hio_dhcp4_add_option(rep, HIO_DHCP4_OPT_END, HIO_NULL, 0) <= -1) return -1;
			reply_dstaddr(req, dstaddr);
			return 1;

		default:
			/* OFFER, ACK and NAK are a server's own words coming back at it;
			 * the lease-query family is not implemented. neither is answered. */
			return 0;
	}
}

/* ------------------------------------------------------------------------- */
/* housekeeping                                                              */
/* ------------------------------------------------------------------------- */

static void on_purge_timer (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job);

/* 'from' is the moment the next sweep is measured from. a reschedule passes
 * the time its handler was given rather than reading the clock again: the
 * interval then follows the timer's own notion of now, so a slow loop does not
 * accumulate drift, and a caller driving the timers with an explicit clock -
 * which is how this is tested - sees the sweep move forward instead of falling
 * due again immediately. HIO_NULL reads the clock, for the first one. */
static int schedule_purge (hio_svc_dhcs_t* dhcs, const hio_ntime_t* from)
{
	hio_t* hio = dhcs->hio;
	hio_tmrjob_t tmrjob;
	hio_ntime_t interval;

	HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
	tmrjob.ctx = dhcs;
	if (from) tmrjob.when = *from;
	else hio_gettime(hio, &tmrjob.when);
	HIO_INIT_NTIME(&interval, PURGE_INTERVAL_SECS, 0);
	HIO_ADD_NTIME(&tmrjob.when, &tmrjob.when, &interval);
	tmrjob.handler = on_purge_timer;
	tmrjob.idxptr = &dhcs->purge_tmridx;

	dhcs->purge_tmridx = hio_instmrjob(hio, &tmrjob);
	return (dhcs->purge_tmridx == HIO_TMRIDX_INVALID)? -1: 0;
}

static void on_purge_timer (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_svc_dhcs_t* dhcs = (hio_svc_dhcs_t*)job->ctx;
	hio_oow_t n;

	if (dhcs->stopping) return;

	n = hio_svc_dhcs_purgeexpiredleases(dhcs);
	if (n > 0) HIO_DEBUG2(hio, "DHCS(%p) - reclaimed %zu expired lease(s)\n", dhcs, n);

	/* rescheduled from the handler rather than run as a repeating job, so a
	 * sweep that cannot be scheduled again is visible in the log instead of
	 * silently ending the housekeeping. the service keeps working either way:
	 * exhaustion still triggers a sweep of its own. */
	if (schedule_purge(dhcs, now) <= -1)
		HIO_INFO1(hio, "DHCS(%p) - unable to reschedule the lease sweep\n", dhcs);
}

/* ------------------------------------------------------------------------- */
/* the socket, which is only a carrier for the above                         */
/* ------------------------------------------------------------------------- */

static int dhcs_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_t* hio = sck->hio;
	dhcs_sck_xtn_t* xtn = (dhcs_sck_xtn_t*)hio_dev_sck_getxtn(sck);
	hio_svc_dhcs_t* dhcs = xtn->dhcs;
	hio_dhcp4_pktinf_t req;
	hio_dhcp4_pktbuf_t rep;
	hio_uint8_t repbuf[REPLY_BUFSIZE];
	hio_skad_t dstaddr;
	int n;

	if (dlen <= 0) return 0; /* nothing, or the socket saying it is done */

	req.hdr = (hio_dhcp4_pkt_hdr_t*)data;
	req.len = (hio_oow_t)dlen;

	rep.hdr = (hio_dhcp4_pkt_hdr_t*)repbuf;
	rep.len = 0;
	rep.capa = HIO_SIZEOF(repbuf);

	n = hio_svc_dhcs_process(dhcs, &req, &rep, &dstaddr);
	if (n <= -1)
	{
		/* one bad datagram is not a reason to stop serving everyone else. a
		 * dhcp server is exposed to whatever is on the segment, so a packet
		 * it cannot make sense of is an expected event, not a fault. */
		HIO_INFO2(hio, "DHCS(%p) - ignoring a request that could not be handled - %js\n", dhcs, hio_geterrmsg(hio));
		return 0;
	}
	if (n == 0) return 0; /* nothing to say */

	if (hio_dev_sck_write(sck, rep.hdr, (hio_iolen_t)rep.len, HIO_NULL, &dstaddr) <= -1)
	{
		HIO_INFO2(hio, "DHCS(%p) - unable to send a reply - %js\n", dhcs, hio_geterrmsg(hio));
		/* and again, not a reason to take the server down */
	}

	return 0;
}

static int dhcs_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void dhcs_on_disconnect (hio_dev_sck_t* sck)
{
	dhcs_sck_xtn_t* xtn = (dhcs_sck_xtn_t*)hio_dev_sck_getxtn(sck);
	if (xtn->dhcs && xtn->dhcs->sck == sck) xtn->dhcs->sck = HIO_NULL;
}

static hio_dev_sck_t* open_socket (hio_svc_dhcs_t* dhcs)
{
	hio_t* hio = dhcs->hio;
	hio_dev_sck_make_t m;
	hio_dev_sck_bind_t b;
	hio_dev_sck_t* sck = HIO_NULL;
	dhcs_sck_xtn_t* xtn;
	int f;

	f = hio_skad_get_family(&dhcs->cfg.bind_addr);
	if (f != HIO_AF_INET)
	{
		/* this service speaks DHCPv4. DHCPv6 is a different protocol with a
		 * different message format, not the same one over another family, so
		 * an ipv6 bind address here is a mistake rather than a variant. */
		hio_seterrbfmt(hio, HIO_EINVAL, "dhcpv4 server needs an ipv4 bind address");
		return HIO_NULL;
	}

	HIO_MEMSET(&m, 0, HIO_SIZEOF(m));
	m.type = HIO_DEV_SCK_UDP4;
	m.on_read = dhcs_on_read;
	m.on_write = dhcs_on_write;
	m.on_disconnect = dhcs_on_disconnect;

	sck = hio_dev_sck_make(hio, HIO_SIZEOF(*xtn), &m);
	if (HIO_UNLIKELY(!sck)) return HIO_NULL;

	xtn = (dhcs_sck_xtn_t*)hio_dev_sck_getxtn(sck);
	xtn->dhcs = dhcs;

	HIO_MEMSET(&b, 0, HIO_SIZEOF(b));
	b.localaddr = dhcs->cfg.bind_addr;
	/* BROADCAST because a reply to a client that has no address yet has to go
	 * to 255.255.255.255, and REUSEADDR so a restart does not have to wait
	 * out the previous socket. */
	b.options = HIO_DEV_SCK_BIND_BROADCAST | HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(sck, &b) <= -1)
	{
		hio_dev_sck_kill(sck);
		return HIO_NULL;
	}

	return sck;
}

/* ------------------------------------------------------------------------- */

hio_svc_dhcs_t* hio_svc_dhcs_start (hio_t* hio, const hio_svc_dhcs_cfg_t* cfg)
{
	hio_svc_dhcs_t* dhcs = HIO_NULL;

	if (cfg->pool_first > cfg->pool_last)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "dhcp pool is empty");
		return HIO_NULL;
	}

	/* the pool bounds every allocation this service makes, so a mistyped
	 * pool is refused here rather than turning into an allocation the size of
	 * the address space later */
	if (cfg->pool_last - cfg->pool_first + 1 > HIO_SVC_DHCS_MAX_POOL_SIZE)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "dhcp pool larger than %d addresses", (int)HIO_SVC_DHCS_MAX_POOL_SIZE);
		return HIO_NULL;
	}

	if (cfg->server_id == 0)
	{
		/* option 54 is how a client tells this server's replies from another's.
		 * without it the protocol does not work, so it is required rather
		 * than defaulted. */
		hio_seterrbfmt(hio, HIO_EINVAL, "dhcp server identifier not set");
		return HIO_NULL;
	}

	dhcs = (hio_svc_dhcs_t*)hio_callocmem(hio, HIO_SIZEOF(*dhcs));
	if (HIO_UNLIKELY(!dhcs)) goto oops;

	dhcs->hio = hio;
	dhcs->svc_stop = (hio_svc_stop_t)hio_svc_dhcs_stop;
	dhcs->cfg = *cfg;

	if (cfg->domain)
	{
		dhcs->domain = hio_dupbcstr(hio, cfg->domain, HIO_NULL);
		if (HIO_UNLIKELY(!dhcs->domain)) goto oops;
		/* and the copy is what the config points at from here on, so nothing
		 * reads the caller's string after this returns */
		dhcs->cfg.domain = dhcs->domain;
	}

	dhcs->purge_tmridx = HIO_TMRIDX_INVALID;

	dhcs->sck = open_socket(dhcs);
	if (HIO_UNLIKELY(!dhcs->sck)) goto oops;

	if (schedule_purge(dhcs, HIO_NULL) <= -1) goto oops;

	HIO_SVCL_APPEND_SVC(&hio->actsvc, (hio_svc_t*)dhcs);

	HIO_DEBUG1(hio, "DHCS - STARTED SERVICE %p\n", dhcs);
	return dhcs;

oops:
	if (dhcs)
	{
		if (dhcs->purge_tmridx != HIO_TMRIDX_INVALID) hio_deltmrjob(hio, dhcs->purge_tmridx);

		/* the socket is killed before the service it points back at is freed,
		 * or its disconnect callback would run against freed memory */
		if (dhcs->sck)
		{
			dhcs_sck_xtn_t* xtn = (dhcs_sck_xtn_t*)hio_dev_sck_getxtn(dhcs->sck);
			xtn->dhcs = HIO_NULL;
			hio_dev_sck_kill(dhcs->sck);
		}
		if (dhcs->domain) hio_freemem(hio, dhcs->domain);
		hio_freemem(hio, dhcs);
	}
	return HIO_NULL;
}

void hio_svc_dhcs_stop (hio_svc_dhcs_t* dhcs)
{
	hio_t* hio = dhcs->hio;
	hio_oow_t i;

	HIO_DEBUG1(hio, "DHCS - STOPPING SERVICE %p\n", dhcs);
	dhcs->stopping = 1;

	/* before the memory it points at goes, or the sweep would run against a
	 * freed service */
	if (dhcs->purge_tmridx != HIO_TMRIDX_INVALID) hio_deltmrjob(hio, dhcs->purge_tmridx);

	if (dhcs->sck)
	{
		/* the callback must not find a service that is being torn down */
		dhcs_sck_xtn_t* xtn = (dhcs_sck_xtn_t*)hio_dev_sck_getxtn(dhcs->sck);
		xtn->dhcs = HIO_NULL;
		hio_dev_sck_kill(dhcs->sck);
		dhcs->sck = HIO_NULL;
	}

	/* each lease owns its copy of the client identifier */
	for (i = 0; i < dhcs->nleases; i++)
	{
		if (dhcs->leases[i].cid) hio_freemem(hio, dhcs->leases[i].cid);
	}
	if (dhcs->leases) hio_freemem(hio, dhcs->leases);

	if (dhcs->domain) hio_freemem(hio, dhcs->domain);

	HIO_SVCL_UNLINK_SVC(dhcs);
	hio_freemem(hio, dhcs);

	HIO_DEBUG1(hio, "DHCS - STOPPED SERVICE %p\n", dhcs);
}
