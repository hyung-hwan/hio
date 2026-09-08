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

#include <hio-sck.h>
#include "hio-prv.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h> /* writev */
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h> /* strerror */

#if defined(HAVE_NETINET_IN_H)
#	include <netinet/in.h>
#endif
#if defined(HAVE_NET_IF_H)
#	include <net/if.h>
#endif
#if defined(HAVE_NETINET_IF_ETHER_H)
#	include <netinet/if_ether.h>
#endif

/* HIO_ENABLE_SCTP is settled by configure (--enable-sctp, on by default where
 * the system provides it) and reported in its summary. it used to be derived
 * here from nothing but the presence of the header, so the feature came and
 * went with whatever happened to be installed and no one was told - which is
 * how the SCTP_INITMSG call below stayed wrong for so long. */
#if defined(HIO_ENABLE_SCTP) && defined(HAVE_NETINET_SCTP_H)
#	include <netinet/sctp.h>
#	if defined(IPPROTO_SCTP)
#		define ENABLE_SCTP
		/* these library calls are a separate question from the socket type. on
		 * linux they live in libsctp, which a machine with sctp headers and an
		 * sctp kernel may not have installed. the basic one-to-one and
		 * one-to-many transports need none of them, so they stand either way.
		 *
		 * peel-off and multi-homing are probed apart because they are separate
		 * features that share nothing but the library they come from: peel-off
		 * needs sctp_peeloff() and multi-homing needs the address calls, and
		 * neither calls into the other. bundling them would mean a system
		 * missing one silently loses both - the same "they always come
		 * together" assumption that let the old header-derived ENABLE_SCTP
		 * stay wrong for so long. */
#		if defined(HAVE_SCTP_PEELOFF)
#			define ENABLE_SCTP_PEELOFF
#		endif
#		if defined(HAVE_SCTP_BINDX) && defined(HAVE_SCTP_GETPADDRS) && defined(HAVE_SCTP_GETLADDRS)
#			define ENABLE_SCTP_MH
#		endif
#	endif
#endif

#if defined(HAVE_NETPACKET_PACKET_H)
#	include <netpacket/packet.h>
#endif

#if defined(HAVE_NET_IF_DL_H)
#	include <net/if_dl.h>
#endif

#if defined(HAVE_SYS_SENDFILE_H)
#	include <sys/sendfile.h>
#endif

/* sendfile() is not one function. the three live flavours disagree on the
 * operand order, on how the offset is passed, and on where the transferred
 * byte count comes back:
 *
 *   linux, solaris      ssize_t sendfile(int out, int in, off_t* off, size_t n);
 *                       socket first, offset by pointer and updated in place,
 *                       count is the return value.
 *
 *   freebsd, dragonfly  int sendfile(int in, int out, off_t off, size_t n,
 *                                    struct sf_hdtr* hdtr, off_t* sbytes, int flags);
 *                       file first, offset by value, count through sbytes,
 *                       return value is only 0 or -1.
 *
 *   darwin              int sendfile(int in, int out, off_t off, off_t* len,
 *                                    struct sf_hdtr* hdtr, int flags);
 *                       file first, len is in-out.
 *
 * the choice is made from the platform rather than from configure because
 * AC_CHECK_FUNCS only answers whether the symbol exists, not which of these
 * it is. this mirrors what libuv and nginx do for the same call. */
#if defined(HAVE_SENDFILE)
#	if defined(__FreeBSD__) || defined(__DragonFly__)
#		define USE_SENDFILE_BSD
#	elif defined(__APPLE__) && defined(__MACH__)
#		define USE_SENDFILE_DARWIN
#	else
#		define USE_SENDFILE_LINUX
#	endif
#endif

#if defined(HAVE_SYS_IOCTL_H)
#	include <sys/ioctl.h>
#endif

#if defined(HAVE_LINUX_FILTER_H)
#	include <linux/filter.h>
#endif

#if defined(HAVE_NET_BPF_H)
#	include <net/bpf.h>
	/* the bsds have no AF_PACKET. /dev/bpf is how they do the same job, so it
	 * is the implementation behind the L2 device types there rather than a
	 * device type of its own - see the note on HIO_DEV_SCK_PACKET. */
#	if defined(BIOCSETIF) && defined(BIOCGBLEN)
#		define USE_BPF
#	endif
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

#if defined(USE_SSL)
/* the largest plaintext record openssl hands back from a single SSL_read().
 * SSL3_RT_MAX_PLAIN_LENGTH is 16384 and the slack covers the padding and
 * compression allowance counted by SSL3_RT_MAX_ENCRYPTED_OVERHEAD. */
#define HIO_SSL_MAX_READ_RECORD (16384 + 2048)

/* see the note at SSL_set_read_ahead() in do_ssl(). the core reads through
 * hio->bigbuf, and that buffer being at least one record wide is what keeps
 * SSL_pending() at zero.
 *
 * the buffer is sized at run time now, so what is checked here is that the
 * floor the core refuses to go below is itself wide enough. the runtime
 * enforcement comes from that floor - see HIO_READ_BUFFER_SIZE. */
HIO_STATIC_ASSERT(HIO_MIN_READ_BUFFER_SIZE >= HIO_SSL_MAX_READ_RECORD);
#endif

/* ========================================================================= */

static hio_syshnd_t open_async_socket (hio_t* hio, int domain, int type, int proto)
{
	hio_syshnd_t sck = HIO_SYSHND_INVALID;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && !(defined(__BEOS__) || defined(__HAIKU__))
	/* haikuos accepts SOCK_NONBLOCK but the returned socket is still blocking. make haikuos an exception */
	type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
open_socket:
#endif
	sck = socket(domain, type, proto);
	if (sck == HIO_SYSHND_INVALID)
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && !(defined(__BEOS__) || defined(__HAIKU__))
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
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && !(defined(__BEOS__) || defined(__HAIKU__))
		if (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) goto done;
	#endif
	}

	if (hio_makesyshndasync(hio, sck) <= -1 ||
	    hio_makesyshndcloexec(hio, sck) <= -1) goto oops;

done:
	return sck;

oops:
	hio_seterrwithsyserr(hio, 0, errno);
	if (sck != HIO_SYSHND_INVALID) close(sck);
	return HIO_SYSHND_INVALID;
}

static hio_syshnd_t open_async_qx (hio_t* hio, hio_syshnd_t* side_chan)
{
	int fd[2];
	int type = SOCK_DGRAM;

#if defined(__BEOS__) || defined(__HAIKU__)
	/* on haiku os r6beta, SOCK_DGRAM isn't reliable. more than one write() on it causes SIGPIPE */
	type = SOCK_STREAM;
#else
	type = SOCK_DGRAM;
#endif

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && !(defined(__BEOS__) || defined(__HAIKU__))
	/* haikuos defines SOCK_NONBLOCK and socket accepts it but the socket is still blocking. make haikuos an exception */
	type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
open_socket:
#endif
	if (socketpair(AF_UNIX, type, 0, fd) <= -1)
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && !(defined(__BEOS__) || defined(__HAIKU__))
		if (errno == EINVAL && (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)))
		{
			type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
			goto open_socket;
		}
	#endif
		hio_seterrwithsyserr(hio, 0, errno);
		return HIO_SYSHND_INVALID;
	}
	else
	{
	#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && !(defined(__BEOS__) || defined(__HAIKU__))
		if (type & (SOCK_NONBLOCK | SOCK_CLOEXEC)) goto done;
	#endif
	}

	if (hio_makesyshndasync(hio, fd[0]) <= -1 ||
	    hio_makesyshndasync(hio, fd[1]) <= -1 ||
	    hio_makesyshndcloexec(hio, fd[0]) <= -1 ||
	    hio_makesyshndcloexec(hio, fd[1]) <= -1)
	{
		hio_seterrwithsyserr(hio, 0, errno);
		close(fd[0]);
		close(fd[1]);
		return HIO_SYSHND_INVALID;
	}

done:
	*side_chan = fd[1]; /* write end of the pipe */
	return fd[0]; /* read end of the pipe */
}

#if defined(USE_BPF)
/* one read of a bpf device yields a run of frames, each behind a struct
 * bpf_hdr, not the single message a recvfrom() gives. so the run is kept here
 * and handed to on_read() one frame at a time - which is what the caller of a
 * datagram-like device expects, and it keeps struct bpf_hdr out of its sight.
 *
 * 'capa' is BIOCGBLEN, the size a read of this device must use. it is not
 * dev_rdmin: the loop's shared buffer only ever carries one frame. */
typedef struct bpf_state_t bpf_state_t;
struct bpf_state_t
{
	hio_uint8_t* buf;
	hio_oow_t capa;
	hio_oow_t len;
	hio_oow_t pos;
};

/* open a bpf device and put it in the state the read method assumes: delivering
 * as soon as a frame arrives rather than when the buffer fills, and reporting
 * the read size it insists on.
 *
 * '/dev/bpf' is the cloning device on anything current. the numbered ones are
 * the older interface, tried in turn because a system may have only those and
 * because each can be held by one reader at a time. */
static hio_syshnd_t open_async_bpf (hio_t* hio, unsigned int* bufsize)
{
	hio_syshnd_t fd = HIO_SYSHND_INVALID;
	int tmp;
	int i;

	fd = open("/dev/bpf", O_RDWR);
	if (fd == HIO_SYSHND_INVALID)
	{
		for (i = 0; i < 256; i++)
		{
			hio_bch_t path[32];
			hio_fmttobcstr(hio, path, HIO_COUNTOF(path), "/dev/bpf%d", i);
			fd = open(path, O_RDWR);
			if (fd != HIO_SYSHND_INVALID) break;
			if (errno != EBUSY) break; /* not "in use by someone else" - stop */
		}
		if (fd == HIO_SYSHND_INVALID) goto oops;
	}

	/* without this a read waits for the buffer to fill, which for a device
	 * driven by a poll loop means waiting for traffic that may never come */
	tmp = 1;
	if (ioctl(fd, BIOCIMMEDIATE, &tmp) <= -1) goto oops;

	/* a read on a bpf device must use exactly this size. it is the size of the
	 * private buffer the read method keeps, not of the loop's shared one - one
	 * read yields many frames and they are handed over one at a time. */
	if (ioctl(fd, BIOCGBLEN, bufsize) <= -1) goto oops;

	if (hio_makesyshndasync(hio, fd) <= -1 ||
	    hio_makesyshndcloexec(hio, fd) <= -1) goto oops_no_syserr;

	return fd;

oops:
	hio_seterrwithsyserr(hio, 0, errno);
oops_no_syserr:
	if (fd != HIO_SYSHND_INVALID) close(fd);
	return HIO_SYSHND_INVALID;
}

static int dev_sck_read_bpf (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	bpf_state_t* st = (bpf_state_t*)rdev->bpf_state;
	struct bpf_hdr* bh;
	hio_oow_t caplen;

	HIO_ASSERT(hio, st != HIO_NULL);

	if (st->pos >= st->len)
	{
		/* the run is spent. a read must use exactly the BIOCGBLEN size. */
		ssize_t x = read(rdev->hnd, st->buf, st->capa);
		if (x <= -1)
		{
			int eno = errno;
			if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;
			if (eno == EINTR) return 0;
			hio_seterrwithsyserr(hio, 0, eno);
			return -1;
		}
		if (x == 0) return 0;
		st->len = (hio_oow_t)x;
		st->pos = 0;
	}

	/* a truncated trailer would make the walk step into nothing */
	if (st->len - st->pos < HIO_SIZEOF(*bh))
	{
		st->pos = st->len = 0;
		return 0;
	}

	bh = (struct bpf_hdr*)(st->buf + st->pos);
	caplen = bh->bh_caplen;
	if (bh->bh_hdrlen + caplen > st->len - st->pos)
	{
		/* the frame claims to run past what was read - refuse the whole run
		 * rather than hand over whatever follows in memory */
		HIO_INFO1(hio, "SCK(%p) - discarding a malformed bpf read\n", rdev);
		st->pos = st->len = 0;
		return 0;
	}

	if (caplen > (hio_oow_t)*len) caplen = (hio_oow_t)*len; /* dev_rdmin makes this unlikely */
	HIO_MEMCPY(buf, st->buf + st->pos + bh->bh_hdrlen, caplen);
	st->pos += BPF_WORDALIGN(bh->bh_hdrlen + bh->bh_caplen);

	/* bpf reports no source address, so the bound interface is the answer -
	 * which is what an AF_PACKET read reports too */
	srcaddr->ptr = &rdev->localaddr;
	srcaddr->len = HIO_SIZEOF(rdev->localaddr);

	*len = (hio_iolen_t)caplen;
	return 1;
}

/* what is left of the last read. the core re-dispatches an IN event for as long
 * as this says yes, so a run of frames is delivered without polling in between
 * and none is dropped for arriving in company. */
static int dev_sck_readpending_bpf (hio_dev_t* dev)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	bpf_state_t* st = (bpf_state_t*)rdev->bpf_state;
	return st && st->pos < st->len;
}

static int dev_sck_write_bpf (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;

	/* a write goes out of the interface the device is bound to. there is no
	 * destination to name - the frame carries its own. */
	x = write(rdev->hnd, data, *len);
	if (x <= -1)
	{
		int eno = errno;
		if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;
		if (eno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, eno);
		return -1;
	}

	*len = (hio_iolen_t)x;
	return 1;
}

static int dev_sck_writev_bpf (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;

	x = writev(rdev->hnd, (const struct iovec*)iov, *iovcnt);
	if (x <= -1)
	{
		int eno = errno;
		if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;
		if (eno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, eno);
		return -1;
	}

	/* the same convention the other writev methods use: the byte count goes
	 * back through iovcnt */
	*iovcnt = (hio_iolen_t)x;
	return 1;
}
#endif

/* ========================================================================= */

static hio_devaddr_t* skad_to_devaddr (hio_dev_sck_t* dev, const hio_skad_t* sckaddr, hio_devaddr_t* devaddr)
{
	if (sckaddr)
	{
		devaddr->ptr = (void*)sckaddr;
		devaddr->len = hio_skad_get_size(sckaddr);
		return devaddr;
	}

	return HIO_NULL;
}

static HIO_INLINE hio_skad_t* devaddr_to_skad (hio_dev_sck_t* dev, const hio_devaddr_t* devaddr, hio_skad_t* sckaddr)
{
	return (hio_skad_t*)devaddr->ptr;
}

/* ========================================================================= */

#define IS_STREAM(sck) ((sck)->dev_cap & HIO_DEV_CAP_STREAM)

struct sck_type_map_t
{
	int domain;
	int type;
	int proto;

	unsigned int connectable: 1;
	unsigned int listenable: 1;

	hio_bitmask_t extra_dev_cap;
};

/* not a real address family. it marks the rows whose socket is opened by
 * open_async_bpf() rather than socket(), the way HIO_AF_QX marks the one opened
 * by open_async_qx(). */
#define __AF_BPF 999998

static struct sck_type_map_t sck_type_map[] =
{
	/* HIO_DEV_SCK_QX */
	{ HIO_AF_QX, 0,               0,                 0, 0, 0 },

#if defined(AF_UNIX)
	{ AF_UNIX,   SOCK_STREAM,     0,                 1, 1, HIO_DEV_CAP_STREAM },
#else
	{ -1,        0,               0,                 0, 0, 0 },
#endif

	/* HIO_DEV_SCK_TCP4 */
	{ AF_INET,   SOCK_STREAM,     0,                 1, 1, HIO_DEV_CAP_STREAM },

	/* HIO_DEV_SCK_TCP6 */
	{ AF_INET6,  SOCK_STREAM,     0,                 1, 1, HIO_DEV_CAP_STREAM },

	/* HIO_DEV_SCK_UPD4 */ /* TODO: the socket api allows connect() on UDP sockets. should i mark it connectable? */
	{ AF_INET,   SOCK_DGRAM,      0,                 0, 0, 0 },

	/* HIO_DEV_SCK_UDP6 */
	{ AF_INET6,  SOCK_DGRAM,      0,                 0, 0, 0 },

#if defined(ENABLE_SCTP)
	/* HIO_DEV_SCK_SCTP4 */
	{ AF_INET,   SOCK_STREAM,     IPPROTO_SCTP,      1, 1, HIO_DEV_CAP_STREAM },

	/* HIO_DEV_SCK_SCTP6 */
	{ AF_INET6,  SOCK_STREAM,     IPPROTO_SCTP,      1, 1, HIO_DEV_CAP_STREAM },

	/* HIO_DEV_SCK_SCTP4_SEQPKT - one-to-many. listen() is called but hio never
	 * accepts: associations are not devices in this model, they are told apart
	 * by the source address on each message.
	 *
	 * marked unconnectable, as udp is. the socket api does allow connect() on a
	 * one-to-many socket - it forms an association and makes it the default
	 * destination - but nothing here can finish the job: a device with no
	 * HIO_DEV_CAP_STREAM gets dev_evcb_sck_ready_stateless(), which looks only
	 * at ERR and HUP and never at the progress bits, so on_connect() would
	 * never fire. the device would sit in HIO_DEV_SCK_CONNECTING for good -
	 * writes failing, a second connect() refused as already in progress, and
	 * with a connect timeout set, silently halted a few seconds later.
	 * refusing outright beats handing back a bricked device. */
	{ AF_INET,   SOCK_SEQPACKET,  IPPROTO_SCTP,      0, 1, 0 },

	/* HIO_DEV_SCK_SCTP6_SEQPKT */
	{ AF_INET6,  SOCK_SEQPACKET,  IPPROTO_SCTP,      0, 1, 0 },
#else
	{ -1,        0,               0,                 0, 0, 0 },
	{ -1,        0,               0,                 0, 0, 0 },
	{ -1,        0,               0,                 0, 0, 0 },
	{ -1,        0,               0,                 0, 0, 0 },
#endif

	/* HIO_DEV_SCK_ICMP4 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET,    SOCK_RAW,       IPPROTO_ICMP,      0, 0, 0 },

	/* HIO_DEV_SCK_ICMP6 - IP protocol field is 1 byte only. no byte order conversion is needed */
	{ AF_INET6,   SOCK_RAW,       IPPROTO_ICMP,      0, 0, 0 },


#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* HIO_DEV_SCK_ARP - Ethernet type is 2 bytes long. Protocol must be specified in the network byte order */
	{ AF_PACKET,  SOCK_RAW,       HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0, 0, 0 },

	/* HIO_DEV_SCK_ARP_DGRAM - link-level header removed*/
	{ AF_PACKET,  SOCK_DGRAM,     HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0, 0, 0 },

#elif defined(USE_BPF)
	/* [NOTE] AF_LINK used to be named here, and it cannot work: socket(AF_LINK,
	 * SOCK_RAW, 0) is EAFNOSUPPORT on freebsd even as root - AF_LINK is an
	 * address family for interface addresses, not a socket domain. these types
	 * were dead on the bsds until /dev/bpf was wired in behind them. */

	/* HIO_DEV_SCK_ARP - bpf plus an ethertype filter, so the type keeps the
	 * meaning the protocol argument gives it on linux */
	{ __AF_BPF, 0,                HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0, 0, 0 },

	/* HIO_DEV_SCK_ARP_DGRAM */
	{ __AF_BPF, 0,                HIO_CONST_HTON16(HIO_ETHHDR_PROTO_ARP), 0, 0, 0 },
#else
	{ -1,       0,                0,                 0, 0, 0 },
	{ -1,       0,                0,                 0, 0, 0 },
#endif

#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	/* HIO_DEV_SCK_PACKET */
	{ AF_PACKET,  SOCK_RAW,       HIO_CONST_HTON16(ETH_P_ALL), 0, 0, 0 }
#elif defined(USE_BPF)
	/* HIO_DEV_SCK_PACKET - every frame, so no filter */
	{ __AF_BPF,   0,              0, 0, 0, 0 }
#else
	{ -1,       0,                0,                   0, 0, 0 }
#endif

};

/* how much read buffer one device of this type needs in a single read.
 *
 * a stream is content to be read in pieces and asks for nothing - a short read
 * leaves the remainder for the next one. a datagram socket is not: whatever
 * does not fit in one recvfrom() is discarded with nothing reported, so it asks
 * for enough to hold the largest datagram it could ever see. the qx channel is
 * message-oriented too, but its message is a fixed struct, so it asks for only
 * that much.
 *
 * both the listening/connecting path and the accepted path go through here, so
 * the two cannot drift apart. */
static hio_oow_t sck_type_rdmin (hio_dev_sck_type_t type)
{
	if (sck_type_map[type].domain == HIO_AF_QX) return HIO_SIZEOF(hio_dev_sck_qxmsg_t);
	if (!(sck_type_map[type].extra_dev_cap & HIO_DEV_CAP_STREAM)) return HIO_DGRAM_READ_BUFFER_SIZE;
	return 0;
}

/* ======================================================================== */

static void connect_timedout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	HIO_ASSERT(hio, IS_STREAM(rdev));

	if (rdev->state & HIO_DEV_SCK_CONNECTING)
	{
		/* the state check for HIO_DEV_TCP_CONNECTING is actually redundant
		 * as it must not be fired  after it gets connected. the timer job
		 * doesn't need to be deleted when it gets connected for this check
		 * here. this libarary, however, deletes the job when it gets
		 * connected. */
		HIO_DEBUG1(hio, "SCK(%p) - connect timed out. halting\n", rdev);
		hio_dev_sck_halt(rdev);
	}
}

static void ssl_accept_timedout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	HIO_ASSERT(hio, IS_STREAM(rdev));

	if (rdev->state & HIO_DEV_SCK_ACCEPTING_SSL)
	{
		HIO_DEBUG1(hio, "SCK(%p) - ssl-accept timed out. halting\n", rdev);
		hio_dev_sck_halt(rdev);
	}
}

static void ssl_connect_timedout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	HIO_ASSERT(hio, IS_STREAM(rdev));

	if (rdev->state & HIO_DEV_SCK_CONNECTING_SSL)
	{
		HIO_DEBUG1(hio, "SCK(%p) - ssl-connect timed out. halting\n", rdev);
		hio_dev_sck_halt(rdev);
	}
}

static HIO_INLINE int schedule_timer_job_at (hio_dev_sck_t* dev, const hio_ntime_t* fire_at, hio_tmrjob_handler_t handler)
{
	return hio_schedtmrjobat(dev->hio, fire_at, handler, &dev->tmrjob_index, dev);
}

static HIO_INLINE int schedule_timer_job_after (hio_dev_sck_t* dev, const hio_ntime_t* fire_after, hio_tmrjob_handler_t handler)
{
	return hio_schedtmrjobafter(dev->hio, fire_after, handler, &dev->tmrjob_index, dev);
}

/* ======================================================================== */
#if defined(USE_SSL)
static void set_ssl_error(hio_t* hio, int sslerr)
{
	hio_bch_t emsg[128];
	ERR_error_string_n (sslerr, emsg, HIO_COUNTOF(emsg));
	hio_seterrbfmt(hio, HIO_ESYSERR, "%hs", emsg);
}
#endif

static int dev_sck_make (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_dev_sck_make_t* arg = (hio_dev_sck_make_t*)ctx;
	hio_syshnd_t hnd = HIO_SYSHND_INVALID;
	hio_syshnd_t side_chan = HIO_SYSHND_INVALID;
	int is_qx;

	HIO_ASSERT(hio, arg->type >= 0 && arg->type < HIO_COUNTOF(sck_type_map));

	/* initialize some fields first where 0 is not somthing initial or invalid. */
	rdev->hnd = HIO_SYSHND_INVALID;
	rdev->tmrjob_index = HIO_TMRIDX_INVALID;

	is_qx = (sck_type_map[arg->type].domain == HIO_AF_QX);
	if (HIO_UNLIKELY(is_qx)) rdev->u.qx.side_chan = HIO_SYSHND_INVALID;

	if (sck_type_map[arg->type].domain <= -1)
	{
		hio_seterrnum(hio, HIO_ENOIMPL); /* TODO: better error info? */
		goto oops;
	}

#if defined(USE_BPF)
	if (HIO_UNLIKELY(sck_type_map[arg->type].domain == __AF_BPF))
	{
		bpf_state_t* st;
		unsigned int bufsize = 0;

		hnd = open_async_bpf(hio, &bufsize);
		if (hnd == HIO_SYSHND_INVALID) goto oops;

		st = (bpf_state_t*)hio_callocmem(hio, HIO_SIZEOF(*st));
		if (HIO_UNLIKELY(!st))
		{
			close(hnd);
			goto oops;
		}

		st->capa = bufsize;
		st->buf = (hio_uint8_t*)hio_allocmem(hio, st->capa);
		if (HIO_UNLIKELY(!st->buf))
		{
			hio_freemem(hio, st);
			close(hnd);
			goto oops;
		}
		rdev->u.bpf.state = st;
	}
	else
#endif
	if (HIO_UNLIKELY(is_qx))
	{
		hnd = open_async_qx(hio, &side_chan);
		if (hnd == HIO_SYSHND_INVALID) goto oops;
	}
	else
	{
		hnd = open_async_socket(hio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
		if (hnd == HIO_SYSHND_INVALID) goto oops;

	#if defined(ENABLE_SCTP)
		if (sck_type_map[arg->type].proto == IPPROTO_SCTP)
		{
			struct sctp_event_subscribe ev;

			/* [NOTE] this used to hand a struct sctp_initmsg to SCTP_EVENTS,
			 * which takes a struct sctp_event_subscribe. eight zero octets
			 * read as "subscribe to nothing", so it returned success and did
			 * nothing at all - and the SCTP_INITMSG that was meant to request
			 * streams never happened. */
			if (arg->sctp_ostreams > 0 || arg->sctp_instreams > 0)
			{
				struct sctp_initmsg im;
				HIO_MEMSET(&im, 0, HIO_SIZEOF(im));
				im.sinit_num_ostreams = arg->sctp_ostreams;
				im.sinit_max_instreams = arg->sctp_instreams;
				if (setsockopt(hnd, IPPROTO_SCTP, SCTP_INITMSG, &im, HIO_SIZEOF(im)) <= -1)
				{
					hio_seterrwithsyserr(hio, 0, errno);
					goto oops;
				}
			}

			/* sctp_data_io_event is what delivers the per-message ancillary
			 * data - the stream number among it - so it is required for any
			 * of the stream handling to work.
			 *
			 * the rest are notifications: association up/down, a path
			 * changing state, a send that failed. they arrive interleaved
			 * with data on this same socket, flagged MSG_NOTIFICATION, and
			 * are routed to on_notification() rather than on_read(). */
			HIO_MEMSET(&ev, 0, HIO_SIZEOF(ev));
			ev.sctp_data_io_event = 1;
			ev.sctp_association_event = 1;
			ev.sctp_address_event = 1;
			ev.sctp_send_failure_event = 1;
			ev.sctp_peer_error_event = 1;
			ev.sctp_shutdown_event = 1;
			if (setsockopt(hnd, IPPROTO_SCTP, SCTP_EVENTS, &ev, HIO_SIZEOF(ev)) <= -1)
			{
				hio_seterrwithsyserr(hio, 0, errno);
				goto oops;
			}
		}
	#endif
	}

	rdev->hnd = hnd;
	rdev->dev_cap = HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT | sck_type_map[arg->type].extra_dev_cap;
	if (HIO_UNLIKELY(is_qx)) rdev->u.qx.side_chan = side_chan;

	rdev->dev_rdmin = sck_type_rdmin(arg->type);
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	rdev->on_connect = arg->on_connect;
	rdev->on_disconnect = arg->on_disconnect;
	rdev->on_notification = arg->on_notification;
	rdev->on_raw_accept = arg->on_raw_accept;
	rdev->type = arg->type;

	if (arg->options & HIO_DEV_SCK_MAKE_LENIENT) rdev->state |= HIO_DEV_SCK_LENIENT;

	return 0;

oops:
	if (hnd != HIO_SYSHND_INVALID) close(hnd);
	if (side_chan != HIO_SYSHND_INVALID) close(side_chan);
	return -1;
}

/* what make_accepted_client_connection() hands to dev_sck_make_client() */
typedef struct sck_make_client_ctx_t sck_make_client_ctx_t;
struct sck_make_client_ctx_t
{
	hio_syshnd_t hnd;
	hio_dev_sck_type_t type;
};

static int dev_sck_make_client (hio_dev_t* dev, void* ctx)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	sck_make_client_ctx_t* mc = (sck_make_client_ctx_t*)ctx;
	int is_qx;

	/* create a socket device that is made of a socket connection
	 * on a listening socket.
	 * nothing special is done here except setting the socket handle.
	 * most of the initialization is done by the listening socket device
	 * after a client socket has been created. */

	rdev->hnd = mc->hnd;
	rdev->tmrjob_index = HIO_TMRIDX_INVALID;

	is_qx = (mc->type == HIO_DEV_SCK_QX);
	if (is_qx) rdev->u.qx.side_chan = HIO_SYSHND_INVALID;

	/* the type is settled again by the caller, but dev_rdmin has to be known
	 * before this method returns: hio_dev_make() checks it against the loop's
	 * read buffer the moment make() is done, and there is no second chance to
	 * refuse the device after that. */
	rdev->type = mc->type;
	rdev->dev_rdmin = sck_type_rdmin(mc->type);

	if (hio_makesyshndasync(hio, rdev->hnd) <= -1 ||
	    hio_makesyshndcloexec(hio, rdev->hnd) <= -1) goto oops;

	return 0;

oops:
	if (rdev->hnd != HIO_SYSHND_INVALID)
	{
		close(rdev->hnd);
		rdev->hnd = HIO_SYSHND_INVALID;
	}
	return -1;
}

/* hio_dev_make() hands this back exactly what was passed to it, for the case
 * where it fails before the make() method could take ownership of the handle.
 *
 * [NOTE] the ctx used to be a bare hio_syshnd_t and is now a struct whose first
 * member is one. the old cast still read the right value - a pointer to a
 * struct points to its first member - but only by accident of layout: put any
 * field ahead of 'hnd' and this would close (int)type instead, which is a small
 * integer, which is to say some other part of the program's descriptor. */
static void dev_sck_fail_before_make_client (void* ctx)
{
	sck_make_client_ctx_t* mc = (sck_make_client_ctx_t*)ctx;
	close(mc->hnd);
}

static int dev_sck_kill (hio_dev_t* dev, int force)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	/* remembered for the trace at the end, by which point rdev->hnd has been
	 * closed and reset. HIO_UNUSED because that trace is the only reader and
	 * it compiles away in a release build. */
	hio_syshnd_t hnd HIO_UNUSED = rdev->hnd;

	HIO_DEBUG2(hio, "SCK(%p) - being killed [%d]\n", rdev, rdev->hnd);
#if 0
	if (IS_STREAM(rdev))
	{
		/*if (HIO_DEV_SCK_GET_PROGRESS(rdev))
		{*/
			/* for HIO_DEV_SCK_CONNECTING, HIO_DEV_SCK_CONNECTING_SSL, and HIO_DEV_SCK_ACCEPTING_SSL
			 * on_disconnect() is called without corresponding on_connect().
			 * it is the same if connect or accept has not been called. */
			if (rdev->on_disconnect) rdev->on_disconnect(rdev);
		/*}*/
	}
	else
	{
		/* non-stream, but lisenable or connectable can have the progress bits on */
		/*HIO_ASSERT(hio, (rdev->state & HIO_DEV_SCK_ALL_PROGRESS_BITS) == 0);*/

		if (rdev->on_disconnect) rdev->on_disconnect(rdev);
	}
#else
	if (rdev->on_disconnect) rdev->on_disconnect(rdev);
#endif
	if (rdev->tmrjob_index != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob(hio, rdev->tmrjob_index);
		HIO_ASSERT(hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);
	}

#if defined(USE_SSL)
	if (rdev->ssl)
	{
		SSL_shutdown((SSL*)rdev->ssl); /* is this needed? */
		SSL_free((SSL*)rdev->ssl);
		rdev->ssl = HIO_NULL;
	}
	if (!(rdev->state & (HIO_DEV_SCK_ACCEPTED | HIO_DEV_SCK_ACCEPTING_SSL)) && rdev->ssl_ctx)
	{
		SSL_CTX_free((SSL_CTX*)rdev->ssl_ctx);
		rdev->ssl_ctx = HIO_NULL;
	}
#endif

	if (rdev->hnd != HIO_SYSHND_INVALID)
	{
		close(rdev->hnd);
		rdev->hnd = HIO_SYSHND_INVALID;
	}

	if (rdev->type == HIO_DEV_SCK_QX && rdev->u.qx.side_chan != HIO_SYSHND_INVALID)
	{
		close(rdev->u.qx.side_chan);
		rdev->u.qx.side_chan = HIO_SYSHND_INVALID;
	}

#if defined(USE_BPF)
	if (rdev->type == HIO_DEV_SCK_PACKET && rdev->u.bpf.state)
	{
		bpf_state_t* st = (bpf_state_t*)rdev->bpf_state;
		if (st->buf) hio_freemem(hio, st->buf);
		hio_freemem(hio, st);
		rdev->u.bpf.state = HIO_NULL;
	}
#endif

	HIO_DEBUG2(hio, "SCK(%p) - killed [%d]\n", rdev, (int)hnd);
	return 0;
}

static hio_syshnd_t dev_sck_getsyshnd (hio_dev_t* dev)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return (hio_syshnd_t)rdev->hnd;
}
/* ------------------------------------------------------------------------------ */

#if defined(USE_SSL)
/* an SSL_read() may need to write and an SSL_write() may need to read - during
 * a tls 1.2 renegotiation or a tls 1.3 key update, for instance. record the
 * direction the tls layer is blocked on so that hio_dev_watch() keeps watching
 * it until the stalled operation gets through. pass 0 to clear.
 *
 * HIO_DEV_WATCH_RENEW recomputes the natural event set - input unless the
 * application disabled it, output only when the write queue is non-empty -
 * and hio_dev_watch() then ORs dev_extra_events on top of that. so clearing
 * this and renewing puts the device back exactly where it would have been. */
static int ssl_want_events (hio_dev_sck_t* rdev, int events)
{
	if (rdev->dev_extra_events == events) return 0;
	rdev->dev_extra_events = events;
	return hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN);
}
#endif

static int dev_sck_read_stream (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
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
			if (err == SSL_ERROR_WANT_READ)
			{
				/* no data yet. input is watched already. */
				return (ssl_want_events(rdev, 0) <= -1)? -1: 0;
			}
			if (err == SSL_ERROR_WANT_WRITE)
			{
				/* the tls layer must push bytes out before it can decrypt any
				 * more. watch output even with nothing queued for writing. */
				return (ssl_want_events(rdev, HIO_DEV_EVENT_OUT) <= -1)? -1: 0;
			}
			set_ssl_error(hio, err);
			return -1;
		}

		if (ssl_want_events(rdev, 0) <= -1) return -1;

		/* openssl buffers whole records internally and bytes left in that
		 * buffer are invisible to epoll and kqueue - a level-triggered
		 * readiness notification never fires for them, so they would sit
		 * there until the peer happens to send more. read_ahead being off
		 * plus a buffer wider than a record is what prevents it. */
		HIO_ASSERT(hio, SSL_pending((SSL*)rdev->ssl) == 0);
		*len = x;
	}
	else
	{
#endif
		ssize_t x;

		x = recv(rdev->hnd, buf, *len, 0);
		if (x <= -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data available */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr(hio, 0, errno);
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

		hio_seterrwithsyserr(hio, 0, eno);

		HIO_DEBUG2(hio, "SCK(%p) - recvfrom failure - %hs", rdev, strerror(eno));
		return -1;
	}

	srcaddr->ptr = &rdev->remoteaddr;
	srcaddr->len = srcaddrlen;

	*len = x;
	return 1;
}



#if defined(ENABLE_SCTP)
static int recvmsg_sctp(
	int s, void* ptr, hio_oow_t len, struct sockaddr* srcaddr, hio_scklen_t* srcaddrlen,
	struct sctp_sndrcvinfo* sinfo, int* msg_flags)
{
	int n;
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr* cmsg;
	hio_uint8_t cmsg_buf[CMSG_SPACE(HIO_SIZEOF(*sinfo))];

	iov.iov_base = ptr;
	iov.iov_len = len;

	HIO_MEMSET(&msg, 0, HIO_SIZEOF(msg));
	msg.msg_name = srcaddr;
	msg.msg_namelen = *srcaddrlen;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = HIO_SIZEOF(cmsg_buf);

	n = recvmsg(s, &msg, 0);
	if (n <= -1) return n;

	*srcaddrlen = msg.msg_namelen;
	if (msg_flags) *msg_flags = msg.msg_flags;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg ; cmsg = CMSG_NXTHDR(&msg, cmsg))
	{
		if (cmsg->cmsg_level == IPPROTO_SCTP && cmsg->cmsg_type == SCTP_SNDRCV)
		{
			HIO_MEMCPY(sinfo, CMSG_DATA(cmsg), HIO_SIZEOF(*sinfo));
			break;
		}
	}

	return n;
}

static int sendmsg_sctp(
	int s, const hio_iovec_t* iov, hio_oow_t iovcnt,struct sockaddr* dstaddr, hio_scklen_t dstaddrlen,
	hio_uint32_t ppid, hio_uint32_t flags, hio_uint16_t stream_no, hio_uint32_t ttl, hio_uint32_t context,
	hio_int32_t assoc_id)
{
	struct sctp_sndrcvinfo* sinfo;
	struct msghdr msg;
	struct cmsghdr* cmsg;
	hio_uint8_t cmsg_buf[CMSG_SPACE(HIO_SIZEOF(*sinfo))];

	/* both are handed to the kernel, so neither may carry anything
	 * uninitialised. recvmsg_sctp() above already zeroes its msghdr and this
	 * one did not - struct msghdr has padding members on some ABIs. the
	 * control buffer is fully written below, so zeroing it is belt and braces. */
	HIO_MEMSET(&msg, 0, HIO_SIZEOF(msg));
	HIO_MEMSET(cmsg_buf, 0, HIO_SIZEOF(cmsg_buf));

	msg.msg_name = dstaddr;
	msg.msg_namelen = dstaddrlen;
	msg.msg_iov = (struct iovec*)iov;
	msg.msg_iovlen = iovcnt;

	msg.msg_control = cmsg_buf;
	msg.msg_controllen = HIO_SIZEOF(cmsg_buf);
	msg.msg_flags = 0;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = IPPROTO_SCTP;
	cmsg->cmsg_type = SCTP_SNDRCV;
	cmsg->cmsg_len = CMSG_LEN(HIO_SIZEOF(*sinfo));

	msg.msg_controllen = cmsg->cmsg_len;
	sinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
	HIO_MEMSET(sinfo, 0, HIO_SIZEOF(*sinfo));
	sinfo->sinfo_ppid = ppid;
	sinfo->sinfo_flags = flags;
	sinfo->sinfo_stream = stream_no;
	sinfo->sinfo_timetolive = ttl;
	sinfo->sinfo_context = context;
	/* on a one-to-many socket a non-zero association id names the association
	 * directly, which survives a peer changing address; zero falls back to
	 * addressing by msg_name. it is ignored on a one-to-one socket. */
	sinfo->sinfo_assoc_id = (sctp_assoc_t)assoc_id;

	/* the plain stream and stateless send paths all ask for this; without it a
	 * failing sctp write raises SIGPIPE and kills the process where the
	 * equivalent tcp write merely returns EPIPE. */
#if defined(MSG_NOSIGNAL)
	return sendmsg(s, &msg, MSG_NOSIGNAL);
#else
	return sendmsg(s, &msg, 0);
#endif
}

/* the core passes no destination for a connected socket - hio_dev_sck_write()
 * with a null dstaddr - so these must not be dereferenced blindly. an absent
 * destination means "the peer we are associated with", stream 0. */
static HIO_INLINE struct sockaddr* dstaddr_sockaddr (const hio_devaddr_t* dstaddr)
{
	return (dstaddr && dstaddr->ptr)? (struct sockaddr*)dstaddr->ptr: HIO_NULL;
}

static HIO_INLINE hio_scklen_t dstaddr_len (const hio_devaddr_t* dstaddr)
{
	return (dstaddr && dstaddr->ptr)? (hio_scklen_t)dstaddr->len: 0;
}

static HIO_INLINE hio_uint16_t dstaddr_chan (const hio_devaddr_t* dstaddr)
{
	return (dstaddr && dstaddr->ptr)? hio_skad_get_chan((const hio_skad_t*)dstaddr->ptr): 0;
}

static HIO_INLINE hio_uint32_t dstaddr_ppid (const hio_devaddr_t* dstaddr)
{
	return (dstaddr && dstaddr->ptr)? hio_skad_get_ppid((const hio_skad_t*)dstaddr->ptr): 0;
}

static HIO_INLINE hio_int32_t dstaddr_assoc (const hio_devaddr_t* dstaddr)
{
	return (dstaddr && dstaddr->ptr)? hio_skad_get_assoc((const hio_skad_t*)dstaddr->ptr): 0;
}

static int dev_sck_read_sctp_seqpkt (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_scklen_t srcaddrlen;
	ssize_t x;
	int msg_flags;
	struct sctp_sndrcvinfo sri;

	srcaddrlen = HIO_SIZEOF(rdev->remoteaddr);

	/* msg_flags -> flags such as MSG_NOTIFICATION or MSG_EOR */
	x = recvmsg_sctp(rdev->hnd, buf, *len, (struct sockaddr*)&rdev->remoteaddr, &srcaddrlen, &sri, &msg_flags);
	if (x <= -1)
	{
		int eno = errno;
		if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;  /* no data available */
		if (eno == EINTR) return 0;

		hio_seterrwithsyserr(hio, 0, eno);

		HIO_DEBUG2(hio, "SCK(%p) - recvfrom failure - %hs", rdev, strerror(eno));
		return -1;
	}

	if (msg_flags & MSG_NOTIFICATION)
	{
		/* an association or path event, not payload. handing this to
		 * on_read() would splice an event record into the data stream.
		 *
		 * every one of these is passed on, association changes included. this
		 * socket takes no view on which associations deserve a device of their
		 * own - hio_dev_sck_peeloff() is a call the application makes, from
		 * here or from on_read() later, for the associations it picks. */
		if (rdev->on_notification) rdev->on_notification(rdev, buf, x);
		return 0; /* nothing readable for the caller this time round */
	}

	/* SOCK_SEQPACKET keeps message boundaries. MSG_EOR says this read reached
	 * the end of one; without it, what we hold is the front of a message too
	 * large for the read buffer, and the rest is still queued.
	 *
	 * such a fragment must not go to on_read(): it would arrive looking like a
	 * whole message and the remainder would arrive as another, quietly turning
	 * one message into two. reassembling here is not possible either - one
	 * device carries every association, so partial messages from different
	 * associations interleave and the buffer would have to be keyed by
	 * association. that belongs with the peel-off model, where an association
	 * is a device of its own and reassembly is per-device.
	 *
	 * so the message is dropped whole and the loss is reported. one oversized
	 * message from one peer costs that message and nothing else - the socket
	 * keeps serving everyone else. the remedy is a larger read buffer, which
	 * HIO_READ_BUFFER_SIZE now makes possible. */
	if (rdev->u.sctp.discarding || !(msg_flags & MSG_EOR))
	{
		if (!rdev->u.sctp.discarding)
		{
			rdev->u.sctp.discarding = 1;
			HIO_INFO2(hio, "SCK(%p) - discarding an sctp message too large for the read buffer of %zu octets\n", rdev, hio->bigbuf.capa);
		}
		/* keep swallowing fragments until the one that ends the message */
		if (msg_flags & MSG_EOR) rdev->u.sctp.discarding = 0;
		return 0;
	}

	hio_skad_set_chan(&rdev->remoteaddr, sri.sinfo_stream);
	hio_skad_set_ppid(&rdev->remoteaddr, sri.sinfo_ppid);
	/* the stable name for the association this arrived on. a reply addressed
	 * by it reaches the same association even if the peer's address changed. */
	hio_skad_set_assoc(&rdev->remoteaddr, (hio_int32_t)sri.sinfo_assoc_id);
	srcaddr->ptr = &rdev->remoteaddr;
	srcaddr->len = srcaddrlen;

	*len = x;
	return 1;
}
#endif

/* ------------------------------------------------------------------------------ */

static int dev_sck_write_stream (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
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
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) <= -1)
			{
				set_ssl_error(hio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		x = SSL_write((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ)
			{
				/* the tls layer must consume incoming bytes before it can
				 * encrypt any more. the application may have disabled reading
				 * for backpressure, so this has to override that. */
				return (ssl_want_events(rdev, HIO_DEV_EVENT_IN) <= -1)? -1: 0;
			}
			if (err == SSL_ERROR_WANT_WRITE)
			{
				/* output is watched already - the core queues and arms it. */
				return (ssl_want_events(rdev, 0) <= -1)? -1: 0;
			}
			set_ssl_error(hio, err);
			return -1;
		}

		if (ssl_want_events(rdev, 0) <= -1) return -1;
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
			if (shutdown(rdev->hnd, SHUT_WR) <= -1)
			{
				hio_seterrwithsyserr(hio, 0, errno);
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
		if (x <= -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}

		*len = x;
#if defined(USE_SSL)
	}
#endif
	return 1;
}


static int dev_sck_writev_stream (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
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
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) <= -1)
			{
				set_ssl_error(hio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		nwritten = 0;
		for (i = 0; i < *iovcnt; i++)
		{
			/* no SSL_writev. invoke multiple calls to SSL_write().
			 * since the write function is for the stream connection,
			 * mutiple calls shouldn't really matter */
			x = SSL_write((SSL*)rdev->ssl, iov[i].iov_ptr, iov[i].iov_len);
			if (x <= -1)
			{
				int err = SSL_get_error ((SSL*)rdev->ssl, x);
				if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
				{
					if (ssl_want_events(rdev, (err == SSL_ERROR_WANT_READ)? HIO_DEV_EVENT_IN: 0) <= -1) return -1;
					/* earlier elements of the vector went out for real. reporting
					 * 0 here would make the caller resend them and duplicate
					 * bytes on the wire. report the partial count instead. */
					if (nwritten > 0) { *iovcnt = nwritten; return 1; }
					return 0;
				}
				set_ssl_error(hio, err);
				return -1;
			}
			nwritten += x;
		}

		if (ssl_want_events(rdev, 0) <= -1) return -1;
		*iovcnt = nwritten;
	}
	else
	{
#endif
		ssize_t x;
		int flags = 0;
	#if defined(HAVE_SENDMSG)
		struct msghdr msg;
	#endif

		if (*iovcnt <= 0)
		{
			/* it's a writing finish indicator. close the writing end of
			 * the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) <= -1)
			{
				hio_seterrwithsyserr(hio, 0, errno);
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
		HIO_MEMSET(&msg, 0, HIO_SIZEOF(msg));
		msg.msg_iov = (struct iovec*)iov;
		msg.msg_iovlen = *iovcnt;
		x = sendmsg(rdev->hnd, &msg, flags);
	#else
		x = writev(rdev->hnd, (const struct iovec*)iov, *iovcnt);
	#endif
		if (x <= -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr(hio, 0, errno);
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
		hio_seterrwithsyserr(hio, 0, errno);
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

	HIO_MEMSET(&msg, 0, HIO_SIZEOF(msg));
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
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

/* ------------------------------------------------------------------------------ */


/* ------------------------------------------------------------------------------ */
#if defined(ENABLE_SCTP)
static int dev_sck_write_sctp_seqpkt (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;
	hio_iovec_t iov;

	iov.iov_ptr = (void*)data;
	iov.iov_len = *len;
	x = sendmsg_sctp(rdev->hnd,
		&iov, 1, dstaddr_sockaddr(dstaddr), dstaddr_len(dstaddr),
		dstaddr_ppid(dstaddr), /* ppid - opaque, carried on the address */
		0, /* flags (e.g. SCTP_UNORDERED, SCTP_EOF, SCT_ABORT, ...) */
		dstaddr_chan(dstaddr), /* stream number */
		0, /* ttl */
		0, /* context*/
		dstaddr_assoc(dstaddr));
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

/* the one-to-one sctp types carry HIO_DEV_CAP_STREAM, so the core treats them
 * like a tcp socket: a zero-length write is the writing-end shutdown, and a
 * zero-length read is EOF. the seqpkt methods above answer neither of those
 * conventions - a zero-length sendmsg() puts an empty message on the wire
 * rather than closing anything - so the stream variants need their own pair.
 *
 * what they add over the plain stream methods is the ancillary data: the
 * stream number and ppid, which is the whole point of using sctp. */
static int dev_sck_read_sctp_stream (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	hio_scklen_t srcaddrlen;
	ssize_t x;
	int msg_flags = 0;
	struct sctp_sndrcvinfo sri;

	HIO_MEMSET(&sri, 0, HIO_SIZEOF(sri));
	srcaddrlen = HIO_SIZEOF(rdev->remoteaddr);

	x = recvmsg_sctp(rdev->hnd, buf, *len, (struct sockaddr*)&rdev->remoteaddr, &srcaddrlen, &sri, &msg_flags);
	if (x <= -1)
	{
		int eno = errno;
		if (eno == EINPROGRESS || eno == EWOULDBLOCK || eno == EAGAIN) return 0;
		if (eno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, eno);
		return -1;
	}

	if (msg_flags & MSG_NOTIFICATION)
	{
		/* an association or path event, not payload */
		if (rdev->on_notification) rdev->on_notification(rdev, buf, x);
		return 0;
	}

	/* x of 0 falls through as a zero length, which is what the core reads as
	 * EOF on a stream device */
	hio_skad_set_chan(&rdev->remoteaddr, sri.sinfo_stream);
	hio_skad_set_ppid(&rdev->remoteaddr, sri.sinfo_ppid);
	hio_skad_set_assoc(&rdev->remoteaddr, (hio_int32_t)sri.sinfo_assoc_id);

	*len = x;
	return 1;
}

static int dev_sck_write_sctp_stream (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;
	hio_iovec_t iov;

	if (*len <= 0)
	{
		/* the writing-end shutdown indicator, as for any stream device */
		if (shutdown(rdev->hnd, SHUT_WR) <= -1)
		{
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}
		return 1; /* must be non-zero, or the core queues the request */
	}

	iov.iov_ptr = (void*)data;
	iov.iov_len = *len;
	x = sendmsg_sctp(rdev->hnd,
		&iov, 1, dstaddr_sockaddr(dstaddr), dstaddr_len(dstaddr),
		dstaddr_ppid(dstaddr), 0, dstaddr_chan(dstaddr), 0, 0, dstaddr_assoc(dstaddr));
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int dev_sck_writev_sctp_stream (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;

	if (*iovcnt <= 0)
	{
		if (shutdown(rdev->hnd, SHUT_WR) <= -1)
		{
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}
		return 1;
	}

	x = sendmsg_sctp(rdev->hnd,
		iov, *iovcnt, dstaddr_sockaddr(dstaddr), dstaddr_len(dstaddr),
		dstaddr_ppid(dstaddr), 0, dstaddr_chan(dstaddr), 0, 0, dstaddr_assoc(dstaddr));
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}

static int dev_sck_writev_sctp_seqpkt (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	ssize_t x;

	x = sendmsg_sctp(rdev->hnd,
		iov, *iovcnt, dstaddr_sockaddr(dstaddr), dstaddr_len(dstaddr),
		dstaddr_ppid(dstaddr), /* ppid - opaque, carried on the address */
		0, /* flags (e.g. SCTP_UNORDERED, SCTP_EOF, SCT_ABORT, ...) */
		dstaddr_chan(dstaddr), /* stream number */
		0, /* ttl */
		0, /* context*/
		dstaddr_assoc(dstaddr));
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	*iovcnt = x;
	return 1;
}
#endif

/* ------------------------------------------------------------------------------ */

static int dev_sck_sendfile_stream (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t* len)
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
			if ((x = SSL_shutdown((SSL*)rdev->ssl)) <= -1)
			{
				set_ssl_error(hio, SSL_get_error((SSL*)rdev->ssl, x));
				return -1;
			}
			return 1;
		}

		x = SSL_write((SSL*)rdev->ssl, data, *len);
		if (x <= -1)
		{
			int err = SSL_get_error ((SSL*)rdev->ssl, x);
			if (err == SSL_ERROR_WANT_READ)
			{
				/* the tls layer must consume incoming bytes before it can
				 * encrypt any more. the application may have disabled reading
				 * for backpressure, so this has to override that. */
				return (ssl_want_events(rdev, HIO_DEV_EVENT_IN) <= -1)? -1: 0;
			}
			if (err == SSL_ERROR_WANT_WRITE)
			{
				/* output is watched already - the core queues and arms it. */
				return (ssl_want_events(rdev, 0) <= -1)? -1: 0;
			}
			set_ssl_error(hio, err);
			return -1;
		}

		if (ssl_want_events(rdev, 0) <= -1) return -1;
		*len = x;
	}
	else
	{
#endif
#if defined(USE_SENDFILE_LINUX)
		ssize_t x;
#endif

		if (*len <= 0)
		{
			/* the write handler for a stream device must handle a zero-length
			 * writing request specially. it's a writing finish indicator. close
			 * the writing end of the socket, probably leaving it in the half-closed state */
			if (shutdown(rdev->hnd, SHUT_WR) <= -1)
			{
				hio_seterrwithsyserr(hio, 0, errno);
				return -1;
			}

			/* it must return a non-zero positive value. if it returns 0, this request
			 * gets enqueued by the core. we must aovid it */
			return 1;
		}

#if defined(USE_SENDFILE_LINUX)
		/* the offset is passed by pointer and updated in place, but the caller
		 * keeps its own cursor in the write queue entry, so the update is of no
		 * use here and foff is a local copy on purpose. */
		x = sendfile(rdev->hnd, in_fd, &foff, *len);
		if (x <= -1)
		{
			if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return 0;  /* no data can be written */
			if (errno == EINTR) return 0;
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}
		*len = x;
		if (x == 0) return 0; /* treat it like EWOULDBLOCK? */

#elif defined(USE_SENDFILE_BSD)
		{
			off_t sbytes = 0;
			int rc;

			/* note the reversed operands - the file is the first argument here
			 * and the socket the second. */
			rc = sendfile(in_fd, rdev->hnd, (off_t)foff, (size_t)*len, HIO_NULL, &sbytes, 0);
			if (rc <= -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
				{
					/* a partial transfer is reported as a failure with sbytes
					 * set. saying 'nothing written' here would make the caller
					 * resend bytes that already went out. */
					if (sbytes > 0) goto bsd_sent;
					return 0;
				}
				hio_seterrwithsyserr(hio, 0, errno);
				return -1;
			}

		bsd_sent:
			*len = (hio_iolen_t)sbytes;
			if (sbytes <= 0) return 0; /* treat it like EWOULDBLOCK */
		}

#elif defined(USE_SENDFILE_DARWIN)
		{
			off_t nsent = (off_t)*len;
			int rc;

			/* nsent is in-out and is set even when the call reports failure. */
			rc = sendfile(in_fd, rdev->hnd, (off_t)foff, &nsent, HIO_NULL, 0);
			if (rc <= -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
				{
					if (nsent > 0) goto darwin_sent;
					return 0;
				}
				hio_seterrwithsyserr(hio, 0, errno);
				return -1;
			}

		darwin_sent:
			*len = (hio_iolen_t)nsent;
			if (nsent <= 0) return 0; /* treat it like EWOULDBLOCK */
		}

#else
		hio_seterrnum(hio, HIO_ENOIMPL);
		return -1;
#endif


#if 0 && defined(USE_SSL)
	}
#endif
	return 1;
}

/* ------------------------------------------------------------------------------ */
#if defined(USE_SSL)
static int dev_sck_readpending (hio_dev_t* dev)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return rdev->ssl? SSL_pending((SSL*)rdev->ssl): 0;
}

static int do_ssl (hio_dev_sck_t* dev, int (*ssl_func)(SSL*))
{
	hio_t* hio = dev->hio;
	int ret, watcher_cmd, watcher_events;

	HIO_ASSERT(hio, dev->ssl_ctx);

	if (!dev->ssl)
	{
		SSL* ssl;

		ssl = SSL_new(dev->ssl_ctx);
		if (!ssl)
		{
			set_ssl_error(hio, ERR_get_error());
			return -1;
		}

		if (SSL_set_fd(ssl, dev->hnd) == 0)
		{
			set_ssl_error(hio, ERR_get_error());
			return -1;
		}

		/* keep openssl reading one record at a time instead of slurping
		 * whatever else the kernel has ready into its own buffer. together
		 * with a read buffer at least HIO_SSL_MAX_READ_RECORD wide, this is
		 * what guarantees a single SSL_read() drains the record and leaves
		 * SSL_pending() at zero. surplus records stay in the kernel buffer
		 * where the multiplexer can still see them. this is load-bearing,
		 * not a tuning knob. */
		SSL_set_read_ahead(ssl, 0);

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
			set_ssl_error(hio, err);
			ret = -1;
		}
	}
	else
	{
		ret = 1; /* accepted */
	}

	if (hio_dev_watch((hio_dev_t*)dev, watcher_cmd, watcher_events) <= -1)
	{
		hio_stop(hio, HIO_STOPREQ_WATCHER_ERROR);
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
				hio_seterrbfmt(hio, HIO_EPERM, "operation in progress. not allowed to bind again");
				return -1;
			}

			if (hio_skad_get_family(&bnd->localaddr) == HIO_AF_INET6) /* getsockopt(rdev->hnd, SO_DOMAIN, ...) may return the domain but it's kernel specific as well */
			{
			#if defined(IPV6_V6ONLY)
				/* TODO: should i make it into bnd->options? HIO_DEV_SCK_BIND_IPV6ONLY? applicable to ipv6 though. */
				int v = 1;
				if (setsockopt(rdev->hnd, IPPROTO_IPV6, IPV6_V6ONLY, &v, HIO_SIZEOF(v)) <= -1)
				{
					if (!(bnd->options & HIO_DEV_SCK_BIND_IGNERR))
					{
						hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to set IPV6_V6ONLY");
						return -1;
					}
				}
			#endif
			}

			if (bnd->options & HIO_DEV_SCK_BIND_BROADCAST)
			{
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_BROADCAST, &v, HIO_SIZEOF(v)) <= -1)
				{
					/* not affected by HIO_DEV_SCK_BIND_IGNERR */
					hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to set SO_BROADCAST");
					return -1;
				}
			}

			if (bnd->options & HIO_DEV_SCK_BIND_REUSEADDR)
			{
			#if defined(SO_REUSEADDR)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_REUSEADDR, &v, HIO_SIZEOF(v)) <= -1)
				{
					if (!(bnd->options & HIO_DEV_SCK_BIND_IGNERR))
					{
						hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to set SO_REUSEADDR");
						return -1;
					}
				}
			/* ignore it if not available
			#else
				hio_seterrnum(hio, HIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (bnd->options & HIO_DEV_SCK_BIND_REUSEPORT)
			{
			#if defined(SO_REUSEPORT)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_SOCKET, SO_REUSEPORT, &v, HIO_SIZEOF(v)) <= -1)
				{
					if (!(bnd->options & HIO_DEV_SCK_BIND_IGNERR))
					{
						hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to set SO_REUSEPORT");
						return -1;
					}
				}
			/* ignore it if not available
			#else
				hio_seterrnum(hio, HIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (bnd->options & HIO_DEV_SCK_BIND_TRANSPARENT)
			{
			#if defined(IP_TRANSPARENT)
				int v = 1;
				if (setsockopt(rdev->hnd, SOL_IP, IP_TRANSPARENT, &v, HIO_SIZEOF(v)) <= -1)
				{
					hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to set IP_TRANSPARENT");
					return -1;
				}
			/* ignore it if not available
			#else
				hio_seterrnum(hio, HIO_ENOIMPL);
				return -1;
			*/
			#endif
			}

			if (rdev->ssl_ctx)
			{
			#if defined(USE_SSL)
				SSL_CTX_free(rdev->ssl_ctx);
			#endif
				rdev->ssl_ctx = HIO_NULL;

				if (rdev->ssl)
				{
				#if defined(USE_SSL)
					SSL_free(rdev->ssl);
				#endif
					rdev->ssl = HIO_NULL;
				}
			}

			if (bnd->options & HIO_DEV_SCK_BIND_SSL)
			{
			#if defined(USE_SSL)
			#if defined(ENABLE_SCTP)
				/* [NOTE] refused rather than quietly downgraded.
				 *
				 * tls needs one reliable, in-order byte stream: its record
				 * sequence number is implicit, so records arriving out of
				 * order fail to decrypt. an sctp association's streams are
				 * ordered only with respect to themselves, so tls cannot span
				 * them - that is a property of tls, not of any library. the
				 * standard answer is dtls over sctp (RFC 6083), which uses
				 * dtls because it carries explicit sequence numbers.
				 *
				 * on top of that, this implementation drives tls through
				 * SSL_set_fd(), which reads and writes the descriptor directly
				 * and so bypasses the sendmsg()/recvmsg() that carry the
				 * stream number - the ancillary data would be unreachable even
				 * on a single stream.
				 *
				 * so the combination would deliver either no encryption or no
				 * streams. it used to silently deliver the former: the
				 * handshake ran from the ready handler while the sctp read and
				 * write methods, which know nothing of ssl, moved plaintext. */
				if (sck_type_map[rdev->type].proto == IPPROTO_SCTP)
				{
					hio_seterrbfmt(hio, HIO_ENOIMPL, "tls over sctp is not supported - see RFC 6083 for why it needs dtls");
					return -1;
				}
			#endif

				if (!bnd->ssl_certfile || !bnd->ssl_keyfile)
				{
					hio_seterrbfmt(hio, HIO_EINVAL, "SSL certficate/key file not set");
					return -1;
				}

				ssl_ctx = SSL_CTX_new(SSLv23_server_method());
				if (!ssl_ctx)
				{
					set_ssl_error(hio, ERR_get_error());
					return -1;
				}

				if (SSL_CTX_use_certificate_file(ssl_ctx, bnd->ssl_certfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_use_PrivateKey_file(ssl_ctx, bnd->ssl_keyfile, SSL_FILETYPE_PEM) == 0 ||
				    SSL_CTX_check_private_key(ssl_ctx) == 0  /*||
				    SSL_CTX_use_certificate_chain_file(ssl_ctx, bnd->chainfile) == 0*/)
				{
					set_ssl_error(hio, ERR_get_error());
					SSL_CTX_free(ssl_ctx);
					return -1;
				}

				SSL_CTX_set_read_ahead(ssl_ctx, 0);
				SSL_CTX_set_mode (ssl_ctx, SSL_CTX_get_mode(ssl_ctx) |
				                           /*SSL_MODE_ENABLE_PARTIAL_WRITE |*/
				                           SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

				SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2); /* no outdated SSLv2 by default */
			#else
				hio_seterrnum(hio, HIO_ENOIMPL);
				return -1;
			#endif
			}

		#if defined(USE_BPF)
			if (rdev->bpf_state)
			{
				/* a bpf device attaches to an interface by name, and what the
				 * caller gave is the ifindex hio_skad_init_for_eth() puts in an
				 * L2 address - so it is converted here rather than making the
				 * caller know which system it is on. */
				struct ifreq ifr;
				int ifindex = hio_skad_get_ifindex(&bnd->localaddr);

				HIO_MEMSET(&ifr, 0, HIO_SIZEOF(ifr));
				if (ifindex <= 0 || !if_indextoname(ifindex, ifr.ifr_name))
				{
					hio_seterrbfmt(hio, HIO_EINVAL, "no interface for the given address");
					return -1;
				}

				if (ioctl(rdev->hnd, BIOCSETIF, &ifr) <= -1)
				{
					hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to attach to %hs", ifr.ifr_name);
					return -1;
				}

				/* on linux the protocol argument to socket() is what narrows an
				 * ARP device to ARP frames. bpf has no such argument, so the
				 * equivalent is a filter - which keeps the device type meaning
				 * the same thing on both. */
				if (sck_type_map[rdev->type].proto != 0)
				{
					hio_uint16_t ethtype = hio_ntoh16((hio_uint16_t)sck_type_map[rdev->type].proto);
					hio_bpf_insn_t prog[4];
					prog[0].code = 0x28; prog[0].jt = 0; prog[0].jf = 0; prog[0].k = 12;          /* ldh [12] */
					prog[1].code = 0x15; prog[1].jt = 0; prog[1].jf = 1; prog[1].k = ethtype;     /* jeq #type */
					prog[2].code = 0x06; prog[2].jt = 0; prog[2].jf = 0; prog[2].k = 0xFFFFFFFFu; /* ret #-1 */
					prog[3].code = 0x06; prog[3].jt = 0; prog[3].jf = 0; prog[3].k = 0;           /* ret #0 */
					if (hio_dev_sck_setfilter(rdev, prog, 4) <= -1) return -1;
				}

				/* the ordinary bind path sets no progress bit either - it
				 * records the address and returns */
				rdev->localaddr = bnd->localaddr;
				return 0;
			}
		#endif

			x = bind(rdev->hnd, (struct sockaddr*)&bnd->localaddr, hio_skad_get_size(&bnd->localaddr));
			if (x <= -1)
			{
				hio_seterrwithsyserr(hio, 0, errno);
			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free(ssl_ctx);
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
				hio_seterrbfmt(hio, HIO_EPERM, "operation in progress. disallowed to connect again");
				return -1;
			}

			if (!sck_type_map[rdev->type].connectable)
			{
				hio_seterrbfmt(hio, HIO_EPERM, "unconnectable socket device");
				return -1;
			}

			if (sa->sa_family == AF_INET) sl = HIO_SIZEOF(struct sockaddr_in);
			else if (sa->sa_family == AF_INET6) sl = HIO_SIZEOF(struct sockaddr_in6);
			else
			{
				hio_seterrbfmt(hio, HIO_EINVAL, "unknown address family %d", sa->sa_family);
				return -1;
			}

		#if defined(USE_SSL)
			if (rdev->ssl_ctx)
			{
				if (rdev->ssl)
				{
					SSL_free(rdev->ssl);
					rdev->ssl = HIO_NULL;
				}

				SSL_CTX_free(rdev->ssl_ctx);
				rdev->ssl_ctx = HIO_NULL;
			}

			if (conn->options & HIO_DEV_SCK_CONNECT_SSL)
			{
				ssl_ctx = SSL_CTX_new(SSLv23_client_method());
				if (!ssl_ctx)
				{
					set_ssl_error(hio, ERR_get_error());
					return -1;
				}

				SSL_CTX_set_read_ahead(ssl_ctx, 0);
				SSL_CTX_set_mode(ssl_ctx, SSL_CTX_get_mode(ssl_ctx) |
				                          /* SSL_MODE_ENABLE_PARTIAL_WRITE | */
				                          SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
			}
		#endif
			/* the socket is already non-blocking */
/*{
int flags = fcntl(rdev->hnd, F_GETFL);
fcntl(rdev->hnd, F_SETFL, flags & ~O_NONBLOCK);
}*/
			x = connect(rdev->hnd, sa, sl);
/*{
int flags = fcntl(rdev->hnd, F_GETFL);
fcntl(rdev->hnd, F_SETFL, flags | O_NONBLOCK);
}*/
			if (x <= -1)
			{
				if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
				{
					if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_UPDATE, HIO_DEV_EVENT_IN | HIO_DEV_EVENT_OUT) <= -1)
					{
						/* watcher update failure. it's critical */
						hio_stop(hio, HIO_STOPREQ_WATCHER_ERROR);
						goto oops_connect_watcher_error;
					}
					else
					{
						HIO_INIT_NTIME(&rdev->tmout, -1, 0); /* just in case */

						if (!HIO_IS_NEG_NTIME(&conn->connect_tmout))
						{
							if (schedule_timer_job_after(rdev, &conn->connect_tmout, connect_timedout) <= -1)
							{
								goto oops_connect;
							}
							else
							{
								/* update rdev->tmout to the deadline of the connect timeout job */
								HIO_ASSERT(hio, rdev->tmrjob_index != HIO_TMRIDX_INVALID);
								hio_gettmrjobdeadline(hio, rdev->tmrjob_index, &rdev->tmout);
							}
						}

						rdev->remoteaddr = conn->remoteaddr;
					#if defined(USE_SSL)
						rdev->ssl_ctx = ssl_ctx;
					#endif
						HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_CONNECTING);
						return 0;
					}
				}

				hio_seterrwithsyserr(hio, 0, errno);

			oops_connect:
				if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_UPDATE, HIO_DEV_EVENT_IN) <= -1)
				{
					/* watcher update failure. it's critical */
					hio_stop(hio, HIO_STOPREQ_WATCHER_ERROR);
				}

			oops_connect_watcher_error:
			#if defined(USE_SSL)
				if (ssl_ctx) SSL_CTX_free(ssl_ctx);
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
					hio_stop(hio, HIO_STOPREQ_WATCHER_ERROR);
					goto oops_connect;
				}

				/* as i know it's connected already,
				 * i don't schedule a connection timeout job */

				rdev->remoteaddr = conn->remoteaddr;
			#if defined(USE_SSL)
				rdev->ssl_ctx = ssl_ctx;
			#endif
				/* set progress CONNECTING so that the ready handler invokes on_connect() */
				HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_CONNECTING);
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
				hio_seterrbfmt(hio, HIO_EPERM, "operation in progress. disallowed to listen again");
				return -1;
			}

			if (!sck_type_map[rdev->type].listenable)
			{
				hio_seterrbfmt(hio, HIO_EPERM, "unlistenable socket device");
				return -1;
			}

			x = listen(rdev->hnd, lstn->backlogs);
			if (x <= -1)
			{
				hio_seterrwithsyserr(hio, 0, errno);
				return -1;
			}

			if (rdev->dev_cap & HIO_DEV_CAP_WATCH_REREG_REQUIRED)
			{
				/* On NetBSD, the listening socket added before listen()
				 * doesn't generate an event even if a new connection is ready
				 * to be accepted. */

				/* TODO: need to keep the old watch flags before STOP and
				 *       use the flags witn START  */
				if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_STOP, 0) <= -1 ||
				    hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_START, 0) <= -1)
				{
					hio_stop(hio, HIO_STOPREQ_WATCHER_ERROR);
					return -1;
				}
			}

			rdev->tmout = lstn->accept_tmout;

			HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_LISTENING);
			return 0;
		}
	}

	return 0;
}

#if defined(USE_BPF)
/* the L2 types on a system with no AF_PACKET. a stateless device as far as the
 * core is concerned - the event callbacks below are the stateless ones - but
 * reading a run of frames out of one kernel buffer needs its own methods, and
 * readpending is what makes the run come out one frame at a time. */
static hio_dev_mth_t dev_mth_sck_bpf =
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,

	dev_sck_read_bpf,
	dev_sck_write_bpf,
	dev_sck_writev_bpf,
	HIO_NULL,                  /* sendfile */

	dev_sck_readpending_bpf
};
#endif

static hio_dev_mth_t dev_mth_sck_stateless =
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,     /* ioctl */

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_writev_stateless,
	HIO_NULL,          /* sendfile */

	HIO_NULL,          /* readpending */
};


#if defined(ENABLE_SCTP)
/* one-to-one sctp. a stream device like tcp, but reads and writes carry the
 * per-message ancillary data, so the stream number and ppid are reachable.
 * no sendfile: it would bypass sendmsg() and lose that data. */
static hio_dev_mth_t dev_mth_sck_sctp_stream =
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,

	dev_sck_read_sctp_stream,
	dev_sck_write_sctp_stream,
	dev_sck_writev_sctp_stream,
	HIO_NULL,          /* sendfile */
	HIO_NULL           /* readpending */
};

static hio_dev_mth_t dev_mth_clisck_sctp_stream =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_fail_before_make_client,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,

	dev_sck_read_sctp_stream,
	dev_sck_write_sctp_stream,
	dev_sck_writev_sctp_stream,
	HIO_NULL,          /* sendfile */
	HIO_NULL           /* readpending */
};
#endif

static hio_dev_mth_t dev_mth_sck_stream =
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,     /* ioctl */

	dev_sck_read_stream,
	dev_sck_write_stream,
	dev_sck_writev_stream,
	dev_sck_sendfile_stream,

#if defined(USE_SSL)
	dev_sck_readpending
#else
	HIO_NULL
#endif
};

#if defined(ENABLE_SCTP)
static hio_dev_mth_t dev_mth_sck_sctp_seqpkt =
{
	dev_sck_make,
	dev_sck_kill,
	HIO_NULL,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,     /* ioctl */

	dev_sck_read_sctp_seqpkt,
	dev_sck_write_sctp_seqpkt,
	dev_sck_writev_sctp_seqpkt,
	HIO_NULL,          /* sendfile */

	HIO_NULL,          /* readpending */
};
#endif

static hio_dev_mth_t dev_mth_clisck_stateless =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_fail_before_make_client,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,

	dev_sck_read_stateless,
	dev_sck_write_stateless,
	dev_sck_writev_stateless,
	HIO_NULL,          /* sendfile */

	HIO_NULL,          /* readpending */
};

static hio_dev_mth_t dev_mth_clisck_stream =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_fail_before_make_client,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,

	dev_sck_read_stream,
	dev_sck_write_stream,
	dev_sck_writev_stream,
	dev_sck_sendfile_stream, /* sendfile */

#if defined(USE_SSL)
	dev_sck_readpending /* readpending */
#else
	HIO_NULL /* readpending */
#endif
};

#if defined(ENABLE_SCTP)
static hio_dev_mth_t dev_mth_clisck_sctp_seqpkt =
{
	dev_sck_make_client,
	dev_sck_kill,
	dev_sck_fail_before_make_client,
	dev_sck_getsyshnd,
	HIO_NULL,
	dev_sck_ioctl,

	dev_sck_read_sctp_seqpkt,
	dev_sck_write_sctp_seqpkt,
	dev_sck_writev_sctp_seqpkt,
	HIO_NULL,  /* sendfile */

	HIO_NULL   /* readpending - no ssl on a seqpkt socket */
};
#endif


/* ========================================================================= */

static int harvest_outgoing_connection (hio_dev_sck_t* rdev)
{
	hio_t* hio = rdev->hio;
	int errcode;
	hio_scklen_t len;

	HIO_ASSERT(hio, !(rdev->state & HIO_DEV_SCK_CONNECTED));

	len = HIO_SIZEOF(errcode);
	if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) <= -1)
	{
		hio_seterrbfmtwithsyserr(hio, 0, errno, "unable to get SO_ERROR");
		return -1;
	}
	else if (errcode == 0)
	{
		hio_skad_t localaddr;
		hio_scklen_t addrlen;

		HIO_MEMSET(&localaddr, 0, HIO_SIZEOF(localaddr)); /* see hio_dev_sck_getsockaddr() */

		/* connected */

		if (rdev->tmrjob_index != HIO_TMRIDX_INVALID)
		{
			hio_deltmrjob(hio, rdev->tmrjob_index);
			HIO_ASSERT(hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);
		}

		addrlen = HIO_SIZEOF(localaddr);
		if (getsockname(rdev->hnd, (struct sockaddr*)&localaddr, &addrlen) == 0) rdev->localaddr = localaddr;

		if (hio_dev_watch((hio_dev_t*)rdev, HIO_DEV_WATCH_RENEW, HIO_DEV_EVENT_IN) <= -1)
		{
			/* watcher update failure. it's critical */
			hio_stop(hio, HIO_STOPREQ_WATCHER_ERROR);
			return -1;
		}

	#if defined(USE_SSL)
		if (rdev->ssl_ctx)
		{
			int x;
			HIO_ASSERT(hio, !rdev->ssl); /* must not be SSL-connected yet */

			x = connect_ssl(rdev);
			if (x <= -1) return -1;
			if (x == 0)
			{
				/* underlying socket connected but not SSL-connected */
				HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_CONNECTING_SSL);

				HIO_ASSERT(hio, rdev->tmrjob_index == HIO_TMRIDX_INVALID);

				/* rdev->tmout has been set to the deadline of the connect task
				 * when the CONNECT IOCTL command has been executed. use the
				 * same deadline here */
				if (!HIO_IS_NEG_NTIME(&rdev->tmout) &&
				    schedule_timer_job_at(rdev, &rdev->tmout, ssl_connect_timedout) <= -1)
				{
					HIO_DEBUG1(hio, "SCK(%p) - ssl-connect timeout scheduling failed. halting\n", rdev);
					hio_dev_sck_halt(rdev);
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
			HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_CONNECTED);
			if (rdev->on_connect) rdev->on_connect(rdev);
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
		hio_seterrwithsyserr(hio, 0, errcode);
		return -1;
	}
}

static int make_accepted_client_connection (hio_dev_sck_t* rdev, hio_syshnd_t clisck, hio_skad_t* remoteaddr, hio_dev_sck_type_t clisck_type)
{
	hio_t* hio = rdev->hio;
	hio_dev_sck_t* clidev;
	hio_scklen_t addrlen;
	hio_dev_mth_t* dev_mth;
	sck_make_client_ctx_t mc;

	if (rdev->on_raw_accept)
	{
		/* this is a special optional callback. If you don't want a client socket device
		 * to be created upon accept, you may implement the on_raw_accept() handler.
		 * the socket handle is delegated to the callback. */
		rdev->on_raw_accept(rdev, clisck, remoteaddr);
		return 0;
	}

	/* rdev->dev_size:
	 *   use rdev->dev_size when instantiating a client sck device
	 *   instead of HIO_SIZEOF(hio_dev_sck_t). therefore, the
	 *   extension area as big as that of the master socket device
	 *   is created in the client sck device
	 * dev_mth:
	 *   choose the client socket method base on the master socket
	 *   device capability. currently, stream or non-stream is supported.
	 */
#if defined(ENABLE_SCTP)
	dev_mth = (sck_type_map[clisck_type].extra_dev_cap & HIO_DEV_CAP_STREAM)?
		((sck_type_map[clisck_type].proto == IPPROTO_SCTP)? &dev_mth_clisck_sctp_stream: &dev_mth_clisck_stream):
		((sck_type_map[clisck_type].proto == IPPROTO_SCTP)? &dev_mth_clisck_sctp_seqpkt: &dev_mth_clisck_stateless);
#else
	dev_mth = (sck_type_map[clisck_type].extra_dev_cap & HIO_DEV_CAP_STREAM)? &dev_mth_clisck_stream: &dev_mth_clisck_stateless;
#endif
	mc.hnd = clisck;
	mc.type = clisck_type;
	clidev = (hio_dev_sck_t*)hio_dev_make(hio, rdev->dev_size, dev_mth, rdev->dev_evcb, &mc);
	if (HIO_UNLIKELY(!clidev))
	{
		/* [NOTE] 'clisck' is closed by callback(fail_before_make) methods called by hio_dev_make() upon failure */
		HIO_DEBUG3(hio, "SCK(%p) - unable to make a new accepted device for %d - %js\n", rdev, (int)clisck, hio_geterrmsg(hio));
		return -1;
	}

	clidev->type = clisck_type;
	HIO_ASSERT(hio, clidev->hnd == clisck);

	clidev->dev_cap |= HIO_DEV_CAP_IN | HIO_DEV_CAP_OUT | sck_type_map[clisck_type].extra_dev_cap;
	clidev->remoteaddr = *remoteaddr;

	addrlen = HIO_SIZEOF(clidev->localaddr);
	if (getsockname(clisck, (struct sockaddr*)&clidev->localaddr, &addrlen) <= -1) clidev->localaddr = rdev->localaddr;

#if defined(SO_ORIGINAL_DST)
	/* if REDIRECT is used, SO_ORIGINAL_DST returns the original
	 * destination address. When REDIRECT is not used, it returnes
	 * the address of the local socket. In this case, it should
	 * be same as the result of getsockname(). */
	addrlen = HIO_SIZEOF(clidev->orgdstaddr);
	if (getsockopt(clisck, SOL_IP, SO_ORIGINAL_DST, &clidev->orgdstaddr, &addrlen) <= -1) clidev->orgdstaddr = rdev->localaddr;
#else
	clidev->orgdstaddr = rdev->localaddr;
#endif

	if (!hio_equal_skads(&clidev->orgdstaddr, &clidev->localaddr, 0))
	{
		clidev->state |= HIO_DEV_SCK_INTERCEPTED;
	}
	else if (hio_skad_get_port(&clidev->localaddr) != hio_skad_get_port(&rdev->localaddr))
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
	HIO_ASSERT(hio, rdev->dev_size == clidev->dev_size);
	HIO_MEMCPY(hio_dev_sck_getxtn(clidev), hio_dev_sck_getxtn(rdev), rdev->dev_size - HIO_SIZEOF(hio_dev_sck_t));

	HIO_ASSERT(hio, clidev->tmrjob_index == HIO_TMRIDX_INVALID);

	if (rdev->ssl_ctx)
	{
		HIO_DEV_SCK_SET_PROGRESS(clidev, HIO_DEV_SCK_ACCEPTING_SSL);
		HIO_ASSERT(hio, clidev->state & HIO_DEV_SCK_ACCEPTING_SSL);
		/* actual SSL acceptance must be completed in the client device */

		/* let the client device know the SSL context to use */
		clidev->ssl_ctx = rdev->ssl_ctx;

		if (!HIO_IS_NEG_NTIME(&rdev->tmout) &&
		    schedule_timer_job_after(clidev, &rdev->tmout, ssl_accept_timedout) <= -1)
		{
			/* timer job scheduling failed. halt the device */
			HIO_DEBUG1(hio, "SCK(%p) - ssl-accept timeout scheduling failed. halting\n", rdev);
			hio_dev_sck_halt(clidev);
		}
	}
	else
	{
		HIO_DEV_SCK_SET_PROGRESS(clidev, HIO_DEV_SCK_ACCEPTED);
		/*if (clidev->on_connect(clidev) <= -1) hio_dev_sck_halt(clidev);*/
		if (clidev->on_connect) clidev->on_connect(clidev);
	}

	return 0;
}

static int accept_incoming_connection (hio_dev_sck_t* rdev)
{
	hio_t* hio = rdev->hio;
	hio_syshnd_t clisck;
	hio_skad_t remoteaddr;
	hio_scklen_t addrlen;

	/* this is a server(lisening) socket */

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_PACCEPT)
	int flags;

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
	int flags;

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

		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
accept_done:
#endif
	/* no separate error handling to close clisck becuase it's supposed to be
	 * handled by hio_dev_make() via the fail_before_make callback inside
	 * make_accepted_client_connection(). if it fails even before hio_dev_make()
	 * inside make_accepted_client_connection(), it should close the socket explicitly */
	return make_accepted_client_connection(rdev, clisck, &remoteaddr, rdev->type);
}

static int dev_evcb_sck_ready_stream (hio_dev_t* dev, int events)
{
	hio_t* hio = dev->hio;
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;

	if (events & HIO_DEV_EVENT_ERR)
	{
		int errcode;
		hio_scklen_t len;

		len = HIO_SIZEOF(errcode);
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) <= -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt(hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr(hio, 0, errcode);
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
				hio_seterrnum(hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & (HIO_DEV_EVENT_OUT | HIO_DEV_EVENT_IN))
			{
				/* either bit means the kernel has something to say about a
				 * connection that has not finished. the socket turns writable
				 * when it completes, and readable when the peer has already
				 * sent something - or, on sctp, when the association
				 * notification arrives. which of the two arrived does not
				 * matter here: SO_ERROR is what actually answers, and
				 * harvest_outgoing_connection() leaves the device in
				 * CONNECTING while that answer is still EINPROGRESS.
				 *
				 * the two bits do not necessarily arrive together. kqueue
				 * reports one event per filter, so a socket that is readable
				 * and writable at once produces two separate wakeups, while
				 * epoll and poll deliver a single mask carrying both, neither
				 * bit may be read as an error on its own. */
				return harvest_outgoing_connection(rdev);
			}
			else if (events & HIO_DEV_EVENT_PRI)
			{
				/* urgent data on a socket that has not finished connecting. it's not expected */
				hio_seterrbfmt(hio, HIO_EDEVERR, "device error - invalid event mask");
				return -1;
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
				hio_seterrnum(hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & HIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				hio_seterrbfmt(hio, HIO_EDEVERR, "device error - invalid event mask");
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
					hio_deltmrjob(rdev->hio, rdev->tmrjob_index);
					rdev->tmrjob_index = HIO_TMRIDX_INVALID;
				}

				HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_CONNECTED);
				if (rdev->on_connect) rdev->on_connect(rdev);
				return 0;
			}
			else
			{
				return 0; /* success. no actual I/O yet */
			}
		#else
			hio_seterrnum(hio, HIO_EINTERN);
			return -1;
		#endif

		case HIO_DEV_SCK_LISTENING:
			if (events & HIO_DEV_EVENT_HUP)
			{
				/* device hang-up */
				hio_seterrnum(hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & (HIO_DEV_EVENT_PRI | HIO_DEV_EVENT_OUT))
			{
				hio_seterrbfmt(hio, HIO_EDEVERR, "device error - invalid event mask");
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
				hio_seterrnum(hio, HIO_EDEVHUP);
				return -1;
			}
			else if (events & HIO_DEV_EVENT_PRI)
			{
				/* invalid event masks. generic device error */
				hio_seterrbfmt(hio, HIO_EDEVERR, "device error - invalid event mask");
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
					hio_deltmrjob(rdev->hio, rdev->tmrjob_index);
					rdev->tmrjob_index = HIO_TMRIDX_INVALID;
				}

				HIO_DEV_SCK_SET_PROGRESS(rdev, HIO_DEV_SCK_ACCEPTED);
				if (rdev->on_connect) rdev->on_connect(rdev);

				return 0;
			}
			else
			{
				return 0; /* no reading or writing yet */
			}
		#else
			hio_seterrnum(hio, HIO_EINTERN);
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

				hio_seterrnum(hio, HIO_EDEVHUP);
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
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) <= -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt(hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr(rdev->hio, 0, errcode);
		}
		return -1;
	}
	else if (events & HIO_DEV_EVENT_HUP)
	{
		hio_seterrnum(hio, HIO_EDEVHUP);
		return -1;
	}

	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read_stream (hio_dev_t* dev, const void* data, hio_iolen_t dlen, const hio_devaddr_t* srcaddr)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)dev;
	return rdev->on_read(rdev, data, dlen, HIO_NULL);
}

static int dev_evcb_sck_on_write_stream (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
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

static hio_dev_evcb_t dev_sck_event_callbacks_stream =
{
	dev_evcb_sck_ready_stream,
	dev_evcb_sck_on_read_stream,
	dev_evcb_sck_on_write_stream
};

static hio_dev_evcb_t dev_sck_event_callbacks_stateless =
{
	dev_evcb_sck_ready_stateless,
	dev_evcb_sck_on_read_stateless,
	dev_evcb_sck_on_write_stateless
};

static hio_dev_evcb_t dev_sck_event_callbacks_sctp_seqpkt =
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
		if (getsockopt(rdev->hnd, SOL_SOCKET, SO_ERROR, (char*)&errcode, &len) <= -1)
		{
			/* the error number is set to the socket error code.
			 * errno resulting from getsockopt() doesn't reflect the actual
			 * socket error. so errno is not used to set the error number.
			 * instead, the generic device error HIO_EDEVERRR is used */
			hio_seterrbfmt(hio, HIO_EDEVERR, "device error - unable to get SO_ERROR");
		}
		else
		{
			hio_seterrwithsyserr(rdev->hio, 0, errcode);
		}
		return -1;
	}
	else if (events & HIO_DEV_EVENT_HUP)
	{
		hio_seterrnum(hio, HIO_EDEVHUP);
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
	#if defined(__BEOS__) || defined(__HAIKU__)
		const hio_uint8_t* p = (const hio_uint8_t*)data;
		hio_oow_t rem = (hio_oow_t)dlen;

		while (rem > 0)
		{
			hio_oow_t want = HIO_SIZEOF(rdev->u.qx.qxacc) - rdev->u.qx.qxacc_len;
			hio_oow_t take = (rem < want)? rem: want;

			HIO_MEMCPY((hio_uint8_t*)&rdev->u.qx.qxacc + rdev->u.qx.qxacc_len, p, take);
			rdev->u.qx.qxacc_len += take;
			p += take;
			rem -= take;


			if (rdev->u.qx.qxacc_len < HIO_SIZEOF(rdev->u.qx.qxacc)) break; /* wait for more */
			rdev->u.qx.qxacc_len = 0;

			if (rdev->u.qx.qxacc.cmd == HIO_DEV_SCK_QXMSG_NEWCONN)
			{
				if (make_accepted_client_connection(rdev, rdev->u.qx.qxacc.syshnd, &rdev->u.qx.qxacc.remoteaddr, rdev->u.qx.qxacc.scktype) <= -1)
				{
/*printf ("unable to accept new client connection %d\n", qxmsg->syshnd);*/
					return (rdev->state & HIO_DEV_SCK_LENIENT)? 0: -1;
				}
			}
			else
			{
				/* the stream is framed by size alone, so a bad command means
				 * the two ends disagree about the message layout and nothing
				 * after this point can be trusted to be a message boundary. */
				hio_seterrbfmt(hio, HIO_EINVAL, "wrong qx command code");
				return 0;
			}
		}
	#else
		hio_dev_sck_qxmsg_t* qxmsg;

		if (dlen != HIO_SIZEOF(*qxmsg))
		{
			hio_seterrbfmt(hio, HIO_EINVAL, "wrong qx packet size");
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
			hio_seterrbfmt(hio, HIO_EINVAL, "wrong qx command code");
			return 0;
		}
	#endif

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

hio_dev_sck_t* hio_dev_sck_make (hio_t* hio, hio_oow_t xtnsize, const hio_dev_sck_make_t* info)
{
	hio_dev_sck_t* rdev;

	if (info->type < 0 && info->type >= HIO_COUNTOF(sck_type_map))
	{
		hio_seterrnum(hio, HIO_EINVAL);
		return HIO_NULL;
	}

	if (info->type == HIO_DEV_SCK_QX)
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_stateless, &dev_sck_event_callbacks_qx, (void*)info);
	}
	else if (sck_type_map[info->type].extra_dev_cap & HIO_DEV_CAP_STREAM) /* can't use the IS_STREAM() macro yet */
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
		#if defined(ENABLE_SCTP)
			(sck_type_map[info->type].proto == IPPROTO_SCTP)? &dev_mth_sck_sctp_stream: &dev_mth_sck_stream,
		#else
			&dev_mth_sck_stream,
		#endif
			&dev_sck_event_callbacks_stream, (void*)info);
	}
#if defined(ENABLE_SCTP)
	else if (sck_type_map[info->type].proto == IPPROTO_SCTP)
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_sctp_seqpkt, &dev_sck_event_callbacks_sctp_seqpkt, (void*)info);
	}
#endif
#if defined(USE_BPF)
	else if (sck_type_map[info->type].domain == __AF_BPF)
	{
		rdev = (hio_dev_sck_t*)hio_dev_make(
			hio, HIO_SIZEOF(hio_dev_sck_t) + xtnsize,
			&dev_mth_sck_bpf, &dev_sck_event_callbacks_stateless, (void*)info);
	}
#endif
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
/* TODO: if connecting to a hostname, do name resolutin first ... before calling ioctl(SCK_CONNECT). also some caching may be required....
for this, hio_dev_sck_connect_t must be changed to accomodate a string as a host name.
*/
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
	int n;
	n = setsockopt(dev->hnd, level, optname, optval, optlen);
	if (n <= -1) hio_seterrwithsyserr(dev->hio, 0, errno);
	return n;
}

int hio_dev_sck_getsockopt (hio_dev_sck_t* dev, int level, int optname, void* optval, hio_scklen_t* optlen)
{
	int n;
	n = getsockopt(dev->hnd, level, optname, optval, optlen);
	if (n <= -1) hio_seterrwithsyserr(dev->hio, 0, errno);
	return n;
}

int hio_dev_sck_getsockaddr (hio_dev_sck_t* dev, hio_skad_t* skad)
{
	hio_scklen_t addrlen = HIO_SIZEOF(*skad);
	/* the system fills the sockaddr and nothing else. the extra area past it -
	 * chan, ppid, assoc - would keep whatever the caller's memory happened to
	 * hold, and those are read back later and passed to the kernel. */
	HIO_MEMSET(skad, 0, HIO_SIZEOF(*skad));
	if (dev->type == HIO_DEV_SCK_QX)
	{
		hio_skad_init_for_qx(skad);
	}
	else if (getsockname(dev->hnd, (struct sockaddr*)skad, &addrlen) <= -1)
	{
		hio_seterrwithsyserr(dev->hio, 0, errno);
		return -1;
	}
	return 0;
}

int hio_dev_sck_getpeeraddr (hio_dev_sck_t* dev, hio_skad_t* skad)
{
	hio_scklen_t addrlen = HIO_SIZEOF(*skad);
	HIO_MEMSET(skad, 0, HIO_SIZEOF(*skad)); /* see hio_dev_sck_getsockaddr() */
	if (dev->type == HIO_DEV_SCK_QX)
	{
		hio_skad_init_for_qx(skad);
	}
	else if (getpeername(dev->hnd, (struct sockaddr*)skad, &addrlen) <= -1)
	{
		hio_seterrwithsyserr(dev->hio, 0, errno);
		return -1;
	}
	return 0;
}

static int update_mcast_group (hio_dev_sck_t* dev, int join, const hio_skad_t* mcast_skad, int ifindex)
{
	int f;

	f = hio_skad_get_family(mcast_skad);
	switch (f)
	{
		case HIO_AF_INET:
		{
			/* TODO: if ip_mreqn doesn't exist, get the ip address of the index and set to imr_address */
		#if defined(HAVE_STRUCT_IP_MREQN)
			struct ip_mreqn mreq;
		#else
			struct ip_mreq mreq;
		#endif
			HIO_MEMSET(&mreq, 0, HIO_SIZEOF(mreq));
			hio_skad_get_ipad_bytes (mcast_skad, &mreq.imr_multiaddr, HIO_SIZEOF(mreq.imr_multiaddr));
			/*mreq.imr_address = TODO: fill it will the ifindex's ip address */
		#if defined(HAVE_STRUCT_IP_MREQN)
			mreq.imr_ifindex = ifindex;
		#endif
			if (hio_dev_sck_setsockopt(dev, IPPROTO_IP, (join? IP_ADD_MEMBERSHIP: IP_DROP_MEMBERSHIP), &mreq, HIO_SIZEOF(mreq)) <= -1) return -1;
			return 0;
		}

		case HIO_AF_INET6:
		{
			struct ipv6_mreq mreq;
			HIO_MEMSET(&mreq, 0, HIO_SIZEOF(mreq));
			hio_skad_get_ipad_bytes (mcast_skad, &mreq.ipv6mr_multiaddr, HIO_SIZEOF(mreq.ipv6mr_multiaddr));
			mreq.ipv6mr_interface = ifindex;
			if (hio_dev_sck_setsockopt(dev, IPPROTO_IPV6, (join? IPV6_JOIN_GROUP: IPV6_LEAVE_GROUP), &mreq, HIO_SIZEOF(mreq)) <= -1) return -1;
			return 0;
		}
	}

	hio_seterrbfmt(hio_dev_sck_gethio(dev), HIO_EINVAL, "invalid multicast address family");
	return -1;
}

int hio_dev_sck_joinmcastgroup (hio_dev_sck_t* dev, const hio_skad_t* mcast_skad, int ifindex)
{
	return update_mcast_group(dev, 1, mcast_skad, ifindex);
}

int hio_dev_sck_leavemcastgroup (hio_dev_sck_t* dev, const hio_skad_t* mcast_skad, int ifindex)
{
	return update_mcast_group(dev, 0, mcast_skad, ifindex);
}

/* ========================================================================= */

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
			hio_seterrnum(dev->hio, HIO_EINVAL);
			return -1;
	}

	if (shutdown(dev->hnd, how) <= -1)
	{
		hio_seterrwithsyserr(dev->hio, 0, errno);
		return -1;
	}

	return 0;
}

/* ------------------------------------------------------------------------- */
/* packet filtering                                                          */
/* ------------------------------------------------------------------------- */

/* SO_ATTACH_FILTER and BIOCSETF take the same instruction, under two names.
 * asserted rather than assumed - a silent layout mismatch here would hand the
 * kernel a filter program made of the wrong fields. */
#if defined(SO_ATTACH_FILTER) && defined(HAVE_LINUX_FILTER_H)
HIO_STATIC_ASSERT(HIO_SIZEOF(hio_bpf_insn_t) == HIO_SIZEOF(struct sock_filter));
#elif defined(BIOCSETF) && defined(HAVE_NET_BPF_H)
HIO_STATIC_ASSERT(HIO_SIZEOF(hio_bpf_insn_t) == HIO_SIZEOF(struct bpf_insn));
#endif

int hio_dev_sck_setfilter (hio_dev_sck_t* dev, const hio_bpf_insn_t* insns, hio_oow_t ninsns)
{
	hio_t* hio = dev->hio;

	if (!insns || ninsns <= 0 || ninsns > 0xFFFF)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "invalid filter program of %zu instruction(s)", ninsns);
		return -1;
	}

#if defined(SO_ATTACH_FILTER) && defined(HAVE_LINUX_FILTER_H)
	{
		struct sock_fprog prog;
		prog.len = (unsigned short)ninsns;
		/* the cast is the point of the assertion above */
		prog.filter = (struct sock_filter*)insns;
		if (setsockopt(dev->hnd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, HIO_SIZEOF(prog)) <= -1)
		{
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}
		return 0;
	}
#elif defined(BIOCSETF) && defined(HAVE_NET_BPF_H)
	{
		struct bpf_program prog;
		prog.bf_len = (unsigned int)ninsns;
		prog.bf_insns = (struct bpf_insn*)insns;
		if (ioctl(dev->hnd, BIOCSETF, &prog) <= -1)
		{
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}
		return 0;
	}
#else
	hio_seterrbfmt(hio, HIO_ENOIMPL, "packet filtering not supported");
	return -1;
#endif
}

int hio_dev_sck_clearfilter (hio_dev_sck_t* dev)
{
	/* HIO_UNUSED because the bsd branch below reaches its answer through
	 * hio_dev_sck_setfilter() and never touches hio itself */
	hio_t* hio HIO_UNUSED = dev->hio;

#if defined(SO_DETACH_FILTER)
	{
		int dummy = 0;
		if (setsockopt(dev->hnd, SOL_SOCKET, SO_DETACH_FILTER, &dummy, HIO_SIZEOF(dummy)) <= -1)
		{
			hio_seterrwithsyserr(hio, 0, errno);
			return -1;
		}
		return 0;
	}
#elif defined(BIOCSETF) && defined(HAVE_NET_BPF_H)
	{
		/* no detach ioctl on the bsds. a one-instruction program that returns
		 * the largest possible snap length accepts everything, which is what
		 * having no filter means. */
		static const hio_bpf_insn_t accept_all[] = { { 0x06, 0, 0, 0xFFFFFFFFu } };
		return hio_dev_sck_setfilter(dev, accept_all, 1);
	}
#else
	hio_seterrbfmt(hio, HIO_ENOIMPL, "packet filtering not supported");
	return -1;
#endif
}

/* ------------------------------------------------------------------------- */
/* sctp peel-off                                                             */
/* ------------------------------------------------------------------------- */

#if defined(ENABLE_SCTP_PEELOFF)
/* the one-to-one type a peeled-off association becomes. sctp_peeloff() hands
 * back a SOCK_STREAM socket carrying one association, which is precisely what
 * the one-to-one types already describe - so the peeled device gets the
 * existing one-to-one methods, write queue and all. */
static int sctp_one_to_one_type (hio_dev_sck_type_t sp_type, hio_dev_sck_type_t* one_to_one)
{
	switch (sp_type)
	{
		case HIO_DEV_SCK_SCTP4_SEQPKT:
			*one_to_one = HIO_DEV_SCK_SCTP4;
			return 0;

		case HIO_DEV_SCK_SCTP6_SEQPKT:
			*one_to_one = HIO_DEV_SCK_SCTP6;
			return 0;

		default:
			return -1;
	}
}

int hio_dev_sck_peeloff (hio_dev_sck_t* rdev, hio_int32_t assoc_id)
{
	hio_t* hio = rdev->hio;
	hio_dev_sck_type_t clitype;
	hio_syshnd_t clisck;
	hio_skad_t remoteaddr;
	hio_scklen_t addrlen;

	if (sctp_one_to_one_type(rdev->type, &clitype) <= -1)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "not a one-to-many sctp socket");
		return -1;
	}

	/* move the association onto a socket of its own. anything still queued for
	 * it in the kernel moves with it, which is what makes a mid-stream peel
	 * safe: messages already handed to on_read() are the caller's, the rest
	 * arrive on the new device, and none are lost or seen twice. */
	clisck = sctp_peeloff(rdev->hnd, (sctp_assoc_t)assoc_id);
	if (clisck <= -1)
	{
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	/* the extra area past the sockaddr is not written by getpeername(), so it
	 * is cleared here rather than left holding the stream and ppid of whatever
	 * message last used this device's remoteaddr. */
	HIO_MEMSET(&remoteaddr, 0, HIO_SIZEOF(remoteaddr));
	addrlen = HIO_SIZEOF(remoteaddr);
	if (getpeername(clisck, (struct sockaddr*)&remoteaddr, &addrlen) <= -1)
	{
		/* fall back on the address the notification came from - the primary
		 * path of the same association. the remaining addresses of a
		 * multi-homed peer are reachable with hio_dev_sck_getpaddrs(). */
		remoteaddr = rdev->remoteaddr;
		hio_skad_set_chan(&remoteaddr, 0);
		hio_skad_set_ppid(&remoteaddr, 0);
		hio_skad_set_assoc(&remoteaddr, 0);
	}

	/* no separate error handling to close clisck becuase it's supposed to be
	 * handled by hio_dev_make() via the fail_before_make callback inside
	 * make_accepted_client_connection(). if it fails even before hio_dev_make()
	 * inside make_accepted_client_connection(), it should close the socket explicitly */
	return make_accepted_client_connection(rdev, clisck, &remoteaddr, clitype);
}

#else /* ENABLE_SCTP_PEELOFF */

int hio_dev_sck_peeloff (hio_dev_sck_t* rdev, hio_int32_t assoc_id)
{
	hio_seterrbfmt(rdev->hio, HIO_ENOIMPL, "sctp peel-off not supported");
	return -1;
}

#endif /* ENABLE_SCTP_PEELOFF */

/* ------------------------------------------------------------------------- */

#if defined(ENABLE_SCTP)

int hio_dev_sck_parse_assoc_event (const void* data, hio_iolen_t dlen, hio_sctp_assoc_event_t* ev)
{
	struct sctp_assoc_change ac;

	/* copied out rather than read in place: a caller may hand over a buffer
	 * whose alignment is not this structure's to assume. */
	if (dlen < (hio_iolen_t)HIO_SIZEOF(ac)) return -1; /* too short to be one */
	HIO_MEMCPY(&ac, data, HIO_SIZEOF(ac));

	if (ac.sac_type != SCTP_ASSOC_CHANGE) return -1; /* a different event, not an error */

	switch (ac.sac_state)
	{
		case SCTP_COMM_UP:        ev->state = HIO_SCTP_ASSOC_COMM_UP;        break;
		case SCTP_COMM_LOST:      ev->state = HIO_SCTP_ASSOC_COMM_LOST;      break;
		case SCTP_RESTART:        ev->state = HIO_SCTP_ASSOC_RESTART;        break;
		case SCTP_SHUTDOWN_COMP:  ev->state = HIO_SCTP_ASSOC_SHUTDOWN_COMP;  break;
		case SCTP_CANT_STR_ASSOC: ev->state = HIO_SCTP_ASSOC_CANT_STR_ASSOC; break;
		default:                  ev->state = HIO_SCTP_ASSOC_STATE_UNKNOWN;  break;
	}

	ev->error = ac.sac_error;
	ev->assoc_id = (hio_int32_t)ac.sac_assoc_id;
	/* the negotiated counts - the outcome of both ends' SCTP_INITMSG requests,
	 * and not reachable any other way */
	ev->ostreams = ac.sac_outbound_streams;
	ev->instreams = ac.sac_inbound_streams;
	return 0;
}

#else

int hio_dev_sck_parse_assoc_event (const void* data, hio_iolen_t dlen, hio_sctp_assoc_event_t* ev)
{
	return -1;
}

#endif

/* ------------------------------------------------------------------------- */
/* sctp multi-homing                                                         */
/* ------------------------------------------------------------------------- */

#if defined(ENABLE_SCTP_MH)

static HIO_INLINE int is_sctp_sck (hio_dev_sck_t* dev)
{
	return sck_type_map[dev->type].proto == IPPROTO_SCTP;
}

/* the sctp calls take addresses as a packed run of sockaddrs of mixed length,
 * not an array of a fixed-size type. these two convert between that and
 * hio_skad_t, which is fixed-size and carries an extra area the kernel neither
 * writes nor reads. */

static int pack_skads (hio_t* hio, const hio_skad_t* addrs, hio_oow_t naddrs, hio_uint8_t** buf)
{
	hio_uint8_t* b, * p;
	hio_oow_t total = 0, i;

	for (i = 0; i < naddrs; i++)
	{
		int len = hio_skad_get_size(&addrs[i]);
		if (len <= 0)
		{
			hio_seterrbfmt(hio, HIO_EINVAL, "address #%zu is not of a supported family", i);
			return -1;
		}
		total += len;
	}

	b = (hio_uint8_t*)hio_allocmem(hio, total);
	if (HIO_UNLIKELY(!b)) return -1;

	for (i = 0, p = b; i < naddrs; i++)
	{
		int len = hio_skad_get_size(&addrs[i]);
		HIO_MEMCPY(p, &addrs[i], len);
		p += len;
	}

	*buf = b;
	return 0;
}

static int unpack_skads (hio_t* hio, const struct sockaddr* packed, int count, hio_skad_t* addrs, hio_oow_t* naddrs)
{
	hio_oow_t room = *naddrs, i;
	const hio_uint8_t* p = (const hio_uint8_t*)packed;

	/* every address is reported, so the caller learns how much room it needed
	 * even when it did not have enough. what does not fit is not written. */
	for (i = 0; i < (hio_oow_t)count; i++)
	{
		hio_oow_t len;

		switch (((const struct sockaddr*)p)->sa_family)
		{
			case AF_INET:
				len = HIO_SIZEOF(struct sockaddr_in);
				break;

		#if defined(AF_INET6)
			case AF_INET6:
				len = HIO_SIZEOF(struct sockaddr_in6);
				break;
		#endif

			default:
				/* the run cannot be walked past an address of unknown length */
				hio_seterrbfmt(hio, HIO_EINVAL, "unsupported address family %d in the sctp address list", (int)((const struct sockaddr*)p)->sa_family);
				*naddrs = i;
				return -1;
		}

		if (i < room)
		{
			/* the extra area past the sockaddr belongs to hio, not the kernel */
			HIO_MEMSET(&addrs[i], 0, HIO_SIZEOF(addrs[i]));
			HIO_MEMCPY(&addrs[i], p, len);
		}
		p += len;
	}

	*naddrs = (hio_oow_t)count;
	if ((hio_oow_t)count > room)
	{
		hio_seterrbfmt(hio, HIO_EBUFFULL, "%d addresses do not fit in %zu slot(s)", count, room);
		return -1;
	}

	return 0;
}

int hio_dev_sck_bindx (hio_dev_sck_t* dev, const hio_skad_t* addrs, hio_oow_t naddrs, hio_dev_sck_bindx_flag_t flags)
{
	hio_t* hio = dev->hio;
	hio_uint8_t* buf;
	int x;

	if (!is_sctp_sck(dev))
	{
		hio_seterrbfmt(hio, HIO_ENOIMPL, "not an sctp socket device");
		return -1;
	}

	if (naddrs <= 0)
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "no address given");
		return -1;
	}

	if (pack_skads(hio, addrs, naddrs, &buf) <= -1) return -1;

	x = sctp_bindx(dev->hnd, (struct sockaddr*)buf, (int)naddrs,
		(flags == HIO_DEV_SCK_BINDX_REM)? SCTP_BINDX_REM_ADDR: SCTP_BINDX_ADD_ADDR);
	if (x <= -1) hio_seterrwithsyserr(hio, 0, errno);

	hio_freemem (hio, buf);
	return (x <= -1)? -1: 0;
}

int hio_dev_sck_getladdrs (hio_dev_sck_t* dev, hio_skad_t* addrs, hio_oow_t* naddrs)
{
	hio_t* hio = dev->hio;
	struct sockaddr* packed = HIO_NULL;
	int count, x;

	if (!is_sctp_sck(dev))
	{
		hio_seterrbfmt(hio, HIO_ENOIMPL, "not an sctp socket device");
		return -1;
	}

	/* association 0 asks about the endpoint, which is what local multi-homing
	 * is about - the addresses belong to the socket, not to one association. */
	count = sctp_getladdrs(dev->hnd, 0, &packed);
	if (count <= -1)
	{
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	x = unpack_skads(hio, packed, count, addrs, naddrs);
	sctp_freeladdrs(packed);
	return x;
}

int hio_dev_sck_getpaddrs (hio_dev_sck_t* dev, hio_skad_t* addrs, hio_oow_t* naddrs)
{
	hio_t* hio = dev->hio;
	struct sockaddr* packed = HIO_NULL;
	int count, x;

	if (!is_sctp_sck(dev))
	{
		hio_seterrbfmt(hio, HIO_ENOIMPL, "not an sctp socket device");
		return -1;
	}

	/* peer addresses belong to an association, and only a one-to-one socket has
	 * exactly one. that is what a peeled-off or connected device is; a
	 * one-to-many socket holds many and cannot answer the question. */
	if (sck_type_map[dev->type].type == SOCK_SEQPACKET)
	{
		hio_seterrbfmt(hio, HIO_EPERM, "peer addresses are per-association - peel the association off first");
		return -1;
	}

	count = sctp_getpaddrs(dev->hnd, 0, &packed);
	if (count <= -1)
	{
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	x = unpack_skads(hio, packed, count, addrs, naddrs);
	sctp_freepaddrs(packed);
	return x;
}

/* SCTP_PRIMARY_ADDR takes the same two members under two names: struct
 * sctp_prim on linux, struct sctp_setprim on the bsds. */
#if defined(HAVE_STRUCT_SCTP_PRIM)
#	define sctp_prim_t struct sctp_prim
#elif defined(HAVE_STRUCT_SCTP_SETPRIM)
#	define sctp_prim_t struct sctp_setprim
#endif

int hio_dev_sck_setprimaryaddr (hio_dev_sck_t* dev, const hio_skad_t* addr)
{
	hio_t* hio = dev->hio;
#if defined(sctp_prim_t)
	sctp_prim_t prim;
	int len;

	if (!is_sctp_sck(dev))
	{
		hio_seterrbfmt(hio, HIO_ENOIMPL, "not an sctp socket device");
		return -1;
	}

	len = hio_skad_get_size(addr);
	if (len <= 0 || (hio_oow_t)len > HIO_SIZEOF(prim.ssp_addr))
	{
		hio_seterrbfmt(hio, HIO_EINVAL, "address is not of a supported family");
		return -1;
	}

	HIO_MEMSET(&prim, 0, HIO_SIZEOF(prim));
	prim.ssp_assoc_id = 0; /* the only association, on a one-to-one socket */
	HIO_MEMCPY(&prim.ssp_addr, addr, len);

	if (setsockopt(dev->hnd, IPPROTO_SCTP, SCTP_PRIMARY_ADDR, &prim, HIO_SIZEOF(prim)) <= -1)
	{
		hio_seterrwithsyserr(hio, 0, errno);
		return -1;
	}

	return 0;
#else
	hio_seterrbfmt(hio, HIO_ENOIMPL, "no structure for SCTP_PRIMARY_ADDR on this system");
	return -1;
#endif
}

#else /* ENABLE_SCTP_MH */

/* the declarations are unconditional, so the symbols have to exist. they say
 * so rather than being absent at link time. */

int hio_dev_sck_bindx (hio_dev_sck_t* dev, const hio_skad_t* addrs, hio_oow_t naddrs, hio_dev_sck_bindx_flag_t flags)
{
	hio_seterrbfmt(dev->hio, HIO_ENOIMPL, "sctp multi-homing not supported");
	return -1;
}

int hio_dev_sck_getladdrs (hio_dev_sck_t* dev, hio_skad_t* addrs, hio_oow_t* naddrs)
{
	hio_seterrbfmt(dev->hio, HIO_ENOIMPL, "sctp multi-homing not supported");
	return -1;
}

int hio_dev_sck_getpaddrs (hio_dev_sck_t* dev, hio_skad_t* addrs, hio_oow_t* naddrs)
{
	hio_seterrbfmt(dev->hio, HIO_ENOIMPL, "sctp multi-homing not supported");
	return -1;
}

int hio_dev_sck_setprimaryaddr (hio_dev_sck_t* dev, const hio_skad_t* addr)
{
	hio_seterrbfmt(dev->hio, HIO_ENOIMPL, "sctp multi-homing not supported");
	return -1;
}

#endif /* ENABLE_SCTP_MH */

int hio_dev_sck_sendfileok (hio_dev_sck_t* dev)
{
	/* the transport has to actually implement it. the sctp methods deliberately
	 * do not - sendfile() would bypass sendmsg() and lose the per-message
	 * ancillary data that is the reason for using sctp - and a transport added
	 * later may not either. without this check the answer was derived purely
	 * from the build and the ssl state, so it said yes for a device whose
	 * method slot is null and hio_dev_sendfile() would fail with HIO_ENOCAPA. */
	if (!dev->dev_mth->sendfile) return 0;

#if defined(USE_SSL)
	#if defined(HAVE_SENDFILE)
	/* unable to use sendfile over ssl */
	return !(dev->ssl);
	#else
	/* no send file implementation */
	return 0;
	#endif
#else
	#if defined(HAVE_SENDFILE)
	return 1;
	#else
	return 0;
	#endif
#endif
}

int hio_dev_sck_writetosidechan (hio_dev_sck_t* dev, const void* dptr, hio_oow_t dlen)
{
	if (dev->type == HIO_DEV_SCK_QX && write(dev->u.qx.side_chan, dptr, dlen) <= -1)
	{
		/* this doesn't set the error information on the main socket. if you may check errno, though */
		/* TODO: make hio_seterrbfmt() thread safe and set the error information properly. still the caller may be in the thread-unsafe context */
		return -1;
	}
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

/* ========================================================================= */

int hio_get_stream_sck_type_from_skad (const hio_skad_t* skad, hio_dev_sck_type_t* type)
{
	/* if you like SCTP or something, use your own mapping.
	 * this utility defaults to TCP for INET and INET6 */

	switch (hio_skad_get_family(skad))
	{
		case HIO_AF_INET:
			*type = HIO_DEV_SCK_TCP4;
			return 0;

		case HIO_AF_INET6:
			*type = HIO_DEV_SCK_TCP6;
			return 0;

	#if defined(HIO_AF_UNIX)
		case HIO_AF_UNIX:
			*type = HIO_DEV_SCK_UNIX;
			return 0;
	#endif
	}

	return -1;
}

