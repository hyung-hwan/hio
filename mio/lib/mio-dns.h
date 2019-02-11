/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

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

#ifndef _MIO_DNS_H_
#define _MIO_DNS_H_

#include <mio.h>

#define MIO_DNS_PORT (53)

enum mio_dns_opcode_t
{
	MIO_DNS_OPCODE_QUERY = 0, /* standard query */
	MIO_DNS_OPCODE_IQUERY = 1, /* inverse query */
	MIO_DNS_OPCODE_STATUS = 2, /* status request */
	/* 3 unassigned */
	MIO_DNS_OPCODE_NOTIFY = 4,
	MIO_DNS_OPCODE_UPDATE = 5
};
typedef enum mio_dns_opcode_t mio_dns_opcode_t;

enum mio_dns_rcode_t
{
	MIO_DNS_RCODE_NOERROR   = 0,
	MIO_DNS_RCODE_FORMERR   = 1,  /* format error */
	MIO_DNS_RCODE_SERVFAIL  = 2,  /* server failure */
	MIO_DNS_RCODE_NXDOMAIN  = 3,  /* non-existent domain */
	MIO_DNS_RCODE_NOTIMPL   = 4,  /* not implemented */
	MIO_DNS_RCODE_REFUSED   = 5,  /* query refused */
	MIO_DNS_RCODE_YXDOMAIN  = 6,  /* name exists when it should not */
	MIO_DNS_RCODE_YXRRSET   = 7,  /* RR set exists when it should not */
	MIO_DNS_RCODE_NXRRSET   = 8,  /* RR set exists when it should not */
	MIO_DNS_RCODE_NOTAUTH   = 9,  /* not authorized or server not authoritative for zone*/
	MIO_DNS_RCODE_NOTZONE   = 10, /* name not contained in zone */
	MIO_DNS_RCODE_BADVERS   = 16,
	MIO_DNS_RCODE_BADSIG    = 17,
	MIO_DNS_RCODE_BADTIME   = 18,
	MIO_DNS_RCODE_BADMODE   = 19,
	MIO_DNS_RCODE_BADNAME   = 20,
	MIO_DNS_RCODE_BADALG    = 21,
	MIO_DNS_RCODE_BADTRUNC  = 22,
	MIO_DNS_RCODE_BADCOOKIE = 23
};
typedef enum mio_dns_rcode_t mio_dns_rcode_t;

enum mio_dns_qtype_t
{
	MIO_DNS_QTYPE_A = 1,
	MIO_DNS_QTYPE_NS = 2,
	MIO_DNS_QTYPE_CNAME = 5,
	MIO_DNS_QTYPE_SOA = 6,
	MIO_DNS_QTYPE_PTR = 12,
	MIO_DNS_QTYPE_MX = 15,
	MIO_DNS_QTYPE_TXT = 16,
	MIO_DNS_QTYPE_AAAA = 28,
	MIO_DNS_QTYPE_OPT = 41,
	MIO_DNS_QTYPE_ANY = 255
};
typedef enum mio_dns_qtype_t mio_dns_qtype_t;

enum mio_dns_qclass_t
{
	MIO_DNS_QCLASS_IN = 1, /* internet */
	MIO_DNS_QCLASS_CH = 3, /* chaos */
	MIO_DNS_QCLASS_HS = 4, /* hesiod */
	MIO_DNS_QCLASS_NONE = 254,
	MIO_DNS_QCLASS_ANY = 255
};
typedef enum mio_dns_qclass_t mio_dns_qclass_t;

#include <mio-pac1.h>
struct mio_dns_msg_t
{
	mio_uint16_t id;

#if defined(MIO_ENDIAN_BIG)
	mio_uint16_t qr: 1; /* query(0), answer(1) */
	mio_uint16_t opcode: 4; /* operation type */
	mio_uint16_t aa: 1; /* authoritative answer */
	mio_uint16_t tc: 1; /* truncated. response too large for UDP */
	mio_uint16_t rd: 1; /* recursion desired */

	mio_uint16_t ra: 1; /* recursion available */
	mio_uint16_t unused_1: 1;
	mio_uint16_t ad: 1; /* authentication data - dnssec */
	mio_uint16_t cd: 1; /* checking disabled - dnssec */
	mio_uint16_t rcode: 4;
#else
	mio_uint16_t rd: 1;
	mio_uint16_t tc: 1;
	mio_uint16_t aa: 1;
	mio_uint16_t opcode: 4;
	mio_uint16_t qr: 1;

	mio_uint16_t rcode: 4;
	mio_uint16_t cd: 1;
	mio_uint16_t ad: 1;
	mio_uint16_t unused_1: 1;
	mio_uint16_t ra: 1;
#endif

	mio_uint16_t qdcount; /* number of questions */
	mio_uint16_t ancount; /* number of answers */
	mio_uint16_t nscount; /* number of name servers */
	mio_uint16_t arcount; /* number of additional resource */
};
typedef struct mio_dns_msg_t mio_dns_msg_t;

struct mio_dns_qdrechdr_t
{
	mio_uint16_t qtype;
	mio_uint16_t qclass;
};
typedef struct moo_dns_qdrechdr_t moo_dns_qdrechdr_t;

struct mio_dns_anrechdr_t
{
	mio_uint16_t qtype;
	mio_uint16_t qclass;
	mio_uint32_t ttl;
	mio_uint16_t dlen; /* data length */
};
typedef struct moo_dns_ansrechdr_t moo_dns_ansrechdr_t;

#include <mio-upac.h>

#endif
