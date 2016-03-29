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

#ifndef _STIO_ARP_H_
#define _STIO_ARP_H_

#include <stio.h>
#include <stio-sck.h>

typedef struct stio_dev_arp_t stio_dev_arp_t;
#define STIO_ETHHDR_PROTO_IP4 0x0800
#define STIO_ETHHDR_PROTO_ARP 0x0806

#define STIO_ARPHDR_OPCODE_REQUEST 1
#define STIO_ARPHDR_OPCODE_REPLY   2

#define STIO_ARPHDR_HTYPE_ETH 0x0001
#define STIO_ARPHDR_PTYPE_IP4 0x0800

#define STIO_ETHADR_LEN 6
#define STIO_IP4ADR_LEN 4

#pragma pack(push)
#pragma pack(1)
struct stio_ethhdr_t
{
	stio_uint8_t  dest[STIO_ETHADR_LEN];
	stio_uint8_t  source[STIO_ETHADR_LEN];
	stio_uint16_t proto;
};
typedef struct stio_ethhdr_t stio_ethhdr_t;

struct stio_arphdr_t
{
	stio_uint16_t htype;   /* hardware type (ethernet: 0x0001) */
	stio_uint16_t ptype;   /* protocol type (ipv4: 0x0800) */
	stio_uint8_t  hlen;    /* hardware address length (ethernet: 6) */
	stio_uint8_t  plen;    /* protocol address length (ipv4 :4) */
	stio_uint16_t opcode;  /* operation code */
};
typedef struct stio_arphdr_t stio_arphdr_t;

/* arp payload for ipv4 over ethernet */
struct stio_etharp_t
{
	stio_uint8_t sha[STIO_ETHADR_LEN];   /* source hardware address */
	stio_uint8_t spa[STIO_IP4ADR_LEN];   /* source protocol address */
	stio_uint8_t tha[STIO_ETHADR_LEN];   /* target hardware address */
	stio_uint8_t tpa[STIO_IP4ADR_LEN];   /* target protocol address */
};
typedef struct stio_etharp_t stio_etharp_t;

struct stio_etharp_pkt_t
{
	stio_ethhdr_t ethhdr;
	stio_arphdr_t arphdr;
	stio_etharp_t arppld;
};
typedef struct stio_etharp_pkt_t stio_etharp_pkt_t;
#pragma pack(pop)


#if 0
typedef int (*stio_dev_arp_on_read_t) (stio_dev_arp_t* dev, stio_pkt_arp_t* pkt, stio_len_t len);
typedef int (*stio_dev_arp_on_write_t) (stio_dev_arp_t* dev, void* wrctx);

typedef struct stio_dev_arp_make_t stio_dev_arp_make_t;
struct stio_dev_arp_make_t
{
	stio_dev_arp_on_write_t on_write;
	stio_dev_arp_on_read_t on_read;
};

struct stio_dev_arp_t
{
	STIO_DEV_HEADERS;
	stio_sckhnd_t sck;

	stio_dev_arp_on_write_t on_write;
	stio_dev_arp_on_read_t on_read;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if 0
STIO_EXPORT stio_dev_arp_t* stio_dev_arp_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_arp_make_t* data
);

STIO_EXPORT int stio_dev_arp_write (
	stio_dev_arp_t*       dev,
	const stio_pkt_arp_t* arp,
	void*                 wrctx
);

STIO_EXPORT int stio_dev_arp_timedwrite (
	stio_dev_arp_t*       dev,
	const stio_pkt_arp_t* arp,
	const stio_ntime_t*   tmout,
	void*                 wrctx
);
#endif

#ifdef __cplusplus
extern "C" {
#endif

#endif
