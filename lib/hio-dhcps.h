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

#ifndef _HIO_DHCPS_H_
#define _HIO_DHCPS_H_

#include <hio-dhcp.h>

/* ---------------------------------------------------------------- */

typedef struct hio_svc_dhcps_t hio_svc_dhcps_t;

/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

/* ---------------------------------------------------------------- */
/* dhcpv4 server service                                            */
/* ---------------------------------------------------------------- */

#define HIO_SVC_DHCPS_DFL_LEASE_SECS (3600)

/** how many addresses one pool may span. a bound keeps a mistyped pool from
 *  turning into an allocation the size of the address space. */
#define HIO_SVC_DHCPS_MAX_POOL_SIZE  (65536)

enum hio_svc_dhcps_lease_state_t
{
	/** offered in reply to a DISCOVER but not yet requested. held briefly so
	 *  two clients discovering at once are not offered the same address. */
	HIO_SVC_DHCPS_LEASE_OFFERED = 0,

	/** requested and acknowledged */
	HIO_SVC_DHCPS_LEASE_BOUND,

	/** a client reported the address already in use. it is kept out of the
	 *  pool rather than handed to the next client to find the same thing. */
	HIO_SVC_DHCPS_LEASE_DECLINED
};
typedef enum hio_svc_dhcps_lease_state_t hio_svc_dhcps_lease_state_t;

typedef struct hio_svc_dhcps_lease_t hio_svc_dhcps_lease_t;
struct hio_svc_dhcps_lease_t
{
	hio_uint32_t ipaddr;   /**< host order */
	hio_ntime_t  expiry;   /**< when this stops being reserved */
	hio_uint8_t* cid;      /**< the client identifier, as hio_dhcp4_get_client_id() derived it */
	hio_uint8_t  cidlen;
	int          state;    /**< #hio_svc_dhcps_lease_state_t */
};

typedef struct hio_svc_dhcps_cfg_t hio_svc_dhcps_cfg_t;
struct hio_svc_dhcps_cfg_t
{
	/** where to listen. usually 0.0.0.0:67. */
	hio_skad_t   bind_addr;

	/** this server's own address - option 54, and what a client's REQUEST is
	 *  checked against to see whether it picked us. host order. */
	hio_uint32_t server_id;

	/** the pool, inclusive at both ends, host order. */
	hio_uint32_t pool_first;
	hio_uint32_t pool_last;

	/** offered alongside an address. zero omits the option entirely rather
	 *  than offering a zero, which would be worse than saying nothing. */
	hio_uint32_t netmask;
	hio_uint32_t router;
	hio_uint32_t dns1;
	hio_uint32_t dns2;

	/** lease duration in seconds. zero means #HIO_SVC_DHCPS_DFL_LEASE_SECS. */
	hio_uint32_t lease_secs;

	/** offered as option 15 if not #HIO_NULL. copied, so the caller need not
	 *  keep it alive. */
	const hio_bch_t* domain;
};

/**
 * Start a DHCPv4 server. It binds the configured address and answers from the
 * configured pool.
 *
 * Fails and reports through hio_geterrnum() if the pool is empty, spans more
 * than #HIO_SVC_DHCPS_MAX_POOL_SIZE addresses, or the socket cannot be bound.
 */
HIO_EXPORT hio_svc_dhcps_t* hio_svc_dhcps_start (
	hio_t*                      hio,
	const hio_svc_dhcps_cfg_t*   cfg
);

HIO_EXPORT void hio_svc_dhcps_stop (
	hio_svc_dhcps_t* dhcps
);

/**
 * Run one request through the server and produce the reply, without any
 * socket being involved.
 *
 * This is the whole state machine, and it is exposed rather than buried in the
 * read callback for two reasons: a caller embedding the server may want to
 * carry the packets itself, and it means the protocol can be tested by feeding
 * it packets instead of by standing up a network.
 *
 * Returns 1 with a reply in 'rep' addressed to 'dstaddr', 0 if the request
 * warrants no answer (a RELEASE, or a REQUEST that selected another server),
 * or -1 on error.
 */
HIO_EXPORT int hio_svc_dhcps_process (
	hio_svc_dhcps_t*             dhcps,
	const hio_dhcp4_pktinf_t*   req,
	hio_dhcp4_pktbuf_t*         rep,
	hio_skad_t*                 dstaddr
);

/** How many leases the server is currently holding, expired ones included
 *  until they are reclaimed. */
HIO_EXPORT hio_oow_t hio_svc_dhcps_getleasecount (
	hio_svc_dhcps_t* dhcps
);

/** Read one lease out by index, for inspection. Returns 0 or -1 if the index
 *  is past the end. The 'cid' pointer in the copy belongs to the service. */
HIO_EXPORT int hio_svc_dhcps_getlease (
	hio_svc_dhcps_t*             dhcps,
	hio_oow_t                   index,
	hio_svc_dhcps_lease_t*       lease
);

/** Discard leases whose time has passed, returning how many went. Called
 *  automatically when the pool is exhausted; exposed so a caller can do it on
 *  its own schedule. */
HIO_EXPORT hio_oow_t hio_svc_dhcps_purgeexpiredleases (
	hio_svc_dhcps_t* dhcps
);



#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_dhcps_gethio(hio_svc_dhcps_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#define hio_svc_dhcps_gethio(svc) hio_svc_gethio(svc)
#endif


#if defined(__cplusplus)
}
#endif

#endif
