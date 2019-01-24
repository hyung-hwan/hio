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


#include "mio-prv.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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

#include <arpa/inet.h>

union sockaddr_t
{
	struct sockaddr    sa;
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
	struct sockaddr_in in4;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	struct sockaddr_in6 in6;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll ll;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
	struct sockaddr_un un;
#endif
};
typedef union sockaddr_t sockaddr_t;

#undef char_t
#undef cstr_t
#undef str_to_sockaddr
#undef str_to_ipv4
#undef str_to_ipv6
#define CHAR_T_IS_BCH
#undef CHAR_T_IS_UCH
#define char_t mio_bch_t
#define cstr_t mio_bcs_t
#define str_to_sockaddr bcstr_to_sockaddr
#define str_to_ipv4 bcstr_to_ipv4
#define str_to_ipv6 bcstr_to_ipv6
#include "sck-addr.h"


#undef char_t
#undef cstr_t
#undef str_to_sockaddr
#undef str_to_ipv4
#undef str_to_ipv6
#undef CHAR_T_IS_BCH
#define CHAR_T_IS_UCH
#define char_t mio_uch_t
#define cstr_t mio_ucs_t
#define str_to_sockaddr ucstr_to_sockaddr
#define str_to_ipv4 ucstr_to_ipv4
#define str_to_ipv6 ucstr_to_ipv6
#include "sck-addr.h"
