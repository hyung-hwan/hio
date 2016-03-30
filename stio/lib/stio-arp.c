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


#include "stio-arp.h"
#include "stio-prv.h"

#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <errno.h>

#if 0
/* ======================================================================== */

static int arp_make (stio_dev_t* dev, void* ctx)
{
	stio_dev_arp_t* arp = (stio_dev_arp_t*)dev;
	stio_dev_arp_make_t* arg = (stio_dev_arp_make_t*)ctx;

	arp->sck = stio_openasyncsck (dev->stio, PF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
	if (arp->sck == STIO_SCKHND_INVALID) goto oops;

	arp->on_write = arg->on_write;
	arp->on_read = arg->on_read;
	return 0;

oops:
	if (arp->sck != STIO_SCKHND_INVALID)
	{
		stio_closeasyncsck (arp->stio, arp->sck);
		arp->sck = STIO_SCKHND_INVALID;
	}
	return -1;
}

static void arp_kill (stio_dev_t* dev)
{
	stio_dev_arp_t* arp = (stio_dev_arp_t*)dev;
	if (arp->sck != STIO_SCKHND_INVALID) 
	{
		stio_closeasyncsck (arp->stio, arp->sck);
		arp->sck = STIO_SCKHND_INVALID;
	}
}

static stio_syshnd_t arp_getsyshnd (stio_dev_t* dev)
{
	stio_dev_arp_t* arp = (stio_dev_arp_t*)dev;
	return (stio_syshnd_t)arp->sck;
}

static int arp_read (stio_dev_t* dev, void* buf, stio_len_t* len)
{
	stio_dev_arp_t* arp = (stio_dev_arp_t*)dev;
	stio_scklen_t addrlen;
	int x;

	struct sockaddr_ll from;

/* TODO: arp_read need source address ... have to extend the send callback to accept the source address */
printf ("ARP RECVFROM...\n");
	addrlen = STIO_SIZEOF(from);
	x = recvfrom (arp->sck, buf, *len, 0, (struct sockaddr*)&from, &addrlen);
	if (x <= -1)
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data available */
		if (errno == EINTR) return 0;
		arp->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

	*len = x;
	return 1;
}

static int arp_write (stio_dev_t* dev, const void* data, stio_len_t* len)
{
	stio_dev_arp_t* arp = (stio_dev_arp_t*)arp;
	ssize_t x;

#if 0
/* TODO: arp_write need target address ... have to extend the send callback to accept the target address */
	x = sendto (arp->sck, data, *len, skad, stio_getskadlen(skad));
	if (x <= -1) 
	{
		if (errno == EINPROGRESS || errno == EWOULDBLOCK) return 0;  /* no data can be written */
		if (errno == EINTR) return 0;
		arp->stio->errnum = stio_syserrtoerrnum(errno);
		return -1;
	}

/* for UDP, if the data chunk can't be written at one go, it's actually a failure */
	if (x != *len) return -1; /* TODO: can i hava an indicator for this in stio? */

	*len = x;
#endif
	return 1;
}


static int arp_ioctl (stio_dev_t* dev, int cmd, void* arg)
{
	return 0;
}

static stio_dev_mth_t arp_mth = 
{
	arp_make,
	arp_kill,
	arp_getsyshnd,

	arp_read,
	arp_write,
	arp_ioctl,     /* ioctl */
};


/* ======================================================================== */

static int arp_ready (stio_dev_t* dev, int events)
{
/* TODO: ... */
	if (events & STIO_DEV_EVENT_ERR) printf ("UDP READY ERROR.....\n");
	if (events & STIO_DEV_EVENT_HUP) printf ("UDP READY HANGUP.....\n");
	if (events & STIO_DEV_EVENT_PRI) printf ("UDP READY PRI.....\n");
	if (events & STIO_DEV_EVENT_IN) printf ("UDP READY IN.....\n");
	if (events & STIO_DEV_EVENT_OUT) printf ("UDP READY OUT.....\n");

	return 0;
}

static int arp_on_read (stio_dev_t* dev, const void* data, stio_len_t len)
{
printf ("dATA received %d bytes\n", (int)len);
	return 0;
}

static int arp_on_write (stio_dev_t* dev, stio_len_t wrlen, void* wrctx)
{
	return 0;

}

static stio_dev_evcb_t arp_evcb =
{
	arp_ready,
	arp_on_read,
	arp_on_write
};

/* ======================================================================== */

stio_dev_arp_t* stio_dev_arp_make (stio_t* stio, stio_size_t xtnsize, const stio_dev_arp_make_t* data)
{
	return (stio_dev_arp_t*)stio_makedev (stio, STIO_SIZEOF(stio_dev_arp_t) + xtnsize, &arp_mth, &arp_evcb, (void*)data);
}

int stio_dev_arp_write (stio_dev_arp_t* dev, const stio_pkt_arp_t* arp, void* wrctx)
{
	return stio_dev_write ((stio_dev_t*)dev, arp, STIO_SIZEOF(*arp), wrctx);
}

int stio_dev_arp_timedwrite (stio_dev_arp_t* dev, const stio_pkt_arp_t* arp, const stio_ntime_t* tmout, void* wrctx)
{
	return stio_dev_write ((stio_dev_t*)dev, arp, STIO_SIZEOF(*arp), wrctx);
}
#endif
