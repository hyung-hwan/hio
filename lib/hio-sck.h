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

#ifndef _HIO_SCK_H_
#define _HIO_SCK_H_

#include <hio.h>
#include <hio-skad.h>

/* ========================================================================= */
/* TOOD: move these to a separate file */

#define HIO_ETHHDR_PROTO_IP4   0x0800
#define HIO_ETHHDR_PROTO_ARP   0x0806
#define HIO_ETHHDR_PROTO_8021Q 0x8100 /* 802.1Q VLAN */
#define HIO_ETHHDR_PROTO_IP6   0x86DD


#define HIO_ARPHDR_OPCODE_REQUEST 1
#define HIO_ARPHDR_OPCODE_REPLY   2

#define HIO_ARPHDR_HTYPE_ETH 0x0001
#define HIO_ARPHDR_PTYPE_IP4 0x0800

#include <hio-pac1.h>

struct HIO_PACKED hio_ethhdr_t
{
	hio_uint8_t  dest[HIO_ETHAD_LEN];
	hio_uint8_t  source[HIO_ETHAD_LEN];
	hio_uint16_t proto;
};
typedef struct hio_ethhdr_t hio_ethhdr_t;

struct HIO_PACKED hio_arphdr_t
{
	hio_uint16_t htype;   /* hardware type (ethernet: 0x0001) */
	hio_uint16_t ptype;   /* protocol type (ipv4: 0x0800) */
	hio_uint8_t  hlen;    /* hardware address length (ethernet: 6) */
	hio_uint8_t  plen;    /* protocol address length (ipv4 :4) */
	hio_uint16_t opcode;  /* operation code */
};
typedef struct hio_arphdr_t hio_arphdr_t;

/* arp payload for ipv4 over ethernet */
struct HIO_PACKED hio_etharp_t
{
	hio_uint8_t sha[HIO_ETHAD_LEN];   /* source hardware address */
	hio_uint8_t spa[HIO_IP4AD_LEN];   /* source protocol address */
	hio_uint8_t tha[HIO_ETHAD_LEN];   /* target hardware address */
	hio_uint8_t tpa[HIO_IP4AD_LEN];   /* target protocol address */
};
typedef struct hio_etharp_t hio_etharp_t;

struct HIO_PACKED hio_etharp_pkt_t
{
	hio_ethhdr_t ethhdr;
	hio_arphdr_t arphdr;
	hio_etharp_t arppld;
};
typedef struct hio_etharp_pkt_t hio_etharp_pkt_t;


struct hio_iphdr_t
{
#if defined(HIO_ENDIAN_LITTLE)
	hio_uint8_t ihl:4;
	hio_uint8_t version:4;
#elif defined(HIO_ENDIAN_BIG)
	hio_uint8_t version:4;
	hio_uint8_t ihl:4;
#else
#	UNSUPPORTED ENDIAN
#endif
	hio_int8_t tos;
	hio_int16_t tot_len;
	hio_int16_t id;
	hio_int16_t frag_off;
	hio_int8_t ttl;
	hio_int8_t protocol;
	hio_int16_t check;
	hio_int32_t saddr;
	hio_int32_t daddr;
	/*The options start here. */
};
typedef struct hio_iphdr_t hio_iphdr_t;


struct HIO_PACKED hio_icmphdr_t
{
	hio_uint8_t type; /* message type */
	hio_uint8_t code; /* subcode */
	hio_uint16_t checksum;
	union
	{
		struct
		{
			hio_uint16_t id;
			hio_uint16_t seq;
		} echo;

		hio_uint32_t gateway;

		struct
		{
			hio_uint16_t frag_unused;
			hio_uint16_t mtu;
		} frag; /* path mut discovery */
	} u;
};
typedef struct hio_icmphdr_t hio_icmphdr_t;

#include <hio-upac.h>


/* ICMP types */
#define HIO_ICMP_ECHO_REPLY        0
#define HIO_ICMP_UNREACH           3 /* destination unreachable */
#define HIO_ICMP_SOURCE_QUENCE     4
#define HIO_ICMP_REDIRECT          5
#define HIO_ICMP_ECHO_REQUEST      8
#define HIO_ICMP_TIME_EXCEEDED     11
#define HIO_ICMP_PARAM_PROBLEM     12
#define HIO_ICMP_TIMESTAMP_REQUEST 13
#define HIO_ICMP_TIMESTAMP_REPLY   14
#define HIO_ICMP_INFO_REQUEST      15
#define HIO_ICMP_INFO_REPLY        16
#define HIO_ICMP_ADDR_MASK_REQUEST 17
#define HIO_ICMP_ADDR_MASK_REPLY   18

/* Subcode for HIO_ICMP_UNREACH */
#define HIO_ICMP_UNREACH_NET          0
#define HIO_ICMP_UNREACH_HOST         1
#define HIO_ICMP_UNREACH_PROTOCOL     2
#define HIO_ICMP_UNREACH_PORT         3
#define HIO_ICMP_UNREACH_FRAG_NEEDED  4

/* Subcode for HIO_ICMP_REDIRECT */
#define HIO_ICMP_REDIRECT_NET      0
#define HIO_ICMP_REDIRECT_HOST     1
#define HIO_ICMP_REDIRECT_NETTOS   2
#define HIO_ICMP_REDIRECT_HOSTTOS  3

/* Subcode for HIO_ICMP_TIME_EXCEEDED */
#define HIO_ICMP_TIME_EXCEEDED_TTL       0
#define HIO_ICMP_TIME_EXCEEDED_FRAGTIME  1

/* ========================================================================= */

#if (HIO_SIZEOF_SOCKLEN_T == HIO_SIZEOF_INT)
	#if defined(HIO_SOCKLEN_T_IS_SIGNED)
		typedef int hio_scklen_t;
	#else
		typedef unsigned int hio_scklen_t;
	#endif
#elif (HIO_SIZEOF_SOCKLEN_T == HIO_SIZEOF_LONG)
	#if defined(HIO_SOCKLEN_T_IS_SIGNED)
		typedef long hio_scklen_t;
	#else
		typedef unsigned long hio_scklen_t;
	#endif
#else
	typedef int hio_scklen_t;
#endif


/* ========================================================================= */

enum hio_dev_sck_ioctl_cmd_t
{
	HIO_DEV_SCK_BIND,
	HIO_DEV_SCK_CONNECT,
	HIO_DEV_SCK_LISTEN
};
typedef enum hio_dev_sck_ioctl_cmd_t hio_dev_sck_ioctl_cmd_t;


#define HIO_DEV_SCK_SET_PROGRESS(dev,bit) do { \
	(dev)->state &= ~HIO_DEV_SCK_ALL_PROGRESS_BITS; \
	(dev)->state |= (bit); \
} while(0)

#define HIO_DEV_SCK_GET_PROGRESS(dev) ((dev)->state & HIO_DEV_SCK_ALL_PROGRESS_BITS)

enum hio_dev_sck_state_t
{
	/* the following items(progress bits) are mutually exclusive */
	HIO_DEV_SCK_CONNECTING     = (1 << 0),
	HIO_DEV_SCK_CONNECTING_SSL = (1 << 1),
	HIO_DEV_SCK_CONNECTED      = (1 << 2),
	HIO_DEV_SCK_LISTENING      = (1 << 3),
	HIO_DEV_SCK_ACCEPTING_SSL  = (1 << 4),
	HIO_DEV_SCK_ACCEPTED       = (1 << 5),

	/* the following items can be bitwise-ORed with an exclusive item above */
	HIO_DEV_SCK_LENIENT        = (1 << 14),
	HIO_DEV_SCK_INTERCEPTED    = (1 << 15),

	/* convenience bit masks */
	HIO_DEV_SCK_ALL_PROGRESS_BITS = (HIO_DEV_SCK_CONNECTING |
	                                 HIO_DEV_SCK_CONNECTING_SSL |
	                                 HIO_DEV_SCK_CONNECTED |
	                                 HIO_DEV_SCK_LISTENING |
	                                 HIO_DEV_SCK_ACCEPTING_SSL |
	                                 HIO_DEV_SCK_ACCEPTED)
};
typedef enum hio_dev_sck_state_t hio_dev_sck_state_t;

typedef struct hio_dev_sck_t hio_dev_sck_t;

typedef int (*hio_dev_sck_on_read_t) (
	hio_dev_sck_t*       dev,
	const void*          data,
	hio_iolen_t          dlen,
	const hio_skad_t* srcaddr
);

typedef int (*hio_dev_sck_on_write_t) (
	hio_dev_sck_t*       dev,
	hio_iolen_t          wrlen,
	void*                wrctx,
	const hio_skad_t* dstaddr
);

typedef void (*hio_dev_sck_on_disconnect_t) (
	hio_dev_sck_t* dev
);

typedef void (*hio_dev_sck_on_connect_t) (
	hio_dev_sck_t* dev
);

/**
 * Called for an SCTP notification - an association coming up or going down, a
 * path changing state, a send failing. These arrive interleaved with data on
 * the same socket and are flagged MSG_NOTIFICATION; they are not payload, so
 * they are never handed to on_read().
 *
 * 'data' points at a union sctp_notification of 'dlen' octets. It belongs to
 * the loop's read buffer and is only valid for the duration of this call.
 *
 * Leave it unset to have notifications discarded.
 */
/**
 * A decoded SCTP_ASSOC_CHANGE notification, as filled in by
 * hio_dev_sck_parse_assoc_event(). The negotiated stream counts are worth
 * having on their own: they are the outcome of the SCTP_INITMSG request from
 * both ends, and are not knowable any other way.
 */
typedef struct hio_sctp_assoc_event_t hio_sctp_assoc_event_t;
struct hio_sctp_assoc_event_t
{
	int          state;      /**< #hio_sctp_assoc_state_t */
	int          error;      /**< the notification's error field, 0 if none */
	hio_int32_t  assoc_id;   /**< pass this to hio_dev_sck_peeloff() */
	hio_uint16_t ostreams;   /**< outbound streams negotiated */
	hio_uint16_t instreams;  /**< inbound streams negotiated */
};

/**
 * The states an association can report. The names mirror the SCTP_COMM_UP
 * family so a reader of RFC 6458 recognises them, without the header's types
 * appearing in this interface.
 */
enum hio_sctp_assoc_state_t
{
	HIO_SCTP_ASSOC_COMM_UP = 0,     /**< a new association is usable */
	HIO_SCTP_ASSOC_COMM_LOST,       /**< it failed */
	HIO_SCTP_ASSOC_RESTART,         /**< the peer restarted it */
	HIO_SCTP_ASSOC_SHUTDOWN_COMP,   /**< it closed in an orderly way */
	HIO_SCTP_ASSOC_CANT_STR_ASSOC,  /**< setup never completed */
	HIO_SCTP_ASSOC_STATE_UNKNOWN    /**< something this build does not name */
};
typedef enum hio_sctp_assoc_state_t hio_sctp_assoc_state_t;

typedef void (*hio_dev_sck_on_notification_t) (
	hio_dev_sck_t*       dev,
	const void*          data,
	hio_iolen_t          dlen
);

typedef void (*hio_dev_sck_on_raw_accept_t) (
	hio_dev_sck_t* dev,
	hio_syshnd_t   syshnd,
	hio_skad_t*    peeradr
);

typedef struct hio_dev_sck_evcb_t hio_dev_sck_evcb_t;

struct hio_dev_sck_evcb_t
{
	hio_dev_sck_on_read_t on_read;
	hio_dev_sck_on_write_t on_write;
	hio_dev_sck_on_disconnect_t on_disconnect;
};


enum hio_dev_sck_type_t
{
	HIO_DEV_SCK_QX,
	HIO_DEV_SCK_UNIX,

	HIO_DEV_SCK_TCP4,
	HIO_DEV_SCK_TCP6,
	HIO_DEV_SCK_UDP4,
	HIO_DEV_SCK_UDP6,

	HIO_DEV_SCK_SCTP4, /*  one-to-one sctp stream */
	HIO_DEV_SCK_SCTP6, /*  one-to-one sctp stream */

	HIO_DEV_SCK_SCTP4_SEQPKT, /*  one-to-one sctp seqpacket */
	HIO_DEV_SCK_SCTP6_SEQPKT, /*  one-to-one sctp seqpacket */

	/* ICMP at the IPv4 layer */
	HIO_DEV_SCK_ICMP4,

	/* ICMP at the IPv6 layer */
	HIO_DEV_SCK_ICMP6,

	/* ARP at the ethernet layer */
	HIO_DEV_SCK_ARP,
	HIO_DEV_SCK_ARP_DGRAM,

	/* raw L2-level packet */
	HIO_DEV_SCK_PACKET,

	/* bpf socket */
	HIO_DEV_SCK_BPF
};
typedef enum hio_dev_sck_type_t hio_dev_sck_type_t;

enum hio_dev_sck_make_option_t
{
	/* for now, accept failure doesn't affect the listing socket if this is set */
	HIO_DEV_SCK_MAKE_LENIENT = (1 << 0)
};
typedef enum hio_dev_sck_make_option_t hio_dev_sck_make_option_t;

typedef struct hio_dev_sck_make_t hio_dev_sck_make_t;
struct hio_dev_sck_make_t
{
	hio_dev_sck_type_t type;

	int options;
	hio_syshnd_t syshnd;

	hio_dev_sck_on_write_t on_write;
	hio_dev_sck_on_read_t on_read;
	hio_dev_sck_on_connect_t on_connect;
	hio_dev_sck_on_disconnect_t on_disconnect;
	hio_dev_sck_on_raw_accept_t on_raw_accept; /* optional */
	hio_dev_sck_on_notification_t on_notification; /* optional. sctp only */

	/** number of outbound streams to ask for on an SCTP association.
	 *  0 leaves it to the system default, which is what every release before
	 *  this field did. only meaningful for the SCTP types. */
	hio_uint16_t sctp_ostreams;
	/** maximum number of inbound streams to accept. 0 for the default. */
	hio_uint16_t sctp_instreams;
};

enum hio_dev_sck_bind_option_t
{
	HIO_DEV_SCK_BIND_BROADCAST   = (1 << 0),
	HIO_DEV_SCK_BIND_REUSEADDR   = (1 << 1),
	HIO_DEV_SCK_BIND_REUSEPORT   = (1 << 2),
	HIO_DEV_SCK_BIND_TRANSPARENT = (1 << 3),

/* TODO: more options --- SO_RCVBUF, SO_SNDBUF, SO_RCVTIMEO, SO_SNDTIMEO, SO_KEEPALIVE */
/*   BINDTODEVICE??? */

	HIO_DEV_SCK_BIND_IGNERR      = (1 << 14), /* ignore non-critical error in binding */
	HIO_DEV_SCK_BIND_SSL         = (1 << 15)
};
typedef enum hio_dev_sck_bind_option_t hio_dev_sck_bind_option_t;

typedef struct hio_dev_sck_bind_t hio_dev_sck_bind_t;
struct hio_dev_sck_bind_t
{
	int options; /** 0 or bitwise-OR'ed of hio_dev_sck_bind_option_t enumerators */
	hio_skad_t localaddr;
	/* TODO: add device name for BIND_TO_DEVICE */

	const hio_bch_t* ssl_certfile;
	const hio_bch_t* ssl_keyfile;
};

enum hio_dev_sck_connect_option_t
{
	HIO_DEV_SCK_CONNECT_SSL         = (1 << 15)
};
typedef enum hio_dev_sck_connect_option_t hio_dev_sck_connect_option_t;

typedef struct hio_dev_sck_connect_t hio_dev_sck_connect_t;
struct hio_dev_sck_connect_t
{
	int options;
	hio_skad_t remoteaddr;
	hio_ntime_t connect_tmout;
};

/** flags for hio_dev_sck_bindx() */
enum hio_dev_sck_bindx_flag_t
{
	HIO_DEV_SCK_BINDX_ADD = 0, /**< add the addresses to the endpoint */
	HIO_DEV_SCK_BINDX_REM = 1  /**< take them away from it */
};
typedef enum hio_dev_sck_bindx_flag_t hio_dev_sck_bindx_flag_t;

typedef struct hio_dev_sck_listen_t hio_dev_sck_listen_t;
struct hio_dev_sck_listen_t
{
	int options; /* no options as of now. set it to 0 */
	int backlogs;
	hio_ntime_t accept_tmout;
};

struct hio_dev_sck_t
{
	HIO_DEV_HEADER;

	hio_dev_sck_type_t type;
	hio_syshnd_t hnd;

	int state;

	/* remote peer address for a stream socket. valid if one of the
	 * followings is set in state:
	 *   HIO_DEV_TCP_ACCEPTING_SSL
	 *   HIO_DEV_TCP_ACCEPTED
	 *   HIO_DEV_TCP_CONNECTED
	 *   HIO_DEV_TCP_CONNECTING
	 *   HIO_DEV_TCP_CONNECTING_SSL
	 *
	 * also used as a placeholder to store source address for
	 * a stateless socket */
	hio_skad_t remoteaddr;

	/* local socket address */
	hio_skad_t localaddr;

	/* original destination address */
	hio_skad_t orgdstaddr;


	hio_dev_sck_on_write_t on_write;
	hio_dev_sck_on_read_t on_read;

	/* called on a new tcp device for an accepted client or
	 *        on a tcp device conntected to a remote server */
	hio_dev_sck_on_connect_t on_connect;
	hio_dev_sck_on_disconnect_t on_disconnect;
	hio_dev_sck_on_raw_accept_t on_raw_accept;
	hio_dev_sck_on_notification_t on_notification;

	/* timer job index for handling
	 *  - connect() timeout for a connecting socket.
	 *  - SSL_accept() timeout for a socket accepting SSL */
	hio_tmridx_t tmrjob_index;

	/* connect timeout, ssl-connect timeout, ssl-accept timeout.
	 * it denotes timeout duration under some circumstances
	 * or an absolute expiry time under some other circumstances. */
	hio_ntime_t tmout;

	void* ssl_ctx;
	void* ssl;

	hio_syshnd_t side_chan; /* side-channel for HIO_DEV_SCK_QX */

	/* set while a message too large for the read buffer is being thrown away.
	 * see the MSG_EOR handling in the sctp seqpacket read method. */
	int sctp_discarding;
};

enum hio_dev_sck_shutdown_how_t
{
	HIO_DEV_SCK_SHUTDOWN_READ  = (1 << 0),
	HIO_DEV_SCK_SHUTDOWN_WRITE = (1 << 1)
};
typedef enum hio_dev_sck_shutdown_how_t hio_dev_sck_shutdown_how_t;

enum hio_dev_sck_qxmsg_cmd_t
{
	HIO_DEV_SCK_QXMSG_NEWCONN = 0
};
typedef enum hio_dev_sck_qxmsg_cmd_t hio_dev_sck_qxmsg_cmd_t;

struct hio_dev_sck_qxmsg_t
{
	hio_dev_sck_qxmsg_cmd_t cmd;
	hio_dev_sck_type_t scktype;
	hio_syshnd_t syshnd;
	hio_skad_t remoteaddr;
};
typedef struct hio_dev_sck_qxmsg_t hio_dev_sck_qxmsg_t;


#if defined(__cplusplus)
extern "C" {
#endif

/* ========================================================================= */

HIO_EXPORT hio_dev_sck_t* hio_dev_sck_make (
	hio_t*                    hio,
	hio_oow_t                 xtnsize,
	const hio_dev_sck_make_t* info
);

#if defined(HIO_HAVE_INLINE)
static HIO_INLINE hio_t* hio_dev_sck_gethio (hio_dev_sck_t* sck) { return hio_dev_gethio((hio_dev_t*)sck); }
static HIO_INLINE void* hio_dev_sck_getxtn (hio_dev_sck_t* sck) { return (void*)(sck + 1); }
static HIO_INLINE hio_dev_sck_type_t hio_dev_sck_gettype (hio_dev_sck_t* sck) { return sck->type; }
static HIO_INLINE hio_syshnd_t hio_dev_sck_getsyshnd (hio_dev_sck_t* sck) { return sck->hnd; }
#else
#	define hio_dev_sck_gethio(sck) hio_dev_gethio(sck)
#	define hio_dev_sck_getxtn(sck) ((void*)(((hio_dev_sck_t*)sck) + 1))
#	define hio_dev_sck_gettype(sck) (((hio_dev_sck_t*)sck)->type)
#	define hio_dev_sck_getsyshnd(sck) (((hio_dev_sck_t*)sck)->hnd)
#endif

HIO_EXPORT int hio_dev_sck_bind (
	hio_dev_sck_t*         dev,
	hio_dev_sck_bind_t*    info
);

HIO_EXPORT int hio_dev_sck_connect (
	hio_dev_sck_t*         dev,
	hio_dev_sck_connect_t* info
);

HIO_EXPORT int hio_dev_sck_listen (
	hio_dev_sck_t*         dev,
	hio_dev_sck_listen_t*  info
);

HIO_EXPORT int hio_dev_sck_write (
	hio_dev_sck_t*        dev,
	const void*           data,
	hio_iolen_t           len,
	void*                 wrctx,
	const hio_skad_t*     dstaddr
);

HIO_EXPORT int hio_dev_sck_writev (
	hio_dev_sck_t*        dev,
	hio_iovec_t*          iov,
	hio_iolen_t           iovcnt,
	void*                 wrctx,
	const hio_skad_t*     dstaddr
);


HIO_EXPORT int hio_dev_sck_timedwrite (
	hio_dev_sck_t*        dev,
	const void*           data,
	hio_iolen_t           len,
	const hio_ntime_t*    tmout,
	void*                 wrctx,
	const hio_skad_t*     dstaddr
);


HIO_EXPORT int hio_dev_sck_timedwritev (
	hio_dev_sck_t*        dev,
	hio_iovec_t*          iov,
	hio_iolen_t           iovcnt,
	const hio_ntime_t*    tmout,
	void*                 wrctx,
	const hio_skad_t*     dstaddr
);

#if defined(HIO_HAVE_INLINE)

static HIO_INLINE void hio_dev_sck_kill (hio_dev_sck_t* sck)
{
	hio_dev_kill ((hio_dev_t*)sck);
}

static HIO_INLINE void hio_dev_sck_halt (hio_dev_sck_t* sck)
{
	hio_dev_halt ((hio_dev_t*)sck);
}

static HIO_INLINE int hio_dev_sck_read (hio_dev_sck_t* sck, int enabled)
{
	return hio_dev_read((hio_dev_t*)sck, enabled);
}

static HIO_INLINE int hio_dev_sck_timedread (hio_dev_sck_t* sck, int enabled, hio_ntime_t* tmout)
{
	return hio_dev_timedread((hio_dev_t*)sck, enabled, tmout);
}

static HIO_INLINE int hio_dev_sck_sendfile (hio_dev_sck_t* sck, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t len, void* wrctx)
{
	return hio_dev_sendfile((hio_dev_t*)sck, in_fd, foff, len, wrctx);
}

static HIO_INLINE int hio_dev_sck_timedsendfile (hio_dev_sck_t* sck, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t len, hio_ntime_t* tmout, void* wrctx)
{
	return hio_dev_timedsendfile((hio_dev_t*)sck, in_fd, foff, len, tmout, wrctx);
}

#else

#define hio_dev_sck_kill(sck) hio_dev_kill((hio_dev_t*)sck)
#define hio_dev_sck_halt(sck) hio_dev_halt((hio_dev_t*)sck)

#define hio_dev_sck_read(sck,enabled) hio_dev_read((hio_dev_t*)sck, enabled)
#define hio_dev_sck_timedread(sck,enabled,tmout) hio_dev_timedread((hio_dev_t*)sck, enabled, tmout)

#define hio_dev_sck_sendfile(sck,in_fd,foff,len,wrctx) hio_dev_sendfile((hio_dev_t*)sck, in_fd, foff, len, wrctx)
#define hio_dev_sck_timedsendfile(sck,in_fd,foff,len,tmout,wrctx) hio_dev_timedsendfile((hio_dev_t*)sck, in_fd, foff, len, tmout, wrctx)
#endif


HIO_EXPORT int hio_dev_sck_setsockopt (
	hio_dev_sck_t* dev,
	int            level,
	int            optname,
	void*          optval,
	hio_scklen_t   optlen
);

HIO_EXPORT int hio_dev_sck_getsockopt (
	hio_dev_sck_t* dev,
	int            level,
	int            optname,
	void*          optval,
	hio_scklen_t*  optlen
);

HIO_EXPORT int hio_dev_sck_getsockaddr (
	hio_dev_sck_t* dev,
	hio_skad_t*    skad
);

HIO_EXPORT int hio_dev_sck_getpeeraddr (
	hio_dev_sck_t* dev,
	hio_skad_t*    skad
);

HIO_EXPORT int hio_dev_sck_joinmcastgroup (
	hio_dev_sck_t*    dev,
	const hio_skad_t* mcast_skad,
	int               ifindex
);

HIO_EXPORT int hio_dev_sck_leavemcastgroup (
	hio_dev_sck_t*    dev,
	const hio_skad_t* mcast_skad,
	int               ifindex
);

HIO_EXPORT int hio_dev_sck_shutdown (
	hio_dev_sck_t* dev,
	int            how  /* bitwise-ORed of hio_dev_sck_shutdown_how_t enumerators */
);

/**
 * The hio_dev_sck_peeloff() function takes one association off a one-to-many
 * SCTP socket and gives it a socket, and a device, of its own. The new device
 * arrives through on_connect() in the #HIO_DEV_SCK_ACCEPTED state carrying the
 * one-to-one methods - a write queue of its own, a read-enable bit of its own,
 * and addresses of its own, which is what per-association multi-homing needs.
 * It is the same door an accepted tcp connection comes through.
 *
 * It may be called at any point in an association's life, and where it is
 * called from is the whole point:
 *
 * - from on_notification(), on an SCTP_COMM_UP whose peer address you like -
 *   use hio_dev_sck_parse_assoc_event() to get the association id;
 * - from on_read(), when a message tells you this peer deserves a session -
 *   the association id is already there, on the source address, via
 *   hio_skad_get_assoc().
 *
 * Peeling mid-stream leaves a clean seam: messages already handed to on_read()
 * are yours, whatever is still queued in the kernel moves to the new socket,
 * and everything after arrives on the new device. Nothing is lost or
 * duplicated. Call it synchronously from inside the callback though - defer it
 * and this socket may read another message for that association first, which
 * widens the seam by however long you waited.
 *
 * Only for the one-to-many SCTP types; anything else fails with #HIO_EINVAL.
 * A build whose system lacks sctp_peeloff() fails with #HIO_ENOIMPL.
 */
HIO_EXPORT int hio_dev_sck_peeloff (
	hio_dev_sck_t*    dev,
	hio_int32_t       assoc_id
);

/**
 * The hio_dev_sck_parse_assoc_event() function decodes what on_notification()
 * hands over, when what it hands over is an association change. It exists so
 * that deciding what to do about a new association does not require picking
 * apart a kernel structure by hand.
 *
 * Returns 0 and fills 'ev' if the notification is an association change, or -1
 * if it is not - some other kind of event, or a buffer too short to be one.
 *
 * -1 is an answer, not a failure: no error number is set, and none can be, as
 * this is a pure decoder with no hio_t to set one on. A caller walking
 * notifications simply ignores the ones it gets -1 for.
 */
HIO_EXPORT int hio_dev_sck_parse_assoc_event (
	const void*                data,
	hio_iolen_t                dlen,
	hio_sctp_assoc_event_t*    ev
);

/**
 * The hio_dev_sck_bindx() function adds or removes local addresses on an
 * already bound SCTP endpoint - which is what local multi-homing is. The
 * addresses belong to the endpoint rather than to any one association, so this
 * is a socket operation and works the same whether associations are peeled off
 * or kept on the one socket.
 *
 * hio_dev_sck_bind() must have been called first: sctp_bindx() adds to an
 * existing binding, it does not replace it.
 *
 * Available only for the SCTP socket types; anything else fails with
 * #HIO_ENOIMPL.
 */
HIO_EXPORT int hio_dev_sck_bindx (
	hio_dev_sck_t*             dev,
	const hio_skad_t*          addrs,
	hio_oow_t                  naddrs,
	hio_dev_sck_bindx_flag_t   flags
);

/**
 * The hio_dev_sck_getladdrs() function reports the local addresses of the
 * endpoint, and hio_dev_sck_getpaddrs() the peer addresses of the association -
 * the two halves of what multi-homing established.
 *
 * On entry *naddrs is the room available in 'addrs'; on return it is how many
 * were written. #HIO_EBUFFULL is reported if there were more, with *naddrs set
 * to the number needed.
 */
HIO_EXPORT int hio_dev_sck_getladdrs (
	hio_dev_sck_t*    dev,
	hio_skad_t*       addrs,
	hio_oow_t*        naddrs
);

HIO_EXPORT int hio_dev_sck_getpaddrs (
	hio_dev_sck_t*    dev,
	hio_skad_t*       addrs,
	hio_oow_t*        naddrs
);

/**
 * The hio_dev_sck_setprimaryaddr() function asks the peer to prefer one of its
 * addresses as the primary path. It must be one of the addresses
 * hio_dev_sck_getpaddrs() reports.
 */
HIO_EXPORT int hio_dev_sck_setprimaryaddr (
	hio_dev_sck_t*      dev,
	const hio_skad_t*   addr
);

HIO_EXPORT int hio_dev_sck_sendfileok (
	hio_dev_sck_t* dev
);

HIO_EXPORT int hio_dev_sck_writetosidechan (
	hio_dev_sck_t* htts,
	const void*    dptr,
	hio_oow_t      dlen
);


HIO_EXPORT hio_uint16_t hio_checksum_ip (
	const void* hdr,
	hio_oow_t   len
);

HIO_EXPORT int hio_get_stream_sck_type_from_skad (
	const hio_skad_t*   skad,
	hio_dev_sck_type_t* type
);

#if defined(__cplusplus)
}
#endif




#endif
