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

#ifndef _HIO_DHCP_H_
#define _HIO_DHCP_H_

#include <hio.h>
#include <hio-sck.h>

/* ---------------------------------------------------------------- */

#define HIO_DHCP4_SERVER_PORT   (67)
#define HIO_DHCP4_CLIENT_PORT   (68)
#define HIO_DHCP4_MAGIC_COOKIE  (0x63825363)

/* operation code */
enum hio_dhcp4_op_t
{
	HIO_DHCP4_OP_BOOTREQUEST = 1,
	HIO_DHCP4_OP_BOOTREPLY   = 2
};

enum hio_dhcp4_htype_t
{
	HIO_DHCP4_HTYPE_ETHERNET   = 1,
	HIO_DHCP4_HTYPE_IEEE802    = 6,
	HIO_DHCP4_HTYPE_ARCNET     = 7,
	HIO_DHCP4_HTYPE_APPLETALK  = 8,
	HIO_DHCP4_HTYPE_HDLC       = 17,
	HIO_DHCP4_HTYPE_ATM        = 19,
	HIO_DHCP4_HTYPE_INFINIBAND = 32
};

/* option codes (partial) */
enum hio_dhcp4_opt_t
{
	HIO_DHCP4_OPT_PADDING          = 0x00,
	HIO_DHCP4_OPT_SUBNET           = 0x01,
	HIO_DHCP4_OPT_TIME_OFFSET      = 0x02,
	HIO_DHCP4_OPT_ROUTER           = 0x03,
	HIO_DHCP4_OPT_TIME_SERVER      = 0x04,
	HIO_DHCP4_OPT_NAME_SERVER      = 0x05,
	HIO_DHCP4_OPT_DNS_SERVER       = 0x06,
	HIO_DHCP4_OPT_LOG_SERVER       = 0x07,
	HIO_DHCP4_OPT_COOKIE_SERVER    = 0x08,
	HIO_DHCP4_OPT_LPR_SERVER       = 0x09,
	HIO_DHCP4_OPT_HOST_NAME        = 0x0c,
	HIO_DHCP4_OPT_BOOT_SIZE        = 0x0d,
	HIO_DHCP4_OPT_DOMAIN_NAME      = 0x0f,
	HIO_DHCP4_OPT_SWAP_SERVER      = 0x10,
	HIO_DHCP4_OPT_ROOT_PATH        = 0x11,
	HIO_DHCP4_OPT_IP_TTL           = 0x17,
	HIO_DHCP4_OPT_MTU              = 0x1a,
	HIO_DHCP4_OPT_BROADCAST        = 0x1c,
	HIO_DHCP4_OPT_NTP_SERVER       = 0x2a,
	HIO_DHCP4_OPT_WINS_SERVER      = 0x2c,
	HIO_DHCP4_OPT_REQUESTED_IPADDR = 0x32,
	HIO_DHCP4_OPT_LEASE_TIME       = 0x33,
	HIO_DHCP4_OPT_OVERLOAD         = 0x34, /* overload sname or file */
	HIO_DHCP4_OPT_MESSAGE_TYPE     = 0x35,
	HIO_DHCP4_OPT_SERVER_ID        = 0x36,
	HIO_DHCP4_OPT_PARAM_REQ        = 0x37,
	HIO_DHCP4_OPT_MESSAGE          = 0x38,
	HIO_DHCP4_OPT_MAX_SIZE         = 0x39,
	HIO_DHCP4_OPT_T1               = 0x3a,
	HIO_DHCP4_OPT_T2               = 0x3b,
	HIO_DHCP4_OPT_VENDOR           = 0x3c,
	HIO_DHCP4_OPT_CLIENT_ID        = 0x3d,
	HIO_DHCP4_OPT_RELAY            = 0x52,
	HIO_DHCP4_OPT_SUBNET_SELECTION = 0x76,
	HIO_DHCP4_OPT_END              = 0xFF
};

/* flags for HIO_DHCP4_OPT_OVERLOAD */
enum hio_dhcp4_opt_overload_t
{
	HIO_DHCP4_OPT_OVERLOAD_FILE  = (1 << 0),
	HIO_DHCP4_OPT_OVERLOAD_SNAME = (1 << 1)
};

/* flags for HIO_DHCP4_OPT_OVERLOAD */
enum hio_dhcp4_opt_relay_t
{
	HIO_DHCP4_OPT_RELAY_CIRCUIT_ID  = 1,
	HIO_DHCP4_OPT_RELAY_REMOTE_ID   = 2
};

/* message type */
enum hio_dhcp4_msg_t
{
	HIO_DHCP4_MSG_DISCOVER         = 1,
	HIO_DHCP4_MSG_OFFER            = 2,
	HIO_DHCP4_MSG_REQUEST          = 3,
	HIO_DHCP4_MSG_DECLINE          = 4,
	HIO_DHCP4_MSG_ACK              = 5,
	HIO_DHCP4_MSG_NAK              = 6,
	HIO_DHCP4_MSG_RELEASE          = 7,
	HIO_DHCP4_MSG_INFORM           = 8,

	/*HIO_DHCP4_MSG_RENEW            = 9,*/

	HIO_DHCP4_MSG_LEASE_QUERY      = 10,
	HIO_DHCP4_MSG_LEASE_UNASSIGNED = 11,
	HIO_DHCP4_MSG_LEASE_UNKNOWN    = 12,
	HIO_DHCP4_MSG_LEASE_ACTIVE     = 13,

	HIO_DHCP4_MSG_BULK_LEASE_QUERY = 14,
	HIO_DHCP4_MSG_LEASE_QUERY_DONE = 15
};

/* --------------------------------------------------- */
#include <hio-pac1.h>
/* --------------------------------------------------- */

struct hio_dhcp4_pkt_hdr_t
{
	hio_uint8_t  op;
	hio_uint8_t  htype;
	hio_uint8_t  hlen;
	hio_uint8_t  hops;
	hio_uint32_t xid;        /* transaction id */
	hio_uint16_t secs;       /* seconds elapsed */
	hio_uint16_t flags;      /* bootp flags */
	hio_uint32_t ciaddr;     /* client ip */
	hio_uint32_t yiaddr;     /* your ip */
	hio_uint32_t siaddr;     /* next server ip */
	hio_uint32_t giaddr;     /* relay agent ip */
	hio_uint8_t  chaddr[16]; /* client mac */

	char     sname[64];      /* server host name */
	char     file[128];      /* boot file name */

	/* options are placed after the header.
	 * the first four bytes of the options compose a magic cookie
	 * 0x63 0x82 0x53 0x63 */
};
typedef struct hio_dhcp4_pkt_hdr_t hio_dhcp4_pkt_hdr_t;

struct hio_dhcp4_opt_hdr_t
{
	hio_uint8_t code;
	hio_uint8_t len;
};
typedef struct hio_dhcp4_opt_hdr_t hio_dhcp4_opt_hdr_t;

/* --------------------------------------------------- */
#include <hio-upac.h>
/* --------------------------------------------------- */


typedef int (*hio_dhcp4_opt_walker_t) (hio_dhcp4_opt_hdr_t* opt);

struct hio_dhcp4_pktinf_t
{
	hio_dhcp4_pkt_hdr_t* hdr;
	hio_oow_t            len;
};
typedef struct hio_dhcp4_pktinf_t hio_dhcp4_pktinf_t;

struct hio_dhcp4_pktbuf_t
{
	hio_dhcp4_pkt_hdr_t* hdr;
	hio_oow_t            len;
	hio_oow_t            capa;
};
typedef struct hio_dhcp4_pktbuf_t hio_dhcp4_pktbuf_t;


/* ---------------------------------------------------------------- */

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
	HIO_DHCP6_OPT_CLIENTID = 1,
	HIO_DHCP6_OPT_SERVERID = 2,
	HIO_DHCP6_OPT_IA_NA = 3,
	HIO_DHCP6_OPT_IA_TA = 4,
	HIO_DHCP6_OPT_IAADDR = 5,
	HIO_DHCP6_OPT_PREFERENCE = 7,
	HIO_DHCP6_OPT_ELAPSED_TIME = 8,
	HIO_DHCP6_OPT_RELAY_MESSAGE = 9,
	HIO_DHCP6_OPT_RAPID_COMMIT = 14,
	HIO_DHCP6_OPT_USER_CLASS = 15,
	HIO_DHCP6_OPT_VENDOR_CLASS = 16,
	HIO_DHCP6_OPT_INTERFACE_ID = 18,
	HIO_DHCP6_OPT_IA_PD = 25,
	HIO_DHCP6_OPT_IAPREFIX = 26
};
typedef enum hio_dhcp6_opt_t hio_dhcp6_opt_t;

/* --------------------------------------------------- */
#include <hio-pac1.h>
/* --------------------------------------------------- */

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

/* --------------------------------------------------- */
#include <hio-upac.h>
/* --------------------------------------------------- */

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

HIO_EXPORT int hio_dhcp4_init_pktbuf (
	hio_dhcp4_pktbuf_t* pkt,
	void*               buf,
	hio_oow_t           capa
);

HIO_EXPORT int hio_dhcp4_add_option (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code,
	void*               optr, /**< option data pointer */
	hio_uint8_t         olen  /**< option data length */
);

HIO_EXPORT int hio_dhcp4_delete_option (
	hio_dhcp4_pktbuf_t* pkt,
	int                 code
);

HIO_EXPORT void hio_dhcp4_compact_options (
	hio_dhcp4_pktbuf_t* pkt
);

#if 0
HIO_EXPORT int hio_dhcp4_add_options (
	hio_dhcp4_pkt_hdr_t* pkt,
	hio_oow_t        len,
	hio_oow_t        max,
	int              code,
	hio_uint8_t*     optr, /* option data */
	hio_uint8_t      olen  /* option length */
);
#endif

HIO_EXPORT int hio_dhcp4_walk_options (
	const hio_dhcp4_pktinf_t* pkt,
	hio_dhcp4_opt_walker_t    walker
);

HIO_EXPORT hio_dhcp4_opt_hdr_t* hio_dhcp4_find_option (
	const hio_dhcp4_pktinf_t* pkt,
	int                       code
);

HIO_EXPORT hio_uint8_t* hio_dhcp4_get_relay_suboption (
	const hio_uint8_t* ptr,
	hio_uint8_t        len,
	int                code,
	hio_uint8_t*       olen
);

/* ---------------------------------------------------------------- */

HIO_EXPORT hio_dhcp6_opt_hdr_t* hio_dhcp6_find_option (
	const hio_dhcp6_pktinf_t* pkt,
	int                       code
);

/* ---------------------------------------------------------------- */


HIO_EXPORT hio_svc_dhcs_t* hio_svc_dhcs_start (
	hio_t*             hio,
	const hio_skad_t*  local_binds,
	hio_oow_t          local_nbinds
);

HIO_EXPORT void hio_svc_dhcs_stop (
	hio_svc_dhcs_t*    dhcs
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_dhcs_gethio(hio_svc_dhcs_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#define hio_svc_dhcs_gethio(svc) hio_svc_gethio(svc)
#endif


#if defined(__cplusplus)
}
#endif

#endif
