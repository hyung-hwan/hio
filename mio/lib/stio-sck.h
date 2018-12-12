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

#ifndef _STIO_SCK_H_
#define _STIO_SCK_H_

#include <stio.h>

/* ========================================================================= */
/* TOOD: move these to a separte file */

#define STIO_ETHHDR_PROTO_IP4   0x0800
#define STIO_ETHHDR_PROTO_ARP   0x0806
#define STIO_ETHHDR_PROTO_8021Q 0x8100 /* 802.1Q VLAN */
#define STIO_ETHHDR_PROTO_IP6   0x86DD


#define STIO_ARPHDR_OPCODE_REQUEST 1
#define STIO_ARPHDR_OPCODE_REPLY   2

#define STIO_ARPHDR_HTYPE_ETH 0x0001
#define STIO_ARPHDR_PTYPE_IP4 0x0800

#define STIO_ETHADDR_LEN 6
#define STIO_IP4ADDR_LEN 4
#define STIO_IP6ADDR_LEN 16 


#if defined(__GNUC__)
#	define STIO_PACKED __attribute__((__packed__))

#else
#	define STIO_PACKED 
#	STIO_PACK_PUSH pack(push)
#	STIO_PACK_PUSH pack(push)
#	STIO_PACK(x) pack(x)
#endif


#if defined(__GNUC__)
	/* nothing */
#else
	#pragma pack(push)
	#pragma pack(1)
#endif
struct STIO_PACKED stio_ethaddr_t
{
	stio_uint8_t v[STIO_ETHADDR_LEN]; 
};
typedef struct stio_ethaddr_t stio_ethaddr_t;

struct STIO_PACKED stio_ip4addr_t
{
	stio_uint8_t v[STIO_IP4ADDR_LEN];
};
typedef struct stio_ip4addr_t stio_ip4addr_t;

struct STIO_PACKED stio_ip6addr_t
{
	stio_uint8_t v[STIO_IP6ADDR_LEN]; 
};
typedef struct stio_ip6addr_t stio_ip6addr_t;


struct STIO_PACKED stio_ethhdr_t
{
	stio_uint8_t  dest[STIO_ETHADDR_LEN];
	stio_uint8_t  source[STIO_ETHADDR_LEN];
	stio_uint16_t proto;
};
typedef struct stio_ethhdr_t stio_ethhdr_t;

struct STIO_PACKED stio_arphdr_t
{
	stio_uint16_t htype;   /* hardware type (ethernet: 0x0001) */
	stio_uint16_t ptype;   /* protocol type (ipv4: 0x0800) */
	stio_uint8_t  hlen;    /* hardware address length (ethernet: 6) */
	stio_uint8_t  plen;    /* protocol address length (ipv4 :4) */
	stio_uint16_t opcode;  /* operation code */
};
typedef struct stio_arphdr_t stio_arphdr_t;

/* arp payload for ipv4 over ethernet */
struct STIO_PACKED stio_etharp_t
{
	stio_uint8_t sha[STIO_ETHADDR_LEN];   /* source hardware address */
	stio_uint8_t spa[STIO_IP4ADDR_LEN];   /* source protocol address */
	stio_uint8_t tha[STIO_ETHADDR_LEN];   /* target hardware address */
	stio_uint8_t tpa[STIO_IP4ADDR_LEN];   /* target protocol address */
};
typedef struct stio_etharp_t stio_etharp_t;

struct STIO_PACKED stio_etharp_pkt_t
{
	stio_ethhdr_t ethhdr;
	stio_arphdr_t arphdr;
	stio_etharp_t arppld;
};
typedef struct stio_etharp_pkt_t stio_etharp_pkt_t;


struct stio_iphdr_t
{
#if defined(STIO_ENDIAN_LITTLE)
	stio_uint8_t ihl:4;
	stio_uint8_t version:4;
#elif defined(STIO_ENDIAN_BIG)
	stio_uint8_t version:4;
	stio_uint8_t ihl:4;
#else
#	UNSUPPORTED ENDIAN
#endif
	stio_int8_t tos;
	stio_int16_t tot_len;
	stio_int16_t id;
	stio_int16_t frag_off;
	stio_int8_t ttl;
	stio_int8_t protocol;
	stio_int16_t check;
	stio_int32_t saddr;
	stio_int32_t daddr;
	/*The options start here. */
};
typedef struct stio_iphdr_t stio_iphdr_t;


struct STIO_PACKED stio_icmphdr_t 
{
	stio_uint8_t type; /* message type */
	stio_uint8_t code; /* subcode */
	stio_uint16_t checksum;
	union
	{
		struct
		{
			stio_uint16_t id;
			stio_uint16_t seq;
		} echo;

		stio_uint32_t gateway;

		struct
		{
			stio_uint16_t frag_unused;
			stio_uint16_t mtu;
		} frag; /* path mut discovery */
	} u;
};
typedef struct stio_icmphdr_t stio_icmphdr_t;

#if defined(__GNUC__)
	/* nothing */
#else
	#pragma pack(pop)
#endif

/* ICMP types */
#define STIO_ICMP_ECHO_REPLY        0
#define STIO_ICMP_UNREACH           3 /* destination unreachable */
#define STIO_ICMP_SOURCE_QUENCE     4
#define STIO_ICMP_REDIRECT          5
#define STIO_ICMP_ECHO_REQUEST      8
#define STIO_ICMP_TIME_EXCEEDED     11
#define STIO_ICMP_PARAM_PROBLEM     12
#define STIO_ICMP_TIMESTAMP_REQUEST 13
#define STIO_ICMP_TIMESTAMP_REPLY   14
#define STIO_ICMP_INFO_REQUEST      15
#define STIO_ICMP_INFO_REPLY        16
#define STIO_ICMP_ADDR_MASK_REQUEST 17
#define STIO_ICMP_ADDR_MASK_REPLY   18

/* Subcode for STIO_ICMP_UNREACH */
#define STIO_ICMP_UNREACH_NET          0
#define STIO_ICMP_UNREACH_HOST         1
#define STIO_ICMP_UNREACH_PROTOCOL     2
#define STIO_ICMP_UNREACH_PORT         3
#define STIO_ICMP_UNREACH_FRAG_NEEDED  4

/* Subcode for STIO_ICMP_REDIRECT */
#define STIO_ICMP_REDIRECT_NET      0
#define STIO_ICMP_REDIRECT_HOST     1
#define STIO_ICMP_REDIRECT_NETTOS   2
#define STIO_ICMP_REDIRECT_HOSTTOS  3

/* Subcode for STIO_ICMP_TIME_EXCEEDED */
#define STIO_ICMP_TIME_EXCEEDED_TTL       0
#define STIO_ICMP_TIME_EXCEEDED_FRAGTIME  1

/* ========================================================================= */

typedef int stio_sckfam_t;

struct stio_sckaddr_t
{
	stio_sckfam_t family;
	stio_uint8_t data[128]; /* TODO: use the actual sockaddr size */
};
typedef struct stio_sckaddr_t stio_sckaddr_t;

#if (STIO_SIZEOF_SOCKLEN_T == STIO_SIZEOF_INT)
	#if defined(STIO_SOCKLEN_T_IS_SIGNED)
		typedef int stio_scklen_t;
	#else
		typedef unsigned int stio_scklen_t;
	#endif
#elif (STIO_SIZEOF_SOCKLEN_T == STIO_SIZEOF_LONG)
	#if defined(STIO_SOCKLEN_T_IS_SIGNED)
		typedef long stio_scklen_t;
	#else
		typedef unsigned long stio_scklen_t;
	#endif
#else
	typedef int stio_scklen_t;
#endif

#if defined(_WIN32)
#	define STIO_IOCP_KEY 1
	/*
	typedef HANDLE stio_syshnd_t;
	typedef SOCKET stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (INVALID_SOCKET)
	*/

	typedef stio_uintptr_t qse_sckhnd_t;
#	define STIO_SCKHND_INVALID (~(qse_sck_hnd_t)0)

#else
	typedef int stio_sckhnd_t;
#	define STIO_SCKHND_INVALID (-1)
	
#endif


/* ========================================================================= */

enum stio_dev_sck_ioctl_cmd_t
{
	STIO_DEV_SCK_BIND, 
	STIO_DEV_SCK_CONNECT,
	STIO_DEV_SCK_LISTEN
};
typedef enum stio_dev_sck_ioctl_cmd_t stio_dev_sck_ioctl_cmd_t;


#define STIO_DEV_SCK_SET_PROGRESS(dev,bit) do { \
	(dev)->state &= ~STIO_DEV_SCK_ALL_PROGRESS_BITS; \
	(dev)->state |= (bit); \
} while(0)

#define STIO_DEV_SCK_GET_PROGRESS(dev) ((dev)->state & STIO_DEV_SCK_ALL_PROGRESS_BITS)

enum stio_dev_sck_state_t
{
	/* the following items(progress bits) are mutually exclusive */
	STIO_DEV_SCK_CONNECTING     = (1 << 0),
	STIO_DEV_SCK_CONNECTING_SSL = (1 << 1),
	STIO_DEV_SCK_CONNECTED      = (1 << 2),
	STIO_DEV_SCK_LISTENING      = (1 << 3),
	STIO_DEV_SCK_ACCEPTING_SSL  = (1 << 4),
	STIO_DEV_SCK_ACCEPTED       = (1 << 5),

	/* the following items can be bitwise-ORed with an exclusive item above */
	STIO_DEV_SCK_INTERCEPTED    = (1 << 15),


	/* convenience bit masks */
	STIO_DEV_SCK_ALL_PROGRESS_BITS = (STIO_DEV_SCK_CONNECTING |
	                                  STIO_DEV_SCK_CONNECTING_SSL |
	                                  STIO_DEV_SCK_CONNECTED |
	                                  STIO_DEV_SCK_LISTENING |
	                                  STIO_DEV_SCK_ACCEPTING_SSL |
	                                  STIO_DEV_SCK_ACCEPTED)
};
typedef enum stio_dev_sck_state_t stio_dev_sck_state_t;

typedef struct stio_dev_sck_t stio_dev_sck_t;

typedef int (*stio_dev_sck_on_read_t) (
	stio_dev_sck_t*       dev,
	const void*           data,
	stio_iolen_t          dlen,
	const stio_sckaddr_t* srcaddr
);

typedef int (*stio_dev_sck_on_write_t) (
	stio_dev_sck_t*       dev,
	stio_iolen_t          wrlen,
	void*                 wrctx,
	const stio_sckaddr_t* dstaddr
);

typedef void (*stio_dev_sck_on_disconnect_t) (
	stio_dev_sck_t* dev
);

typedef int (*stio_dev_sck_on_connect_t) (
	stio_dev_sck_t* dev
);

enum stio_dev_sck_type_t
{
	STIO_DEV_SCK_TCP4,
	STIO_DEV_SCK_TCP6,
	STIO_DEV_SCK_UPD4,
	STIO_DEV_SCK_UDP6,

	/* ARP at the ethernet layer */
	STIO_DEV_SCK_ARP,
	STIO_DEV_SCK_ARP_DGRAM,

	/* ICMP at the IPv4 layer */
	STIO_DEV_SCK_ICMP4,

	/* ICMP at the IPv6 layer */
	STIO_DEV_SCK_ICMP6

#if 0
	STIO_DEV_SCK_RAW,  /* raw L2-level packet */
#endif
};
typedef enum stio_dev_sck_type_t stio_dev_sck_type_t;

typedef struct stio_dev_sck_make_t stio_dev_sck_make_t;
struct stio_dev_sck_make_t
{
	stio_dev_sck_type_t type;
	stio_dev_sck_on_write_t on_write;
	stio_dev_sck_on_read_t on_read;
	stio_dev_sck_on_disconnect_t on_disconnect;
};

enum stio_dev_sck_bind_option_t
{
	STIO_DEV_SCK_BIND_BROADCAST   = (1 << 0),
	STIO_DEV_SCK_BIND_REUSEADDR   = (1 << 1),
	STIO_DEV_SCK_BIND_REUSEPORT   = (1 << 2),
	STIO_DEV_SCK_BIND_TRANSPARENT = (1 << 3),

/* TODO: more options --- SO_RCVBUF, SO_SNDBUF, SO_RCVTIMEO, SO_SNDTIMEO, SO_KEEPALIVE */
/*   BINDTODEVICE??? */

	STIO_DEV_SCK_BIND_SSL         = (1 << 15)
};
typedef enum stio_dev_sck_bind_option_t stio_dev_sck_bind_option_t;

typedef struct stio_dev_sck_bind_t stio_dev_sck_bind_t;
struct stio_dev_sck_bind_t
{
	int options;
	stio_sckaddr_t localaddr;
	/* TODO: add device name for BIND_TO_DEVICE */

	const stio_mchar_t* ssl_certfile;
	const stio_mchar_t* ssl_keyfile;
	stio_ntime_t accept_tmout;
};

enum stio_def_sck_connect_option_t
{
	STIO_DEV_SCK_CONNECT_SSL = (1 << 15)
};
typedef enum stio_dev_sck_connect_option_t stio_dev_sck_connect_option_t;

typedef struct stio_dev_sck_connect_t stio_dev_sck_connect_t;
struct stio_dev_sck_connect_t
{
	int options;
	stio_sckaddr_t remoteaddr;
	stio_ntime_t connect_tmout;
	stio_dev_sck_on_connect_t on_connect;
};

typedef struct stio_dev_sck_listen_t stio_dev_sck_listen_t;
struct stio_dev_sck_listen_t
{
	int backlogs;
	stio_dev_sck_on_connect_t on_connect; /* optional, but new connections are dropped immediately without this */
};

typedef struct stio_dev_sck_accept_t stio_dev_sck_accept_t;
struct stio_dev_sck_accept_t
{
	stio_syshnd_t  sck;
/* TODO: add timeout */
	stio_sckaddr_t remoteaddr;
};

struct stio_dev_sck_t
{
	STIO_DEV_HEADERS;

	stio_dev_sck_type_t type;
	stio_sckhnd_t sck;

	int state;

	/* remote peer address for a stateful stream socket. valid if one of the 
	 * followings is set in state:
	 *   STIO_DEV_TCP_ACCEPTING_SSL
	 *   STIO_DEV_TCP_ACCEPTED
	 *   STIO_DEV_TCP_CONNECTED
	 *   STIO_DEV_TCP_CONNECTING
	 *   STIO_DEV_TCP_CONNECTING_SSL
	 *
	 * also used as a placeholder to store source address for
	 * a stateless socket */
	stio_sckaddr_t remoteaddr; 

	/* local socket address */
	stio_sckaddr_t localaddr;

	/* original destination address */
	stio_sckaddr_t orgdstaddr;

	stio_dev_sck_on_write_t on_write;
	stio_dev_sck_on_read_t on_read;

	/* return 0 on succes, -1 on failure.
	 * called on a new tcp device for an accepted client or
	 *        on a tcp device conntected to a remote server */
	stio_dev_sck_on_connect_t on_connect;
	stio_dev_sck_on_disconnect_t on_disconnect;

	/* timer job index for handling
	 *  - connect() timeout for a connecting socket.
	 *  - SSL_accept() timeout for a socket accepting SSL */
	stio_tmridx_t tmrjob_index;

	/* connect timeout, ssl-connect timeout, ssl-accept timeout.
	 * it denotes timeout duration under some circumstances
	 * or an absolute expiry time under some other circumstances. */
	stio_ntime_t tmout;

	void* ssl_ctx;
	void* ssl;

};

#ifdef __cplusplus
extern "C" {
#endif

STIO_EXPORT stio_sckhnd_t stio_openasyncsck (
	stio_t* stio,
	int     domain, 
	int     type,
	int     proto
);

STIO_EXPORT void stio_closeasyncsck (
	stio_t*       stio,
	stio_sckhnd_t sck
);

STIO_EXPORT int stio_makesckasync (
	stio_t*       stio,
	stio_sckhnd_t sck
);

STIO_EXPORT int stio_getsckaddrinfo (
	stio_t*                stio,
	const stio_sckaddr_t*  addr,
	stio_scklen_t*         len,
	stio_sckfam_t*         family
);

/*
 * The stio_getsckaddrport() function returns the port number of a socket
 * address in the host byte order. If the address doesn't support the port
 * number, it returns 0.
 */
STIO_EXPORT stio_uint16_t stio_getsckaddrport (
	const stio_sckaddr_t* addr
);

/*
 * The stio_getsckaddrifindex() function returns an interface number.
 * If the address doesn't support the interface number, it returns 0. */
STIO_EXPORT int stio_getsckaddrifindex (
	const stio_sckaddr_t* addr
);


STIO_EXPORT void stio_sckaddr_initforip4 (
	stio_sckaddr_t* sckaddr,
	stio_uint16_t   port,
	stio_ip4addr_t* ip4addr
);

STIO_EXPORT void stio_sckaddr_initforip6 (
	stio_sckaddr_t* sckaddr,
	stio_uint16_t   port,
	stio_ip6addr_t* ip6addr
);

STIO_EXPORT void stio_sckaddr_initforeth (
	stio_sckaddr_t* sckaddr,
	int             ifindex,
	stio_ethaddr_t* ethaddr
);

/* ========================================================================= */

STIO_EXPORT stio_dev_sck_t* stio_dev_sck_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_sck_make_t* info
);

STIO_EXPORT int stio_dev_sck_bind (
	stio_dev_sck_t*         dev,
	stio_dev_sck_bind_t*    info
);

STIO_EXPORT int stio_dev_sck_connect (
	stio_dev_sck_t*         dev,
	stio_dev_sck_connect_t* info
);

STIO_EXPORT int stio_dev_sck_listen (
	stio_dev_sck_t*         dev,
	stio_dev_sck_listen_t*  info
);

STIO_EXPORT int stio_dev_sck_write (
	stio_dev_sck_t*        dev,
	const void*            data,
	stio_iolen_t           len,
	void*                  wrctx,
	const stio_sckaddr_t*  dstaddr
);

STIO_EXPORT int stio_dev_sck_timedwrite (
	stio_dev_sck_t*        dev,
	const void*            data,
	stio_iolen_t           len,
	const stio_ntime_t*    tmout,
	void*                  wrctx,
	const stio_sckaddr_t*  dstaddr
);

#if defined(STIO_HAVE_INLINE)

static STIO_INLINE void stio_dev_sck_halt (stio_dev_sck_t* sck)
{
	stio_dev_halt ((stio_dev_t*)sck);
}

static STIO_INLINE int stio_dev_sck_read (stio_dev_sck_t* sck, int enabled)
{
	return stio_dev_read ((stio_dev_t*)sck, enabled);
}

#else

#define stio_dev_sck_halt(sck) stio_dev_halt((stio_dev_t*)sck)
#define stio_dev_sck_read(sck,enabled) stio_dev_read((stio_dev_t*)sck, enabled)

#endif




STIO_EXPORT stio_uint16_t stio_checksumip (
	const void* hdr,
	stio_size_t len
);


#ifdef __cplusplus
}
#endif




#endif
