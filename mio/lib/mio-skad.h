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

#ifndef _MIO_SKAD_H_
#define _MIO_SKAD_H_

#include <mio.h>
#include <mio-utl.h>

#define MIO_SIZEOF_SKAD_T 1
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_IN
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_IN6
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_LL > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_LL
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_UN > MIO_SIZEOF_SKAD_T)
#	undef MIO_SIZEOF_SKAD_T
#	define MIO_SIZEOF_SKAD_T MIO_SIZEOF_STRUCT_SOCKADDR_UN
#endif

struct mio_skad_t
{
	mio_uint8_t data[MIO_SIZEOF_SKAD_T];
};
typedef struct mio_skad_t mio_skad_t;


#define MIO_SKAD_TO_OOCSTR_ADDR (1 << 0)
#define MIO_SKAD_TO_OOCSTR_PORT (1 << 1)
#define MIO_SKAD_TO_UCSTR_ADDR MIO_SKAD_TO_OOCSTR_ADDR
#define MIO_SKAD_TO_UCSTR_PORT MIO_SKAD_TO_OOCSTR_PORT
#define MIO_SKAD_TO_BCSTR_ADDR MIO_SKAD_TO_OOCSTR_ADDR
#define MIO_SKAD_TO_BCSTR_PORT MIO_SKAD_TO_OOCSTR_PORT

/* -------------------------------------------------------------------- */

#define MIO_ETHADDR_LEN 6
#define MIO_IP4ADDR_LEN 4
#define MIO_IP6ADDR_LEN 16 

#include <mio-pac1.h>
struct MIO_PACKED mio_ethaddr_t
{
	mio_uint8_t v[MIO_ETHADDR_LEN]; 
};
typedef struct mio_ethaddr_t mio_ethaddr_t;

struct MIO_PACKED mio_ip4ad_t
{
	mio_uint8_t v[MIO_IP4ADDR_LEN];
};
typedef struct mio_ip4ad_t mio_ip4ad_t;

struct MIO_PACKED mio_ip6ad_t
{
	mio_uint8_t v[MIO_IP6ADDR_LEN]; 
};
typedef struct mio_ip6ad_t mio_ip6ad_t;
#include <mio-upac.h>

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_ucharstoskad (
	mio_t*            mio,
	const mio_uch_t*  str,
	mio_oow_t         len,
	mio_skad_t*       skad
);

MIO_EXPORT int mio_bcharstoskad (
	mio_t*            mio,
	const mio_bch_t*  str,
	mio_oow_t         len,
	mio_skad_t*       skad
);

#define mio_bcstrtoskad(mio,str,skad) mio_bcharstoskad(mio, str, mio_count_bcstr(str), skad)
#define mio_ucstrtoskad(mio,str,skad) mio_ucharstoskad(mio, str, mio_count_ucstr(str), skad)


MIO_EXPORT mio_oow_t mio_skadtoucstr (
	mio_t*            mio,
	const mio_skad_t* skad,
	mio_uch_t*        buf,
	mio_oow_t         len,
	int               flags
);

MIO_EXPORT mio_oow_t mio_skadtobcstr (
	mio_t*            mio,
	const mio_skad_t* skad,
	mio_bch_t*        buf,
	mio_oow_t         len,
	int               flags
);

#if defined(MIO_OOCH_IS_UCH)
#       define mio_oocharstoskad mio_ucharstoskad
#       define mio_skadtooocstr mio_skadtoucstr
#else
#       define mio_oocharstoskad mio_bcharstoskad
#       define mio_skadtooocstr mio_skadtobcstr
#endif

MIO_EXPORT void mio_skad_init_for_ip4 (
	mio_skad_t*        skad,
	mio_uint16_t       port,
	mio_ip4ad_t*       ip4ad
);

MIO_EXPORT void mio_skad_init_for_ip6 (
	mio_skad_t*        skad,
	mio_uint16_t       port,
	mio_ip6ad_t*       ip6ad,
	int                scope_id
);

MIO_EXPORT void mio_skad_init_for_ip_with_bytes (
	mio_skad_t*        skad,
	mio_uint16_t       port,
	const mio_uint8_t* bytes,
	mio_oow_t          len 
);

MIO_EXPORT void mio_skad_init_for_eth (
	mio_skad_t*        skad,
	int                ifindex,
	mio_ethaddr_t*     ethaddr
);

MIO_EXPORT int mio_skad_family (
	const mio_skad_t* skad
);

MIO_EXPORT int mio_skad_size (
	const mio_skad_t* skad
);

MIO_EXPORT int mio_skad_port (
	const mio_skad_t* skad
);

MIO_EXPORT int mio_skad_ifindex (
	const mio_skad_t* skad
);

MIO_EXPORT void mio_clear_skad (
	mio_skad_t* skad
);

MIO_EXPORT int mio_equal_skads (
	const mio_skad_t* addr1,
	const mio_skad_t* addr2,
	int               strict
);

MIO_EXPORT mio_oow_t mio_ipad_bytes_to_ucstr (
	const mio_uint8_t* iptr,
	mio_oow_t          ilen,
	mio_uch_t*         buf,
	mio_oow_t          blen
);

MIO_EXPORT mio_oow_t mio_ipad_bytes_to_bcstr (
	const mio_uint8_t* iptr,
	mio_oow_t          ilen,
	mio_bch_t*         buf,
	mio_oow_t          blen
);


MIO_EXPORT int mio_uchars_to_ipad_bytes (
	const mio_uch_t*   str,
	mio_oow_t          slen,
	mio_uint8_t*       buf,
	mio_oow_t          blen
);

MIO_EXPORT int mio_bchars_to_ipad_bytes (
	const mio_bch_t*   str,
	mio_oow_t          slen,
	mio_uint8_t*       buf,
	mio_oow_t          blen
);


#if defined(__cplusplus)
}
#endif

#endif
