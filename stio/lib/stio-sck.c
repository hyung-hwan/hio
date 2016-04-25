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


#include "stio-sck.h"
#include "stio-prv.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netpacket/packet.h>

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
void stio_closeasyncsck (stio_t* stio, stio_sckhnd_t sck)
{
#if defined(_WIN32)
	closesocket (sck);
#else
	close (sck);
#endif
}

int stio_makesckasync (stio_t* stio, stio_sckhnd_t sck)
{
	return stio_makesyshndasync (stio, (stio_syshnd_t)sck);
}

stio_sckhnd_t stio_openasyncsck (stio_t* stio, int domain, int type, int proto)
{
	stio_sckhnd_t sck;

#if defined(_WIN32)
	sck = WSASocket (domain, type, proto, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
	if (sck == STIO_SCKHND_INVALID) 
	{
		/* stio_seterrnum (dev->stio, STIO_ESYSERR); or translate errno to stio errnum */
		return STIO_SCKHND_INVALID;
	}
#else
	sck = socket (domain, type, proto); 
	if (sck == STIO_SCKHND_INVALID) 
	{
		stio->errnum = stio_syserrtoerrnum(errno);
		return STIO_SCKHND_INVALID;
	}

#if defined(FD_CLOEXEC)
	{
		int flags = fcntl (sck, F_GETFD, 0);
		if (fcntl (sck, F_SETFD, flags | FD_CLOEXEC) == -1)
		{
			stio->errnum = stio_syserrtoerrnum(errno);
			return STIO_SCKHND_INVALID;
		}
	}
#endif

	if (stio_makesckasync (stio, sck) <= -1)
	{
		close (sck);
		return STIO_SCKHND_INVALID;
	}

#endif

	return sck;
}

int stio_getsckaddrinfo (stio_t* stio, const stio_sckaddr_t* addr, stio_scklen_t* len, stio_sckfam_t* family)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	switch (saddr->sa_family)
	{
		case AF_INET:
			if (len) *len = STIO_SIZEOF(struct sockaddr_in);
			if (family) *family = AF_INET;
			return 0;

		case AF_INET6:
			if (len) *len =  STIO_SIZEOF(struct sockaddr_in6);
			if (family) *family = AF_INET6;
			return 0;

		case AF_PACKET:
			if (len) *len =  STIO_SIZEOF(struct sockaddr_ll);
			if (family) *family = AF_PACKET;
			return 0;

		/* TODO: more address type */
	}

	stio->errnum = STIO_EINVAL;
	return -1;
}

stio_uint16_t stio_getsckaddrport (const stio_sckaddr_t* addr)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	switch (saddr->sa_family)
	{
		case AF_INET:
			return stio_ntoh16(((struct sockaddr_in*)addr)->sin_port);

		case AF_INET6:
			return stio_ntoh16(((struct sockaddr_in6*)addr)->sin6_port);
	}

	return 0;
}


int stio_equalsckaddrs (stio_t* stio, const stio_sckaddr_t* addr1, const stio_sckaddr_t* addr2)
{
	stio_sckfam_t fam1, fam2;
	stio_scklen_t len1, len2;

	stio_getsckaddrinfo (stio, addr1, &len1, &fam1);
	stio_getsckaddrinfo (stio, addr2, &len2, &fam2);
	return fam1 == fam2 && len1 == len2 && STIO_MEMCMP (addr1, addr2, len1) == 0;
}

/* ========================================================================= */

void stio_sckaddr_initforip4 (stio_sckaddr_t* sckaddr, stio_uint16_t port, stio_ip4addr_t* ip4addr)
{
	struct sockaddr_in* sin = (struct sockaddr_in*)sckaddr;

	STIO_MEMSET (sin, 0, STIO_SIZEOF(*sin));
	sin->sin_family = AF_INET;
	sin->sin_port = htons(port);
	if (ip4addr) STIO_MEMCPY (&sin->sin_addr, ip4addr, STIO_IP4ADDR_LEN);
}

void stio_sckaddr_initforip6 (stio_sckaddr_t* sckaddr, stio_uint16_t port, stio_ip6addr_t* ip6addr)
{
	struct sockaddr_in6* sin = (struct sockaddr_in6*)sckaddr;

/* TODO: include sin6_scope_id */
	STIO_MEMSET (sin, 0, STIO_SIZEOF(*sin));
	sin->sin6_family = AF_INET;
	sin->sin6_port = htons(port);
	if (ip6addr) STIO_MEMCPY (&sin->sin6_addr, ip6addr, STIO_IP6ADDR_LEN);
}

void stio_sckaddr_initforeth (stio_sckaddr_t* sckaddr, int ifindex, stio_ethaddr_t* ethaddr)
{
	struct sockaddr_ll* sll = (struct sockaddr_ll*)sckaddr;
	STIO_MEMSET (sll, 0, STIO_SIZEOF(*sll));
	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = ifindex;
	if (ethaddr)
	{
		sll->sll_halen = STIO_ETHADDR_LEN;
		STIO_MEMCPY (sll->sll_addr, ethaddr, STIO_ETHADDR_LEN);
	}
}

/* ========================================================================= */

static stio_devaddr_t* sckaddr_to_devaddr (stio_dev_sck_t* dev, const stio_sckaddr_t* sckaddr, stio_devaddr_t* devaddr)
{
	if (sckaddr)
	{
		stio_scklen_t len;

		stio_getsckaddrinfo (dev->stio, sckaddr, &len, STIO_NULL);
		devaddr->ptr = (void*)sckaddr;
		devaddr->len = len;
		return devaddr;
	}

	return STIO_NULL;
}

static STIO_INLINE stio_sckaddr_t* devaddr_to_sckaddr (stio_dev_sck_t* dev, const stio_devaddr_t* devaddr, stio_sckaddr_t* sckaddr)
{
	return (stio_sckaddr_t*)devaddr->ptr;
}

/* ========================================================================= */

#define IS_STATEFUL(sck) ((sck)->dev_capa & STIO_DEV_CAPA_STREAM)

struct sck_type_map_t
{
	int domain;
	int type;
	int proto;
	int extra_dev_capa;
};

static struct sck_type_map_t sck_type_map[] =
{
	/* STIO_DEV_SCK_TCP4 */
	{ AF_INET,    SOCK_STREAM,    0,                         STIO_DEV_CAPA_STREAM  | STIO_DEV_CAPA_OUT_QUEUED },

	/* STIO_DEV_SCK_TCP6 */
	{ AF_INET6,   SOCK_STREAM,    0,                         STIO_DEV_CAPA_STREAM  | STIO_DEV_CAPA_OUT_QUEUED },

	/* STIO_DEV_SCK_UPD4 */
	{ AF_INET,    SOCK_DGRAM,     0,                         0                                                },

	/* STIO_DEV_SCK_UDP6 */
	{ AF_INET6,   SOCK_DGRAM,     0,                         0                                                },

	/* STIO_DEV_SCK_ARP - Ethernet type is 2 bytes long. Protocol must be specified in the network byte order */
	{ AF_PACKET,  SOCK_RAW,       STIO_CONST_HTON16(STIO_ETHHDR_PROTO_ARP), 0                                 },

	/* STIO_DEV_SCK_DGRAM */
	{ AF_PACKET,  SOCK_DGRAM,     STIO_CONST_HTON16(STIO_ETHHDR_PROTO_ARP), 0                                 },

	/* STIO_DEV_SCK_ICMP4 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET,    SOCK_RAW,       IPPROTO_ICMP,              0,                                               },

	/* STIO_DEV_SCK_ICMP6 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET6,   SOCK_RAW,       IPPROTO_ICMP,              0,                                               }
};

/* ======================================================================== */

static void connect_timedout (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)job->ctx;

	STIO_ASSERT (IS_STATEFUL(rdev));

	if (rdev->state & STIO_DEV_SCK_CONNECTING)
	{
		/* the state check for STIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job 
		 * doesn't need to be deleted when it gets connected for this check 
		 * here. this libarary, however, deletes the job when it gets 
		 * connected. */
		stio_dev_sck_halt (rdev);
	}
}

static void ssl_accept_timedout (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)job->ctx;

	STIO_ASSERT (IS_STATEFUL(rdev));

	if (rdev->state & STIO_DEV_SCK_ACCEPTING_SSL)
	{
		stio_dev_sck_halt(rdev);
	}
}

static void ssl_connect_timedout (stio_t* stio, const stio_ntime_t* now, stio_tmrjob_t* job)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)job->ctx;

	STIO_ASSERT (IS_STATEFUL(rdev));

	if (rdev->state & STIO_DEV_SCK_CONNECTING_SSL)
	{
		stio_dev_sck_halt(rdev);
	}
}

static int schedule_timer_job_at (stio_dev_sck_t* dev, const stio_ntime_t* fire_at, stio_tmrjob_handler_t handler)
{
	stio_tmrjob_t tmrjob;

	STIO_MEMSET (&tmrjob, 0, STIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = &dev->tmrjob_index;

	STIO_ASSERT (dev->tmrjob_index == STIO_TMRIDX_INVALID);
	dev->tmrjob_index = stio_instmrjob (dev->stio, &tmrjob);
	return dev->tmrjob_index == STIO_TMRIDX_INVALID? -1: 0;
}

static int schedule_timer_job_after (stio_dev_sck_t* dev, const stio_ntime_t* fire_after, stio_tmrjob_handler_t handler)
{
	stio_ntime_t fire_at;

	STIO_ASSERT (stio_ispostime(fire_after));

	stio_gettime (&fire_at);
	stio_addtime (&fire_at, fire_after, &fire_at);

	return schedule_timer_job_at (dev, &fire_at, handler);
}

/* ======================================================================== */

static int dev_sck_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_dev_sck_make_t* arg = (stio_dev_sck_make_t*)ctx;

	STIO_ASSERT (arg->type >= 0 && arg->type < STIO_COUNTOF(sck_type_map));

	rdev->sck = stio_openasyncsck (dev->stio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
	if (rdev->sck == STIO_SCKHND_INVALID) goto oops;

	rdev->dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT | sck_type_map[arg->type].extra_dev_capa;
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	rdev->on_disconnect = arg->on_disconnect;
	rdev->type = arg->type;
	rdev->tmrjob_index = STIO_TMRIDX_INVALID;

	return 0;

oops:
	if (rdev->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (rdev->stio, rdev->sck);
		rdev->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static int dev_sck_make_client (stio_dev_t* dev, void* ctx)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_syshnd_t* sck = (stio_syshnd_t*)ctx;

	/* nothing special is done here except setting the sock et handle.
	 * most of the initialization is done by the listening socket device
	 * after a client socket has been created. */

	rdev->sck = *sck;
	rdev->tmrjob_index = STIO_TMRIDX_INVALID;

	if (stio_makesckasync (rdev->stio, rdev->sck) <= -1) return -1;
#if defined(FD_CLOEXEC)
	{
		int flags = fcntl (rdev->sck, F_GETFD, 0);
		if (fcntl (rdev->sck, F_SETFD, flags | FD_CLOEXEC) == -1)
		{
			rdev->stio->errnum = stio_syserrtoerrnum(errno);
			return -1;
		}
	}
#endif

	return 0;
}

static int dev_sck_kill (stio_dev_t* dev, int force)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	if (IS_STATEFUL(rdev))
	{
		/*if (STIO_DEV_SCK_GET_PROGRESS(rdev))
		{*/
			/* for STIO_DEV_SCK_CONNECTING, STIO_DEV_SCK_CONNECTING_SSL, and STIO_DEV_ACCEPTING_SSL
			 * on_disconnect() is called without corresponding on_connect() */
			if (rdev->on_disconnect) rdev->on_disconnect (rdev);
		/*}*/

		if (rdev->tmrjob_index != STIO_TMRIDX_INVALID)
		{
			stio_deltmrjob (dev->stio, rdev->tmrjob_index);
			STIO_ASSERT (rdev->tmrjob_index == STIO_TMRIDX_INVALID);
		}
	}
	else
	{
		STIO_ASSERT (rdev->state == 0);
		STIO_ASSERT (rdev->tmrjob_index == STIO_TMRIDX_INVALID);

		if (rdev->on_disconnect) rdev->on_disconnect (rdev);
	}

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		SSL_shutdown ((SSL*)rdev->ssl); /* is this needed? */
		SSL_free ((SSL*)rdev->ssl);
		rdev->ssl = STIO_NULL;
	}
	if (!(rdev->state & (STIO_DEV_SCK_ACCEPTED | STIO_DEV_SCK_ACCEPTING_SSL)) && rdev->ssl_ctx)
	{
		SSL_CTX_free ((SSL_CTX*)rdev->ssl_ctx);
		rdev->ssl_ctx = STIO_NULL;
	}
#endif

	if (rdev->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (rdev->stio, rdev->sck);
		rdev->sck = STIO_SCKHND_INVALID;
	}

	return 0;
}

static stio_syshnd_t dev_sck_getsyshnd (stio_dev_t* dev)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return (stio_syshnd_t)rdev->sck;
}

static int dev_sck_read_stateful (stio_dev_t* dev, void* buf, stio_iolen_t* len, stio_devaddr_t* srcaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		x = SSL_read ((SSL*)rdev->ssl, buf, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			rdev->stio->errnum = STIO_ESYSERR;
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
			rdev->stio->errnum = stio_syserrtoerrnum(errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

static int dev_sck_read_stateless (stio_dev_t* dev, void* buf, stio_iolen_t* len, stio_devaddr_t* srcaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_scklen_t srcaddrlen;
	ssize_t x;

	srcaddrlen = STIO_SIZEOF(rdev->remoteaddr);
	x = recvfrom (rdev->sck, buf, *len, 0, (struct sockaddr*)&rdev->remoteaddr, &srcaddrlen);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	srcaddr->ptr = &rdev->remoteaddr;
	srcaddr->len = srcaddrlen;

	*len = x;
	return 1;
}


static int dev_sck_write_stateful (stio_dev_t* dev, const void* data, stio_iolen_t* len, const stio_devaddr_t* dstaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

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
				rdev->stio->errnum = STIO_ESYSERR;
				return -1;
			}

			return 1;
		}

		x = SSL_write ((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			rdev->stio->errnum = STIO_ESYSERR;
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
			if (shutdown (rdev->sck, SHUT_WR) == -1)
			{
				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			return 1;
		}

		/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
	#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
	#endif
		x = send (rdev->sck, data, *len, flags);
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			rdev->stio->errnum = stio_syserrtoerrnum(errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

static int dev_sck_write_stateless (stio_dev_t* dev, const void* data, stio_iolen_t* len, const stio_devaddr_t* dstaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	ssize_t x;

	x = sendto (rdev->sck, data, *len, 0, dstaddr->ptr, dstaddr->len);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

#if defined(USE_SSL)

static int do_ssl (stio_dev_sck_t* dev, int (*ssl_func)(SSL*))
{
	int ret;
	int watcher_cmd;
	int watcher_events;

	STIO_ASSERT (dev->ssl_ctx);

	if (!dev->ssl)
	{
		SSL* ssl;

		ssl = SSL_new (dev->ssl_ctx);
		if (!ssl)
		{
			dev->stio->errnum = STIO_ESYSERR;
			return -1;
		}

		if (SSL_set_fd (ssl, dev->sck) == 0)
		{
			dev->stio->errnum = STIO_ESYSERR;
			return -1;
		}

		SSL_set_read_ahead (ssl, 0);

		dev->ssl = ssl;
	}

	watcher_cmd = STIO_DEV_WATCH_RENEW;
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
			watcher_cmd = STIO_DEV_WATCH_UPDATE;
			watcher_events = STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT;
			ret = 0;
		}
		else
		{
			dev->stio->errnum = STIO_ESYSERR;
			ret = -1;
		}
	}
	else
	{
		ret = 1; /* accepted */
	}

	if (stio_dev_watch ((stio_dev_t*)dev, watcher_cmd, watcher_events) <= -1)
	{
		stio_stop (dev->stio, STIO_STOPREQ_WATCHER_ERROR);
		ret = -1;
	}

	return ret;
}

static STIO_INLINE int connect_ssl (stio_dev_sck_t* dev)
{
	return do_ssl (dev, SSL_connect);
}

static STIO_INLINE int accept_ssl (stio_dev_sck_t* dev)
{
	return do_ssl (dev, SSL_accept);
}
#endif

static int dev_sck_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	switch (cmd)
	{
		case STIO_DEV_SCK_BIND:
		{
			stio_dev_sck_bind_t* bnd = (stio_dev_sck_bind_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&bnd->localaddr;
			stio_scklen_t sl;
			stio_sckfam_t fam;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = STIO_NULL;
		#endif
			if (STIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't bind again */
				rdev->stio->errnum = STIO_EPERM;
				return -1;
			}

			if (bnd->options & STIO_DEV_SCK_BIND_BROADCAST)
			{
				int v = 1;
				if (setsockopt (rdev->sck, SOL_SOCKET, SO_BROADCAST, &v, STIO_SIZEOF(v)) == -1)
				{
					rdev->stio->errnum = stio_syserrtoerrnum(errno);
					return -1;
				}
			}

			if (bnd->options & STIO_DEV_SCK_BIND_REUSEADDR)
			{
			#if defined(SO_REUSEADDR)
				int v = 1;
				if (setsockopt (rdev->sck, SOL_SOCKET, SO_REUSEADDR, &v, STIO_SIZEOF(v)) == -1)
				{
					rdev->stio->errnum = stio_syserrtoerrnum(errno);
					return -1;
				}
			#else
				rdev->stio->errnum = STIO_ENOIMPL;
				return -1;
			#endif
			}

			if (bnd->options & STIO_DEV_SCK_BIND_REUSEPORT)
			{
			#if defined(SO_REUSEPORT)
				int v = 1;
				if (setsockopt (rdev->sck, SOL_SOCKET, SO_REUSEPORT, &v, STIO_SIZEOF(v)) == -1)
				{
					rdev->stio->errnum = stio_syserrtoerrnum(errno);
					return -1;
				}
			#else
				rdev->stio->errnum = STIO_ENOIMPL;
				return -1;
			#endif
			}

			if (bnd->options & STIO_DEV_SCK_BIND_TRANSPARENT)
			{
			#if defined(IP_TRANSPARENT)
				int v = 1;
				if (setsockopt (rdev->sck, SOL_IP, IP_TRANSPARENT, &v, STIO_SIZEOF(v)) == -1)
				{
					rdev->stio->errnum = stio_syserrtoerrnum(errno);
					return -1;
				}
			#else
				rdev->stio->errnum = STIO_ENOIMPL;
				return -1;
			#endif
			}

			if (rdev->ssl_ctx)
			{
				SSL_CTX_free (rdev->ssl_ctx);
				rdev->ssl_ctx = STIO_NULL;

				if (rdev->ssl)
				{
					SSL_free (rdev->ssl);
					rdev->ssl = STIO_NULL;
				}
			}

			if (bnd->options & STIO_DEV_SCK_BIND_SSL)
			{
			#if defined(USE_SSL)
				if (!bnd->ssl_certfile || !bnd->ssl_keyfile)
				{
					rdev->stio->errnum = STIO_EINVAL;
					return -1;
				}

				ssl_ctx = SSL_CTX_new (SSLv23_server_method());
				if (!ssl_ctx)
				{
					rdev->stio->errnum = STIO_ESYSERR;
					return -1;
				}

				if (SSL_CTX_use_certificate_file (ssl_ctx, bnd->ssl_certfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_use_PrivateKey_file (ssl_ctx, bnd->ssl_keyfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_check_private_key (ssl_ctx) == 0  /*||
				    SSL_CTX_use_certificate_chain_file (ssl_ctx, bnd->chainfile) == 0*/)
				{
					SSL_CTX_free (ssl_ctx);
					rdev->stio->errnum = STIO_ESYSERR;
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /*SSL_MODE_ENABLE_PARTIAL_WRITE |*/
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

				SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2); /* no outdated SSLv2 by default */

				rdev->tmout = bnd->accept_tmout;
			#else
				rdev->stio->errnum = STIO_ENOIMPL;
				return -1;
			#endif
			}

			if (stio_getsckaddrinfo (dev->stio, &bnd->localaddr, &sl, &fam) <= -1) return -1;

			x = bind (rdev->sck, sa, sl);
			if (x == -1)
			{
				rdev->stio->errnum = stio_syserrtoerrnum(errno);
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

		case STIO_DEV_SCK_CONNECT:
		{
			stio_dev_sck_connect_t* conn = (stio_dev_sck_connect_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&conn->remoteaddr;
			stio_scklen_t sl;
			stio_sckaddr_t localaddr;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = STIO_NULL;
		#endif

			if (STIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't connect again */
				rdev->stio->errnum = STIO_EPERM;
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				dev->stio->errnum = STIO_ENOCAPA;
				return -1;
			}

			if (sa->sa_family == AF_INET) sl = STIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = STIO_SIZEOF(struct sockaddr_in6);
			else 
			{
				dev->stio->errnum = STIO_EINVAL;
				return -1;
			}

		#if defined(USE_SSL)
			if (rdev->ssl_ctx)
			{
				if (rdev->ssl)
				{
					SSL_free (rdev->ssl);
					rdev->ssl = STIO_NULL;
				}

				SSL_CTX_free (rdev->ssl_ctx);
				rdev->ssl_ctx = STIO_NULL;
			}

			if (conn->options & STIO_DEV_SCK_CONNECT_SSL)
			{
				ssl_ctx = SSL_CTX_new(SSLv23_client_method());
				if (!ssl_ctx)
				{
					rdev->stio->errnum = STIO_ESYSERR;
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
			x = connect (rdev->sck, sa, sl);
/*{
int flags = fcntl (rdev->sck, F_GETFL);
fcntl (rdev->sck, F_SETFL, flags | O_NONBLOCK);
}*/
			if (x == -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
				{
					if (stio_dev_watch ((stio_dev_t*)rdev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT) <= -1)
					{
						/* watcher update failure. it's critical */
						stio_stop (rdev->stio, STIO_STOPREQ_WATCHER_ERROR);
						goto oops_connect;
					}
					else
					{
						stio_inittime (&rdev->tmout, 0, 0); /* just in case */

						if (stio_ispostime(&conn->connect_tmout))
						{
							if (schedule_timer_job_after (rdev, &conn->connect_tmout, connect_timedout) <= -1) 
							{
								goto oops_connect;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								STIO_ASSERT (rdev->tmrjob_index != STIO_TMRIDX_INVALID);
								stio_gettmrjobdeadline (rdev->stio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						rdev->remoteaddr = conn->remoteaddr;
						rdev->on_connect = conn->on_connect;
					#if defined(USE_SSL)
						rdev->ssl_ctx = ssl_ctx;
					#endif
						STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_CONNECTING);
						return 0;
					}
				}

				rdev->stio->errnum = stio_syserrtoerrnum(errno);

			oops_connect:
				if (stio_dev_watch ((stio_dev_t*)rdev, STIO_DEV_WATCH_UPDATE, STIO_DEV_EVENT_IN) <= -1)
				{
					/* watcher update failure. it's critical */
					stio_stop (rdev->stio, STIO_STOPREQ_WATCHER_ERROR);
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
				rdev->on_connect = conn->on_connect;

				sl = STIO_SIZEOF(localaddr);
				if (getsockname (rdev->sck, (struct sockaddr*)&localaddr, &sl) == 0) rdev->localaddr = localaddr;

			#if defined(USE_SSL)
				if (ssl_ctx)
				{
					int x;
					rdev->ssl_ctx = ssl_ctx;

					x = connect_ssl (rdev);
					if (x <= -1) 
					{
						SSL_CTX_free (rdev->ssl_ctx);
						rdev->ssl_ctx = STIO_NULL;

						STIO_ASSERT (rdev->ssl == STIO_NULL);
						return -1;
					}
					if (x == 0) 
					{
						STIO_ASSERT (rdev->tmrjob_index == STIO_TMRIDX_INVALID);
						stio_inittime (&rdev->tmout, 0, 0); /* just in case */

						/* it's ok to use conn->connect_tmout for ssl-connect as
						 * the underlying socket connection has been established immediately */
						if (stio_ispostime(&conn->connect_tmout))
						{
							if (schedule_timer_job_after (rdev, &conn->connect_tmout, ssl_connect_timedout) <= -1) 
							{
								/* no device halting in spite of failure.
								 * let the caller handle this after having 
								 * checked the return code as it is an IOCTL call. */
								SSL_CTX_free (rdev->ssl_ctx);
								rdev->ssl_ctx = STIO_NULL;

								STIO_ASSERT (rdev->ssl == STIO_NULL);
								return -1;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								STIO_ASSERT (rdev->tmrjob_index != STIO_TMRIDX_INVALID);
								stio_gettmrjobdeadline (rdev->stio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_CONNECTING_SSL);
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
					STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_CONNECTED);
					if (rdev->on_connect (rdev) <= -1) return -1;
			#if defined(USE_SSL)
				}
			#endif
				return 0;
			}
		}

		case STIO_DEV_SCK_LISTEN:
		{
			stio_dev_sck_listen_t* lstn = (stio_dev_sck_listen_t*)arg;
			int x;

			if (STIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't listen again */
				rdev->stio->errnum = STIO_EPERM;
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				dev->stio->errnum = STIO_ENOCAPA;
				return -1;
			}

			x = listen (rdev->sck, lstn->backlogs);
			if (x == -1) 
			{
				rdev->stio->errnum = stio_syserrtoerrnum(errno);
				return -1;
			}

			STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_LISTENING);
			rdev->on_connect = lstn->on_connect;
			return 0;
		}
	}

	return 0;
}

static stio_dev_mth_t dev_sck_methods_stateless = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_ioctl,     /* ioctl */
};


static stio_dev_mth_t dev_sck_methods_stateful = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_ioctl,     /* ioctl */
};

static stio_dev_mth_t dev_mth_clisck =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_ioctl
};
/* ========================================================================= */

static int harvest_outgoing_connection (stio_dev_sck_t* rdev)
{
	int errcode;
	stio_scklen_t len;

	STIO_ASSERT (!(rdev->state & STIO_DEV_SCK_CONNECTED));

	len = STIO_SIZEOF(errcode);
	if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
	{
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}
	else if (errcode == 0)
	{
		stio_sckaddr_t localaddr;
		stio_scklen_t addrlen;

		/* connected */

		if (rdev->tmrjob_index != STIO_TMRIDX_INVALID)
		{
			stio_deltmrjob (rdev->stio, rdev->tmrjob_index);
			STIO_ASSERT (rdev->tmrjob_index == STIO_TMRIDX_INVALID);
		}

		addrlen = STIO_SIZEOF(localaddr);
		if (getsockname (rdev->sck, (struct sockaddr*)&localaddr, &addrlen) == 0) rdev->localaddr = localaddr;

		if (stio_dev_watch ((stio_dev_t*)rdev, STIO_DEV_WATCH_RENEW, 0) <= -1) 
		{
			/* watcher update failure. it's critical */
			stio_stop (rdev->stio, STIO_STOPREQ_WATCHER_ERROR);
			return -1;
		}

	#if defined(USE_SSL)
		if (rdev->ssl_ctx)
		{
			int x;
			STIO_ASSERT (!rdev->ssl); /* must not be SSL-connected yet */

			x = connect_ssl (rdev);
			if (x <= -1) return -1;
			if (x == 0)
			{
				/* underlying socket connected but not SSL-connected */
				STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_CONNECTING_SSL);

				STIO_ASSERT (rdev->tmrjob_index == STIO_TMRIDX_INVALID);

				/* rdev->tmout has been set to the deadline of the connect task
				 * when the CONNECT IOCTL command has been executed. use the 
				 * same deadline here */
				if (stio_ispostime(&rdev->tmout) &&
				    schedule_timer_job_at (rdev, &rdev->tmout, ssl_connect_timedout) <= -1)
				{
					stio_dev_halt ((stio_dev_t*)rdev);
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
			STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_CONNECTED);
			if (rdev->on_connect (rdev) <= -1) return -1;
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
		rdev->stio->errnum = stio_syserrtoerrnum(errcode);
		return -1;
	}
}

static int accept_incoming_connection (stio_dev_sck_t* rdev)
{
	stio_sckhnd_t clisck;
	stio_sckaddr_t remoteaddr;
	stio_scklen_t addrlen;
	stio_dev_sck_t* clidev;

	/* this is a server(lisening) socket */

	addrlen = STIO_SIZEOF(remoteaddr);
	clisck = accept (rdev->sck, (struct sockaddr*)&remoteaddr, &addrlen);
	if (clisck == STIO_SCKHND_INVALID)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
		if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	/* use rdev->dev_size when instantiating a client sck device
	 * instead of STIO_SIZEOF(stio_dev_sck_t). therefore, the 
	 * extension area as big as that of the master sck device
	 * is created in the client sck device */
	clidev = (stio_dev_sck_t*)stio_makedev (rdev->stio, rdev->dev_size, &dev_mth_clisck, rdev->dev_evcb, &clisck); 
	if (!clidev) 
	{
		close (clisck);
		return -1;
	}

	STIO_ASSERT (clidev->sck == clisck);

	clidev->dev_capa |= STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT | STIO_DEV_CAPA_STREAM | STIO_DEV_CAPA_OUT_QUEUED;
	clidev->remoteaddr = remoteaddr;

	addrlen = STIO_SIZEOF(clidev->localaddr);
	if (getsockname(clisck, (struct sockaddr*)&clidev->localaddr, &addrlen) == -1) clidev->localaddr = rdev->localaddr;

#if defined(SO_ORIGINAL_DST)
	/* if REDIRECT is used, SO_ORIGINAL_DST returns the original
	 * destination address. When REDIRECT is not used, it returnes
	 * the address of the local socket. In this case, it should
	 * be same as the result of getsockname(). */
	addrlen = STIO_SIZEOF(clidev->orgdstaddr);
	if (getsockopt (clisck, SOL_IP, SO_ORIGINAL_DST, &clidev->orgdstaddr, &addrlen) == -1) clidev->orgdstaddr = rdev->localaddr;
#else
	clidev->orgdstaddr = rdev->localaddr;
#endif

	if (!stio_equalsckaddrs (rdev->stio, &clidev->orgdstaddr, &clidev->localaddr))
	{
		clidev->state |= STIO_DEV_SCK_INTERCEPTED;
	}
	else if (stio_getsckaddrport (&clidev->localaddr) != stio_getsckaddrport(&rdev->localaddr))
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
		clidev->state |= STIO_DEV_SCK_INTERCEPTED;
	}
	#if 0
	else if ((clidev->initial_ifindex = resolve_ifindex (fd, clidev->localaddr)) <= -1)
	{
		/* the local_address is not one of a local address.
		 * it's probably proxied. */
		clidev->state |= STIO_DEV_SCK_INTERCEPTED;
	}
	#endif

	/* inherit some event handlers from the parent.
	 * you can still change them inside the on_connect handler */
	clidev->on_connect = rdev->on_connect;
	clidev->on_disconnect = rdev->on_disconnect; 
	clidev->on_write = rdev->on_write;
	clidev->on_read = rdev->on_read;

	STIO_ASSERT (clidev->tmrjob_index == STIO_TMRIDX_INVALID);

	if (rdev->ssl_ctx)
	{
		STIO_DEV_SCK_SET_PROGRESS (clidev, STIO_DEV_SCK_ACCEPTING_SSL);
		STIO_ASSERT (clidev->state & STIO_DEV_SCK_ACCEPTING_SSL);
		/* actual SSL acceptance must be completed in the client device */

		/* let the client device know the SSL context to use */
		clidev->ssl_ctx = rdev->ssl_ctx;

		if (stio_ispostime(&rdev->tmout) &&
		    schedule_timer_job_after (clidev, &rdev->tmout, ssl_accept_timedout) <= -1)
		{
			/* TODO: call a warning/error callback */
			/* timer job scheduling failed. halt the device */
			stio_dev_halt ((stio_dev_t*)clidev);
		}
	}
	else
	{
		STIO_DEV_SCK_SET_PROGRESS (clidev, STIO_DEV_SCK_ACCEPTED);
		if (clidev->on_connect(clidev) <= -1) stio_dev_sck_halt (clidev);
	}

	return 0;
}

static int dev_evcb_sck_ready_stateful (stio_dev_t* dev, int events)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	if (events & STIO_DEV_EVENT_ERR)
	{
		int errcode;
		stio_scklen_t len;

		len = STIO_SIZEOF(errcode);
		if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error STIO_EDEVERRR is used */
			rdev->stio->errnum = STIO_EDEVERR;
		}
		else
		{
			rdev->stio->errnum = stio_syserrtoerrnum (errcode);
		}
		return -1;
	}

	/* this socket can connect */
	switch (STIO_DEV_SCK_GET_PROGRESS(rdev))
	{
		case STIO_DEV_SCK_CONNECTING:
			if (events & STIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->stio->errnum = STIO_EDEVHUP;
				return -1;
			}
			else if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN))
			{
				/* invalid event masks. generic device error */
				rdev->stio->errnum = STIO_EDEVERR;
				return -1;
			}
			else if (events & STIO_DEV_EVENT_OUT)
			{
				/* when connected, the socket becomes writable */
				return harvest_outgoing_connection (rdev);
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case STIO_DEV_SCK_CONNECTING_SSL:
		#if defined(USE_SSL)
			if (events & STIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->stio->errnum = STIO_EDEVHUP;
				return -1;
			}
			else if (events & STIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				rdev->stio->errnum = STIO_EDEVERR;
				return -1;
			}
			else if (events & (STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT))
			{
				int x;

				x = connect_ssl (rdev);
				if (x <= -1) return -1;
				if (x == 0) return 0; /* not SSL-Connected */

				if (rdev->tmrjob_index != STIO_TMRIDX_INVALID)
				{
					stio_deltmrjob (rdev->stio, rdev->tmrjob_index);
					rdev->tmrjob_index = STIO_TMRIDX_INVALID;
				}

				STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_CONNECTED);
				if (rdev->on_connect (rdev) <= -1) return -1;
				return 0;
			}
			else
			{
				return 0; /* success. no actual I/O yet */
			}
		#else
			rdev->stio->errnum = STIO_EINTERN;
			return -1;
		#endif

		case STIO_DEV_SCK_LISTENING:

			if (events & STIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->stio->errnum = STIO_EDEVHUP;
				return -1;
			}
			else if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_OUT))
			{
				rdev->stio->errnum = STIO_EDEVERR;
				return -1;
			}
			else if (events & STIO_DEV_EVENT_IN)
			{
				return accept_incoming_connection (rdev);
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case STIO_DEV_SCK_ACCEPTING_SSL:
		#if defined(USE_SSL)
			if (events & STIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				rdev->stio->errnum = STIO_EDEVHUP;
				return -1;
			}
			else if (events & STIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				rdev->stio->errnum = STIO_EDEVERR;
				return -1;
			}
			else if (events & (STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT))
			{
				int x;
				x = accept_ssl (rdev);
				if (x <= -1) return -1;
				if (x <= 0) return 0; /* not SSL-accepted yet */

				if (rdev->tmrjob_index != STIO_TMRIDX_INVALID)
				{
					stio_deltmrjob (rdev->stio, rdev->tmrjob_index);
					rdev->tmrjob_index = STIO_TMRIDX_INVALID;
				}

				STIO_DEV_SCK_SET_PROGRESS (rdev, STIO_DEV_SCK_ACCEPTED);
				if (rdev->on_connect(rdev) <= -1) stio_dev_sck_halt (rdev);

				return 0;
			}
			else
			{
				return 0; /* no reading or writing yet */
			}
		#else
			rdev->stio->errnum = STIO_EINTERN;
			return -1;
		#endif


		default:
			if (events & STIO_DEV_EVENT_HUP)
			{
				if (events & (STIO_DEV_EVENT_PRI | STIO_DEV_EVENT_IN | STIO_DEV_EVENT_OUT)) 
				{
					/* probably half-open? */
					return 1;
				}

				rdev->stio->errnum = STIO_EDEVHUP;
				return -1;
			}

			return 1; /* the device is ok. carry on reading or writing */
	}
}

static int dev_evcb_sck_ready_stateless (stio_dev_t* dev, int events)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;

	if (events & STIO_DEV_EVENT_ERR)
	{
		int errcode;
		stio_scklen_t len;

		len = STIO_SIZEOF(errcode);
		if (getsockopt (rdev->sck, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error STIO_EDEVERRR is used */
			rdev->stio->errnum = STIO_EDEVERR;
		}
		else
		{
			rdev->stio->errnum = stio_syserrtoerrnum (errcode);
		}
		return -1;
	}
	else if (events & STIO_DEV_EVENT_HUP)
	{
		rdev->stio->errnum = STIO_EDEVHUP;
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read_stateful (stio_dev_t* dev, const void* data, stio_iolen_t dlen, const stio_devaddr_t* srcaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return rdev->on_read (rdev, data, dlen, STIO_NULL);
}

static int dev_evcb_sck_on_write_stateful (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devaddr_t* dstaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return rdev->on_write (rdev, wrlen, wrctx, STIO_NULL);
}

static int dev_evcb_sck_on_read_stateless (stio_dev_t* dev, const void* data, stio_iolen_t dlen, const stio_devaddr_t* srcaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return rdev->on_read (rdev, data, dlen, srcaddr->ptr);
}

static int dev_evcb_sck_on_write_stateless (stio_dev_t* dev, stio_iolen_t wrlen, void* wrctx, const stio_devaddr_t* dstaddr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return rdev->on_write (rdev, wrlen, wrctx, dstaddr->ptr);
}

static stio_dev_evcb_t dev_sck_event_callbacks_stateful =
{
	dev_evcb_sck_ready_stateful,
	dev_evcb_sck_on_read_stateful,
	dev_evcb_sck_on_write_stateful
};

static stio_dev_evcb_t dev_sck_event_callbacks_stateless =
{
	dev_evcb_sck_ready_stateless,
	dev_evcb_sck_on_read_stateless,
	dev_evcb_sck_on_write_stateless
};

/* ========================================================================= */

stio_dev_sck_t* stio_dev_sck_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_sck_make_t* info)
{
	stio_dev_sck_t* rdev;

	if (info->type < 0 && info->type >= STIO_COUNTOF(sck_type_map))
	{
		stio->errnum = STIO_EINVAL;
		return STIO_NULL;
	}

	if (sck_type_map[info->type].extra_dev_capa & STIO_DEV_CAPA_STREAM) /* can't use the IS_STATEFUL() macro yet */
	{
		rdev = (stio_dev_sck_t*)stio_makedev (
			stio, STIO_SIZEOF(stio_dev_sck_t) + xtnsize, 
			&dev_sck_methods_stateful, &dev_sck_event_callbacks_stateful, (void*)info);
	}
	else
	{
		rdev = (stio_dev_sck_t*)stio_makedev (
			stio, STIO_SIZEOF(stio_dev_sck_t) + xtnsize,
			&dev_sck_methods_stateless, &dev_sck_event_callbacks_stateless, (void*)info);
	}

	return rdev;
}

int stio_dev_sck_bind (stio_dev_sck_t* dev, stio_dev_sck_bind_t* info)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_SCK_BIND, info);
}

int stio_dev_sck_connect (stio_dev_sck_t* dev, stio_dev_sck_connect_t* info)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_SCK_CONNECT, info);
}

int stio_dev_sck_listen (stio_dev_sck_t* dev, stio_dev_sck_listen_t* info)
{
	return stio_dev_ioctl ((stio_dev_t*)dev, STIO_DEV_SCK_LISTEN, info);
}

int stio_dev_sck_write (stio_dev_sck_t* dev, const void* data, stio_iolen_t dlen, void* wrctx, const stio_sckaddr_t* dstaddr)
{
	stio_devaddr_t devaddr;
	return stio_dev_write ((stio_dev_t*)dev, data, dlen, wrctx, sckaddr_to_devaddr(dev, dstaddr, &devaddr));
}

int stio_dev_sck_timedwrite (stio_dev_sck_t* dev, const void* data, stio_iolen_t dlen, const stio_ntime_t* tmout, void* wrctx, const stio_sckaddr_t* dstaddr)
{
	stio_devaddr_t devaddr;
	return stio_dev_timedwrite ((stio_dev_t*)dev, data, dlen, tmout, wrctx, sckaddr_to_devaddr(dev, dstaddr, &devaddr));
}






/* ========================================================================= */

stio_uint16_t stio_checksumip (const void* hdr, stio_size_t len)
{
	stio_uint32_t sum = 0;
	stio_uint16_t *ptr = (stio_uint16_t*)hdr;

	
	while (len > 1)
	{
		sum += *ptr++;
		if (sum & 0x80000000)
		sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}
 
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

	return (stio_uint16_t)~sum;
}
