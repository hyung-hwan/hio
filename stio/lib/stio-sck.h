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


#define STIO_SCK_ETH_PROTO_IP4   0x0800 
#define STIO_SCK_ETH_PROTO_ARP   0x0806
#define STIO_SCK_ETH_PROTO_8021Q 0x8100 /* 802.1Q VLAN */
#define STIO_SCK_ETH_PROTO_IP6   0x86DD

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

typedef int (*stio_dev_sck_on_connect_t) (stio_dev_sck_t* dev);
typedef void (*stio_dev_sck_on_disconnect_t) (stio_dev_sck_t* dev);

enum stio_dev_sck_type_t
{
	STIO_DEV_SCK_TCP4,
	STIO_DEV_SCK_TCP6,
	STIO_DEV_SCK_UPD4,
	STIO_DEV_SCK_UDP6,

	STIO_DEV_SCK_ARP,
	STIO_DEV_SCK_ARP_DGRAM,

	STIO_DEV_SCK_ICMP4

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
	stio_ntime_t ssl_accept_tmout;
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
	stio_ntime_t tmout; /* connect timeout */
	stio_dev_sck_on_connect_t on_connect;
	stio_dev_sck_on_disconnect_t on_disconnect;
};

typedef struct stio_dev_sck_listen_t stio_dev_sck_listen_t;
struct stio_dev_sck_listen_t
{
	int backlogs;
	stio_dev_sck_on_connect_t on_connect; /* optional, but new connections are dropped immediately without this */
	stio_dev_sck_on_disconnect_t on_disconnect; /* should on_discconneted be part of on_accept_t??? */
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

	void* ssl_ctx;
	void* ssl;
	stio_ntime_t ssl_accept_tmout;

	int state;

	/* remote peer address for a stateful stream socket. valid if one of the 
	 * followings is set in state:
	 *   STIO_DEV_TCP_ACCEPTING_SSL
	 *   STIO_DEV_TCP_ACCEPTED
	 *   STIO_DEV_TCP_CONNECTED
	 *   STIO_DEV_TCP_CONNECTING
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
	stio_t*              stio,
	const stio_sckaddr_t* addr,
	stio_scklen_t*       len,
	stio_sckfam_t*       family
);

/*
 * The stio_getsckaddrport() function returns the port number of a socket
 * address in the host byte order. If the address doesn't support the port
 * number, it returns 0.
 */
STIO_EXPORT stio_uint16_t stio_getsckaddrport (
	const stio_sckaddr_t* addr
);

STIO_EXPORT void stio_sckaddr_initforip4 (
	stio_sckaddr_t* sckaddr,
	stio_uint16_t  port,
	stio_ip4addr_t* ip4addr
);

STIO_EXPORT void stio_sckaddr_initforip6 (
	stio_sckaddr_t* sckaddr,
	stio_uint16_t  port,
	stio_ip6addr_t* ip6addr
);

STIO_EXPORT void stio_sckaddr_initforeth (
	stio_sckaddr_t* sckaddr,
	int            ifindex,
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
	stio_dev_sck_t*       dev,
	const void*           data,
	stio_iolen_t          len,
	void*                 wrctx,
	const stio_sckaddr_t*  dstaddr
);

STIO_EXPORT int stio_dev_sck_timedwrite (
	stio_dev_sck_t*       dev,
	const void*           data,
	stio_iolen_t          len,
	const stio_ntime_t*   tmout,
	void*                 wrctx,
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


#ifdef __cplusplus
}
#endif




#endif
