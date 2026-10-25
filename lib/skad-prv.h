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

#ifndef _HIO_SKAD_PRV_H_
#define _HIO_SKAD_PRV_H_

#include <sys/types.h>
#include <sys/socket.h>
#if defined(HAVE_NETINET_IN_H)
#	include <netinet/in.h>
#endif
#if defined(HAVE_SYS_UN_H)
#	include <sys/un.h>
#endif
#if defined(HAVE_NETPACKET_PACKET_H)
#	include <netpacket/packet.h>
#endif
#if defined(HAVE_NET_IF_DL_H)
#	include <net/if_dl.h>
#endif

struct sockaddr_extra_t
{
	hio_uint16_t chan;
	hio_uint32_t ppid;
	hio_int32_t assoc;
};
typedef struct sockaddr_extra_t sockaddr_extra_t;

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
/* dirty hack to secure more space at the end of the actual socket address.
 * the extra fiels must be transparent to unaware parties.
 * if you add/delete extra fields or change the size of existing fields,
 * you must update the corresponding checks in configure.ac.
 *
 * extra fields:
 *   chan - used as a stream number for SCTP PACKETSEQ sockets.
 *          use hio_skad_get_chan() and hio_skad_set_chan() for safe access.
 *   ppid - the SCTP payload protocol identifier, opaque to SCTP itself and
 *          passed through unchanged. use hio_skad_get_ppid()/set_ppid().
 *   assoc - the SCTP association identifier (sctp_assoc_t, an int32 on the
 *          systems that have it). the stable name for an association: its
 *          addresses can change under multi-homing, this does not. use
 *          hio_skad_get_assoc()/set_assoc().
 */
struct sockaddr_in_x
{
	struct sockaddr_in a;
	sockaddr_extra_t x;
};
#endif

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
struct sockaddr_in6_x
{
	struct sockaddr_in6 a;
	sockaddr_extra_t x;
};
#endif

union hio_skad_alt_t
{
	struct sockaddr    sa;
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
	struct sockaddr_in_x in4;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	struct sockaddr_in6_x in6;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll ll;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	struct sockaddr_dl dl;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
	struct sockaddr_un un;
#endif
};
typedef union hio_skad_alt_t hio_skad_alt_t;

/* The public storage must accommodate native addresses and SCTP metadata. */
HIO_STATIC_ASSERT(HIO_SIZEOF(hio_skad_t) >= HIO_SIZEOF(hio_skad_alt_t));
HIO_STATIC_ASSERT(HIO_ALIGNOF(hio_skad_t) >= HIO_ALIGNOF(hio_skad_alt_t));

#endif
