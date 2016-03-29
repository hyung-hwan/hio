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

struct stio_sckadr_t
{
	int family;
	stio_uint8_t data[128]; /* TODO: use the actual sockaddr size */
};

typedef struct stio_sckadr_t stio_sckadr_t;

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

typedef int stio_sckfam_t;


#define STIO_SCK_ETH_PROTO_IP4   0x0800 
#define STIO_SCK_ETH_PROTO_ARP   0x0806
#define STIO_SCK_ETH_PROTO_8021Q 0x8100 /* 802.1Q VLAN */
#define STIO_SCK_ETH_PROTO_IP6   0x86DD

/* ========================================================================= */


typedef struct stio_dev_sck_t stio_dev_sck_t;

typedef int (*stio_dev_sck_on_read_t) (stio_dev_sck_t* dev, const void* data, stio_len_t dlen, const stio_sckadr_t* srcadr);
typedef int (*stio_dev_sck_on_write_t) (stio_dev_sck_t* dev, stio_len_t wrlen, void* wrctx);

enum stio_dev_sck_type_t
{
	STIO_DEV_SCK_TCP4,
	STIO_DEV_SCK_TCP6,
	STIO_DEV_SCK_UPD4,
	STIO_DEV_SCK_UDP6,

	STIO_DEV_SCK_ARP,
	STIO_DEV_SCK_ARP_DGRAM
};
typedef enum stio_dev_sck_type_t stio_dev_sck_type_t;

typedef struct stio_dev_sck_make_t stio_dev_sck_make_t;
struct stio_dev_sck_make_t
{
	stio_dev_sck_type_t type;
	stio_dev_sck_on_write_t on_write;
	stio_dev_sck_on_read_t on_read;
};

struct stio_dev_sck_t
{
	STIO_DEV_HEADERS;
	stio_sckhnd_t sck;
	stio_dev_sck_on_write_t on_write;
	stio_dev_sck_on_read_t on_read;
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

STIO_EXPORT int stio_getsckadrinfo (
	stio_t*              stio,
	const stio_sckadr_t* addr,
	stio_scklen_t*       len,
	stio_sckfam_t*       family
);

/* ========================================================================= */

STIO_EXPORT stio_dev_sck_t* stio_dev_sck_make (
	stio_t*                    stio,
	stio_size_t                xtnsize,
	const stio_dev_sck_make_t* data
);

STIO_EXPORT int stio_dev_sck_write (
	stio_dev_sck_t*       dev,
	const void*           data,
	stio_len_t            len,
	void*                 wrctx,
	const stio_adr_t*     dstadr
);

STIO_EXPORT int stio_dev_sck_timedwrite (
	stio_dev_sck_t*       dev,
	const void*           data,
	stio_len_t            len,
	const stio_ntime_t*   tmout,
	void*                 wrctx,
	const stio_adr_t*     dstadr
);


#ifdef __cplusplus
}
#endif




#endif
