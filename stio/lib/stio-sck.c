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

/* ========================================================================= */
void stio_closeasyncsck (stio_t* stio, stio_sckhnd_t sck)
{
#if defined(_WIN32)
	closesocket (sck);
#else
	close (sck);
#endif
}

#if 0
int  stio_shutasyncsck (stio_t* stio, stio_sckhnd_t sck, int how)
{
	shutdown (sck, how);
}
#endif

int stio_makesckasync (stio_t* stio, stio_sckhnd_t sck)
{
	return stio_makesyshndasync (stio, (stio_syshnd_t)sck);
}

stio_sckhnd_t stio_openasyncsck (stio_t* stio, int domain, int type, int proto)
{
	stio_sckhnd_t sck;

#if defined(_WIN32)
	sck = WSASocket (domain, type, proto, NULL, 0, WSA_FLAG_OVERLAPPED /*| WSA_FLAG_NO_HANDLE_INHERIT*/);
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

	if (stio_makesckasync (stio, sck) <= -1)
	{
		close (sck);
		return STIO_SCKHND_INVALID;
	}

	/* TODO: set CLOEXEC if it's defined */
#endif

	return sck;
}

int stio_getsckadrinfo (stio_t* stio, const stio_sckadr_t* addr, stio_scklen_t* len, stio_sckfam_t* family)
{
	struct sockaddr* saddr = (struct sockaddr*)addr;

	if (saddr->sa_family == AF_INET) 
	{
		if (len) *len = STIO_SIZEOF(struct sockaddr_in);
		if (family) *family = AF_INET;
		return 0;
	}
	else if (saddr->sa_family == AF_INET6)
	{
		if (len) *len =  STIO_SIZEOF(struct sockaddr_in6);
		if (family) *family = AF_INET6;
		return 0;
	}

	stio->errnum = STIO_EINVAL;
	return -1;
}


/* ========================================================================= */
struct sck_type_map_t
{
	int domain;
	int type;
	int proto;
} sck_type_map[] =
{
	{ AF_INET,      SOCK_STREAM,      0 },
	{ AF_INET6,     SOCK_STREAM,      0 },
	{ AF_INET,      SOCK_DGRAM,       0 },
	{ AF_INET6,     SOCK_DGRAM,       0 },

	{ AF_PACKET,    SOCK_RAW,         STIO_CONST_HTON16(0x0806) },
	{ AF_PACKET,    SOCK_DGRAM,       STIO_CONST_HTON16(0x0806) } 
};

static int dev_sck_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_dev_sck_make_t* arg = (stio_dev_sck_make_t*)ctx;


	if (arg->type < 0 && arg->type >= STIO_COUNTOF(sck_type_map))
	{
		dev->stio->errnum = STIO_EINVAL;
		return -1;
	}

	rdev->sck = stio_openasyncsck (dev->stio, sck_type_map[arg->type].domain, sck_type_map[arg->type].type, sck_type_map[arg->type].proto);
	if (rdev->sck == STIO_SCKHND_INVALID) goto oops;

	rdev->dev_capa = STIO_DEV_CAPA_IN | STIO_DEV_CAPA_OUT;
	rdev->on_write = arg->on_write;
	rdev->on_read = arg->on_read;
	return 0;

oops:
	if (rdev->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (rdev->stio, rdev->sck);
		rdev->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static void dev_sck_kill (stio_dev_t* dev)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	if (rdev->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (rdev->stio, rdev->sck);
		rdev->sck = STIO_SCKHND_INVALID;
	}
}

static stio_syshnd_t dev_sck_getsyshnd (stio_dev_t* dev)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	return (stio_syshnd_t)rdev->sck;
}

static int dev_sck_read (stio_dev_t* dev, void* buf, stio_len_t* len, stio_adr_t* srcadr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	stio_scklen_t srcadrlen;
	int x;

	srcadrlen = srcadr->len;
	x = recvfrom (rdev->sck, buf, *len, 0, srcadr->ptr, &srcadrlen);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	srcadr->len = srcadrlen;
	*len = x;
	return 1;
}

static int dev_sck_write (stio_dev_t* dev, const void* data, stio_len_t* len, const stio_adr_t* dstadr)
{
	stio_dev_sck_t* rdev = (stio_dev_sck_t*)dev;
	ssize_t x;

	x = sendto (rdev->sck, data, *len, 0, dstadr->ptr, dstadr->len);
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		rdev->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}


static int dev_sck_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	return 0;
}

static stio_dev_mth_t dev_mth_sck = 
{
	dev_sck_make,
	dev_sck_kill,
	dev_sck_getsyshnd,

	dev_sck_read,
	dev_sck_write,
	dev_sck_ioctl,     /* ioctl */
};


/* ========================================================================= */

static int dev_evcb_sck_ready (stio_dev_t* dev, int events)
{
/* TODO: ... */
	if (events & STIO_DEV_EVENT_ERR) printf ("SCK READY ERROR.....\n");
	if (events & STIO_DEV_EVENT_HUP) printf ("SCK READY HANGUP.....\n");
	if (events & STIO_DEV_EVENT_PRI) printf ("SCK READY PRI.....\n");
	if (events & STIO_DEV_EVENT_IN) printf ("SCK READY IN.....\n");
	if (events & STIO_DEV_EVENT_OUT) printf ("SCK READY OUT.....\n");


	return 1; /* the device is ok. carry on reading or writing */
}

static int dev_evcb_sck_on_read (stio_dev_t* dev, const void* data, stio_len_t dlen, const stio_adr_t* adr)
{
printf ("dATA received %d bytes\n", (int)dlen);
	return 0;

}

static int dev_evcb_sck_on_write (stio_dev_t* dev, stio_len_t wrlen, void* wrctx, const stio_adr_t* adr)
{
	return 0;

}

static stio_dev_evcb_t dev_evcb_sck =
{
	dev_evcb_sck_ready,
	dev_evcb_sck_on_read,
	dev_evcb_sck_on_write
};

/* ======================================================================== */

stio_dev_sck_t* stio_dev_sck_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_sck_make_t* data)
{
	return (stio_dev_sck_t*)stio_makedev (stio, STIO_SIZEOF(stio_dev_sck_t) + xtnsize, &dev_mth_sck, &dev_evcb_sck, (void*)data);
}

int stio_dev_sck_write (stio_dev_sck_t* dev, const void* data, stio_len_t dlen, void* wrctx, const stio_adr_t* dstadr)
{
	return stio_dev_write ((stio_dev_t*)dev, data, dlen, wrctx, dstadr);
}

int stio_dev_sck_timedwrite (stio_dev_sck_t* dev, const void* data, stio_len_t dlen, const stio_ntime_t* tmout, void* wrctx, const stio_adr_t* dstadr)
{
	return stio_dev_write ((stio_dev_t*)dev, data, dlen, wrctx, dstadr);
}
