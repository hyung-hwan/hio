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


#include <hio-sck.h>
#include "hio-prv.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h> /* strerror */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#if defined(HAVE_NETPACKET_PACKET_H)
#	include <netpacket/packet.h>
#endif

#if defined(HAVE_NET_IF_DL_H)
#	include <net/if_dl.h>
#endif

#if defined(HAVE_SYS_SENDFILE_H)
#	include <sys/sendfile.h>
#endif

#if defined(HAVE_SYS_IOCTL_H)
#	include <sys/ioctl.h>
#endif

#if defined(HAVE_NET_BPF_H)
#	include <net/bpf.h>
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

static hio_syshnd_t open_async_socket (hio_t* hio, int domain, int type, int proto)
{
	hio_syshnd_t sck = HIO_SYSHND_INVALID;
	int flags;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
open_socket:
#endif
	sck = socket(domain, type, proto); 
	if (sck == HIO_SYSHND_INVALID) 
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

	if (hio_makesyshndasync(hio, sck) <= -1 ||
	    hio_makesyshndcloexec(hio, sck) <= -1) goto oops;

done:
	return sck;

oops:
	hio_seterrwithsyserr (hio, 0, errno);
	if (sck != HIO_SYSHND_INVALID) close (sck);
	return HIO_SYSHND_INVALID;
}

static hio_syshnd_t open_async_qx (hio_t* hio, hio_syshnd_t* side_chan)
{
	int fd[2];
	int type = SOCK_DGRAM;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
open_socket:
#endif
	if (socketpair(AF_UNIX, type, 0, fd) <= -1)
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL && (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)))
		{
			type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
			goto open_socket;
		}
	#endif
		hio_seterrwithsyserr (hio, 0, errno);
		return HIO_SYSHND_INVALID;
	}
	else
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
		if (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) goto done;
	#endif
	}

	if (hio_makesyshndasync(hio, fd[0]) <= -1 ||
	    hio_makesyshndasync(hio, fd[1]) <= -1 ||
	    hio_makesyshndcloexec(hio, fd[0]) <= -1 ||
	    hio_makesyshndcloexec(hio, fd[1]) <= -1) 
	{
		hio_seterrwithsyserr (hio, 0, errno);
		close (fd[0]);
		close (fd[1]);
		return HIO_SYSHND_INVALID;
	}

done:
	*side_chan = fd[1]; /* write end of the pipe */
	return fd[0]; /* read end of the pipe */
}

static hio_syshnd_t open_async_bpf (hio_t* hio)
{
	hio_syshnd_t fd = HIO_SYSHND_INVALID;
	int tmp;
	unsigned int bufsize;

	fd = open("/dev/bpf", O_RDWR);
	if (fd == HIO_SYSHND_INVALID) goto oops;

#if 0
	if (ioctl(fd, BIOCIMMEDIATE, &tmp) == -1) goto oops;
	if (ioctl(fd, BIOCGBLEN, &bufsize) == -1) goto oops;
#endif

	return fd;
oops:
	hio_seterrwithsyserr (hio, 0, errno);
	if (fd != HIO_SYSHND_INVALID) close (fd);
	return HIO_SYSHND_INVALID;
}

/* ========================================================================= */

static hio_devaddr_t* skad_to_devaddr (hio_dev_sck_t* dev, const hio_skad_t* sckaddr, hio_devaddr_t* devaddr)
{
	if (sckaddr)
	{
		devaddr->ptr = (void*)sckaddr;
		devaddr->len = hio_skad_size(sckaddr);
		return devaddr;
	}

	return HIO_NULL;
}

static HIO_INLINE hio_skad_t* devaddr_to_skad (hio_dev_sck_t* dev, const hio_devaddr_t* devaddr, hio_skad_t* sckaddr)
{
	return (hio_skad_t*)devaddr->ptr;
}

/* ========================================================================= */

#define IS_STATEFUL(sck) ((sck)->dev_cap & HIO_DEV_CAP_STREAM)

struct sck_type_map_t
{
	int domain;
	int type;
	int proto;
	int extra_dev_cap;
};

#define __AF_QX 999999
#define __AF_BPF 999998

static struct sck_type_map_t sck_type_map[] =
{
	/* HIO_DEV_SCK_QX */
	{ __AF_QX,    0,              0,                         0 },

	/* HIO_DEV_SCK_TCP4 */
	{ AF_INET,    SOCK_STREAM,    0,                         HIO_DEV_CAP_STREAM },

	/* HIO_DEV_SCK_TCP6 */
	{ AF_INET6,   SOCK_STREAM,    0,                         HIO_DEV_CAP_STREAM },

	/* HIO_DEV_SCK_UPD4 */
	{ AF_INET,    SOCK_DGRAM,     0,                         0                                             },

	/* HIO_DEV_SCK_UDP6 */
	{ AF_INET6,   SOCK_DGRAM,     0,                         0                                             },

	/* HIO_DEV_SCK_ICMP4 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET,    SOCK_RAW,       IPPROTO_ICMP,              0,                                             },

	/* HIO_DEV_SCK_ICMP6 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET6,   SOCK_RAW,       IPPROTO_ICMP,              0,                                             },

#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* HIO_DEV_SCK_ARP - Ethernet type is 2 bytes long. Protocol must be specified in the network byte order */
	{ AF_PACKET,  SOCK_RAW,       HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0                                 },

	/* HIO_DEV_SCK_ARP_DGRAM - link-level header removed*/
	{ AF_PACKET,  SOCK_DGRAM,     HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0                                 },

#elif defined(AF_LINK) && (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	/* HIO_DEV_SCK_ARP */
	{ AF_LINK,  SOCK_RAW,         HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0                                 },

	/* HIO_DEV_SCK_ARP_DGRAM */
	{ AF_LINK,  SOCK_DGRAM,       HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0                                 },
#else
	{ -1,       0,                0,                         0                                              },
	{ -1,       0,                0,                         0                                              },
#endif

#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* HIO_DEV_SCK_PACKET */
	{ AF_PACKET,  SOCK_RAW,       HIO_CONST_HTON16(ETH_P_ALL), 0                                            },
#elif defined(AF_LINK) && (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	/* HIO_DEV_SCK_PACKET */
	{ AF_LINK,    SOCK_RAW,       HIO_CONST_HTON16(0),       0                                              },
#else
	{ -1,       0,                0,                         0                                              },
#endif


	/* HIO_DEV_SCK_BPF - arp */
	{ __AF_BPF, 0, 0, 0 } /* not implemented yet */
};

/* ======================================================================== */

static void connect_timedout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	HIO_ASSERT (hio, IS_STATEFUL(rdev));

	if (rdev->state & HIO_DEV_SCK_CONNECTING)
	{
		/* the state check for HIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job 
		 * doesn't need to be deleted when it gets connected for this check 
		 * here. this libarary, however, deletes the job when it gets 
		 * connected. */
		HIO_DEBUG1 (hio, "SCK(%p) - connect timed out. halting\n", rdev);
		hio_dev_sck_halt (rdev);
	}
}

static void ssl_accept_timedout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	HIO_ASSERT (hio, IS_STATEFUL(rdev));

	if (rdev->state & HIO_DEV_SCK_ACCEPTING_SSL)
	{
		HIO_DEBUG1 (hio, "SCK(%p) - ssl-accept timed out. halting\n", rdev);
		hio_dev_sck_halt(rdev);
	}
}

static void ssl_connect_timedout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	HIO_ASSERT (hio, IS_STATEFUL(rdev));

	if (rdev->state & HIO_DEV_SCK_CONNECTING_SSL)
	{
		HIO_DEBUG1 (hio, "SCK(%p) - ssl-connect timed out. halting\n", rdev);
		hio_dev_sck_halt(rdev);
	}
}

static HIO_INLINE int schedule_timer_job_at (hio_dev_sck_t* dev, const hio_ntime_t* fire_at, hio_tmrjob_handler_t handler)
{
#if 1
	return hio_schedtmrjobat(dev->hio, fire_at, handler, &dev->tmrjob_index, dev);
#else
	hio_tmrjob_t tmrjob;

	HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = &dev->tmrjob_index;

	HIO_ASSERT (dev->hio, dev->tmrjob_index == HIO_TMRIDX_INVALID);
	dev->tmrjob_index = hio_instmrjob(dev->hio, &tmrjob);
	return dev->tmrjob_index == HIO_TMRIDX_INVALID? -1: 0;
#endif
}

static HIO_INLINE int schedule_timer_job_after (hio_dev_sck_t* dev, const hio_ntime_t* fire_after, hio_tmrjob_handler_t handler)
{
#if 1
	return hio_schedtmrjobafter(dev->hio, fire_after, handler, &dev->tmrjob_index, dev);
#else
	hio_t* hio = dev->hio;
	hio_ntime_t fire_at;

	HIO_ASSERT (hio, HIO_IS_POS_NTIME(fire_after));

	hio_gettime (hio, &fire_at);
	HIO_ADD_NTIME (&fire_at, &fire_at, fire_after);

	return schedule_timer_job_at(dev, &fire_at, handler);
#endif
}

/* ======================================================================== */
#if defined(USE_SSL)
static void set_ssl_error (hio_t* hio, int sslerr)
{
	hio_bch_t emsg[128];
	ERR_error_string_n (sslerr, emsg, HIO_COUNTOF(emsg));
	hio_seterrbfmt (hio, HIO_ESYSERR, "%hs", emsg);
}
#endif

static int dev_sck_make (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_dev_sck_make_t* arg = (hio_dev_sck_make_t*)ctx;
	hio_syshnd_t hnd = HIO_SYSHND_INVALID;
	hio_syshnd_t side_chan = HIO_SYSHND_INVALID;

	HIO_ASSERT (hio, arg->type >= 0 && arg->type < HIO_COUNTOF(sck_type_map));

	/* initialize some fields first where 0 is not somthing initial or invalid. */
	rdev->hnd = HIO_SYSHND_INVALID;
	rdev->side_chan = HIO_SYSHND_INVALID;
	rdev->tmrjob_index = HIO_TMRIDX_INVALID;

	if (sck_type_map[arg->type].domain <= -1)
	{
		hio_seterrnum (hio, HIO_ENOIMPL); /* TODO: better error info? */
		goto oops;
	}

	if (HIO_UNLIKELY(sck_type_map[arg->type].domain == __AF_QX))
	{
		hnd = open_async_qx(hio, &side_chan);
		if (hnd == HIO_SYSHND_INVALID) goto oops;
	}
	else
	{
		hnd = open_async_socket(hio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
		if (hnd == HIO_SYSHND_INVALID) goto oops;
	}

	rdev->hnd = hnd;
	rdev->side_chan = side_chan;
	rdev->dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT | sck_type_map[arg->type].extra_dev_cap;
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	rdev->on_connect = arg->on_connect;
	rdev->on_disconnect = arg->on_disconnect;
	rdev->on_raw_accept = arg->on_raw_accept;
	rdev->type = arg->type;

	if (arg->options & HIO_DEV_SCK_MAKE_LENIENT) rdev->state |= HIO_DEV_SCK_LENIENT;

	return 0;

oops:
	if (hnd != HIO_SYSHND_INVALID) 
	{
		close (hnd);
	}
	if (side_chan != HIO_SYSHND_INVALID) 
	{
		close (side_chan);
	}
	return -1;
}

static int dev_sck_make_client (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_syshnd_t* clisckhnd = (hio_syshnd_t*)ctx;

	/* create a socket device that is made of a socket connection
	 * on a listening socket.
	 * nothing special is done here except setting the socket handle.
	 * most of the initialization is done by the listening socket device
	 * after a client socket has been created. */

	rdev->hnd = *clisckhnd;
	rdev->tmrjob_index = HIO_TMRIDX_INVALID;
	rdev->side_chan = HIO_SYSHND_INVALID;

	if (hio_makesyshndasync(hio, rdev->hnd) <= -1 ||
	    hio_makesyshndcloexec(hio, rdev->hnd) <= -1) goto oops;

	return 0;

oops:
	if (rdev->hnd != HIO_SYSHND_INVALID)
	{
		close (rdev->hnd);
		rdev->hnd = HIO_SYSHND_INVALID;
	}
	return -1;
}

static void dev_sck_fail_before_make_client (void* ctx)
{
	hio_syshnd_t* clisckhnd = (hio_syshnd_t*)ctx;
	close (*clisckhnd);
}

static int dev_sck_kill (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (IS_STATEFUL(rdev))
	{
		/*if (HIO_DEV_SCK_GET_PROGRESS(rdev))
		{*/
			/* for HIO_DEV_SCK_CONNECTING, HIO_DEV_SCK_CONNECTING_SSL, and HIO_DEV_SCK_ACCEPTING_SSL
			 * on_disconnect() is called without corresponding on_connect(). 
			 * it is the same if connect or accept has not been called. */
			if (rdev->on_disconnect) rdev->on_disconnect (rdev);
		/*}*/

		if (rdev->tmrjob_index != HIO_TMRIDX_INVALID)
		{
			hio_deltmrjob (hio, rdev->tmrjob_index);
			HIO_ASSERT (hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);
		}
	}
	else
	{
		HIO_ASSERT (hio, (rdev->state & HIO_DEV_SCK_ALL_PROGRESS_BITS) == 0);
		HIO_ASSERT (hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);

		if (rdev->on_disconnect) rdev->on_disconnect (rdev);
	}

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		SSL_shutdown ((SSL*)rdev->ssl); /* is this needed? */
		SSL_free ((SSL*)rdev->ssl);
		rdev->ssl = HIO_NULL;
	}
	if (!(rdev->state & (HIO_DEV_SCK_ACCEPTED | HIO_DEV_SCK_ACCEPTING_SSL)) && rdev->ssl_ctx)
	{
		SSL_CTX_free ((SSL_CTX*)rdev->ssl_ctx);
		rdev->ssl_ctx = HIO_NULL;
	}
#endif

	if (rdev->hnd != HIO_SYSHND_INVALID) 
	{
		close (rdev->hnd);
		rdev->hnd = HIO_SYSHND_INVALID;
	}

	if (rdev->side_chan != HIO_SYSHND_INVALID)
	{
		close (rdev->side_chan);
		rdev->side_chan = HIO_SYSHND_INVALID;
	}
	return 0;
}

static hio_syshnd_t dev_sck_getsyshnd (hio_dev_t* dev)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return (hio_syshnd_t)rdev->hnd;
}
/* ------------------------------------------------------------------------------ */

static int dev_sck_read_stateful (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		x = SSL_read((SSL*)rdev->ssl, buf, *len);
		if (x <= -1)
		{
			int err = SSL_get_error((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			set_ssl_error (hio, err);
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;

		x = recv(rdev->hnd, buf, *len, 0);
		if (x == -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr (hio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

static int dev_sck_read_stateless (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_scklen_t srcaddrlen;
	ssize_t x;

	srcaddrlen = HIO_SIZEOF(rdev->remoteaddr);
	x = recvfrom(rdev->hnd, buf, *len, 0, (struct sockaddr*)&rdev->remoteaddr, &srcaddrlen);
	if (x <= -1)
	{
		int eno = errno;
		if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;  /* no data available */
		if (eno == EINTR) return 0;

		hio_seterrwithsyserr (hio, 0, eno);

		HIO_DEBUG2 (hio, "SCK(%p) - recvfrom failure - %hs", rdev, strerror(eno)); 
		return -1;
	}

	srcaddr->ptr = &rdev->remoteaddr;
	srcaddr->len = srcaddrlen;

	*len = x;
	return 1;
}

static int dev_sck_read_bpf (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_seterrwithsyserr (hio, 0, HIO_ENOIMPL);
	return -1;
}

/* ------------------------------------------------------------------------------ */

static int dev_sck_write_stateful (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;

		if (*len <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) == -1)
			{
				set_ssl_error (hio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		x = SSL_write((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			set_ssl_error (hio, err);
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
			/* the write handler for a stream device must handle a zero-length 
			 * writing request specially. it's a writing finish indicator. close
			 * the writing end of the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) == -1)
			{
				hio_seterrwithsyserr (hio, 0, errno);
				return -1;
			}

			/* it must return a non-zero positive value. if it returns 0, this request 
			 * gets enqueued by the core. we must aovid it */
			return 1;
		}

		/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
	#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
	#endif
		x = send(rdev->hnd, data, *len, flags);
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr (hio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}


static int dev_sck_writev_stateful (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		int x;
		hio_iolen_t i, nwritten;

		if (*iovcnt <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) == -1)
			{
				set_ssl_error (hio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		nwritten = 0;
		for (i = 0; i < *iovcnt; i++)
		{
			/* no SSL_writev. invoke multiple calls to SSL_write(). 
			 * since the write function is for the stateful connection,
			 * mutiple calls shouldn't really matter */
			x = SSL_write((SSL*)rdev->ssl, iov[i].iov_ptr, iov[i].iov_len);
			if (x <= -1)
			{
				int err = SSL_get_error ((SSL*)rdev->ssl, x);
				if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
				set_ssl_error (hio, err);
				return -1;
			}
			nwritten += x;
		}

		*iovcnt = nwritten;
	}
	else
	{
#endif
		ssize_t x;
		int flags = 0;
		struct msghdr msg;

		if (*iovcnt <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) == -1)
			{
				hio_seterrwithsyserr (hio, 0, errno);
				return -1;
			}

			return 1;
		}

		/* TODO: flags MSG_DONTROUTE, MSG_DONTWAIT, MSG_MORE, MSG_OOB, MSG_NOSIGNAL */
	#if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
	#endif
	#if defined(MSG_DONTWAIT)
		flags |= MSG_DONTWAIT;
	#endif

	#if defined(HAVE_SENDMSG)
		HIO_MEMSET (&msg, 0, HIO_SIZEOF(msg));
		msg.msg_iov = (struct iovec*)iov;
		msg.msg_iovlen = *iovcnt;
		x = sendmsg(rdev->hnd, &msg, flags);
	#else
		x = writev(rdev->hnd, (const struct iovec*)iov, *iovcnt);
	#endif
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr (hio, 0, errno);
			return -1;
		}

		*iovcnt = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}

/* ------------------------------------------------------------------------------ */

static int dev_sck_write_stateless (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;

	x = sendto(rdev->hnd, data, *len, 0, dstaddr->ptr, dstaddr->len);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_sck_writev_stateless (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	struct msghdr msg;
	ssize_t x;
	int flags = 0;

	HIO_MEMSET (&msg, 0, HIO_SIZEOF(msg));
	if (HIO_LIKELY(dstaddr))
	{
		msg.msg_name = dstaddr->ptr;
		msg.msg_namelen = dstaddr->len;
	}
	msg.msg_iov = (struct iovec*)iov;
	msg.msg_iovlen = *iovcnt;


#if defined(MSG_NOSIGNAL)
	flags |= MSG_NOSIGNAL;
#endif
#if defined(MSG_DONTWAIT)
	flags |= MSG_DONTWAIT;
#endif

	x = sendmsg(rdev->hnd, &msg, flags);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

/* ------------------------------------------------------------------------------ */
static int dev_sck_write_bpf (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_seterrwithsyserr (hio, 0, HIO_ENOIMPL);
	return -1;
}

static int dev_sck_writev_bpf (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_seterrwithsyserr (hio, 0, HIO_ENOIMPL);
	return -1;
}


/* ------------------------------------------------------------------------------ */

static int dev_sck_sendfile_stateful (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t* len)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

#if 0 && defined(USE_SSL)
/* TODO: ssl needs to read from the file... and send... */
	if (rdev->ssl)
	{
		int x;

		if (*len <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) == -1)
			{
				set_ssl_error (hio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		x = SSL_write((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
			set_ssl_error (hio, err);
			return -1;
		}

		*len = x;
	}
	else
	{
#endif
		ssize_t x;

		if (*len <= 0)
		{
			/* the write handler for a stream device must handle a zero-length 
			 * writing request specially. it's a writing finish indicator. close
			 * the writing end of the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) == -1)
			{
				hio_seterrwithsyserr (hio, 0, errno);
				return -1;
			}

			/* it must return a non-zero positive value. if it returns 0, this request 
			 * gets enqueued by the core. we must aovid it */
			return 1;
		}

#if defined(HAVE_SENDFILE)
/* TODO: cater for other systems */
		x = sendfile(rdev->hnd, in_fd, &foff, *len);
		if (x == -1) 
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr (hio, 0, errno);
			return -1;
		}
		*len = x;
#else
		hio_seterrnum (hio, HIO_ENOIMPL);
		return -1;
#endif

	
#if 0 && defined(USE_SSL)
	}
#endif
	return 1;
}

/* ------------------------------------------------------------------------------ */

#if defined(USE_SSL)

static int do_ssl (hio_dev_sck_t* dev, int (*ssl_func)(SSL*))
{
	hio_t* hio = dev->hio;
	int ret, watcher_cmd, watcher_events;

	HIO_ASSERT (hio, dev->ssl_ctx);

	if (!dev->ssl)
	{
		SSL* ssl;

		ssl = SSL_new(dev->ssl_ctx);
		if (!ssl)
		{
			set_ssl_error (hio, ERR_get_error());
			return -1;
		}

		if (SSL_set_fd(ssl, dev->hnd) == 0)
		{
			set_ssl_error (hio, ERR_get_error());
			return -1;
		}

		SSL_set_read_ahead (ssl, 0);

		dev->ssl = ssl;
	}

	watcher_cmd = HIO_DEV_WATCH_RENEW;
	watcher_events = HIO_DEV_EVENT_IN;

	ret = ssl_func((SSL*)dev->ssl);
	if (ret <= 0)
	{
		int err = SSL_get_error(dev->ssl, ret);
		if (err == SSL_ERROR_WANT_READ)
		{
			/* handshaking isn't complete */
			ret = 0;
		}
		else if (err == SSL_ERROR_WANT_WRITE)
		{
			/* handshaking isn't complete */
			watcher_cmd = HIO_DEV_WATCH_UPDATE;
			watcher_events = HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT;
			ret = 0;
		}
		else
		{
			set_ssl_error (hio, err);
			ret = -1;
		}
	}
	else
	{
		ret = 1; /* accepted */
	}

	if (hio_dev_watch((hio_dev_t*)dev, watcher_cmd, watcher_events) <= -1)
	{
		hio_stop (hio, HIO_STOPREQ_WATCHER_ERROR);
		ret = -1;
	}

	return ret;
}

static HIO_INLINE int connect_ssl (hio_dev_sck_t* dev)
{
	return do_ssl(dev, SSL_connect);
}

static HIO_INLINE int accept_ssl (hio_dev_sck_t* dev)
{
	return do_ssl(dev, SSL_accept);
}
#endif

static int dev_sck_ioctl (hio_dev_t* dev, int cmd, void* arg)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	switch (cmd)
	{
		case HIO_DEV_SCK_BIND:
		{
			hio_dev_sck_bind_t* bnd = (hio_dev_sck_bind_t*)arg;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = HIO_NULL;
		#endif
			if (HIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't bind again */
				hio_seterrbfmt (hio, HIO_EPERM, "operation in progress. not allowed to bind again");
				return -1;
			}

			if (hio_skad_family(&bnd->localaddr) == HIO_AF_INET6) /* getsockopt(rdev->hnd, SO_DOMAIN, ...) may return the domain but it's kernel specific as well */
			{
				/* TODO: should i make it into bnd->options? HIO_DEV_SCK_BIND_IPV6ONLY? applicable to ipv6 though. */
				int v = 1;
				if (setsockopt(rdev->hnd, IPPROTO_IPV6, IPV6_V6ONLY, &v, HIO_SIZEOF(v)) == -1)
				{
					hio_seterrbfmtwithsyserr (hio, 0, errno, "unable to set IPV6_V6ONLY");
					return -1;
				}
			}

			if (bnd->options & HIO_DEV_SCK_BIND_BROADCAST)
			{
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_BROADCAST, &v, HIO_SIZEOF(v)) == -1)
				{
					hio_seterrbfmtwithsyserr (hio, 0, errno, "unable to set SO_BROADCAST");
					return -1;
				}
			}

			if (bnd->options & HIO_DEV_SCK_BIND_REUSEADDR)
			{
			#if defined(SO_REUSEADDR)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_REUSEADDR, &v, HIO_SIZEOF(v)) == -1)
				{
					if (!(bnd->options & HIO_DEV_SCK_BIND_IGNERR))
					{
						hio_seterrbfmtwithsyserr (hio, 0, errno, "unable to set SO_REUSEADDR");
						return -1;
					}
				}
			/* ignore it if not available
			#else
				hio_seterrnum (hio, HIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (bnd->options & HIO_DEV_SCK_BIND_REUSEPORT)
			{
			#if defined(SO_REUSEPORT)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_REUSEPORT, &v, HIO_SIZEOF(v)) == -1)
				{
					if (!(bnd->options & HIO_DEV_SCK_BIND_IGNERR))
					{
						hio_seterrbfmtwithsyserr (hio, 0, errno, "unable to set SO_REUSEPORT");
						return -1;
					}
				}
			/* ignore it if not available
			#else
				hio_seterrnum (hio, HIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (bnd->options & HIO_DEV_SCK_BIND_TRANSPARENT)
			{
			#if defined(IP_TRANSPARENT)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_IP, IP_TRANSPARENT, &v, HIO_SIZEOF(v)) == -1)
				{
					hio_seterrbfmtwithsyserr (hio, 0, errno, "unable to set IP_TRANSPARENT");
					return -1;
				}
			/* ignore it if not available
			#else
				hio_seterrnum (hio, HIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (rdev->ssl_ctx)
			{
			#if defined(USE_SSL)
				SSL_CTX_free (rdev->ssl_ctx);
			#endif
				rdev->ssl_ctx = HIO_NULL;

				if (rdev->ssl)
				{
				#if defined(USE_SSL)
					SSL_free (rdev->ssl);
				#endif
					rdev->ssl = HIO_NULL;
				}
			}

			if (bnd->options & HIO_DEV_SCK_BIND_SSL)
			{
			#if defined(USE_SSL)
				if (!bnd->ssl_certfile || !bnd->ssl_keyfile)
				{
					hio_seterrbfmt (hio, HIO_EINVAL, "SSL certficate/key file not set");
					return -1;
				}

				ssl_ctx = SSL_CTX_new(SSLv23_server_method());
				if (!ssl_ctx)
				{
					set_ssl_error (hio, ERR_get_error());
					return -1;
				}

				if (SSL_CTX_use_certificate_file(ssl_ctx, bnd->ssl_certfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_use_PrivateKey_file(ssl_ctx, bnd->ssl_keyfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_check_private_key(ssl_ctx) == 0  /*||
				    SSL_CTX_use_certificate_chain_file(ssl_ctx, bnd->chainfile) == 0*/)
				{
					set_ssl_error (hio, ERR_get_error());
					SSL_CTX_free (ssl_ctx);
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /*SSL_MODE_ENABLE_PARTIAL_WRITE |*/
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

				SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2); /* no outdated SSLv2 by default */
			#else
				hio_seterrnum (hio, HIO_ENOIMPL);
				return -1;
			#endif
			}

			x = bind(rdev->hnd, (struct sockaddr*)&bnd->localaddr, hio_skad_size(&bnd->localaddr));
			if (x == -1)
			{
				hio_seterrwithsyserr (hio, 0, errno);
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

		case HIO_DEV_SCK_CONNECT:
		{
			hio_dev_sck_connect_t* conn = (hio_dev_sck_connect_t*)arg;
			struct sockaddr* sa = (struct sockaddr*)&conn->remoteaddr;
			hio_scklen_t sl;
			int x;
		#if defined(USE_SSL)
			SSL_CTX* ssl_ctx = HIO_NULL;
		#endif

			if (HIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't connect again */
				hio_seterrbfmt (hio, HIO_EPERM, "operation in progress. disallowed to connect again");
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				hio_seterrbfmt (hio, HIO_EPERM, "disallowed to connect stateless device");
				return -1;
			}

			if (sa->sa_family == AF_INET) sl = HIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = HIO_SIZEOF(struct sockaddr_in6);
			else 
			{
				hio_seterrbfmt (hio, HIO_EINVAL, "unknown address family %d", sa->sa_family);
				return -1;
			}

		#if defined(USE_SSL)
			if (rdev->ssl_ctx)
			{
				if (rdev->ssl)
				{
					SSL_free (rdev->ssl);
					rdev->ssl = HIO_NULL;
				}

				SSL_CTX_free (rdev->ssl_ctx);
				rdev->ssl_ctx = HIO_NULL;
			}

			if (conn->options & HIO_DEV_SCK_CONNECT_SSL)
			{
				ssl_ctx = SSL_CTX_new(SSLv23_client_method());
				if (!ssl_ctx)
				{
					set_ssl_error (hio, ERR_get_error());
					return -1;
				}

				SSL_CTX_set_read_ahead (ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | 
				                           /* SSL_MODE_ENABLE_PARTIAL_WRITE | */
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
			}
		#endif
			/* the socket is already non-blocking */
/*{
int flags = fcntl (rdev->hnd, F_GETFL);
fcntl (rdev->hnd, F_SETFL, flags & ~O_NONBLOCK);
}*/
			x = connect(rdev->hnd, sa, sl);
/*{
int flags = fcntl (rdev->hnd, F_GETFL);
fcntl (rdev->hnd, F_SETFL, flags | O_NONBLOCK);
}*/
			if (x <= -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
				{
					if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_UPDATE, HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT) <= -1)
					{
						/* watcher update failure. it's critical */
						hio_stop (hio, HIO_STOPREQ_WATCHER_ERROR);
						goto oops_connect;
					}
					else
					{
						HIO_INIT_NTIME (&rdev->tmout, 0, 0); /* just in case */

						if (HIO_IS_POS_NTIME(&conn->connect_tmout))
						{
							if (schedule_timer_job_after(rdev, &conn->connect_tmout, connect_timedout) <= -1) 
							{
								goto oops_connect;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								HIO_ASSERT (hio, rdev->tmrjob_index != HIO_TMRIDX_INVALID);
								hio_gettmrjobdeadline (hio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						rdev->remoteaddr = conn->remoteaddr;
					#if defined(USE_SSL)
						rdev->ssl_ctx = ssl_ctx;
					#endif
						HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_CONNECTING);
						return 0;
					}
				}

				hio_seterrwithsyserr (hio, 0, errno);

			oops_connect:
				if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_UPDATE, HIO_DEV_EVENT_IN) <= -1)
				{
					/* watcher update failure. it's critical */
					hio_stop (hio, HIO_STOPREQ_WATCHER_ERROR);
				}

			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free (ssl_ctx);
			#endif
				return -1;
			}
			else
			{
				/* connected immediately */

				/* don't call on_connect() callback even though the connection has been established.
				 * i don't want on_connect() to be called within the this function. */
				if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_UPDATE, HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT) <= -1)
				{
					/* watcher update failure. it's critical */
					hio_stop (hio, HIO_STOPREQ_WATCHER_ERROR);
					goto oops_connect;
				}

				/* as i know it's connected already,
				 * i don't schedule a connection timeout job */

				rdev->remoteaddr = conn->remoteaddr;
			#if defined(USE_SSL)
				rdev->ssl_ctx = ssl_ctx;
			#endif
				/* set progress CONNECTING so that the ready handler invokes on_connect() */
				HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_CONNECTING);
				return 0;
			}
		}

		case HIO_DEV_SCK_LISTEN:
		{
			hio_dev_sck_listen_t* lstn = (hio_dev_sck_listen_t*)arg;
			int x;

			if (HIO_DEV_SCK_GET_PROGRESS(rdev))
			{
				/* can't listen again */
				hio_seterrbfmt (hio, HIO_EPERM, "operation in progress. disallowed to listen again");
				return -1;
			}

			if (!IS_STATEFUL(rdev)) 
			{
				hio_seterrbfmt (hio, HIO_EPERM, "disallowed to listen on stateless device");
				return -1;
			}

			x = listen(rdev->hnd, lstn->backlogs);
			if (x == -1) 
			{
				hio_seterrwithsyserr (hio, 0, errno);
				return -1;
			}

			if (rdev->dev_cap & HIO_DEV_CAP_WATCH_REREG_REQUIRED)
			{
				/* On NetBSD, the listening socket added before listen()
				 * doesn't generate an event even if a new connection is ready
				 * to be accepted. */

				/* TODO: need to keep the old watch flags before STOP and
				 *       use the flags witn START  */
				if (hio_dev_watch(rdev, HIO_DEV_WATCH_STOP, 0) <= -1 ||
				    hio_dev_watch(rdev, HIO_DEV_WATCH_START, 0) <= -1)
				{
					hio_stop (hio, HIO_STOPREQ_WATCHER_ERROR);
					return -1;
				}
			}

			rdev->tmout = lstn->accept_tmout;

			HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_LISTENING);
			return 0;
		}
	}

	return 0;
}

static hio_dev_mth_t dev_mth_sck_stateless = 
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_writev_stateless,
	HIO_NULL,          /* sendfile */
	dev_sck_ioctl,     /* ioctl */
};


static hio_dev_mth_t dev_mth_sck_stateful = 
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_writev_stateful,
	dev_sck_sendfile_stateful,
	dev_sck_ioctl,     /* ioctl */
};

static hio_dev_mth_t dev_mth_clisck =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_fail_before_make_client,
	dev_sck_getsyshnd,

	dev_sck_read_stateful,
	dev_sck_write_stateful,
	dev_sck_writev_stateful,
	dev_sck_sendfile_stateful,
	dev_sck_ioctl
};

static hio_dev_mth_t dev_mth_sck_bpf = 
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,

	dev_sck_read_bpf,
	dev_sck_write_bpf,
	dev_sck_writev_bpf,
	HIO_NULL,          /* sendfile */
	dev_sck_ioctl,     /* ioctl */
};

/* ========================================================================= */

static int harvest_outgoing_connection (hio_dev_sck_t* rdev)
{
	hio_t* hio = rdev->hio;
	int errcode;
	hio_scklen_t len;

	HIO_ASSERT (hio, !(rdev->state & HIO_DEV_SCK_CONNECTED));

	len = HIO_SIZEOF(errcode);
	if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
	{
		hio_seterrbfmtwithsyserr (hio, 0, errno, "unable to get SO_ERROR");
		return -1;
	}
	else if (errcode == 0)
	{
		hio_skad_t localaddr;
		hio_scklen_t addrlen;

		/* connected */

		if (rdev->tmrjob_index != HIO_TMRIDX_INVALID)
		{
			hio_deltmrjob (hio, rdev->tmrjob_index);
			HIO_ASSERT (hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);
		}

		addrlen = HIO_SIZEOF(localaddr);
		if (getsockname(rdev->hnd, (struct sockaddr*)&localaddr, &addrlen) == 0) rdev->localaddr = localaddr;

		if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN) <= -1) 
		{
			/* watcher update failure. it's critical */
			hio_stop (hio, HIO_STOPREQ_WATCHER_ERROR);
			return -1;
		}

	#if defined(USE_SSL)
		if (rdev->ssl_ctx)
		{
			int x;
			HIO_ASSERT (hio, !rdev->ssl); /* must not be SSL-connected yet */

			x = connect_ssl(rdev);
			if (x <= -1) return -1;
			if (x == 0)
			{
				/* underlying socket connected but not SSL-connected */
				HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_CONNECTING_SSL);

				HIO_ASSERT (hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);

				/* rdev->tmout has been set to the deadline of the connect task
				 * when the CONNECT IOCTL command has been executed. use the 
				 * same deadline here */
				if (HIO_IS_POS_NTIME(&rdev->tmout) &&
				    schedule_timer_job_at(rdev, &rdev->tmout, ssl_connect_timedout) <= -1)
				{
					HIO_DEBUG1 (hio, "SCK(%p) - ssl-connect timeout scheduling failed. halting\n", rdev);
					hio_dev_sck_halt (rdev);
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
			HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_CONNECTED);
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
		hio_seterrwithsyserr (hio, 0, errcode);
		return -1;
	}
}

static int make_accepted_client_connection (hio_dev_sck_t* rdev, hio_syshnd_t clisck, hio_skad_t* remoteaddr, hio_dev_sck_type_t clisck_type)
{
	hio_t* hio = rdev->hio;
	hio_dev_sck_t* clidev;
	hio_scklen_t addrlen;

	if (rdev->on_raw_accept)
	{
		/* this is a special optional callback. If you don't want a client socket device 
		 * to be created upon accept, you may implement the on_raw_accept() handler. 
		 * the socket handle is delegated to the callback. */
		rdev->on_raw_accept (rdev, clisck, remoteaddr);
		return 0;
	}

	/* use rdev->dev_size when instantiating a client sck device
	 * instead of HIO_SIZEOF(hio_dev_sck_t). therefore, the  
	 * extension area as big as that of the master sck device
	 * is created in the client sck device */
	clidev = (hio_dev_sck_t*)hio_dev_make(hio, rdev->dev_size, &dev_mth_clisck, rdev->dev_evcb, &clisck); 
	if (HIO_UNLIKELY(!clidev))
	{
		/* [NOTE] 'clisck' is closed by callback methods called by hio_dev_make() upon failure */
		HIO_DEBUG3 (hio, "SCK(%p) - unable to make a new accepted device for %d - %js\n", rdev, (int)clisck, hio_geterrmsg(hio));
		return -1;
	}

	clidev->type = clisck_type;
	HIO_ASSERT (hio, clidev->hnd == clisck);

	clidev->dev_cap |= HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT | HIO_DEV_CAP_STREAM;
	clidev->remoteaddr = *remoteaddr;

	addrlen = HIO_SIZEOF(clidev->localaddr);
	if (getsockname(clisck, (struct sockaddr*)&clidev->localaddr, &addrlen) == -1) clidev->localaddr = rdev->localaddr;

#if defined(SO_ORIGINAL_DST)
	/* if REDIRECT is used, SO_ORIGINAL_DST returns the original
	 * destination address. When REDIRECT is not used, it returnes
	 * the address of the local socket. In this case, it should
	 * be same as the result of getsockname(). */
	addrlen = HIO_SIZEOF(clidev->orgdstaddr);
	if (getsockopt(clisck, SOL_IP, SO_ORIGINAL_DST, &clidev->orgdstaddr, &addrlen) == -1) clidev->orgdstaddr = rdev->localaddr;
#else
	clidev->orgdstaddr = rdev->localaddr;
#endif

	if (!hio_equal_skads(&clidev->orgdstaddr, &clidev->localaddr, 0))
	{
		clidev->state |= HIO_DEV_SCK_INTERCEPTED;
	}
	else if (hio_skad_port(&clidev->localaddr) != hio_skad_port(&rdev->localaddr))
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
		clidev->state |= HIO_DEV_SCK_INTERCEPTED;
	}
	#if 0
	else if ((clidev->initial_ifindex = resolve_ifindex(fd, clidev->localaddr)) <= -1)
	{
		/* the local_address is not one of a local address.
		 * it's probably proxied. */
		clidev->state |= HIO_DEV_SCK_INTERCEPTED;
	}
	#endif

	/* inherit some event handlers from the parent.
	 * you can still change them inside the on_connect handler */
	clidev->on_connect = rdev->on_connect;
	clidev->on_disconnect = rdev->on_disconnect; 
	clidev->on_raw_accept = HIO_NULL; /* don't inherit this */
	clidev->on_write = rdev->on_write;
	clidev->on_read = rdev->on_read;

	/* inherit the contents of the extension area */
	HIO_ASSERT (hio, rdev->dev_size == clidev->dev_size);
	HIO_MEMCPY (hio_dev_sck_getxtn(clidev), hio_dev_sck_getxtn(rdev), rdev->dev_size - HIO_SIZEOF(hio_dev_sck_t));

	HIO_ASSERT (hio, clidev->tmrjob_index == HIO_TMRIDX_INVALID);

	if (rdev->ssl_ctx)
	{
		HIO_DEV_SCK_SET_PROGRESS (clidev, HIO_DEV_SCK_ACCEPTING_SSL);
		HIO_ASSERT (hio, clidev->state & HIO_DEV_SCK_ACCEPTING_SSL);
		/* actual SSL acceptance must be completed in the client device */

		/* let the client device know the SSL context to use */
		clidev->ssl_ctx = rdev->ssl_ctx;

		if (HIO_IS_POS_NTIME(&rdev->tmout) &&
		    schedule_timer_job_after(clidev, &rdev->tmout, ssl_accept_timedout) <= -1)
		{
			/* timer job scheduling failed. halt the device */
			HIO_DEBUG1 (hio, "SCK(%p) - ssl-accept timeout scheduling failed. halting\n", rdev);
			hio_dev_sck_halt (clidev);
		}
	}
	else
	{
		HIO_DEV_SCK_SET_PROGRESS (clidev, HIO_DEV_SCK_ACCEPTED);
		/*if (clidev->on_connect(clidev) <= -1) hio_dev_sck_halt (clidev);*/
		if (clidev->on_connect) clidev->on_connect (clidev);
	}

	return 0;
}

static int accept_incoming_connection (hio_dev_sck_t* rdev)
{
	hio_t* hio = rdev->hio;
	hio_syshnd_t clisck;
	hio_skad_t remoteaddr;
	hio_scklen_t addrlen;
	int flags;

	/* this is a server(lisening) socket */

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_PACCEPT)
	flags = SOCK_NONBLOCK | SOCK_CLOEXEC;

	addrlen = HIO_SIZEOF(remoteaddr);
	clisck = paccept(rdev->hnd, (struct sockaddr*)&remoteaddr, &addrlen, HIO_NULL, flags);
	if (clisck <= -1)
	{
		 if (errno != ENOSYS) goto accept_error;
		 /* go on for the normal 3-parameter accept */
	}
	else
	{
		 goto accept_done;
	}
#elif defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
	flags = SOCK_NONBLOCK | SOCK_CLOEXEC;

	addrlen = HIO_SIZEOF(remoteaddr);
	clisck = accept4(rdev->hnd, (struct sockaddr*)&remoteaddr, &addrlen, flags);
	if (clisck <= -1)
	{
		 if (errno != ENOSYS) goto accept_error;
		 /* go on for the normal 3-parameter accept */
	}
	else
	{
		 goto accept_done;
	}
#endif

	addrlen = HIO_SIZEOF(remoteaddr);
	clisck = accept(rdev->hnd, (struct sockaddr*)&remoteaddr, &addrlen);
	if (clisck <=  -1)
	{
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
	accept_error:
#endif
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
		if (errno == EINTR) return 0; /* if interrupted by a signal, treat it as if it's EINPROGRESS */

		hio_seterrwithsyserr (hio, 0, errno);
		return -1;
	}

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
accept_done:
#endif
	return make_accepted_client_connection(rdev, clisck, &remoteaddr, rdev->type);
}

static int dev_evcb_sck_ready_stateful (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (events & HIO_DEV_EVENT_ERR)
	{
		int errcode;
		hio_scklen_t len;

		len = HIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt (hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr (hio, 0, errcode);
		}
		return -1;
	}

	/* this socket can connect */
	switch (HIO_DEV_SCK_GET_PROGRESS(rdev))
	{
		case HIO_DEV_SCK_CONNECTING:
			if (events & HIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				hio_seterrnum (hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_IN))
			{
				/* invalid event masks. generic device error */
				hio_seterrbfmt (hio, HIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & HIO_DEV_EVENT_OUT)
			{
				/* when connected, the socket becomes writable */
				return harvest_outgoing_connection(rdev);
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case HIO_DEV_SCK_CONNECTING_SSL:
		#if defined(USE_SSL)
			if (events & HIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				hio_seterrnum (hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & HIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				hio_seterrbfmt (hio, HIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & (HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT))
			{
				int x;

				x = connect_ssl(rdev);
				if (x <= -1) return -1;
				if (x == 0) return 0; /* not SSL-Connected */

				if (rdev->tmrjob_index != HIO_TMRIDX_INVALID)
				{
					hio_deltmrjob (rdev->hio, rdev->tmrjob_index);
					rdev->tmrjob_index = HIO_TMRIDX_INVALID;
				}

				HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_CONNECTED);
				if (rdev->on_connect) rdev->on_connect (rdev);
				return 0;
			}
			else
			{
				return 0; /* success. no actual I/O yet */
			}
		#else
			hio_seterrnum (hio, HIO_EINTERN);
			return -1;
		#endif

		case HIO_DEV_SCK_LISTENING:

			if (events & HIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				hio_seterrnum (hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_OUT))
			{
				hio_seterrbfmt (hio, HIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & HIO_DEV_EVENT_IN)
			{
				if (rdev->state & HIO_DEV_SCK_LENIENT)
				{
					accept_incoming_connection(rdev);
					return 0; /* return ok to the core regardless of accept()'s result */
				}
				else
				{
					/* [NOTE] if the accept operation fails, the core also kills this listening device. */
					return accept_incoming_connection(rdev);
				}
			}
			else
			{
				return 0; /* success but don't invoke on_read() */ 
			}

		case HIO_DEV_SCK_ACCEPTING_SSL:
		#if defined(USE_SSL)
			if (events & HIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				hio_seterrnum (hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & HIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				hio_seterrbfmt (hio, HIO_EDEVERR, "device error - invalid event mask");
				return -1;
			}
			else if (events & (HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT))
			{
				int x;

				x = accept_ssl(rdev);
				if (x <= -1) return -1;
				if (x == 0) return 0; /* not SSL-accepted yet */

				if (rdev->tmrjob_index != HIO_TMRIDX_INVALID)
				{
					hio_deltmrjob (rdev->hio, rdev->tmrjob_index);
					rdev->tmrjob_index = HIO_TMRIDX_INVALID;
				}

				HIO_DEV_SCK_SET_PROGRESS (rdev, HIO_DEV_SCK_ACCEPTED);
				if (rdev->on_connect) rdev->on_connect (rdev);

				return 0;
			}
			else
			{
				return 0; /* no reading or writing yet */
			}
		#else
			hio_seterrnum (hio, HIO_EINTERN);
			return -1;
		#endif


		default:
			if (events & HIO_DEV_EVENT_HUP)
			{
				if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT)) 
				{
					/* probably half-open? */
					return 1;
				}

				hio_seterrnum (hio, HIO_EDEVHUP);
				return -1;
			}

			return 1; /* the device is ok. carry on reading or writing */
	}
}

static int dev_evcb_sck_ready_stateless (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (events & HIO_DEV_EVENT_ERR)
	{
		int errcode;
		hio_scklen_t len;

		len = HIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt (hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr (rdev->hio, 0, errcode);
		}
		return -1;
	}
	else if (events & HIO_DEV_EVENT_HUP)
	{
		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read_stateful (hio_dev_t* dev, const void* data, hio_iolen_t dlen, const hio_devaddr_t* srcaddr)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return rdev->on_read(rdev, data, dlen, HIO_NULL);
}

static int dev_evcb_sck_on_write_stateful (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return rdev->on_write(rdev, wrlen, wrctx, HIO_NULL);
}

static int dev_evcb_sck_on_read_stateless (hio_dev_t* dev, const void* data, hio_iolen_t dlen, const hio_devaddr_t* srcaddr)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return rdev->on_read(rdev, data, dlen, srcaddr->ptr);
}

static int dev_evcb_sck_on_write_stateless (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return rdev->on_write(rdev, wrlen, wrctx, dstaddr->ptr);
}

/* ========================================================================= */

static hio_dev_evcb_t dev_sck_event_callbacks_stateful =
{
	dev_evcb_sck_ready_stateful,
	dev_evcb_sck_on_read_stateful,
	dev_evcb_sck_on_write_stateful
};

static hio_dev_evcb_t dev_sck_event_callbacks_stateless =
{
	dev_evcb_sck_ready_stateless,
	dev_evcb_sck_on_read_stateless,
	dev_evcb_sck_on_write_stateless
};
/* ========================================================================= */

static int dev_evcb_sck_ready_qx (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (events & HIO_DEV_EVENT_ERR)
	{
		int errcode;
		hio_scklen_t len;

		len = HIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) == -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt (hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr (rdev->hio, 0, errcode);
		}
		return -1;
	}
	else if (events & HIO_DEV_EVENT_HUP)
	{
		hio_seterrnum (hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}


static int dev_evcb_sck_on_read_qx (hio_dev_t* dev, const void* data, hio_iolen_t dlen, const hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (rdev->type == HIO_DEV_SCK_QX)
	{
		hio_dev_sck_qxmsg_t* qxmsg;

		if (dlen != HIO_SIZEOF(*qxmsg))
		{
			hio_seterrbfmt (hio, HIO_EINVAL, "wrong qx packet size");
			return 0;
		}

		qxmsg = (hio_dev_sck_qxmsg_t*)data;
		if (qxmsg->cmd == HIO_DEV_SCK_QXMSG_NEWCONN)
		{
			if (make_accepted_client_connection(rdev, qxmsg->syshnd, &qxmsg->remoteaddr, qxmsg->scktype) <= -1)
			{
/*printf ("unable to accept new client connection %d\n", qxmsg->syshnd);*/
				return (rdev->state & HIO_DEV_SCK_LENIENT)? 0: -1;
			}
		}
		else
		{
			hio_seterrbfmt (hio, HIO_EINVAL, "wrong qx command code");
			return 0;
		}

		return 0;
	}


	/* this is not for a qx socket */
	return rdev->on_read(rdev, data, dlen, HIO_NULL);
}

static int dev_evcb_sck_on_write_qx (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (rdev->type == HIO_DEV_SCK_QX)
	{
		/* this should not be called */
		return 0;
	}
	
	return rdev->on_write(rdev, wrlen, wrctx, HIO_NULL);
}

static hio_dev_evcb_t dev_sck_event_callbacks_qx =
{
	dev_evcb_sck_ready_qx,
	dev_evcb_sck_on_read_qx,
	dev_evcb_sck_on_write_qx
};

/* ========================================================================= */

static int dev_evcb_sck_ready_bpf (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
}

static int dev_evcb_sck_on_read_bpf (hio_dev_t* dev, const void* data, hio_iolen_t dlen, const hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
}

static int dev_evcb_sck_on_write_bpf (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_seterrnum (hio, HIO_ENOIMPL);
	return -1;
}

static hio_dev_evcb_t dev_sck_event_callbacks_bpf =
{
	dev_evcb_sck_ready_bpf,
	dev_evcb_sck_on_read_bpf,
	dev_evcb_sck_on_write_bpf
};

/* ========================================================================= */

hio_dev_sck_t* hio_dev_sck_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_sck_make_t* info)
{
	hio_dev_sck_t* rdev;

	if (info->type < 0 && info->type >= HIO_COUNTOF(sck_type_map))
	{
		hio_seterrnum (hio, HIO_EINVAL);
		return HIO_NULL;
	}

	if (info->type == HIO_DEV_SCK_QX)
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateless, &dev_sck_event_callbacks_qx, (void*)info);
	}
	else if (info->type == HIO_DEV_SCK_BPF)
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_bpf, &dev_sck_event_callbacks_bpf, (void*)info);
	}
	else if (sck_type_map[info->type].extra_dev_cap & HIO_DEV_CAP_STREAM) /* can't use the IS_STATEFUL() macro yet */
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateful, &dev_sck_event_callbacks_stateful, (void*)info);
	}
	else
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateless, &dev_sck_event_callbacks_stateless, (void*)info);
	}

	return rdev;
}

int hio_dev_sck_bind (hio_dev_sck_t* dev, hio_dev_sck_bind_t* info)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_SCK_BIND, info);
}

int hio_dev_sck_connect (hio_dev_sck_t* dev, hio_dev_sck_connect_t* info)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_SCK_CONNECT, info);
}

int hio_dev_sck_listen (hio_dev_sck_t* dev, hio_dev_sck_listen_t* info)
{
	return hio_dev_ioctl((hio_dev_t*)dev, HIO_DEV_SCK_LISTEN, info);
}

int hio_dev_sck_write (hio_dev_sck_t* dev, const void* data, hio_iolen_t dlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_devaddr_t devaddr;
	return hio_dev_write((hio_dev_t*)dev, data, dlen, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

int hio_dev_sck_writev (hio_dev_sck_t* dev, hio_iovec_t* iov, hio_iolen_t iovcnt, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_devaddr_t devaddr;
	return hio_dev_writev((hio_dev_t*)dev, iov, iovcnt, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

int hio_dev_sck_timedwrite (hio_dev_sck_t* dev, const void* data, hio_iolen_t dlen, const hio_ntime_t* tmout, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_devaddr_t devaddr;
	return hio_dev_timedwrite((hio_dev_t*)dev, data, dlen, tmout, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

int hio_dev_sck_timedwritev (hio_dev_sck_t* dev, hio_iovec_t* iov, hio_iolen_t iovcnt, const hio_ntime_t* tmout, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_devaddr_t devaddr;
	return hio_dev_timedwritev((hio_dev_t*)dev, iov, iovcnt, tmout, wrctx, skad_to_devaddr(dev, dstaddr, &devaddr));
}

/* ========================================================================= */
int hio_dev_sck_setsockopt (hio_dev_sck_t* dev, int level, int optname, void* optval, hio_scklen_t optlen)
{
	return setsockopt(dev->hnd, level, optname, optval, optlen);
}

int hio_dev_sck_getsockopt (hio_dev_sck_t* dev, int level, int optname, void* optval, hio_scklen_t* optlen)
{
	return getsockopt(dev->hnd, level, optname, optval, optlen);
}

int hio_dev_sck_getsockaddr (hio_dev_sck_t* dev, hio_skad_t* skad)
{
	hio_scklen_t addrlen = HIO_SIZEOF(*skad);
	if (getsockname(dev->hnd, (struct sockaddr*)skad, &addrlen) <= -1)
	{
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	return 0;
}

int hio_dev_sck_getpeeraddr (hio_dev_sck_t* dev, hio_skad_t* skad)
{
	hio_scklen_t addrlen = HIO_SIZEOF(*skad);
	if (getpeername(dev->hnd, (struct sockaddr*)skad, &addrlen) <= -1)
	{
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	return 0;
}

int hio_dev_sck_shutdown (hio_dev_sck_t* dev, int how)
{
	switch (how & (HIO_DEV_SCK_SHUTDOWN_READ | HIO_DEV_SCK_SHUTDOWN_WRITE))
	{
		case (HIO_DEV_SCK_SHUTDOWN_READ | HIO_DEV_SCK_SHUTDOWN_WRITE):
			how = SHUT_RDWR;
			break;

		case HIO_DEV_SCK_SHUTDOWN_READ:
			how = SHUT_RD;
			break;

		case HIO_DEV_SCK_SHUTDOWN_WRITE:
			how = SHUT_WR;
			break;

		default:
			hio_seterrnum (dev->hio, HIO_EINVAL);
			return -1;
	}

	if (shutdown(dev->hnd, how) <= -1)
	{
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}

	return 0;
}

int hio_dev_sck_sendfileok (hio_dev_sck_t* dev)
{
#if defined(USE_SSL)
	return !(dev->ssl);
#else
	return 1;
#endif
}

int hio_dev_sck_writetosidechan (hio_dev_sck_t* dev, const void* dptr, hio_oow_t dlen)
{
	if (write(dev->side_chan, dptr, dlen) <= -1) return -1; /* this doesn't set the error information. if you may check errno, though */
	return 0;
}

/* ========================================================================= */

hio_uint16_t hio_checksum_ip (const void* hdr, hio_oow_t len)
{
	hio_uint32_t sum = 0;
	hio_uint16_t *ptr = (hio_uint16_t*)hdr;

	while (len > 1)
	{
		sum += *ptr++;
		if (sum & 0x80000000)
		sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}
 
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

	return (hio_uint16_t)~sum;
}

