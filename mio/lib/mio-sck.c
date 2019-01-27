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


#include "mio-sck.h"
#include "mio-prv.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(HAVE_NETPACKET_PACKET_H)
#	include <netpacket/packet.h>
#endif

#if defined(HAVE_NET_IF_DL_H)
#	include <net/if_dl.h>
#endif


#if defined(__linux__)
#	include <limits.h>
#	if defined(HAVE_LINUX_NETFILTER_IPV4_H)
#		include <linux/netfilter_ipv4.h> /* SO_ORIGINAL_DST */
#	endif
#	if !defined(SO_ORIGINAL_DST)
#		define SO_ORIGINAL_DST 80
#	endif
#	if !defined(IP_TRANSPARENT)
#		define IP_TRANSPARENT 19
#	endif
#	if !defined(SO_REUSEPORT)
#		define SO_REUSEPORT 15
#	endif
#endif

#if defined(HAVE_OPENSSL_SSL_H) && defined(HAVE_SSL)
#	include <openssl/ssl.h>
#	if defined(HAVE_OPENSSL_ERR_H)
#		include <openssl/err.h>
#	endif
#	if defined(HAVE_OPENSSL_ENGINE_H)
#		include <openssl/engine.h>
#	endif
#	define USE_SSL
#endif


/* ========================================================================= */

int mio_makesckasync (mio_t* mio, mio_sckhnd_t sck)
{
	return mio_makesyshndasync (mio, (mio_syshnd_t)sck);
}

static void close_async_socket (mio_t* mio, mio_sckhnd_t sck)
{
	close (sck);
}

static mio_sckhnd_t open_async_socket (mio_t* mio, int domain, int type, int proto)
{
	mio_sckhnd_t sck = MIO_SCKHND_INVALID;
	int flags;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	type |= SOCK_NONBLOCK;
	type |= SOCK_CLOEXEC;
open_socket:
#endif
	sck = socket(domain, type, proto); 
	if (sck == MIO_SCKHND_INVALID) 
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL && (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)))
		{
			type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
			goto open_socket;
		}
	#endif
		goto oops;
	}
	else
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) goto done;
	#endif
	}

	flags = fcntl(sck, F_GETFD, 0);
	if (flags == -1) goto oops;
#if defined(FD_CLOEXEC)
	flags |= FD_CLOEXEC;
#endif
#if defined(O_NONBLOCK)
	flags |= O_NONBLOCK;
#endif
	if (fcntl(sck, F_SETFD, flags) == -1) goto oops;

done:
	return sck;

oops:
	if (sck != MIO_SCKHND_INVALID) close (sck);
	mio_seterrwithsyserr (mio, 0, errno);
	return MIO_SCKHND_INVALID;
}

int mio_getsckaddrinfo (mio_t* mio, const mio_sckaddr_t* addr, mio_scklen_t* len, mio_sckfam_t* family)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	switch (saddr->sa_family)
	{
		case AF_INET:
			if (len) *len = MIO_SIZEOF(struct sockaddr_in);
			if (family) *family = AF_INET;
			return 0;

		case AF_INET6:
			if (len) *len =  MIO_SIZEOF(struct sockaddr_in6);
			if (family) *family = AF_INET6;
			return 0;

	#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
		case AF_PACKET:
			if (len) *len =  MIO_SIZEOF(struct sockaddr_ll);
			if (family) *family = AF_PACKET;
			return 0;
	#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
		case AF_LINK:
			if (len) *len =  MIO_SIZEOF(struct sockaddr_dl);
			if (family) *family = AF_LINK;
			return 0;
	#endif

		/* TODO: more address type */
	}

	mio->errnum = MIO_EINVAL;
	return -1;
}

mio_uint16_t mio_getsckaddrport (const mio_sckaddr_t* addr)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	switch (saddr->sa_family)
	{
		case AF_INET:
			return mio_ntoh16(((struct sockaddr_in*)addr)->sin_port);

		case AF_INET6:
			return mio_ntoh16(((struct sockaddr_in6*)addr)->sin6_port);
	}

	return 0;
}

int mio_getsckaddrifindex (const mio_sckaddr_t* addr)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	if (saddr->sa_family == AF_PACKET)
	{
		return ((struct sockaddr_ll*)addr)->sll_ifindex;
	}

#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	if (saddr->sa_family == AF_LINK)
	{
		return ((struct sockaddr_dl*)addr)->sdl_index;
	}
#endif

	return 0;
}

int mio_equalsckaddrs (mio_t* mio, const mio_sckaddr_t* addr1, const mio_sckaddr_t* addr2)
{
	mio_sckfam_t fam1, fam2;
	mio_scklen_t len1, len2;

	mio_getsckaddrinfo (mio, addr1, &len1, &fam1);
	mio_getsckaddrinfo (mio, addr2, &len2, &fam2);
	return fam1 == fam2 && len1 == len2 && MIO_MEMCMP(addr1, addr2, len1) == 0;
}

/* ========================================================================= */

void mio_sckaddr_initforip4 (mio_sckaddr_t* sckaddr, mio_uint16_t port, mio_ip4addr_t* ip4addr)
{
	struct sockaddr_in* sin = (struct sockaddr_in*)sckaddr;

	MIO_MEMSET (sin, 0, MIO_SIZEOF(*sin));
	sin->sin_family = AF_INET;
	sin->sin_port = htons(port);
	if (ip4addr) MIO_MEMCPY (&sin->sin_addr, ip4addr, MIO_IP4ADDR_LEN);
}

void mio_sckaddr_initforip6 (mio_sckaddr_t* sckaddr, mio_uint16_t port, mio_ip6addr_t* ip6addr)
{
	struct sockaddr_in6* sin = (struct sockaddr_in6*)sckaddr;

/* TODO: include sin6_scope_id */
	MIO_MEMSET (sin, 0, MIO_SIZEOF(*sin));
	sin->sin6_family = AF_INET;
	sin->sin6_port = htons(port);
	if (ip6addr) MIO_MEMCPY (&sin->sin6_addr, ip6addr, MIO_IP6ADDR_LEN);
}

void mio_sckaddr_initforeth (mio_sckaddr_t* sckaddr, int ifindex, mio_ethaddr_t* ethaddr)
{
#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll* sll = (struct sockaddr_ll*)sckaddr;
	MIO_MEMSET (sll, 0, MIO_SIZEOF(*sll));
	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = ifindex;
	if (ethaddr)
	{
		sll->sll_halen = MIO_ETHADDR_LEN;
		MIO_MEMCPY (sll->sll_addr, ethaddr, MIO_ETHADDR_LEN);
	}

#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	struct sockaddr_dl* sll = (struct sockaddr_dl*)sckaddr;
	MIO_MEMSET (sll, 0, MIO_SIZEOF(*sll));
	sll->sdl_family = AF_LINK;
	sll->sdl_index = ifindex;
	if (ethaddr)
	{
		sll->sdl_alen = MIO_ETHADDR_LEN;
		MIO_MEMCPY (sll->sdl_data, ethaddr, MIO_ETHADDR_LEN);
	}
#else
#	error UNSUPPORTED DATALINK SOCKET ADDRESS
#endif
}

/* ========================================================================= */

static mio_devaddr_t* sckaddr_to_devaddr (mio_dev_sck_t* dev, const mio_sckaddr_t* sckaddr, mio_devaddr_t* devaddr)
{
	if (sckaddr)
	{
		mio_scklen_t len;

		mio_getsckaddrinfo (dev->mio, sckaddr, &len, MIO_NULL);
		devaddr->ptr = (void*)sckaddr;
		devaddr->len = len;
		return devaddr;
	}

	return MIO_NULL;
}

static MIO_INLINE mio_sckaddr_t* devaddr_to_sckaddr (mio_dev_sck_t* dev, const mio_devaddr_t* devaddr, mio_sckaddr_t* sckaddr)
{
	return (mio_sckaddr_t*)devaddr->ptr;
}

/* ========================================================================= */

#define IS_STATEFUL(sck) ((sck)->dev_capa & MIO_DEV_CAPA_STREAM)

struct sck_type_map_t
{
	int domain;
	int type;
	int proto;
	int extra_dev_capa;
};

static struct sck_type_map_t sck_type_map[] =
{
	/* MIO_DEV_SCK_TCP4 */
	{ AF_INET,    SOCK_STREAM,    0,                         MIO_DEV_CAPA_STREAM  | MIO_DEV_CAPA_OUT_QUEUED },

	/* MIO_DEV_SCK_TCP6 */
	{ AF_INET6,   SOCK_STREAM,    0,                         MIO_DEV_CAPA_STREAM  | MIO_DEV_CAPA_OUT_QUEUED },

	/* MIO_DEV_SCK_UPD4 */
	{ AF_INET,    SOCK_DGRAM,     0,                         0                                                },

	/* MIO_DEV_SCK_UDP6 */
	{ AF_INET6,   SOCK_DGRAM,     0,                         0                                                },


#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* MIO_DEV_SCK_ARP - Ethernet type is 2 bytes long. Protocol must be specified in the network byte order */
	{ AF_PACKET,  SOCK_RAW,       MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },

	/* MIO_DEV_SCK_DGRAM */
	{ AF_PACKET,  SOCK_DGRAM,     MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },

#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	/* MIO_DEV_SCK_ARP */
	{ AF_LINK,  SOCK_RAW,         MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },

	/* MIO_DEV_SCK_DGRAM */
	{ AF_LINK,  SOCK_DGRAM,       MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP), 0                                 },
#else
	{ -1,       0,                0,                            0                                             },
	{ -1,       0,                0,                            0                                             },
#endif

	/* MIO_DEV_SCK_ICMP4 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET,    SOCK_RAW,       IPPROTO_ICMP,              0,                                               },

	/* MIO_DEV_SCK_ICMP6 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET6,   SOCK_RAW,       IPPROTO_ICMP,              0,                                               }
};

/* ======================================================================== */

static void connect_timedout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	MIO_ASSERT (mio, IS_STATEFUL(rdev));

	if (rdev->state & MIO_DEV_SCK_CONNECTING)
	{
		/* the state check for MIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job 
		 * doesn't need to be deleted when it gets connected for this check 
		 * here. this libarary, however, deletes the job when it gets 
		 * connected. */
		mio_dev_sck_halt (rdev);
	}
}

static void ssl_accept_timedout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	MIO_ASSERT (mio, IS_STATEFUL(rdev));

	if (rdev->state & MIO_DEV_SCK_ACCEPTING_SSL)
	{
		mio_dev_sck_halt(rdev);
	}
}

static void ssl_connect_timedout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	MIO_ASSERT (mio, IS_STATEFUL(rdev));

	if (rdev->state & MIO_DEV_SCK_CONNECTING_SSL)
	{
		mio_dev_sck_halt(rdev);
	}
}

static int schedule_timer_job_at (mio_dev_sck_t* dev, const mio_ntime_t* fire_at, mio_tmrjob_handler_t handler)
{
	mio_tmrjob_t tmrjob;

	MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = &dev->tmrjob_index;

	MIO_ASSERT (dev->mio, dev->tmrjob_index == MIO_TMRIDX_INVALID);
	dev->tmrjob_index = mio_instmrjob (dev->mio, &tmrjob);
	return dev->tmrjob_index == MIO_TMRIDX_INVALID? -1: 0;
}

static int schedule_timer_job_after (mio_dev_sck_t* dev, const mio_ntime_t* fire_after, mio_tmrjob_handler_t handler)
{
	mio_ntime_t fire_at;

	MIO_ASSERT (dev->mio, MIO_IS_POS_NTIME(fire_after));

	mio_sys_gettime (&fire_at);
	MIO_ADD_NTIME (&fire_at, &fire_at, fire_after);

	return schedule_timer_job_at (dev, &fire_at, handler);
}

/* ======================================================================== */

static int dev_sck_make (mio_dev_t* dev, void* ctx)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_dev_sck_make_t* arg = (mio_dev_sck_make_t*)ctx;

	MIO_ASSERT (dev->mio, arg->type >= 0 && arg->type < MIO_COUNTOF(sck_type_map));

	if (sck_type_map[arg->type].domain <= -1)
	{
		dev->mio->errnum = MIO_ENOIMPL;
		return -1;
	}

	rdev->sck = open_async_socket(dev->mio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
	if (rdev->sck == MIO_SCKHND_INVALID) goto oops;

	rdev->dev_capa = MIO_DEV_CAPA_IN | MIO_DEV_CAPA_OUT | sck_type_map[arg->type].extra_dev_capa;
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	rdev->on_connect = arg->on_connect;
	rdev->on_disconnect = arg->on_disconnect;
	rdev->type = arg->type;
	rdev->tmrjob_index = MIO_TMRIDX_INVALID;

	return 0;

oops:
	if (rdev->sck != MIO_SCKHND_INVALID)
	{
		close_async_socket (rdev->mio, rdev->sck);
		rdev->sck = MIO_SCKHND_INVALID;
	}
	return -1;
}

static int dev_sck_make_client (mio_dev_t* dev, void* ctx)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_syshnd_t* sck = (mio_syshnd_t*)ctx;

	/* create a socket device that is made of a socket connection
	 * on a listening socket.
	 * nothing special is done here except setting the socket handle.
	 * most of the initialization is done by the listening socket device
	 * after a client socket has been created. */

	rdev->sck = *sck;
	rdev->tmrjob_index = MIO_TMRIDX_INVALID;

	if (mio_makesckasync(rdev->mio, rdev->sck) <= -1) return -1;
#if defined(FD_CLOEXEC)
	{
		int flags = fcntl(rdev->sck, F_GETFD, 0);
		if (fcntl(rdev->sck, F_SETFD, flags | FD_CLOEXEC) == -1)
		{
			mio_seterrwithsyserr (rdev->mio, 0, errno);
			return -1;
		}
	}
#endif

	return 0;
}

static int dev_sck_kill (mio_dev_t* dev, int force)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (IS_STATEFUL(rdev))
	{
		/*if (MIO_DEV_SCK_GET_PROGRESS(rdev))
		{*/
			/* for MIO_DEV_SCK_CONNECTING, MIO_DEV_SCK_CONNECTING_SSL, and MIO_DEV_ACCEPTING_SSL
			 * on_disconnect() is called without corresponding on_connect() */
			if (rdev->on_disconnect) rdev->on_disconnect (rdev);
		/*}*/

		if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
		{
			mio_deltmrjob (dev->mio, rdev->tmrjob_index);
			MIO_ASSERT (dev->mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);
		}
	}
	else
	{
		MIO_ASSERT (dev->mio, rdev->state == 0);
		MIO_ASSERT (dev->mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);

		if (rdev->on_disconnect) rdev->on_disconnect (rdev);
	}

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		SSL_shutdown ((SSL*)rdev->ssl); /* is this needed? */
		SSL_free ((SSL*)rdev->ssl);
		rdev->ssl = MIO_NULL;
	}
	if (!(rdev->state & (MIO_DEV_SCK_ACCEPTED | MIO_DEV_SCK_ACCEPTING_SSL)) && rdev->ssl_ctx)
	{
		SSL_CTX_free ((SSL_CTX*)rdev->ssl_ctx);
		rdev->ssl_ctx = MIO_NULL;
	}
#endif

	if (rdev->sck != MIO_SCKHND_INVALID) 
	{
		close_async_socket (rdev->mio, rdev->sck);
		rdev->sck = MIO_SCKHND_INVALID;
	}

	return 0;
}

static mio_syshnd_t dev_sck_getsyshnd (mio_dev_t* dev)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return (mio_syshnd_t)rdev->sck;
}

static int dev_sck_read_stateful (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		x = SSL_read ((SSL*)rdev->ssl, buf, *len);
		if (x <= -1)
		{
			int err = SSL_get_error((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			rdev->mio->errnum = MIO_ESYSERR;
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;

		x = recv (rdev->sck, buf, *len, 0);
		if (x == -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
			if (errno == EINTR) return 0;
			mio_seterrwithsyserr (rdev->mio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

static int dev_sck_read_stateless (mio_dev_t* dev, void* buf, mio_iolen_t* len, mio_devaddr_t* srcaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_scklen_t srcaddrlen;
	ssize_t x;

	srcaddrlen = MIO_SIZEOF(rdev->remoteaddr);
	x = recvfrom (rdev->sck, buf, *len, 0, (struct sockaddr*)&rdev->remoteaddr, &srcaddrlen);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (rdev->mio, 0, errno);
		return -1;
	}

	srcaddr->ptr = &rdev->remoteaddr;
	srcaddr->len = srcaddrlen;

	*len = x;
	return 1;
}


static int dev_sck_write_stateful (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		if (*len <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if (SSL_shutdown ((SSL*)rdev->ssl) == -1)
			{
				rdev->mio->errnum = MIO_ESYSERR;
				return -1;
			}

			return 1;
		}

		x = SSL_write ((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			rdev->mio->errnum = MIO_ESYSERR;
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;
		int flags = 0;

		if (*len <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->sck, SHUT_WR) == -1)
			{
				mio_seterrwithsyserr (rdev->mio, 0, errno);
				return -1;
			}

			return 1;
		}

		/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
	#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
	#endif
		x = send(rdev->sck, data, *len, flags);
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			mio_seterrwithsyserr (rdev->mio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

static int dev_sck_write_stateless (mio_dev_t* dev, const void* data, mio_iolen_t* len, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	ssize_t x;

	x = sendto(rdev->sck, data, *len, 0, dstaddr->ptr, dstaddr->len);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		mio_seterrwithsyserr (rdev->mio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

#if defined(USE_SSL)

static int do_ssl (mio_dev_sck_t* dev, int (*ssl_func)(SSL*))
{
	int ret;
	int watcher_cmd;
	int watcher_events;

	MIO_ASSERT (dev->ssl_ctx);

	if (!dev->ssl)
	{
		SSL* ssl;

		ssl = SSL_new (dev->ssl_ctx);
		if (!ssl)
		{
			dev->mio->errnum = MIO_ESYSERR;
			return -1;
		}

		if (SSL_set_fd (ssl, dev->sck) == 0)
		{
			dev->mio->errnum = MIO_ESYSERR;
			return -1;
		}

		SSL_set_read_ahead (ssl, 0);

		dev->ssl = ssl;
	}

	watcher_cmd = MIO_DEV_WATCH_RENEW;
	watcher_events = 0;

	ret = ssl_func ((SSL*)dev->ssl);
	if (ret <= 0)
	{
		int err = SSL_get_error (dev->ssl, ret);
		if (err == SSL_ERROR_WANT_READ)
		{
			/* handshaking isn't complete */
			ret = 0;
		}
		else if (err == SSL_ERROR_WANT_WRITE)
		{
			/* handshaking isn't complete */
			watcher_cmd = MIO_DEV_WATCH_UPDATE;
			watcher_events = MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT;
			ret = 0;
		}
		else
		{
			dev->mio->errnum = MIO_ESYSERR;
			ret = -1;
		}
	}
	else
	{
		ret = 1; /* accepted */
	}

	if (mio_dev_watch ((mio_dev_t*)dev, watcher_cmd, watcher_events) <= -1)
	{
		mio_stop (dev->mio, MIO_STOPREQ_WATCHER_ERROR);
		ret = -1;
	}

	return ret;
}

static MIO_INLINE int connect_ssl (mio_dev_sck_t* dev)
{
	return do_ssl (dev, SSL_connect);
}

static MIO_INLINE int accept_ssl (mio_dev_sck_t* dev)
{
	return do_ssl (dev, SSL_accept);
}
#endif

static int dev_sck_ioctl (mio_dev_t* dev, int cmd, void* arg)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	mio_t* mio = dev->mio;

	switch (cmd)
	{
		case MIO_DEV_SCK_BIND:
		{
			mio_dev_sck_bind_t* bnd = (mio_dev_sck_bind_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&bnd->localaddr;
			mio_scklen_t sl;
			mio_sckfam_t fam;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = MIO_NULL;
		#endif
			if (MIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't bind again */
				rdev->mio->errnum = MIO_EPERM;
				return -1;
			}

			if (bnd->options & MIO_DEV_SCK_BIND_BROADCAST)
			{
				int v = 1;
				if (setsockopt (rdev->sck, SOL_SOCKET, SO_BROADCAST, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrwithsyserr (rdev->mio, 0, errno);
					return -1;
				}
			}

			if (bnd->options & MIO_DEV_SCK_BIND_REUSEADDR)
			{
			#if defined(SO_REUSEADDR)
				int v = 1;
				if (setsockopt (rdev->sck, SOL_SOCKET, SO_REUSEADDR, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrwithsyserr (rdev->mio, 0, errno);
					return -1;
				}
			#else
				rdev->mio->errnum = MIO_ENOIMPL;
				return -1;
			#endif
			}

			if (bnd->options & MIO_DEV_SCK_BIND_REUSEPORT)
			{
			#if defined(SO_REUSEPORT)
				int v = 1;
				if (setsockopt (rdev->sck, SOL_SOCKET, SO_REUSEPORT, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrwithsyserr (rdev->mio, 0, errno);
					return -1;
				}
			#else
				rdev->mio->errnum = MIO_ENOIMPL;
				return -1;
			#endif
			}

			if (bnd->options & MIO_DEV_SCK_BIND_TRANSPARENT)
			{
			#if defined(IP_TRANSPARENT)
				int v = 1;
				if (setsockopt(rdev->sck, SOL_IP, IP_TRANSPARENT, &v, MIO_SIZEOF(v)) == -1)
				{
					mio_seterrwithsyserr (rdev->mio, 0, errno);
					return -1;
				}
			#else
				rdev->mio->errnum = MIO_ENOIMPL;
				return -1;
			#endif
			}

			if (rdev->ssl_ctx)
			{
			#if defined(USE_SSL)
				SSL_CTX_free (rdev->ssl_ctx);
			#endif
				rdev->ssl_ctx = MIO_NULL;

				if (rdev->ssl)
				{
				#if defined(USE_SSL)
					SSL_free (rdev->ssl);
				#endif
					rdev->ssl = MIO_NULL;
				}
			}

			if (bnd->options & MIO_DEV_SCK_BIND_SSL)
			{
			#if defined(USE_SSL)
				if (!bnd->ssl_certfile || !bnd->ssl_keyfile)
				{
					rdev->mio->errnum = MIO_EINVAL;
					return -1;
				}

				ssl_ctx = SSL_CTX_new(SSLv23_server_method());
				if (!ssl_ctx)
				{
					rdev->mio->errnum = MIO_ESYSERR;
					return -1;
				}

				if (SSL_CTX_use_certificate_file(ssl_ctx, bnd->ssl_certfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_use_PrivateKey_file(ssl_ctx, bnd->ssl_keyfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_check_private_key(ssl_ctx) == 0  /*||
				    SSL_CTX_use_certificate_chain_file (ssl_ctx, bnd->chainfile) == 0*/)
				{
					SSL_CTX_free (ssl_ctx);
					rdev->mio->errnum = MIO_ESYSERR;
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /*SSL_MODE_ENABLE_PARTIAL_WRITE |*/
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

				SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2); /* no outdated SSLv2 by default */

				rdev->tmout = bnd->accept_tmout;
			#else
				rdev->mio->errnum = MIO_ENOIMPL;
				return -1;
			#endif
			}

			if (mio_getsckaddrinfo(dev->mio, &bnd->localaddr, &sl, &fam) <= -1) return -1;

			x = bind(rdev->sck, sa, sl);
			if (x == -1)
			{
				mio_seterrwithsyserr (rdev->mio, 0, errno);
			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free (ssl_ctx);
			#endif
				return -1;
			}

			rdev->localaddr = bnd->localaddr;

		#if defined(USE_SSL)
			rdev->ssl_ctx = ssl_ctx;
		#endif

			return 0;
		}

		case MIO_DEV_SCK_CONNECT:
		{
			mio_dev_sck_connect_t* conn = (mio_dev_sck_connect_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&conn->remoteaddr;
			mio_scklen_t sl;
			mio_sckaddr_t localaddr;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = MIO_NULL;
		#endif

			if (MIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't connect again */
				rdev->mio->errnum = MIO_EPERM;
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				dev->mio->errnum = MIO_ENOCAPA;
				return -1;
			}

			if (sa->sa_family == AF_INET) sl = MIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = MIO_SIZEOF(struct sockaddr_in6);
			else 
			{
				dev->mio->errnum = MIO_EINVAL;
				return -1;
			}

		#if defined(USE_SSL)
			if (rdev->ssl_ctx)
			{
				if (rdev->ssl)
				{
					SSL_free (rdev->ssl);
					rdev->ssl = MIO_NULL;
				}

				SSL_CTX_free (rdev->ssl_ctx);
				rdev->ssl_ctx = MIO_NULL;
			}

			if (conn->options & MIO_DEV_SCK_CONNECT_SSL)
			{
				ssl_ctx = SSL_CTX_new(SSLv23_client_method());
				if (!ssl_ctx)
				{
					rdev->mio->errnum = MIO_ESYSERR;
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /* SSL_MODE_ENABLE_PARTIAL_WRITE | */
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
			}
		#endif
/*{
int flags = fcntl (rdev->sck, F_GETFL);
fcntl (rdev->sck, F_SETFL, flags & ~O_NONBLOCK);
}*/

			/* the socket is already non-blocking */
			x = connect(rdev->sck, sa, sl);
/*{
int flags = fcntl (rdev->sck, F_GETFL);
fcntl (rdev->sck, F_SETFL, flags | O_NONBLOCK);
}*/
			if (x == -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
				{
					if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_UPDATE, MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT) <= -1)
					{
						/* watcher update failure. it's critical */
						mio_stop (rdev->mio, MIO_STOPREQ_WATCHER_ERROR);
						goto oops_connect;
					}
					else
					{
						MIO_INIT_NTIME (&rdev->tmout, 0, 0); /* just in case */

						if (MIO_IS_POS_NTIME(&conn->connect_tmout))
						{
							if (schedule_timer_job_after(rdev, &conn->connect_tmout, connect_timedout) <= -1) 
							{
								goto oops_connect;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								MIO_ASSERT (mio, rdev->tmrjob_index != MIO_TMRIDX_INVALID);
								mio_gettmrjobdeadline (rdev->mio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						rdev->remoteaddr = conn->remoteaddr;
					#if defined(USE_SSL)
						rdev->ssl_ctx = ssl_ctx;
					#endif
						MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTING);
						return 0;
					}
				}

				mio_seterrwithsyserr (rdev->mio, 0, errno);

			oops_connect:
				if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_UPDATE, MIO_DEV_EVENT_IN) <= -1)
				{
					/* watcher update failure. it's critical */
					mio_stop (rdev->mio, MIO_STOPREQ_WATCHER_ERROR);
				}

			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free (ssl_ctx);
			#endif
				return -1;
			}
			else
			{
				/* connected immediately */
				rdev->remoteaddr = conn->remoteaddr;

				sl = MIO_SIZEOF(localaddr);
				if (getsockname(rdev->sck, (struct sockaddr*)&localaddr, &sl) == 0) rdev->localaddr = localaddr;

			#if defined(USE_SSL)
				if (ssl_ctx)
				{
					int x;
					rdev->ssl_ctx = ssl_ctx;

					x = connect_ssl(rdev);
					if (x <= -1) 
					{
						SSL_CTX_free (rdev->ssl_ctx);
						rdev->ssl_ctx = MIO_NULL;

						MIO_ASSERT (rdev->ssl == MIO_NULL);
						return -1;
					}
					if (x == 0) 
					{
						MIO_ASSERT (rdev->tmrjob_index == MIO_TMRIDX_INVALID);
						MIO_INIT_NTIME (&rdev->tmout, 0, 0); /* just in case */

						/* it's ok to use conn->connect_tmout for ssl-connect as
						 * the underlying socket connection has been established immediately */
						if (MIO_IS_POS_NTIME(&conn->connect_tmout))
						{
							if (schedule_timer_job_after(rdev, &conn->connect_tmout, ssl_connect_timedout) <= -1) 
							{
								/* no device halting in spite of failure.
								 * let the caller handle this after having 
								 * checked the return code as it is an IOCTL call. */
								SSL_CTX_free (rdev->ssl_ctx);
								rdev->ssl_ctx = MIO_NULL;

								MIO_ASSERT (rdev->ssl == MIO_NULL);
								return -1;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								MIO_ASSERT (rdev->tmrjob_index != MIO_TMRIDX_INVALID);
								mio_gettmrjobdeadline (rdev->mio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTING_SSL);
					}
					else 
					{
						goto ssl_connected;
					}
				}
				else
				{
				ssl_connected:
			#endif
					MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTED);
					if (rdev->on_connect) rdev->on_connect (rdev);
			#if defined(USE_SSL)
				}
			#endif
				return 0;
			}
		}

		case MIO_DEV_SCK_LISTEN:
		{
			mio_dev_sck_listen_t* lstn = (mio_dev_sck_listen_t*)arg;
			int x;

			if (MIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't listen again */
				rdev->mio->errnum = MIO_EPERM;
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				dev->mio->errnum = MIO_ENOCAPA;
				return -1;
			}

			x = listen (rdev->sck, lstn->backlogs);
			if (x == -1) 
			{
				mio_seterrwithsyserr (rdev->mio, 0, errno);
				return -1;
			}

			MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_LISTENING);
			return 0;
		}
	}

	return 0;
}

static mio_dev_mth_t dev_sck_methods_stateless = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_ioctl,     /* ioctl */
};


static mio_dev_mth_t dev_sck_methods_stateful = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_ioctl,     /* ioctl */
};

static mio_dev_mth_t dev_mth_clisck =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_ioctl
};
/* ========================================================================= */

static int harvest_outgoing_connection (mio_dev_sck_t* rdev)
{
	int errcode;
	mio_scklen_t len;

	MIO_ASSERT (rdev->mio, !(rdev->state & MIO_DEV_SCK_CONNECTED));

	len = MIO_SIZEOF(errcode);
	if (getsockopt(rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
	{
		mio_seterrwithsyserr (rdev->mio, 0, errno);
		return -1;
	}
	else if (errcode == 0)
	{
		mio_sckaddr_t localaddr;
		mio_scklen_t addrlen;

		/* connected */

		if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
		{
			mio_deltmrjob (rdev->mio, rdev->tmrjob_index);
			MIO_ASSERT (rdev->mio, rdev->tmrjob_index == MIO_TMRIDX_INVALID);
		}

		addrlen = MIO_SIZEOF(localaddr);
		if (getsockname(rdev->sck, (struct sockaddr*)&localaddr, &addrlen) == 0) rdev->localaddr = localaddr;

		if (mio_dev_watch((mio_dev_t*)rdev, MIO_DEV_WATCH_RENEW, 0) <= -1) 
		{
			/* watcher update failure. it's critical */
			mio_stop (rdev->mio, MIO_STOPREQ_WATCHER_ERROR);
			return -1;
		}

	#if defined(USE_SSL)
		if (rdev->ssl_ctx)
		{
			int x;
			MIO_ASSERT (!rdev->ssl); /* must not be SSL-connected yet */

			x = connect_ssl (rdev);
			if (x <= -1) return -1;
			if (x == 0)
			{
				/* underlying socket connected but not SSL-connected */
				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTING_SSL);

				MIO_ASSERT (rdev->tmrjob_index == MIO_TMRIDX_INVALID);

				/* rdev->tmout has been set to the deadline of the connect task
				 * when the CONNECT IOCTL command has been executed. use the 
				 * same deadline here */
				if (MIO_IS_POS_NTIME(&rdev->tmout) &&
				    schedule_timer_job_at(rdev, &rdev->tmout, ssl_connect_timedout) <= -1)
				{
					mio_dev_halt ((mio_dev_t*)rdev);
				}

				return 0;
			}
			else
			{
				goto ssl_connected;
			}
		}
		else
		{
		ssl_connected:
	#endif
			MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTED);
			if (rdev->on_connect) rdev->on_connect (rdev);
	#if defined(USE_SSL)
		}
	#endif

		return 0;
	}
	else if (errcode == EINPROGRESS || errcode == EWOULDBLOCK)
	{
		/* still in progress */
		return 0;
	}
	else
	{
		mio_seterrwithsyserr (rdev->mio, 0, errcode);
		return -1;
	}
}

static int accept_incoming_connection (mio_dev_sck_t* rdev)
{
	mio_t* mio = rdev->mio;
	mio_sckhnd_t clisck;
	mio_sckaddr_t remoteaddr;
	mio_scklen_t addrlen;
	mio_dev_sck_t* clidev;
	int flags;

	/* this is a server(lisening) socket */

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
	flags = SOCK_NONBLOCK | SOCK_CLOEXEC;

	addrlen = MIO_SIZEOF(remoteaddr);
	clisck = accept4(rdev->sck, (struct sockaddr*)&remoteaddr, &addrlen, flags);
	if (clisck == MIO_SCKHND_INVALID)
	{
		 if (errno != ENOSYS)
		 {
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
			if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

			mio_seterrwithsyserr (rdev->mio, 0, errno);
			return -1;
		 }

		 // go on for the normal 3-parameter accept
	}
	else
	{
		 goto accept_done;
	}

#endif
	addrlen = MIO_SIZEOF(remoteaddr);
	clisck = accept(rdev->sck, (struct sockaddr*)&remoteaddr, &addrlen);
	if (clisck == MIO_SCKHND_INVALID)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
		if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

		mio_seterrwithsyserr (rdev->mio, 0, errno);
		return -1;
	}


accept_done:
	/* use rdev->dev_size when instantiating a client sck device
	 * instead of MIO_SIZEOF(mio_dev_sck_t). therefore, the 
	 * extension area as big as that of the master sck device
	 * is created in the client sck device */
	clidev = (mio_dev_sck_t*)mio_makedev(rdev->mio, rdev->dev_size, &dev_mth_clisck, rdev->dev_evcb, &clisck); 
	if (!clidev) 
	{
		close (clisck);
		return -1;
	}

	MIO_ASSERT (mio, clidev->sck == clisck);

	clidev->dev_capa |= MIO_DEV_CAPA_IN | MIO_DEV_CAPA_OUT | MIO_DEV_CAPA_STREAM | MIO_DEV_CAPA_OUT_QUEUED;
	clidev->remoteaddr = remoteaddr;

	addrlen = MIO_SIZEOF(clidev->localaddr);
	if (getsockname(clisck, (struct sockaddr*)&clidev->localaddr, &addrlen) == -1) clidev->localaddr = rdev->localaddr;

#if defined(SO_ORIGINAL_DST)
	/* if REDIRECT is used, SO_ORIGINAL_DST returns the original
	 * destination address. When REDIRECT is not used, it returnes
	 * the address of the local socket. In this case, it should
	 * be same as the result of getsockname(). */
	addrlen = MIO_SIZEOF(clidev->orgdstaddr);
	if (getsockopt (clisck, SOL_IP, SO_ORIGINAL_DST, &clidev->orgdstaddr, &addrlen) == -1) clidev->orgdstaddr = rdev->localaddr;
#else
	clidev->orgdstaddr = rdev->localaddr;
#endif

	if (!mio_equalsckaddrs (rdev->mio, &clidev->orgdstaddr, &clidev->localaddr))
	{
		clidev->state |= MIO_DEV_SCK_INTERCEPTED;
	}
	else if (mio_getsckaddrport (&clidev->localaddr) != mio_getsckaddrport(&rdev->localaddr))
	{
		/* When TPROXY is used, getsockname() and SO_ORIGNAL_DST return
		 * the same addresses. however, the port number may be different
		 * as a typical TPROXY rule is set to change the port number.
		 * However, this check is fragile if the server port number is
		 * set to 0.
		 *
		 * Take note that the above assumption gets wrong if the TPROXY
		 * rule doesn't change the port number. so it won't be able
		 * to handle such a TPROXYed packet without port transformation. */
		clidev->state |= MIO_DEV_SCK_INTERCEPTED;
	}
	#if 0
	else if ((clidev->initial_ifindex = resolve_ifindex(fd, clidev->localaddr)) <= -1)
	{
		/* the local_address is not one of a local address.
		 * it's probably proxied. */
		clidev->state |= MIO_DEV_SCK_INTERCEPTED;
	}
	#endif

	/* inherit some event handlers from the parent.
	 * you can still change them inside the on_connect handler */
	clidev->on_connect = rdev->on_connect;
	clidev->on_disconnect = rdev->on_disconnect; 
	clidev->on_write = rdev->on_write;
	clidev->on_read = rdev->on_read;

	MIO_ASSERT (mio, clidev->tmrjob_index == MIO_TMRIDX_INVALID);

	if (rdev->ssl_ctx)
	{
		MIO_DEV_SCK_SET_PROGRESS (clidev, MIO_DEV_SCK_ACCEPTING_SSL);
		MIO_ASSERT (mio, clidev->state & MIO_DEV_SCK_ACCEPTING_SSL);
		/* actual SSL acceptance must be completed in the client device */

		/* let the client device know the SSL context to use */
		clidev->ssl_ctx = rdev->ssl_ctx;

		if (MIO_IS_POS_NTIME(&rdev->tmout) &&
		    schedule_timer_job_after (clidev, &rdev->tmout, ssl_accept_timedout) <= -1)
		{
			/* TODO: call a warning/error callback */
			/* timer job scheduling failed. halt the device */
			mio_dev_halt ((mio_dev_t*)clidev);
		}
	}
	else
	{
		MIO_DEV_SCK_SET_PROGRESS (clidev, MIO_DEV_SCK_ACCEPTED);
		/*if (clidev->on_connect(clidev) <= -1) mio_dev_sck_halt (clidev);*/
		if (clidev->on_connect) clidev->on_connect (clidev);
	}

	return 0;
}

static int dev_evcb_sck_ready_stateful (mio_dev_t* dev, int events)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (events & MIO_DEV_EVENT_ERR)
	{
		int errcode;
		mio_scklen_t len;

		len = MIO_SIZEOF(errcode);
		if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error MIO_EDEVERRR is used */
			rdev->mio->errnum = MIO_EDEVERR;
		}
		else
		{
			mio_seterrwithsyserr (rdev->mio, 0, errcode);
		}
		return -1;
	}

	/* this socket can connect */
	switch (MIO_DEV_SCK_GET_PROGRESS(rdev))
	{
		case MIO_DEV_SCK_CONNECTING:
			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->mio->errnum = MIO_EDEVHUP;
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN))
			{
				/* invalid event masks. generic device error */
				rdev->mio->errnum = MIO_EDEVERR;
				return -1;
			}
			else if (events & MIO_DEV_EVENT_OUT)
			{
				/* when connected, the socket becomes writable */
				return harvest_outgoing_connection (rdev);
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case MIO_DEV_SCK_CONNECTING_SSL:
		#if defined(USE_SSL)
			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->mio->errnum = MIO_EDEVHUP;
				return -1;
			}
			else if (events & MIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				rdev->mio->errnum = MIO_EDEVERR;
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT))
			{
				int x;

				x = connect_ssl (rdev);
				if (x <= -1) return -1;
				if (x == 0) return 0; /* not SSL-Connected */

				if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
				{
					mio_deltmrjob (rdev->mio, rdev->tmrjob_index);
					rdev->tmrjob_index = MIO_TMRIDX_INVALID;
				}

				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_CONNECTED);
				if (rdev->on_connect) rdev->on_connect (rdev);
				return 0;
			}
			else
			{
				return 0; /* success. no actual I/O yet */
			}
		#else
			rdev->mio->errnum = MIO_EINTERN;
			return -1;
		#endif

		case MIO_DEV_SCK_LISTENING:

			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->mio->errnum = MIO_EDEVHUP;
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_OUT))
			{
				rdev->mio->errnum = MIO_EDEVERR;
				return -1;
			}
			else if (events & MIO_DEV_EVENT_IN)
			{
				return accept_incoming_connection (rdev);
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case MIO_DEV_SCK_ACCEPTING_SSL:
		#if defined(USE_SSL)
			if (events & MIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->mio->errnum = MIO_EDEVHUP;
				return -1;
			}
			else if (events & MIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				rdev->mio->errnum = MIO_EDEVERR;
				return -1;
			}
			else if (events & (MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT))
			{
				int x;
				x = accept_ssl (rdev);
				if (x <= -1) return -1;
				if (x <= 0) return 0; /* not SSL-accepted yet */

				if (rdev->tmrjob_index != MIO_TMRIDX_INVALID)
				{
					mio_deltmrjob (rdev->mio, rdev->tmrjob_index);
					rdev->tmrjob_index = MIO_TMRIDX_INVALID;
				}

				MIO_DEV_SCK_SET_PROGRESS (rdev, MIO_DEV_SCK_ACCEPTED);
				/*if (rdev->on_connect(rdev) <= -1) mio_dev_sck_halt (rdev);*/
				if (rdev->on_connect) rdev->on_connect (rdev);

				return 0;
			}
			else
			{
				return 0; /* no reading or writing yet */
			}
		#else
			rdev->mio->errnum = MIO_EINTERN;
			return -1;
		#endif


		default:
			if (events & MIO_DEV_EVENT_HUP)
			{
				if (events & (MIO_DEV_EVENT_PRI | MIO_DEV_EVENT_IN | MIO_DEV_EVENT_OUT)) 
				{
					/* probably half-open? */
					return 1;
				}

				rdev->mio->errnum = MIO_EDEVHUP;
				return -1;
			}

			return 1; /* the device is ok. carry on reading or writing */
	}
}

static int dev_evcb_sck_ready_stateless (mio_dev_t* dev, int events)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;

	if (events & MIO_DEV_EVENT_ERR)
	{
		int errcode;
		mio_scklen_t len;

		len = MIO_SIZEOF(errcode);
		if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error MIO_EDEVERRR is used */
			rdev->mio->errnum = MIO_EDEVERR;
		}
		else
		{
			mio_seterrwithsyserr (rdev->mio, 0, errcode);
		}
		return -1;
	}
	else if (events & MIO_DEV_EVENT_HUP)
	{
		rdev->mio->errnum = MIO_EDEVHUP;
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read_stateful (mio_dev_t* dev, const void* data, mio_iolen_t dlen, const mio_devaddr_t* srcaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_read (rdev, data, dlen, MIO_NULL);
}

static int dev_evcb_sck_on_write_stateful (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_write (rdev, wrlen, wrctx, MIO_NULL);
}

static int dev_evcb_sck_on_read_stateless (mio_dev_t* dev, const void* data, mio_iolen_t dlen, const mio_devaddr_t* srcaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_read (rdev, data, dlen, srcaddr->ptr);
}

static int dev_evcb_sck_on_write_stateless (mio_dev_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_devaddr_t* dstaddr)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)dev;
	return rdev->on_write (rdev, wrlen, wrctx, dstaddr->ptr);
}

static mio_dev_evcb_t dev_sck_event_callbacks_stateful =
{
	dev_evcb_sck_ready_stateful,
	dev_evcb_sck_on_read_stateful,
	dev_evcb_sck_on_write_stateful
};

static mio_dev_evcb_t dev_sck_event_callbacks_stateless =
{
	dev_evcb_sck_ready_stateless,
	dev_evcb_sck_on_read_stateless,
	dev_evcb_sck_on_write_stateless
};

/* ========================================================================= */

mio_dev_sck_t* mio_dev_sck_make (mio_t* mio, mio_oow_t xtnsize, const mio_dev_sck_make_t* info)
{
	mio_dev_sck_t* rdev;

	if (info->type < 0 && info->type >= MIO_COUNTOF(sck_type_map))
	{
		mio->errnum = MIO_EINVAL;
		return MIO_NULL;
	}

	if (sck_type_map[info->type].extra_dev_capa & MIO_DEV_CAPA_STREAM) /* can't use the IS_STATEFUL() macro yet */
	{
		rdev = (mio_dev_sck_t*)mio_makedev(
			mio, MIO_SIZEOF(mio_dev_sck_t) + xtnsize, 
			&dev_sck_methods_stateful, &dev_sck_event_callbacks_stateful, (void*)info);
	}
	else
	{
		rdev = (mio_dev_sck_t*)mio_makedev(
			mio, MIO_SIZEOF(mio_dev_sck_t) + xtnsize,
			&dev_sck_methods_stateless, &dev_sck_event_callbacks_stateless, (void*)info);
	}

	return rdev;
}

int mio_dev_sck_bind (mio_dev_sck_t* dev, mio_dev_sck_bind_t* info)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_SCK_BIND, info);
}

int mio_dev_sck_connect (mio_dev_sck_t* dev, mio_dev_sck_connect_t* info)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_SCK_CONNECT, info);
}

int mio_dev_sck_listen (mio_dev_sck_t* dev, mio_dev_sck_listen_t* info)
{
	return mio_dev_ioctl((mio_dev_t*)dev, MIO_DEV_SCK_LISTEN, info);
}

int mio_dev_sck_write (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	mio_devaddr_t devaddr;
	return mio_dev_write((mio_dev_t*)dev, data, dlen, wrctx, sckaddr_to_devaddr(dev, dstaddr, &devaddr));
}

int mio_dev_sck_timedwrite (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_ntime_t* tmout, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	mio_devaddr_t devaddr;
	return mio_dev_timedwrite((mio_dev_t*)dev, data, dlen, tmout, wrctx, sckaddr_to_devaddr(dev, dstaddr, &devaddr));
}


/* ========================================================================= */

mio_uint16_t mio_checksumip (const void* hdr, mio_oow_t len)
{
	mio_uint32_t sum = 0;
	mio_uint16_t *ptr = (mio_uint16_t*)hdr;

	
	while (len > 1)
	{
		sum += *ptr++;
		if (sum & 0x80000000)
		sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}
 
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

	return (mio_uint16_t)~sum;
}
