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

#ifndef _HIO_SKAD_H_
#define _HIO_SKAD_H_

#include <hio.h>
#include <hio-utl.h>

#define HIO_SIZEOF_SKAD_T 1
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN_X > HIO_SIZEOF_SKAD_T)
#	undef HIO_SIZEOF_SKAD_T
#	define HIO_SIZEOF_SKAD_T HIO_SIZEOF_STRUCT_SOCKADDR_IN_X
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6_X > HIO_SIZEOF_SKAD_T)
#	undef HIO_SIZEOF_SKAD_T
#	define HIO_SIZEOF_SKAD_T HIO_SIZEOF_STRUCT_SOCKADDR_IN6_X
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_LL > HIO_SIZEOF_SKAD_T)
#	undef HIO_SIZEOF_SKAD_T
#	define HIO_SIZEOF_SKAD_T HIO_SIZEOF_STRUCT_SOCKADDR_LL
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_DL > HIO_SIZEOF_SKAD_T)
#	undef HIO_SIZEOF_SKAD_T
#	define HIO_SIZEOF_SKAD_T HIO_SIZEOF_STRUCT_SOCKADDR_DL
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_UN > HIO_SIZEOF_SKAD_T)
#	undef HIO_SIZEOF_SKAD_T
#	define HIO_SIZEOF_SKAD_T HIO_SIZEOF_STRUCT_SOCKADDR_UN
#endif

#if (HIO_SIZEOF_SA_FAMILY_T == 1) && !defined(HIO_SA_FAMILY_T_IS_SIGNED)
#	if !defined(HIO_AF_UNIX)
#		define HIO_AF_UNIX (254)
#	endif
#	define HIO_AF_QX (255)
#elif (HIO_SIZEOF_SA_FAMILY_T == 1) && defined(HIO_SA_FAMILY_T_IS_SIGNED)
#	if !defined(HIO_AF_UNIX)
#		define HIO_AF_UNIX (-2)
#	endif
#	define HIO_AF_QX (-1)
#else
#	if !defined(HIO_AF_UNIX)
		/* this is a fake value */
#		define HIO_AF_UNIX (65534)
#	endif
	/* this is HIO specific. No AF_XXXX definitions must overlap with this */
#	define HIO_AF_QX (65530)
#endif

struct hio_skad_t
{
	hio_uint8_t data[HIO_SIZEOF_SKAD_T];
};
typedef struct hio_skad_t hio_skad_t;


#define HIO_SKAD_TO_OOCSTR_ADDR (1 << 0)
#define HIO_SKAD_TO_OOCSTR_PORT (1 << 1)
#define HIO_SKAD_TO_UCSTR_ADDR HIO_SKAD_TO_OOCSTR_ADDR
#define HIO_SKAD_TO_UCSTR_PORT HIO_SKAD_TO_OOCSTR_PORT
#define HIO_SKAD_TO_BCSTR_ADDR HIO_SKAD_TO_OOCSTR_ADDR
#define HIO_SKAD_TO_BCSTR_PORT HIO_SKAD_TO_OOCSTR_PORT

#define HIO_IP4AD_STRLEN (15) /* not including the terminating '\0' */
#define HIO_IP6AD_STRLEN (45) /* not including the terminating '\0'. pure IPv6 address, not including the scope(e.g. %10, %eth0) */

/* size large enough to hold the ip address plus port number.
 * [IPV6ADDR%SCOPE]:PORT -> 9 for [] % : and PORT
 * Let's reserve 16 for SCOPE and not include the terminting '\0'
 */
#define HIO_SKAD_IP_STRLEN (HIO_IP6AD_STRLEN + 25)

/* -------------------------------------------------------------------- */

#define HIO_ETHAD_LEN (6)
#define HIO_IP4AD_LEN (4)
#define HIO_IP6AD_LEN (16)

#include <hio-pac1.h>
struct HIO_PACKED hio_ethad_t
{
	hio_uint8_t v[HIO_ETHAD_LEN];
};
typedef struct hio_ethad_t hio_ethad_t;

struct HIO_PACKED hio_ip4ad_t
{
	hio_uint8_t v[HIO_IP4AD_LEN];
};
typedef struct hio_ip4ad_t hio_ip4ad_t;

struct HIO_PACKED hio_ip6ad_t
{
	hio_uint8_t v[HIO_IP6AD_LEN];
};
typedef struct hio_ip6ad_t hio_ip6ad_t;
#include <hio-upac.h>

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT int hio_ucharstoskad (
	hio_t*            hio,
	const hio_uch_t*  str,
	hio_oow_t         len,
	hio_skad_t*       skad
);

HIO_EXPORT int hio_bcharstoskad (
	hio_t*            hio,
	const hio_bch_t*  str,
	hio_oow_t         len,
	hio_skad_t*       skad
);

#define hio_ucstrtoskad(hio,str,skad) hio_ucharstoskad(hio, str, hio_count_ucstr(str), skad)
#define hio_bcstrtoskad(hio,str,skad) hio_bcharstoskad(hio, str, hio_count_bcstr(str), skad)

HIO_EXPORT hio_oow_t hio_skadtoucstr (
	hio_t*            hio,
	const hio_skad_t* skad,
	hio_uch_t*        buf,
	hio_oow_t         len,
	int               flags
);

HIO_EXPORT hio_oow_t hio_skadtobcstr (
	hio_t*            hio,
	const hio_skad_t* skad,
	hio_bch_t*        buf,
	hio_oow_t         len,
	int               flags
);

#if defined(HIO_OOCH_IS_UCH)
#       define hio_oocstrtoskad hio_ucstrtoskad
#       define hio_oocharstoskad hio_ucharstoskad
#       define hio_skadtooocstr hio_skadtoucstr
#else
#       define hio_oocstrtoskad hio_bcstrtoskad
#       define hio_oocharstoskad hio_bcharstoskad
#       define hio_skadtooocstr hio_skadtobcstr
#endif

HIO_EXPORT void hio_skad_init_for_ip4 (
	hio_skad_t*        skad,
	hio_uint16_t       port,
	hio_ip4ad_t*       ip4ad
);

HIO_EXPORT void hio_skad_init_for_ip6 (
	hio_skad_t*        skad,
	hio_uint16_t       port,
	hio_ip6ad_t*       ip6ad,
	int                scope_id
);

HIO_EXPORT void hio_skad_init_for_ip_with_bytes (
	hio_skad_t*        skad,
	hio_uint16_t       port,
	const hio_uint8_t* bytes,
	hio_oow_t          len
);

HIO_EXPORT void hio_skad_init_for_eth (
	hio_skad_t*        skad,
	int                ifindex,
	hio_ethad_t*       ethad
);

HIO_EXPORT void hio_skad_init_for_qx (
	hio_skad_t*        skad
);

HIO_EXPORT int hio_skad_get_family (
	const hio_skad_t* skad
);

HIO_EXPORT int hio_skad_get_size (
	const hio_skad_t* skad
);

HIO_EXPORT int hio_skad_get_port (
	const hio_skad_t* skad
);

/* for link-level addresses */
HIO_EXPORT int hio_skad_get_ifindex (
	const hio_skad_t* skad
);

/* for ipv6 */
HIO_EXPORT int hio_skad_get_scope_id (
	const hio_skad_t* skad
);

HIO_EXPORT void hio_skad_set_scope_id (
	hio_skad_t*       skad,
	int               scope_id
);

/* for sctp */
HIO_EXPORT hio_uint16_t hio_skad_get_chan (
	const hio_skad_t* skad
);

HIO_EXPORT void hio_skad_set_chan (
	hio_skad_t*       skad,
	hio_uint16_t      chan
);

HIO_EXPORT hio_oow_t hio_skad_get_ipad_bytes (
	const hio_skad_t* skad,
	void*             buf,
	hio_oow_t         len
);

HIO_EXPORT void hio_clear_skad (
	hio_skad_t* skad
);

HIO_EXPORT int hio_equal_skads (
	const hio_skad_t* addr1,
	const hio_skad_t* addr2,
	int               strict
);

HIO_EXPORT hio_oow_t hio_ipad_bytes_to_ucstr (
	const hio_uint8_t* iptr,
	hio_oow_t          ilen,
	hio_uch_t*         buf,
	hio_oow_t          blen
);

HIO_EXPORT hio_oow_t hio_ipad_bytes_to_bcstr (
	const hio_uint8_t* iptr,
	hio_oow_t          ilen,
	hio_bch_t*         buf,
	hio_oow_t          blen
);


HIO_EXPORT int hio_uchars_to_ipad_bytes (
	const hio_uch_t*   str,
	hio_oow_t          slen,
	hio_uint8_t*       buf,
	hio_oow_t          blen
);

HIO_EXPORT int hio_bchars_to_ipad_bytes (
	const hio_bch_t*   str,
	hio_oow_t          slen,
	hio_uint8_t*       buf,
	hio_oow_t          blen
);


#define hio_ucstr_to_ipad_bytes(str,buf,blen) hio_uchars_to_ipad_bytes(str, hio_count_ucstr(str,buf,len)
#define hio_bcstr_to_ipad_bytes(str,buf,blen) hio_bchars_to_ipad_bytes(str, hio_count_bcstr(str,buf,len)

#if defined(HIO_OOCH_IS_UCH)
#	define hio_ipad_bytes_to_oocstr hio_ipad_bytes_to_ucstr
#	define hio_oochars_to_ipad_bytes hio_uchars_to_ipad_bytes
#	define hio_oocstr_to_ipad_bytes hio_ucstr_to_ipad_bytes
#else
#	define hio_ipad_bytes_to_oocstr hio_ipad_bytes_to_bcstr
#	define hio_oochars_to_ipad_bytes hio_bchars_to_ipad_bytes
#	define hio_oocstr_to_ipad_bytes hio_bcstr_to_ipad_bytes
#endif

HIO_EXPORT int hio_ipad_bytes_is_v4_mapped (
	const hio_uint8_t* iptr,
	hio_oow_t          ilen
);

HIO_EXPORT int hio_ipad_bytes_is_loop_back (
	const hio_uint8_t* iptr,
	hio_oow_t          ilen
);

HIO_EXPORT int hio_ipad_bytes_is_link_local (
	const hio_uint8_t* iptr,
	hio_oow_t          ilen
);

#if defined(__cplusplus)
}
#endif

#endif
