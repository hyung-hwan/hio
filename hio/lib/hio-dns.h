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

#ifndef _HIO_DNS_H_
#define _HIO_DNS_H_

#include <hio.h>
#include <hio-skad.h>

#define HIO_DNS_PORT (53)

enum hio_dns_opcode_t
{
	HIO_DNS_OPCODE_QUERY = 0, /* standard query */
	HIO_DNS_OPCODE_IQUERY = 1, /* inverse query */
	HIO_DNS_OPCODE_STATUS = 2, /* status request */
	/* 3 unassigned */
	HIO_DNS_OPCODE_NOTIFY = 4,
	HIO_DNS_OPCODE_UPDATE = 5
};
typedef enum hio_dns_opcode_t hio_dns_opcode_t;

enum hio_dns_rcode_t
{
	HIO_DNS_RCODE_NOERROR   = 0,
	HIO_DNS_RCODE_FORMERR   = 1,  /* format error */
	HIO_DNS_RCODE_SERVFAIL  = 2,  /* server failure */
	HIO_DNS_RCODE_NXDOMAIN  = 3,  /* non-existent domain */
	HIO_DNS_RCODE_NOTIMPL   = 4,  /* not implemented */
	HIO_DNS_RCODE_REFUSED   = 5,  /* query refused */
	HIO_DNS_RCODE_YXDOMAIN  = 6,  /* name exists when it should not */
	HIO_DNS_RCODE_YXRRSET   = 7,  /* RR set exists when it should not */
	HIO_DNS_RCODE_NXRRSET   = 8,  /* RR set exists when it should not */
	HIO_DNS_RCODE_NOTAUTH   = 9,  /* not authorized or server not authoritative for zone*/
	HIO_DNS_RCODE_NOTZONE   = 10, /* name not contained in zone */

	/* the standard rcode field is 4 bit long. so the max is 15. */
	/* items belows require EDNS0 */

	HIO_DNS_RCODE_BADVERS   = 16,
	HIO_DNS_RCODE_BADSIG    = 17,
	HIO_DNS_RCODE_BADTIME   = 18,
	HIO_DNS_RCODE_BADMODE   = 19,
	HIO_DNS_RCODE_BADNAME   = 20,
	HIO_DNS_RCODE_BADALG    = 21,
	HIO_DNS_RCODE_BADTRUNC  = 22,
	HIO_DNS_RCODE_BADCOOKIE = 23
};
typedef enum hio_dns_rcode_t hio_dns_rcode_t;


enum hio_dns_rrt_t
{
/*
 * [RFC1035]
 *  TYPE fields are used in resource records.  Note that these types are a
 *  subset of QTYPEs.
 */
	HIO_DNS_RRT_A = 1,
	HIO_DNS_RRT_NS = 2,
	HIO_DNS_RRT_MD = 3, /* mail destination. RFC973 replaced this with MX*/
	HIO_DNS_RRT_MF = 4, /* mail forwarder. RFC973 replaced this with MX */
	
	HIO_DNS_RRT_CNAME = 5,
	HIO_DNS_RRT_SOA = 6,

	HIO_DNS_RRT_MB = 7, /* kind of obsoleted. RFC1035, RFC2505 */
	HIO_DNS_RRT_MG = 8, /* kind of obsoleted. RFC1035, RFC2505 */
	HIO_DNS_RRT_MR = 9, /* kind of obsoleted. RFC1035, RFC2505 */

	HIO_DNS_RRT_NULL = 10,
	HIO_DNS_RRT_PTR = 12,

	HIO_DNS_RRT_MINFO = 15, /* kind of obsoleted. RFC1035, RFC2505 */

	HIO_DNS_RRT_MX = 15,
	HIO_DNS_RRT_TXT = 16,
	HIO_DNS_RRT_AAAA = 28,
	HIO_DNS_RRT_EID = 31,
	HIO_DNS_RRT_SRV = 33,
	HIO_DNS_RRT_OPT = 41,
	HIO_DNS_RRT_RRSIG = 46,

/*
 * [RFC1035] 
 *  QTYPE fields appear in the question part of a query.  QTYPES are a
 *  superset of TYPEs, hence all TYPEs are valid QTYPEs.  In addition, the
 *  following QTYPEs are defined:
 */
	HIO_DNS_RRT_Q_AFXR = 252, /* A request for a transfer of an entire zone */
	HIO_DNS_RRT_Q_MAILB = 253, /*  A request for mailbox-related records (MB, MG or MR) */
	HIO_DNS_RRT_Q_MAILA = 254, /* A request for mail agent RRs (Obsolete - see MX) */
	HIO_DNS_RRT_Q_ANY = 255 /*  A request for all records */
};
typedef enum hio_dns_rrt_t hio_dns_rrt_t;

/*
 * CLASS fields appear in resource records.  The following CLASS mnemonics
 * and values are defined:
 */
enum hio_dns_rrc_t
{
	HIO_DNS_RRC_IN = 1, /* internet */
	HIO_DNS_RRC_CH = 3, /* chaos */
	HIO_DNS_RRC_HS = 4, /* Hesiod [Dyer 87] */
	HIO_DNS_RRC_NONE = 254,

/*
 * 
 * QCLASS fields appear in the question section of a query.  QCLASS values
 * are a superset of CLASS values; every CLASS is a valid QCLASS.  In
 * addition to CLASS values, the following QCLASSes are defined:
 */

	HIO_DNS_RRC_Q_ANY = 255
};
typedef enum hio_dns_rrc_t hio_dns_rrc_t;



enum hio_dns_eopt_code_t
{
	HIO_DNS_EOPT_NSID         = 3,
	HIO_DNS_EOPT_DAU          = 5,
	HIO_DNS_EOPT_DHU          = 6,
	HIO_DNS_EOPT_N3U          = 7,
	HIO_DNS_EOPT_ECS          = 8,
	HIO_DNS_EOPT_EXPIRE       = 9,
	HIO_DNS_EOPT_COOKIE       = 10,
	HIO_DNS_EOPT_TCPKEEPALIVE = 11,
	HIO_DNS_EOPT_PADDING      = 12,
	HIO_DNS_EOPT_CHAIN        = 13,
	HIO_DNS_EOPT_KEYTAG       = 14,
};
typedef enum hio_dns_eopt_code_t hio_dns_eopt_code_t;

/* dns message preamble */
typedef struct hio_dns_msg_t hio_dns_msg_t;
struct hio_dns_msg_t
{
	hio_oow_t      msglen;
	hio_oow_t      ednsrrtroff; /* offset to trailing data after the name in the dns0 RR*/
	hio_oow_t      pktlen;
	hio_oow_t      pktalilen;
};

#include <hio-pac1.h>
struct hio_dns_pkt_t /* dns packet header */
{
	hio_uint16_t id;
#if defined(HIO_ENDIAN_BIG)
	hio_uint16_t qr: 1; /* query(0), answer(1) */
	hio_uint16_t opcode: 4; /* operation type */
	hio_uint16_t aa: 1; /* authoritative answer */
	hio_uint16_t tc: 1; /* truncated. response too large for UDP */
	hio_uint16_t rd: 1; /* recursion desired */

	hio_uint16_t ra: 1; /* recursion available */
	hio_uint16_t unused_1: 1;
	hio_uint16_t ad: 1; /* authentication data - dnssec */
	hio_uint16_t cd: 1; /* checking disabled - dnssec */
	hio_uint16_t rcode: 4; /* reply code - for reply only */
#else
	hio_uint16_t rd: 1;
	hio_uint16_t tc: 1;
	hio_uint16_t aa: 1;
	hio_uint16_t opcode: 4;
	hio_uint16_t qr: 1;

	hio_uint16_t rcode: 4;
	hio_uint16_t cd: 1;
	hio_uint16_t ad: 1;
	hio_uint16_t unused_1: 1;
	hio_uint16_t ra: 1;
#endif

	hio_uint16_t qdcount; /* number of questions */
	hio_uint16_t ancount; /* number of answers (answer part) */
	hio_uint16_t nscount; /* number of name servers (authority part. only NS types) */
	hio_uint16_t arcount; /* number of additional resource (additional part) */
};
typedef struct hio_dns_pkt_t hio_dns_pkt_t;

struct hio_dns_pkt_alt_t
{
	hio_uint16_t id;
	hio_uint16_t flags;
	hio_uint16_t rrcount[4];
};
typedef struct hio_dns_pkt_alt_t hio_dns_pkt_alt_t;
/* question
 *   name, qtype, qclass
 * answer
 *   name, qtype, qclass, ttl, rlength, rdata
 */

/* trailing part after the domain name in a resource record in a question */
struct hio_dns_qrtr_t
{
	/* qname upto 64 bytes */
	hio_uint16_t qtype;
	hio_uint16_t qclass;
};
typedef struct hio_dns_qrtr_t hio_dns_qrtr_t;

/* trailing part after the domain name in a resource record in an answer */
struct hio_dns_rrtr_t
{
	/* qname upto 64 bytes */
	hio_uint16_t rrtype;
	hio_uint16_t rrclass;
	hio_uint32_t ttl;
	hio_uint16_t dlen; /* data length */
	/* actual data if if dlen > 0 */
};
typedef struct hio_dns_rrtr_t hio_dns_rrtr_t;

struct hio_dns_eopt_t
{
	hio_uint16_t code;
	hio_uint16_t dlen;
	/* actual data if if dlen > 0 */
};
typedef struct hio_dns_eopt_t hio_dns_eopt_t;

#include <hio-upac.h>

/* ---------------------------------------------------------------- */

/*
#define HIO_DNS_HEADER_MAKE_FLAGS(qr,opcode,aa,tc,rd,ra,ad,cd,rcode) \
	((((qr) & 0x01) << 15) | (((opcode) & 0x0F) << 14) | (((aa) & 0x01) << 10) | (((tc) & 0x01) << 9) | \
	(((rd) & 0x01) << 8) | (((ra) & 0x01) << 7) | (((ad) & 0x01) << 5) | (((cd) & 0x01) << 4) | ((rcode) & 0x0F))
*/

/* breakdown of the dns message id and flags. it excludes rr count fields.*/
struct hio_dns_bhdr_t
{
	hio_int32_t id;

	hio_uint8_t qr; /* query(0), answer(1) */
	hio_uint8_t opcode; /* operation type */
	hio_uint8_t aa; /* authoritative answer */
	hio_uint8_t tc; /* truncated. response too large for UDP */
	hio_uint8_t rd; /* recursion desired */

	hio_uint8_t ra; /* recursion available */
	hio_uint8_t ad; /* authentication data - dnssec */
	hio_uint8_t cd; /* checking disabled - dnssec */
	hio_uint8_t rcode; /* reply code - for reply only */
};
typedef struct hio_dns_bhdr_t hio_dns_bhdr_t;

/* breakdown of question record */
struct hio_dns_bqr_t
{
	hio_bch_t*   qname;
	hio_uint16_t qtype;
	hio_uint16_t qclass;
};
typedef struct hio_dns_bqr_t hio_dns_bqr_t;


enum hio_dns_rr_part_t
{
	HIO_DNS_RR_PART_ANSWER,
	HIO_DNS_RR_PART_AUTHORITY,
	HIO_DNS_RR_PART_ADDITIONAL
};
typedef enum hio_dns_rr_part_t hio_dns_rr_part_t;

/* breakdown of resource record */
struct hio_dns_brr_t
{
	hio_dns_rr_part_t  part;
	hio_bch_t*         rrname;
	hio_uint16_t       rrtype;
	hio_uint16_t       rrclass;
	hio_uint32_t       ttl;
	hio_uint16_t       dlen;
	void*              dptr;
};
typedef struct hio_dns_brr_t hio_dns_brr_t;

#if 0
/* A RDATA */
struct hio_dns_brrd_a_t 
{
};
typedef struct hio_dns_brrd_a_t hio_dns_brrd_a_t;

/* 3.3.1 CNAME RDATA format */
struct hio_dns_brrd_cname_t
{
};
typedef struct hio_dns_brrd_cname_t hio_dns_brc_cname_t;

#endif

/* 3.3.9 MX RDATA format */
struct hio_dns_brrd_mx_t
{
	hio_uint16_t preference;
	hio_bch_t*   exchange;
};
typedef struct hio_dns_brrd_mx_t hio_dns_brrd_mx_t;

/* 3.3.13. SOA RDATA format */
struct hio_dns_brrd_soa_t
{
	hio_bch_t*   mname;
	hio_bch_t*   rname; 
	hio_uint32_t serial;
	hio_uint32_t refresh;
	hio_uint32_t retry;
	hio_uint32_t expire;
	hio_uint32_t minimum;
};
typedef struct hio_dns_brrd_soa_t hio_dns_brrd_soa_t;

struct hio_dns_beopt_t
{
	hio_uint16_t code;
	hio_uint16_t dlen;
	void*        dptr;
};
typedef struct hio_dns_beopt_t hio_dns_beopt_t;

/* the full rcode must be given. the macro takes the upper 8 bits */
#define HIO_DNS_EDNS_MAKE_TTL(rcode,version,dnssecok) ((((((hio_uint32_t)rcode) >> 4) & 0xFF) << 24) | (((hio_uint32_t)version & 0xFF) << 16) | (((hio_uint32_t)dnssecok & 0x1) << 15))

struct hio_dns_bedns_t
{
	hio_uint16_t     uplen; /* udp payload len - will be placed in the qclass field of RR. */

	/* the ttl field(32 bits) of RR holds extended rcode, version, dnssecok */
	hio_uint8_t      version; 
	hio_uint8_t      dnssecok;

	hio_oow_t        beonum; /* option count */
	hio_dns_beopt_t* beoptr;
};
typedef struct hio_dns_bedns_t hio_dns_bedns_t;

/* ---------------------------------------------------------------- */

typedef struct hio_svc_dns_t hio_svc_dns_t; /* server service */
typedef struct hio_svc_dnc_t hio_svc_dnc_t; /* client service */
typedef struct hio_svc_dnr_t hio_svc_dnr_t; /* recursor service */

typedef void (*hio_svc_dnc_on_done_t) (
	hio_svc_dnc_t* dnc,
	hio_dns_msg_t* reqmsg,
	hio_errnum_t   status,
	const void*    data,
	hio_oow_t      len
);


typedef void (*hio_svc_dnc_on_resolve_t) (
	hio_svc_dnc_t* dnc,
	hio_dns_msg_t* reqmsg,
	hio_errnum_t   status,
	const void*    data,
	hio_oow_t      len
);



enum hio_svc_dnc_send_flag_t
{
	HIO_SVC_DNC_SEND_FLAG_PREFER_TCP = (1 << 0),
	HIO_SVC_DNC_SEND_FLAG_TCP_IF_TC  = (1 << 1), // retry over tcp if the truncated bit is set in an answer over udp.

	HIO_SVC_DNC_SEND_FLAG_ALL = (HIO_SVC_DNC_SEND_FLAG_PREFER_TCP | HIO_SVC_DNC_SEND_FLAG_TCP_IF_TC)
};
typedef enum hio_svc_dnc_send_flag_t hio_svc_dnc_send_flag_t;

enum hio_svc_dnc_resolve_flag_t
{
	HIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP = HIO_SVC_DNC_SEND_FLAG_PREFER_TCP,
	HIO_SVC_DNC_RESOLVE_FLAG_TCP_IF_TC  = HIO_SVC_DNC_SEND_FLAG_TCP_IF_TC,

	/* the following flag bits are resolver specific. it must not overlap with send flag bits */
	HIO_SVC_DNC_RESOLVE_FLAG_BRIEF      = (1 << 8),
	HIO_SVC_DNC_RESOLVE_FLAG_COOKIE     = (1 << 9),
	HIO_SVC_DNC_RESOLVE_FLAG_DNSSEC     = (1 << 10),

	HIO_SVC_DNC_RESOLVE_FLAG_ALL = (HIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP | HIO_SVC_DNC_RESOLVE_FLAG_TCP_IF_TC | HIO_SVC_DNC_RESOLVE_FLAG_BRIEF | HIO_SVC_DNC_RESOLVE_FLAG_COOKIE | HIO_SVC_DNC_RESOLVE_FLAG_DNSSEC)
};
typedef enum hio_svc_dnc_resolve_flag_t  hio_svc_dnc_resolve_flag_t;

/* ---------------------------------------------------------------- */

#define HIO_DNS_COOKIE_CLIENT_LEN (8)
#define HIO_DNS_COOKIE_SERVER_MIN_LEN (16)
#define HIO_DNS_COOKIE_SERVER_MAX_LEN (40)
#define HIO_DNS_COOKIE_MAX_LEN (HIO_DNS_COOKIE_CLIENT_LEN + HIO_DNS_COOKIE_SERVER_MAX_LEN)

typedef struct hio_dns_cookie_data_t hio_dns_cookie_data_t;
#include <hio-pac1.h>
struct hio_dns_cookie_data_t
{
	hio_uint8_t client[HIO_DNS_COOKIE_CLIENT_LEN];
	hio_uint8_t server[HIO_DNS_COOKIE_SERVER_MAX_LEN];
};
#include <hio-upac.h>

typedef struct hio_dns_cookie_t hio_dns_cookie_t;
struct hio_dns_cookie_t
{
	hio_dns_cookie_data_t data;
	hio_uint8_t client_len;
	hio_uint8_t server_len;
	hio_uint8_t key[16];
};

/* ---------------------------------------------------------------- */

struct hio_dns_pkt_info_t
{
	/* the following 5 fields are internal use only */
	hio_uint8_t* _start;
	hio_uint8_t* _end;
	hio_uint8_t* _ptr;
	hio_oow_t _rrdlen; /* length needed to store RRs decoded */
	hio_uint8_t* _rrdptr;

	/* you may access the following fields */
	hio_dns_bhdr_t hdr;

	struct
	{
		int exist;
		hio_uint16_t uplen; /* udp payload len - will be placed in the qclass field of RR. */
		hio_uint8_t  version; 
		hio_uint8_t  dnssecok;
		hio_dns_cookie_t cookie;
	} edns;

	hio_uint16_t qdcount; /* number of questions */
	hio_uint16_t ancount; /* number of answers (answer part) */
	hio_uint16_t nscount; /* number of name servers (authority part. only NS types) */
	hio_uint16_t arcount; /* number of additional resource (additional part) */

	struct
	{
		hio_dns_bqr_t* qd;
		hio_dns_brr_t* an;
		hio_dns_brr_t* ns;
		hio_dns_brr_t* ar;
	} rr;

};
typedef struct hio_dns_pkt_info_t hio_dns_pkt_info_t;


/* ---------------------------------------------------------------- */

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_svc_dnc_t* hio_svc_dnc_start (
	hio_t*             hio,
	const hio_skad_t*  serv_addr, /* required */
	const hio_skad_t*  bind_addr, /* optional. can be HIO_NULL */
	const hio_ntime_t* send_tmout, /* required */
	const hio_ntime_t* reply_tmout, /* required */
	hio_oow_t          max_tries /* required */
);

HIO_EXPORT void hio_svc_dnc_stop (
	hio_svc_dnc_t* dnc
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_svc_dns_gethio(hio_svc_dns_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
static HIO_INLINE hio_t* hio_svc_dnc_gethio(hio_svc_dnc_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
static HIO_INLINE hio_t* hio_svc_dnr_gethio(hio_svc_dnr_t* svc) { return hio_svc_gethio((hio_svc_t*)svc); }
#else
#	define hio_svc_dns_gethio(svc) hio_svc_gethio(svc)
#	define hio_svc_dnc_gethio(svc) hio_svc_gethio(svc)
#	define hio_svc_dnr_gethio(svc) hio_svc_gethio(svc)
#endif

HIO_EXPORT hio_dns_msg_t* hio_svc_dnc_sendmsg (
	hio_svc_dnc_t*         dnc,
	hio_dns_bhdr_t*        bdns,
	hio_dns_bqr_t*         qr,
	hio_oow_t              qr_count,
	hio_dns_brr_t*         rr,
	hio_oow_t              rr_count,
	hio_dns_bedns_t*       edns,
	int                    send_flags,
	hio_svc_dnc_on_done_t  on_done,
	hio_oow_t              xtnsize
);

HIO_EXPORT hio_dns_msg_t* hio_svc_dnc_sendreq (
	hio_svc_dnc_t*         dnc,
	hio_dns_bhdr_t*        bdns,
	hio_dns_bqr_t*         qr,
	hio_dns_bedns_t*       edns,
	int                    send_flags,
	hio_svc_dnc_on_done_t  on_done,
	hio_oow_t              xtnsize
);


HIO_EXPORT hio_dns_msg_t* hio_svc_dnc_resolve (
	hio_svc_dnc_t*           dnc,
	const hio_bch_t*         qname,
	hio_dns_rrt_t            qtype,
	int                      resolve_flags,
	hio_svc_dnc_on_resolve_t on_resolve,
	hio_oow_t                xtnsize
);

/*
 * -1: cookie in the request but no client cookie in the response. this may be ok or not ok depending on your policy 
 * 0: client cookie mismatch in the request in the response
 * 1: client cookie match in the request in the response
 * 2: no client cookie in the requset. so it deson't case about the response 
 */
HIO_EXPORT int hio_svc_dnc_checkclientcookie (
	hio_svc_dnc_t*      dnc,
	hio_dns_msg_t*      reqmsg,
	hio_dns_pkt_info_t* respi
);

/* ---------------------------------------------------------------- */

HIO_EXPORT hio_dns_pkt_info_t* hio_dns_make_pkt_info (
	hio_t*                hio,
	const hio_dns_pkt_t*  pkt,
	hio_oow_t             len
);

HIO_EXPORT void hio_dns_free_pkt_info (
	hio_t*                hio,
	hio_dns_pkt_info_t*   pi
);

/* ---------------------------------------------------------------- */

#if defined(HIO_HAVE_INLINE)
	static HIO_INLINE hio_dns_pkt_t* hio_dns_msg_to_pkt (hio_dns_msg_t* msg) { return (hio_dns_pkt_t*)(msg + 1); }
#else
#	define hio_dns_msg_to_pkt(msg) ((hio_dns_pkt_t*)((hio_dns_msg_t*)(msg) + 1))
#endif

HIO_EXPORT hio_dns_msg_t* hio_dns_make_msg (
	hio_t*                hio,
	hio_dns_bhdr_t*       bhdr,
	hio_dns_bqr_t*        qr,
	hio_oow_t             qr_count,
	hio_dns_brr_t*        rr,
	hio_oow_t             rr_count,
	hio_dns_bedns_t*      edns,
	hio_oow_t             xtnsize
);

HIO_EXPORT void hio_dns_free_msg (
	hio_t*                hio,
	hio_dns_msg_t*        msg
);

/* 
 * return the pointer to the client cookie data in the packet.
 * if cookie is not HIO_NULL, it copies the client cookie there.
 */
HIO_EXPORT hio_uint8_t* hio_dns_find_client_cookie_in_msg (
	hio_dns_msg_t* reqmsg,
	hio_uint8_t  (*cookie)[HIO_DNS_COOKIE_CLIENT_LEN]
);

HIO_EXPORT hio_bch_t* hio_dns_rcode_to_bcstr (
	hio_dns_rcode_t rcode
);

#if defined(__cplusplus)
}
#endif

#endif
