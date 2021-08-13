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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _HIO_DHCP_H_
#define _HIO_DHCP_H_

#include <hio.h>
#include <hio-skad.h>

#define HIO_DHCP6_SERVER_PORT     (547)
#define HIO_DHCP6_CLIENT_PORT     (546)
#define HIO_DHCP6_HOP_COUNT_LIMIT (32)

enum hio_dhcp6_msg_t
{
	HIO_DHCP6_MSG_SOLICIT     = 1,
	HIO_DHCP6_MSG_ADVERTISE   = 2,
	HIO_DHCP6_MSG_REQUEST     = 3,
	HIO_DHCP6_MSG_CONFIRM     = 4,
	HIO_DHCP6_MSG_RENEW       = 5,
	HIO_DHCP6_MSG_REBIND      = 6,
	HIO_DHCP6_MSG_REPLY       = 7,
	HIO_DHCP6_MSG_RELEASE     = 8,
	HIO_DHCP6_MSG_DECLINE     = 9,
	HIO_DHCP6_MSG_RECONFIGURE = 10,
	HIO_DHCP6_MSG_INFOREQ     = 11,
	HIO_DHCP6_MSG_RELAYFORW   = 12,
	HIO_DHCP6_MSG_RELAYREPL   = 13,
};
typedef enum hio_dhcp6_msg_t hio_dhcp6_msg_t;

enum hio_dhcp6_opt_t
{
	HIO_DHCP6_OPT_RELAY_MESSAGE = 9,
	HIO_DHCP6_OPT_INTERFACE_ID = 18
};
typedef enum hio_dhcp6_opt_t hio_dhcp6_opt_t;



/* ---------------------------------------------------------------- */
#include <hio-pac1.h>

struct hio_dhcp6_pkt_hdr_t
{
	hio_uint8_t msgtype;
	hio_uint8_t transid[3];
};
typedef struct hio_dhcp6_pkt_hdr_t hio_dhcp6_pkt_hdr_t;

struct hio_dhcp6_relay_hdr_t
{
	hio_uint8_t msgtype;  /* RELAY-FORW, RELAY-REPL */
	hio_uint8_t hopcount;
	hio_uint8_t linkaddr[16];
	hio_uint8_t peeraddr[16];
};
typedef struct hio_dhcp6_relay_hdr_t hio_dhcp6_relay_hdr_t;

struct hio_dhcp6_opt_hdr_t
{
	hio_uint16_t code;
	hio_uint16_t len; /* length of option data, excludes the option header */
};
typedef struct hio_dhcp6_opt_hdr_t hio_dhcp6_opt_hdr_t;

#include <hio-upac.h>


struct hio_dhcp6_pktinf_t
{
	hio_dhcp6_pkt_hdr_t* hdr;
	hio_oow_t            len;
};
typedef struct hio_dhcp6_pktinf_t hio_dhcp6_pktinf_t;

/* ---------------------------------------------------------------- */


typedef struct hio_svc_dhcs_t hio_svc_dhcs_t; 

/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_svc_dhcs_t* hio_svc_dhcs_start (
	hio_t*             hio,
	const hio_skad_t*  local_binds,
	hio_oow_t          local_nbinds


#if 0
	,
	const hio_skad_t*  serv_addr, /* required */
	const hio_skad_t*  bind_addr, /* optional. can be HIO_NULL */
	const hio_ntime_t* send_tmout, /* required */
	const hio_ntime_t* reply_tmout, /* required */
	hio_oow_t          max_tries /* required */
#endif
);

HIO_EXPORT void hio_svc_dhcs_stop (
	hio_svc_dhcs_t* dhcs
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_dhcs_gethio(hio_svc_dhcs_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_dhcs_gethio(svc) hio_svc_gethio(svc)

#endif


#if defined(__cplusplus)
}
#endif

#endif
