/*
 * $Id$
 *
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

#ifndef _MIO_SCK_H_
#define _MIO_SCK_H_

#include <mio.h>
#include <mio-skad.h>

/* ========================================================================= */
/* TOOD: move these to a separte file */

#define MIO_ETHHDR_PROTO_IP4   0x0800
#define MIO_ETHHDR_PROTO_ARP   0x0806
#define MIO_ETHHDR_PROTO_8021Q 0x8100 /* 802.1Q VLAN */
#define MIO_ETHHDR_PROTO_IP6   0x86DD


#define MIO_ARPHDR_OPCODE_REQUEST 1
#define MIO_ARPHDR_OPCODE_REPLY   2

#define MIO_ARPHDR_HTYPE_ETH 0x0001
#define MIO_ARPHDR_PTYPE_IP4 0x0800

#include <mio-pac1.h>

struct MIO_PACKED mio_ethhdr_t
{
	mio_uint8_t  dest[MIO_ETHADDR_LEN];
	mio_uint8_t  source[MIO_ETHADDR_LEN];
	mio_uint16_t proto;
};
typedef struct mio_ethhdr_t mio_ethhdr_t;

struct MIO_PACKED mio_arphdr_t
{
	mio_uint16_t htype;   /* hardware type (ethernet: 0x0001) */
	mio_uint16_t ptype;   /* protocol type (ipv4: 0x0800) */
	mio_uint8_t  hlen;    /* hardware address length (ethernet: 6) */
	mio_uint8_t  plen;    /* protocol address length (ipv4 :4) */
	mio_uint16_t opcode;  /* operation code */
};
typedef struct mio_arphdr_t mio_arphdr_t;

/* arp payload for ipv4 over ethernet */
struct MIO_PACKED mio_etharp_t
{
	mio_uint8_t sha[MIO_ETHADDR_LEN];   /* source hardware address */
	mio_uint8_t spa[MIO_IP4ADDR_LEN];   /* source protocol address */
	mio_uint8_t tha[MIO_ETHADDR_LEN];   /* target hardware address */
	mio_uint8_t tpa[MIO_IP4ADDR_LEN];   /* target protocol address */
};
typedef struct mio_etharp_t mio_etharp_t;

struct MIO_PACKED mio_etharp_pkt_t
{
	mio_ethhdr_t ethhdr;
	mio_arphdr_t arphdr;
	mio_etharp_t arppld;
};
typedef struct mio_etharp_pkt_t mio_etharp_pkt_t;


struct mio_iphdr_t
{
#if defined(MIO_ENDIAN_LITTLE)
	mio_uint8_t ihl:4;
	mio_uint8_t version:4;
#elif defined(MIO_ENDIAN_BIG)
	mio_uint8_t version:4;
	mio_uint8_t ihl:4;
#else
#	UNSUPPORTED ENDIAN
#endif
	mio_int8_t tos;
	mio_int16_t tot_len;
	mio_int16_t id;
	mio_int16_t frag_off;
	mio_int8_t ttl;
	mio_int8_t protocol;
	mio_int16_t check;
	mio_int32_t saddr;
	mio_int32_t daddr;
	/*The options start here. */
};
typedef struct mio_iphdr_t mio_iphdr_t;


struct MIO_PACKED mio_icmphdr_t 
{
	mio_uint8_t type; /* message type */
	mio_uint8_t code; /* subcode */
	mio_uint16_t checksum;
	union
	{
		struct
		{
			mio_uint16_t id;
			mio_uint16_t seq;
		} echo;

		mio_uint32_t gateway;

		struct
		{
			mio_uint16_t frag_unused;
			mio_uint16_t mtu;
		} frag; /* path mut discovery */
	} u;
};
typedef struct mio_icmphdr_t mio_icmphdr_t;

#include <mio-upac.h>


/* ICMP types */
#define MIO_ICMP_ECHO_REPLY        0
#define MIO_ICMP_UNREACH           3 /* destination unreachable */
#define MIO_ICMP_SOURCE_QUENCE     4
#define MIO_ICMP_REDIRECT          5
#define MIO_ICMP_ECHO_REQUEST      8
#define MIO_ICMP_TIME_EXCEEDED     11
#define MIO_ICMP_PARAM_PROBLEM     12
#define MIO_ICMP_TIMESTAMP_REQUEST 13
#define MIO_ICMP_TIMESTAMP_REPLY   14
#define MIO_ICMP_INFO_REQUEST      15
#define MIO_ICMP_INFO_REPLY        16
#define MIO_ICMP_ADDR_MASK_REQUEST 17
#define MIO_ICMP_ADDR_MASK_REPLY   18

/* Subcode for MIO_ICMP_UNREACH */
#define MIO_ICMP_UNREACH_NET          0
#define MIO_ICMP_UNREACH_HOST         1
#define MIO_ICMP_UNREACH_PROTOCOL     2
#define MIO_ICMP_UNREACH_PORT         3
#define MIO_ICMP_UNREACH_FRAG_NEEDED  4

/* Subcode for MIO_ICMP_REDIRECT */
#define MIO_ICMP_REDIRECT_NET      0
#define MIO_ICMP_REDIRECT_HOST     1
#define MIO_ICMP_REDIRECT_NETTOS   2
#define MIO_ICMP_REDIRECT_HOSTTOS  3

/* Subcode for MIO_ICMP_TIME_EXCEEDED */
#define MIO_ICMP_TIME_EXCEEDED_TTL       0
#define MIO_ICMP_TIME_EXCEEDED_FRAGTIME  1

/* ========================================================================= */

#if (MIO_SIZEOF_SOCKLEN_T == MIO_SIZEOF_INT)
	#if defined(MIO_SOCKLEN_T_IS_SIGNED)
		typedef int mio_scklen_t;
	#else
		typedef unsigned int mio_scklen_t;
	#endif
#elif (MIO_SIZEOF_SOCKLEN_T == MIO_SIZEOF_LONG)
	#if defined(MIO_SOCKLEN_T_IS_SIGNED)
		typedef long mio_scklen_t;
	#else
		typedef unsigned long mio_scklen_t;
	#endif
#else
	typedef int mio_scklen_t;
#endif

#if defined(_WIN32)
#	define MIO_IOCP_KEY 1
	/*
	typedef HANDLE mio_syshnd_t;
	typedef SOCKET mio_sckhnd_t;
#	define MIO_SCKHND_INVALID (INVALID_SOCKET)
	*/

	typedef mio_uintptr_t qse_sckhnd_t;
#	define MIO_SCKHND_INVALID (~(qse_sck_hnd_t)0)

#else
	typedef int mio_sckhnd_t;
#	define MIO_SCKHND_INVALID (-1)
	
#endif


/* ========================================================================= */

enum mio_dev_sck_ioctl_cmd_t
{
	MIO_DEV_SCK_BIND, 
	MIO_DEV_SCK_CONNECT,
	MIO_DEV_SCK_LISTEN
};
typedef enum mio_dev_sck_ioctl_cmd_t mio_dev_sck_ioctl_cmd_t;


#define MIO_DEV_SCK_SET_PROGRESS(dev,bit) do { \
	(dev)->state &= ~MIO_DEV_SCK_ALL_PROGRESS_BITS; \
	(dev)->state |= (bit); \
} while(0)

#define MIO_DEV_SCK_GET_PROGRESS(dev) ((dev)->state & MIO_DEV_SCK_ALL_PROGRESS_BITS)

enum mio_dev_sck_state_t
{
	/* the following items(progress bits) are mutually exclusive */
	MIO_DEV_SCK_CONNECTING     = (1 << 0),
	MIO_DEV_SCK_CONNECTING_SSL = (1 << 1),
	MIO_DEV_SCK_CONNECTED      = (1 << 2),
	MIO_DEV_SCK_LISTENING      = (1 << 3),
	MIO_DEV_SCK_ACCEPTING_SSL  = (1 << 4),
	MIO_DEV_SCK_ACCEPTED       = (1 << 5),

	/* the following items can be bitwise-ORed with an exclusive item above */
	MIO_DEV_SCK_INTERCEPTED    = (1 << 15),


	/* convenience bit masks */
	MIO_DEV_SCK_ALL_PROGRESS_BITS = (MIO_DEV_SCK_CONNECTING |
	                                  MIO_DEV_SCK_CONNECTING_SSL |
	                                  MIO_DEV_SCK_CONNECTED |
	                                  MIO_DEV_SCK_LISTENING |
	                                  MIO_DEV_SCK_ACCEPTING_SSL |
	                                  MIO_DEV_SCK_ACCEPTED)
};
typedef enum mio_dev_sck_state_t mio_dev_sck_state_t;

typedef struct mio_dev_sck_t mio_dev_sck_t;

typedef int (*mio_dev_sck_on_read_t) (
	mio_dev_sck_t*       dev,
	const void*          data,
	mio_iolen_t          dlen,
	const mio_skad_t* srcaddr
);

typedef int (*mio_dev_sck_on_write_t) (
	mio_dev_sck_t*       dev,
	mio_iolen_t          wrlen,
	void*                wrctx,
	const mio_skad_t* dstaddr
);

typedef void (*mio_dev_sck_on_disconnect_t) (
	mio_dev_sck_t* dev
);

typedef void (*mio_dev_sck_on_connect_t) (
	mio_dev_sck_t* dev
);

enum mio_dev_sck_type_t
{
	MIO_DEV_SCK_TCP4,
	MIO_DEV_SCK_TCP6,
	MIO_DEV_SCK_UDP4,
	MIO_DEV_SCK_UDP6,

	/* ARP at the ethernet layer */
	MIO_DEV_SCK_ARP,
	MIO_DEV_SCK_ARP_DGRAM,

	/* ICMP at the IPv4 layer */
	MIO_DEV_SCK_ICMP4,

	/* ICMP at the IPv6 layer */
	MIO_DEV_SCK_ICMP6

#if 0
	MIO_DEV_SCK_RAW,  /* raw L2-level packet */
#endif
};
typedef enum mio_dev_sck_type_t mio_dev_sck_type_t;

typedef struct mio_dev_sck_make_t mio_dev_sck_make_t;
struct mio_dev_sck_make_t
{
	mio_dev_sck_type_t type;
	mio_dev_sck_on_write_t on_write;
	mio_dev_sck_on_read_t on_read;
	mio_dev_sck_on_connect_t on_connect;
	mio_dev_sck_on_disconnect_t on_disconnect;
};

enum mio_dev_sck_bind_option_t
{
	MIO_DEV_SCK_BIND_BROADCAST   = (1 << 0),
	MIO_DEV_SCK_BIND_REUSEADDR   = (1 << 1),
	MIO_DEV_SCK_BIND_REUSEPORT   = (1 << 2),
	MIO_DEV_SCK_BIND_TRANSPARENT = (1 << 3),

/* TODO: more options --- SO_RCVBUF, SO_SNDBUF, SO_RCVTIMEO, SO_SNDTIMEO, SO_KEEPALIVE */
/*   BINDTODEVICE??? */

	MIO_DEV_SCK_BIND_SSL         = (1 << 15)
};
typedef enum mio_dev_sck_bind_option_t mio_dev_sck_bind_option_t;

typedef struct mio_dev_sck_bind_t mio_dev_sck_bind_t;
struct mio_dev_sck_bind_t
{
	int options;
	mio_skad_t localaddr;
	/* TODO: add device name for BIND_TO_DEVICE */

	const mio_bch_t* ssl_certfile;
	const mio_bch_t* ssl_keyfile;
	mio_ntime_t accept_tmout;
};

enum mio_def_sck_connect_option_t
{
	MIO_DEV_SCK_CONNECT_SSL = (1 << 15)
};
typedef enum mio_dev_sck_connect_option_t mio_dev_sck_connect_option_t;

typedef struct mio_dev_sck_connect_t mio_dev_sck_connect_t;
struct mio_dev_sck_connect_t
{
	int options;
	mio_skad_t remoteaddr;
	mio_ntime_t connect_tmout;
};

typedef struct mio_dev_sck_listen_t mio_dev_sck_listen_t;
struct mio_dev_sck_listen_t
{
	int backlogs;
};

typedef struct mio_dev_sck_accept_t mio_dev_sck_accept_t;
struct mio_dev_sck_accept_t
{
	mio_syshnd_t  sck;
/* TODO: add timeout */
	mio_skad_t remoteaddr;
};

struct mio_dev_sck_t
{
	MIO_DEV_HEADERS;

	mio_dev_sck_type_t type;
	mio_sckhnd_t sck;

	int state;

	/* remote peer address for a stateful stream socket. valid if one of the 
	 * followings is set in state:
	 *   MIO_DEV_TCP_ACCEPTING_SSL
	 *   MIO_DEV_TCP_ACCEPTED
	 *   MIO_DEV_TCP_CONNECTED
	 *   MIO_DEV_TCP_CONNECTING
	 *   MIO_DEV_TCP_CONNECTING_SSL
	 *
	 * also used as a placeholder to store source address for
	 * a stateless socket */
	mio_skad_t remoteaddr; 

	/* local socket address */
	mio_skad_t localaddr;

	/* original destination address */
	mio_skad_t orgdstaddr;

	mio_dev_sck_on_write_t on_write;
	mio_dev_sck_on_read_t on_read;

	/* return 0 on succes, -1 on failure.
	 * called on a new tcp device for an accepted client or
	 *        on a tcp device conntected to a remote server */
	mio_dev_sck_on_connect_t on_connect;
	mio_dev_sck_on_disconnect_t on_disconnect;

	/* timer job index for handling
	 *  - connect() timeout for a connecting socket.
	 *  - SSL_accept() timeout for a socket accepting SSL */
	mio_tmridx_t tmrjob_index;

	/* connect timeout, ssl-connect timeout, ssl-accept timeout.
	 * it denotes timeout duration under some circumstances
	 * or an absolute expiry time under some other circumstances. */
	mio_ntime_t tmout;

	void* ssl_ctx;
	void* ssl;

};

#ifdef __cplusplus
extern "C" {
#endif

MIO_EXPORT int mio_makesckasync (
	mio_t*       mio,
	mio_sckhnd_t sck
);

/* ========================================================================= */

MIO_EXPORT mio_dev_sck_t* mio_dev_sck_make (
	mio_t*                    mio,
	mio_oow_t                 xtnsize,
	const mio_dev_sck_make_t* info
);

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_dev_sck_getxtn (mio_dev_sck_t* sck) { return (void*)(sck + 1); }
#else
#	define mio_dev_sck_getxtn(sck) ((void*)(((mio_dev_sck_t*)sck) + 1))
#endif

MIO_EXPORT int mio_dev_sck_bind (
	mio_dev_sck_t*         dev,
	mio_dev_sck_bind_t*    info
);

MIO_EXPORT int mio_dev_sck_connect (
	mio_dev_sck_t*         dev,
	mio_dev_sck_connect_t* info
);

MIO_EXPORT int mio_dev_sck_listen (
	mio_dev_sck_t*         dev,
	mio_dev_sck_listen_t*  info
);

MIO_EXPORT int mio_dev_sck_write (
	mio_dev_sck_t*        dev,
	const void*           data,
	mio_iolen_t           len,
	void*                 wrctx,
	const mio_skad_t*     dstaddr
);

MIO_EXPORT int mio_dev_sck_timedwrite (
	mio_dev_sck_t*        dev,
	const void*           data,
	mio_iolen_t           len,
	const mio_ntime_t*    tmout,
	void*                 wrctx,
	const mio_skad_t*     dstaddr
);


MIO_EXPORT int mio_dev_sck_timedwritev (
	mio_dev_sck_t*        dev,
	const mio_iovec_t*    iov,
	mio_iolen_t           iovcnt,
	const mio_ntime_t*    tmout,
	void*                 wrctx,
	const mio_skad_t*     dstaddr
);


#if defined(MIO_HAVE_INLINE)

static MIO_INLINE void mio_dev_sck_kill (mio_dev_sck_t* sck)
{
	mio_dev_kill ((mio_dev_t*)sck);
}

static MIO_INLINE void mio_dev_sck_halt (mio_dev_sck_t* sck)
{
	mio_dev_halt ((mio_dev_t*)sck);
}

static MIO_INLINE int mio_dev_sck_read (mio_dev_sck_t* sck, int enabled)
{
	return mio_dev_read((mio_dev_t*)sck, enabled);
}

static MIO_INLINE int mio_dev_sck_timedread (mio_dev_sck_t* sck, int enabled, mio_ntime_t* tmout)
{
	return mio_dev_timedread((mio_dev_t*)sck, enabled, tmout);
}
#else

#define mio_dev_sck_kill(sck) mio_dev_kill((mio_dev_t*)sck)
#define mio_dev_sck_halt(sck) mio_dev_halt((mio_dev_t*)sck)
#define mio_dev_sck_read(sck,enabled) mio_dev_read((mio_dev_t*)sck, enabled)
#define mio_dev_sck_timedread(sck,enabled,tmout) mio_dev_timedread((mio_dev_t*)sck, enabled, tmout)

#endif




MIO_EXPORT mio_uint16_t mio_checksumip (
	const void* hdr,
	mio_oow_t len
);

#ifdef __cplusplus
}
#endif




#endif
