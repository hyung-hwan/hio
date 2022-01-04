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

/*
 * Do NOT edit hio-utl.h. Edit hio-utl.h.m4 instead.
 *
 * Generate hio-utl.h.m4 with m4
 *   $ m4 hio-utl.h.m4 > hio-utl.h
 */

#ifndef _HIO_UTL_H_
#define _HIO_UTL_H_

#include <hio-cmn.h>
#include <stdarg.h>





/* =========================================================================
 * ENDIAN CHANGE OF A CONSTANT
 * ========================================================================= */
#define HIO_CONST_BSWAP16(x) \
	((hio_uint16_t)((((hio_uint16_t)(x) & ((hio_uint16_t)0xff << 0)) << 8) | \
	                (((hio_uint16_t)(x) & ((hio_uint16_t)0xff << 8)) >> 8)))

#define HIO_CONST_BSWAP32(x) \
	((hio_uint32_t)((((hio_uint32_t)(x) & ((hio_uint32_t)0xff <<  0)) << 24) | \
	                (((hio_uint32_t)(x) & ((hio_uint32_t)0xff <<  8)) <<  8) | \
	                (((hio_uint32_t)(x) & ((hio_uint32_t)0xff << 16)) >>  8) | \
	                (((hio_uint32_t)(x) & ((hio_uint32_t)0xff << 24)) >> 24)))

#if defined(HIO_HAVE_UINT64_T)
#define HIO_CONST_BSWAP64(x) \
	((hio_uint64_t)((((hio_uint64_t)(x) & ((hio_uint64_t)0xff <<  0)) << 56) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff <<  8)) << 40) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff << 16)) << 24) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff << 24)) <<  8) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff << 32)) >>  8) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff << 40)) >> 24) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff << 48)) >> 40) | \
	                (((hio_uint64_t)(x) & ((hio_uint64_t)0xff << 56)) >> 56)))
#endif

#if defined(HIO_HAVE_UINT128_T)
#define HIO_CONST_BSWAP128(x) \
	((hio_uint128_t)((((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 0)) << 120) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 8)) << 104) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 16)) << 88) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 24)) << 72) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 32)) << 56) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 40)) << 40) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 48)) << 24) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 56)) << 8) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 64)) >> 8) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 72)) >> 24) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 80)) >> 40) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 88)) >> 56) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 96)) >> 72) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 104)) >> 88) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 112)) >> 104) | \
	                 (((hio_uint128_t)(x) & ((hio_uint128_t)0xff << 120)) >> 120)))
#endif

#if defined(HIO_ENDIAN_LITTLE)

#	if defined(HIO_HAVE_UINT16_T)
#	define HIO_CONST_NTOH16(x) HIO_CONST_BSWAP16(x)
#	define HIO_CONST_HTON16(x) HIO_CONST_BSWAP16(x)
#	define HIO_CONST_HTOBE16(x) HIO_CONST_BSWAP16(x)
#	define HIO_CONST_HTOLE16(x) (x)
#	define HIO_CONST_BE16TOH(x) HIO_CONST_BSWAP16(x)
#	define HIO_CONST_LE16TOH(x) (x)
#	endif

#	if defined(HIO_HAVE_UINT32_T)
#	define HIO_CONST_NTOH32(x) HIO_CONST_BSWAP32(x)
#	define HIO_CONST_HTON32(x) HIO_CONST_BSWAP32(x)
#	define HIO_CONST_HTOBE32(x) HIO_CONST_BSWAP32(x)
#	define HIO_CONST_HTOLE32(x) (x)
#	define HIO_CONST_BE32TOH(x) HIO_CONST_BSWAP32(x)
#	define HIO_CONST_LE32TOH(x) (x)
#	endif

#	if defined(HIO_HAVE_UINT64_T)
#	define HIO_CONST_NTOH64(x) HIO_CONST_BSWAP64(x)
#	define HIO_CONST_HTON64(x) HIO_CONST_BSWAP64(x)
#	define HIO_CONST_HTOBE64(x) HIO_CONST_BSWAP64(x)
#	define HIO_CONST_HTOLE64(x) (x)
#	define HIO_CONST_BE64TOH(x) HIO_CONST_BSWAP64(x)
#	define HIO_CONST_LE64TOH(x) (x)
#	endif

#	if defined(HIO_HAVE_UINT128_T)
#	define HIO_CONST_NTOH128(x) HIO_CONST_BSWAP128(x)
#	define HIO_CONST_HTON128(x) HIO_CONST_BSWAP128(x)
#	define HIO_CONST_HTOBE128(x) HIO_CONST_BSWAP128(x)
#	define HIO_CONST_HTOLE128(x) (x)
#	define HIO_CONST_BE128TOH(x) HIO_CONST_BSWAP128(x)
#	define HIO_CONST_LE128TOH(x) (x)
#endif

#elif defined(HIO_ENDIAN_BIG)

#	if defined(HIO_HAVE_UINT16_T)
#	define HIO_CONST_NTOH16(x) (x)
#	define HIO_CONST_HTON16(x) (x)
#	define HIO_CONST_HTOBE16(x) (x)
#	define HIO_CONST_HTOLE16(x) HIO_CONST_BSWAP16(x)
#	define HIO_CONST_BE16TOH(x) (x)
#	define HIO_CONST_LE16TOH(x) HIO_CONST_BSWAP16(x)
#	endif

#	if defined(HIO_HAVE_UINT32_T)
#	define HIO_CONST_NTOH32(x) (x)
#	define HIO_CONST_HTON32(x) (x)
#	define HIO_CONST_HTOBE32(x) (x)
#	define HIO_CONST_HTOLE32(x) HIO_CONST_BSWAP32(x)
#	define HIO_CONST_BE32TOH(x) (x)
#	define HIO_CONST_LE32TOH(x) HIO_CONST_BSWAP32(x)
#	endif

#	if defined(HIO_HAVE_UINT64_T)
#	define HIO_CONST_NTOH64(x) (x)
#	define HIO_CONST_HTON64(x) (x)
#	define HIO_CONST_HTOBE64(x) (x)
#	define HIO_CONST_HTOLE64(x) HIO_CONST_BSWAP64(x)
#	define HIO_CONST_BE64TOH(x) (x)
#	define HIO_CONST_LE64TOH(x) HIO_CONST_BSWAP64(x)
#	endif

#	if defined(HIO_HAVE_UINT128_T)
#	define HIO_CONST_NTOH128(x) (x)
#	define HIO_CONST_HTON128(x) (x)
#	define HIO_CONST_HTOBE128(x) (x)
#	define HIO_CONST_HTOLE128(x) HIO_CONST_BSWAP128(x)
#	define HIO_CONST_BE128TOH(x) (x)
#	define HIO_CONST_LE128TOH(x) HIO_CONST_BSWAP128(x)
#	endif

#else
#	error UNKNOWN ENDIAN
#endif


/* =========================================================================
 * HASH
 * ========================================================================= */
#if (HIO_SIZEOF_OOW_T == 4)
#	define HIO_HASH_FNV_MAGIC_INIT (0x811c9dc5)
#	define HIO_HASH_FNV_MAGIC_PRIME (0x01000193)
#elif (HIO_SIZEOF_OOW_T == 8)
#	define HIO_HASH_FNV_MAGIC_INIT (0xCBF29CE484222325)
#	define HIO_HASH_FNV_MAGIC_PRIME (0x100000001B3l)
#elif (HIO_SIZEOF_OOW_T == 16)
#	define HIO_HASH_FNV_MAGIC_INIT (0x6C62272E07BB014262B821756295C58D)
#	define HIO_HASH_FNV_MAGIC_PRIME (0x1000000000000000000013B)
#endif

#if defined(HIO_HASH_FNV_MAGIC_INIT)
	/* FNV-1 hash */
#	define HIO_HASH_INIT HIO_HASH_FNV_MAGIC_INIT
#	define HIO_HASH_VALUE(hv,v) (((hv) ^ (v)) * HIO_HASH_FNV_MAGIC_PRIME)

#else
	/* SDBM hash */
#	define HIO_HASH_INIT 0
#	define HIO_HASH_VALUE(hv,v) (((hv) << 6) + ((hv) << 16) - (hv) + (v))
#endif

#define HIO_HASH_VPTL(hv, ptr, len, type) do { \
	hv = HIO_HASH_INIT; \
	HIO_HASH_MORE_VPTL (hv, ptr, len, type); \
} while(0)

#define HIO_HASH_MORE_VPTL(hv, ptr, len, type) do { \
	type* __hio_hash_more_vptl_p = (type*)(ptr); \
	type* __hio_hash_more_vptl_q = (type*)__hio_hash_more_vptl_p + (len); \
	while (__hio_hash_more_vptl_p < __hio_hash_more_vptl_q) \
	{ \
		hv = HIO_HASH_VALUE(hv, *__hio_hash_more_vptl_p); \
		__hio_hash_more_vptl_p++; \
	} \
} while(0)

#define HIO_HASH_VPTR(hv, ptr, type) do { \
	hv = HIO_HASH_INIT; \
	HIO_HASH_MORE_VPTR (hv, ptr, type); \
} while(0)

#define HIO_HASH_MORE_VPTR(hv, ptr, type) do { \
	type* __hio_hash_more_vptr_p = (type*)(ptr); \
	while (*__hio_hash_more_vptr_p) \
	{ \
		hv = HIO_HASH_VALUE(hv, *__hio_hash_more_vptr_p); \
		__hio_hash_more_vptr_p++; \
	} \
} while(0)

#define HIO_HASH_BYTES(hv, ptr, len) HIO_HASH_VPTL(hv, ptr, len, const hio_uint8_t)
#define HIO_HASH_MORE_BYTES(hv, ptr, len) HIO_HASH_MORE_VPTL(hv, ptr, len, const hio_uint8_t)

#define HIO_HASH_BCHARS(hv, ptr, len) HIO_HASH_VPTL(hv, ptr, len, const hio_bch_t)
#define HIO_HASH_MORE_BCHARS(hv, ptr, len) HIO_HASH_MORE_VPTL(hv, ptr, len, const hio_bch_t)

#define HIO_HASH_UCHARS(hv, ptr, len) HIO_HASH_VPTL(hv, ptr, len, const hio_uch_t)
#define HIO_HASH_MORE_UCHARS(hv, ptr, len) HIO_HASH_MORE_VPTL(hv, ptr, len, const hio_uch_t)

#define HIO_HASH_BCSTR(hv, ptr) HIO_HASH_VPTR(hv, ptr, const hio_bch_t)
#define HIO_HASH_MORE_BCSTR(hv, ptr) HIO_HASH_MORE_VPTR(hv, ptr, const hio_bch_t)

#define HIO_HASH_UCSTR(hv, ptr) HIO_HASH_VPTR(hv, ptr, const hio_uch_t)
#define HIO_HASH_MORE_UCSTR(hv, ptr) HIO_HASH_MORE_VPTR(hv, ptr, const hio_uch_t)

/* =========================================================================
 * STRING
 * ========================================================================= */

enum hio_trim_oochars_flag_t
{
        HIO_TRIM_OOCHARS_LEFT  = (1 << 0), /**< trim leading spaces */
#define HIO_TRIM_OOCHARS_LEFT HIO_TRIM_OOCHARS_LEFT
#define HIO_TRIM_UCHARS_LEFT HIO_TRIM_OOCHARS_LEFT
#define HIO_TRIM_BCHARS_LEFT HIO_TRIM_OOCHARS_LEFT
        HIO_TRIM_OOCHARS_RIGHT = (1 << 1)  /**< trim trailing spaces */
#define HIO_TRIM_OOCHARS_RIGHT HIO_TRIM_OOCHARS_RIGHT
#define HIO_TRIM_UCHARS_RIGHT HIO_TRIM_OOCHARS_RIGHT
#define HIO_TRIM_BCHARS_RIGHT HIO_TRIM_OOCHARS_RIGHT
};

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT hio_oow_t hio_count_ucstr (
	const hio_uch_t* str
);

HIO_EXPORT hio_oow_t hio_count_bcstr (
	const hio_bch_t* str
);

/**
 * The hio_equal_uchars() function determines equality of two strings
 * of the same length \a len.
 */
HIO_EXPORT int hio_equal_uchars (
	const hio_uch_t* str1,
	const hio_uch_t* str2,
	hio_oow_t        len
);

HIO_EXPORT int hio_equal_bchars (
	const hio_bch_t* str1,
	const hio_bch_t* str2,
	hio_oow_t        len
);

HIO_EXPORT int hio_comp_uchars (
	const hio_uch_t* str1,
	hio_oow_t        len1,
	const hio_uch_t* str2,
	hio_oow_t        len2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_bchars (
	const hio_bch_t* str1,
	hio_oow_t        len1,
	const hio_bch_t* str2,
	hio_oow_t        len2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_ucstr (
	const hio_uch_t* str1,
	const hio_uch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_bcstr (
	const hio_bch_t* str1,
	const hio_bch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_ucstr_limited (
	const hio_uch_t* str1,
	const hio_uch_t* str2,
	hio_oow_t        maxlen,
	int              ignorecase
);

HIO_EXPORT int hio_comp_bcstr_limited (
	const hio_bch_t* str1,
	const hio_bch_t* str2,
	hio_oow_t        maxlen,
	int              ignorecase
);

HIO_EXPORT int hio_comp_ucstr_bcstr (
	const hio_uch_t* str1,
	const hio_bch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_uchars_ucstr (
	const hio_uch_t* str1,
	hio_oow_t        len,
	const hio_uch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_uchars_bcstr (
	const hio_uch_t* str1,
	hio_oow_t        len,
	const hio_bch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_bchars_bcstr (
	const hio_bch_t* str1,
	hio_oow_t        len,
	const hio_bch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_bchars_ucstr (
	const hio_bch_t* str1,
	hio_oow_t        len,
	const hio_uch_t* str2,
	int              ignorecase
);

HIO_EXPORT void hio_copy_uchars (
	hio_uch_t*       dst,
	const hio_uch_t* src,
	hio_oow_t        len
);

HIO_EXPORT void hio_copy_bchars (
	hio_bch_t*       dst,
	const hio_bch_t* src,
	hio_oow_t        len
);

HIO_EXPORT void hio_copy_bchars_to_uchars (
	hio_uch_t*       dst,
	const hio_bch_t* src,
	hio_oow_t        len
);
HIO_EXPORT void hio_copy_uchars_to_bchars (
	hio_bch_t*       dst,
	const hio_uch_t* src,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_copy_uchars_to_ucstr_unlimited (
	hio_uch_t*       dst,
	const hio_uch_t* src,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_copy_bchars_to_bcstr_unlimited (
	hio_bch_t*       dst,
	const hio_bch_t* src,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_copy_ucstr (
	hio_uch_t*       dst,
	hio_oow_t        len,
	const hio_uch_t* src
);

HIO_EXPORT hio_oow_t hio_copy_bcstr (
	hio_bch_t*       dst,
	hio_oow_t        len,
	const hio_bch_t* src
);

HIO_EXPORT hio_oow_t hio_copy_uchars_to_ucstr (
	hio_uch_t*       dst,
	hio_oow_t        dlen,
	const hio_uch_t* src,
	hio_oow_t        slen
);

HIO_EXPORT hio_oow_t hio_copy_bchars_to_bcstr (
	hio_bch_t*       dst,
	hio_oow_t        dlen,
	const hio_bch_t* src,
	hio_oow_t        slen
);

HIO_EXPORT hio_oow_t hio_copy_ucstr_unlimited (
	hio_uch_t*       dst,
	const hio_uch_t* src
);

HIO_EXPORT hio_oow_t hio_copy_bcstr_unlimited (
	hio_bch_t*       dst,
	const hio_bch_t* src
);

HIO_EXPORT hio_oow_t hio_concat_uchars_to_ucstr (
	hio_uch_t*       buf,
	hio_oow_t        bsz,
	const hio_uch_t* src,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_concat_bchars_to_bcstr (
	hio_bch_t*       buf,
	hio_oow_t        bsz,
	const hio_bch_t* src,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_concat_ucstr (
	hio_uch_t*       buf,
	hio_oow_t        bsz,
	const hio_uch_t* src
);

HIO_EXPORT hio_oow_t hio_concat_bcstr (
	hio_bch_t*       buf,
	hio_oow_t        bsz,
	const hio_bch_t* src
);

HIO_EXPORT void hio_fill_uchars (
	hio_uch_t*       dst,
	const hio_uch_t  ch,
	hio_oow_t        len
);

HIO_EXPORT void hio_fill_bchars (
	hio_bch_t*       dst,
	const hio_bch_t  ch,
	hio_oow_t        len
);

HIO_EXPORT const hio_bch_t* hio_find_bcstr_word_in_bcstr (
	const hio_bch_t* str,
	const hio_bch_t* word,
	hio_bch_t        extra_delim,
	int              ignorecase
);

HIO_EXPORT const hio_uch_t* hio_find_ucstr_word_in_ucstr (
	const hio_uch_t* str,
	const hio_uch_t* word,
	hio_uch_t        extra_delim,
	int              ignorecase
);

HIO_EXPORT hio_uch_t* hio_find_uchar (
	const hio_uch_t* ptr,
	hio_oow_t        len,
	hio_uch_t        c
);

HIO_EXPORT hio_bch_t* hio_find_bchar (
	const hio_bch_t* ptr,
	hio_oow_t        len,
	hio_bch_t        c
);

HIO_EXPORT hio_uch_t* hio_rfind_uchar (
	const hio_uch_t* ptr,
	hio_oow_t        len,
	hio_uch_t        c
);

HIO_EXPORT hio_bch_t* hio_rfind_bchar (
	const hio_bch_t* ptr,
	hio_oow_t        len,
	hio_bch_t        c
);

HIO_EXPORT hio_uch_t* hio_find_uchar_in_ucstr (
	const hio_uch_t* ptr,
	hio_uch_t        c
);

HIO_EXPORT hio_bch_t* hio_find_bchar_in_bcstr (
	const hio_bch_t* ptr,
	hio_bch_t        c
);

HIO_EXPORT hio_oow_t hio_rotate_uchars (
	hio_uch_t*       str,
	hio_oow_t        len,
	int              dir,
	hio_oow_t        n
);

HIO_EXPORT hio_oow_t hio_rotate_bchars (
	hio_bch_t*       str,
	hio_oow_t        len,
	int              dir,
	hio_oow_t        n
);


HIO_EXPORT hio_uch_t* hio_trim_uchars (
	const hio_uch_t* str,
	hio_oow_t*       len,
	int              flags
);

HIO_EXPORT hio_bch_t* hio_trim_bchars (
	const hio_bch_t* str,
	hio_oow_t*       len,
	int              flags
);

HIO_EXPORT int hio_split_ucstr (
	hio_uch_t*       s,
	const hio_uch_t* delim,
	hio_uch_t        lquote,
	hio_uch_t        rquote,
	hio_uch_t        escape
);

HIO_EXPORT int hio_split_bcstr (
	hio_bch_t*       s,
	const hio_bch_t* delim,
	hio_bch_t        lquote,
	hio_bch_t        rquote,
	hio_bch_t        escape
);


#if defined(HIO_OOCH_IS_UCH)
#	define hio_count_oocstr hio_count_ucstr
#	define hio_equal_oochars hio_equal_uchars
#	define hio_comp_oochars hio_comp_uchars
#	define hio_comp_oocstr_bcstr hio_comp_ucstr_bcstr
#	define hio_comp_oochars_bcstr hio_comp_uchars_bcstr
#	define hio_comp_oochars_ucstr hio_comp_uchars_ucstr
#	define hio_comp_oochars_oocstr hio_comp_uchars_ucstr
#	define hio_comp_oocstr hio_comp_ucstr

#	define hio_copy_oochars hio_copy_uchars
#	define hio_copy_bchars_to_oochars hio_copy_bchars_to_uchars
#	define hio_copy_oochars_to_bchars hio_copy_uchars_to_bchars
#	define hio_copy_uchars_to_oochars hio_copy_uchars
#	define hio_copy_oochars_to_uchars hio_copy_uchars

#	define hio_copy_oochars_to_oocstr hio_copy_uchars_to_ucstr
#	define hio_copy_oochars_to_oocstr_unlimited hio_copy_uchars_to_ucstr_unlimited
#	define hio_copy_oocstr hio_copy_ucstr
#	define hio_copy_oocstr_unlimited hio_copy_ucstr_unlimited

#	define hio_concat_oochars_to_ucstr hio_concat_uchars_to_ucstr
#	define hio_concat_oocstr hio_concat_ucstr

#	define hio_fill_oochars hio_fill_uchars
#	define hio_find_oocstr_word_in_oocstr hio_find_ucstr_word_in_ucstr
#	define hio_find_oochar hio_find_uchar
#	define hio_rfind_oochar hio_rfind_uchar
#	define hio_find_oochar_in_oocstr hio_find_uchar_in_ucstr

#	define hio_rotate_oochars hio_rotate_uchars
#	define hio_trim_oochars hio_trim_uchars
#	define hio_split_oocstr hio_split_ucstr
#else
#	define hio_count_oocstr hio_count_bcstr
#	define hio_equal_oochars hio_equal_bchars
#	define hio_comp_oochars hio_comp_bchars
#	define hio_comp_oocstr_bcstr hio_comp_bcstr
#	define hio_comp_oochars_bcstr hio_comp_bchars_bcstr
#	define hio_comp_oochars_ucstr hio_comp_bchars_ucstr
#	define hio_comp_oochars_oocstr hio_comp_bchars_bcstr
#	define hio_comp_oocstr hio_comp_bcstr

#	define hio_copy_oochars hio_copy_bchars
#	define hio_copy_bchars_to_oochars hio_copy_bchars
#	define hio_copy_oochars_to_bchars hio_copy_bchars
#	define hio_copy_uchars_to_oochars hio_copy_uchars_to_bchars
#	define hio_copy_oochars_to_uchars hio_copy_bchars_to_uchars

#	define hio_copy_oochars_to_oocstr hio_copy_bchars_to_bcstr
#	define hio_copy_oochars_to_oocstr_unlimited hio_copy_bchars_to_bcstr_unlimited
#	define hio_copy_oocstr hio_copy_bcstr
#	define hio_copy_oocstr_unlimited hio_copy_bcstr_unlimited

#	define hio_concat_oochars_to_bcstr hio_concat_bchars_to_bcstr
#	define hio_concat_oocstr hio_concat_bcstr

#	define hio_fill_oochars hio_fill_bchars
#	define hio_find_oocstr_word_in_oocstr hio_find_bcstr_word_in_bcstr
#	define hio_find_oochar hio_find_bchar
#	define hio_rfind_oochar hio_rfind_bchar
#	define hio_find_oochar_in_oocstr hio_find_bchar_in_bcstr

#	define hio_rotate_oochars hio_rotate_bchars
#	define hio_trim_oochars hio_trim_bchars
#	define hio_split_oocstr hio_split_bcstr
#endif
/* ------------------------------------------------------------------------- */

#define HIO_BYTE_TO_BCSTR_RADIXMASK (0xFF)
#define HIO_BYTE_TO_BCSTR_LOWERCASE (1 << 8)
 
HIO_EXPORT hio_oow_t hio_byte_to_bcstr (
	hio_uint8_t   byte,  
	hio_bch_t*    buf,
	hio_oow_t     size,
	int           flagged_radix,
	hio_bch_t     fill
);

/* ------------------------------------------------------------------------- */
#define HIO_CHARS_TO_INT_MAKE_OPTION(e,ltrim,rtrim,base) (((!!(e)) << 0) | ((!!(ltrim)) << 2) | ((!!(rtrim)) << 3) | ((base) << 8))
#define HIO_CHARS_TO_INT_GET_OPTION_E(option) ((option) & 1)
#define HIO_CHARS_TO_INT_GET_OPTION_LTRIM(option) ((option) & 4)
#define HIO_CHARS_TO_INT_GET_OPTION_RTRIM(option) ((option) & 8)
#define HIO_CHARS_TO_INT_GET_OPTION_BASE(option) ((option) >> 8)

#define HIO_CHARS_TO_UINT_MAKE_OPTION(e,ltrim,rtrim,base) (((!!(e)) << 0) | ((!!(ltrim)) << 2) | ((!!(rtrim)) << 3) | ((base) << 8))
#define HIO_CHARS_TO_UINT_GET_OPTION_E(option) ((option) & 1)
#define HIO_CHARS_TO_UINT_GET_OPTION_LTRIM(option) ((option) & 4)
#define HIO_CHARS_TO_UINT_GET_OPTION_RTRIM(option) ((option) & 8)
#define HIO_CHARS_TO_UINT_GET_OPTION_BASE(option) ((option) >> 8)

#define HIO_OOCHARS_TO_INTMAX_MAKE_OPTION(e,ltrim,rtrim,base)  HIO_CHARS_TO_INT_MAKE_OPTION(e,ltrim,rtrim,base)
#define HIO_OOCHARS_TO_INTMAX_GET_OPTION_E(option)             HIO_CHARS_TO_INT_GET_OPTION_E(option)
#define HIO_OOCHARS_TO_INTMAX_GET_OPTION_LTRIM(option)         HIO_CHARS_TO_INT_GET_OPTION_LTRIM(option)
#define HIO_OOCHARS_TO_INTMAX_GET_OPTION_RTRIM(option)         HIO_CHARS_TO_INT_GET_OPTION_RTRIM(option)
#define HIO_OOCHARS_TO_INTMAX_GET_OPTION_BASE(option)          HIO_CHARS_TO_INT_GET_OPTION_BASE(option)

#define HIO_OOCHARS_TO_UINTMAX_MAKE_OPTION(e,ltrim,rtrim,base) HIO_CHARS_TO_UINT_MAKE_OPTION(e,ltrim,rtrim,base)
#define HIO_OOCHARS_TO_UINTMAX_GET_OPTION_E(option)            HIO_CHARS_TO_UINT_GET_OPTION_E(option)
#define HIO_OOCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option)        HIO_CHARS_TO_UINT_GET_OPTION_LTRIM(option)
#define HIO_OOCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option)        HIO_CHARS_TO_UINT_GET_OPTION_RTRIM(option)
#define HIO_OOCHARS_TO_UINTMAX_GET_OPTION_BASE(option)         HIO_CHARS_TO_UINT_GET_OPTION_BASE(option)

#define HIO_UCHARS_TO_INTMAX_MAKE_OPTION(e,ltrim,rtrim,base)   HIO_CHARS_TO_INT_MAKE_OPTION(e,ltrim,rtrim,base)
#define HIO_UCHARS_TO_INTMAX_GET_OPTION_E(option)              HIO_CHARS_TO_INT_GET_OPTION_E(option)
#define HIO_UCHARS_TO_INTMAX_GET_OPTION_LTRIM(option)          HIO_CHARS_TO_INT_GET_OPTION_LTRIM(option)
#define HIO_UCHARS_TO_INTMAX_GET_OPTION_RTRIM(option)          HIO_CHARS_TO_INT_GET_OPTION_RTRIM(option)
#define HIO_UCHARS_TO_INTMAX_GET_OPTION_BASE(option)           HIO_CHARS_TO_INT_GET_OPTION_BASE(option)

#define HIO_BCHARS_TO_INTMAX_MAKE_OPTION(e,ltrim,rtrim,base)   HIO_CHARS_TO_INT_MAKE_OPTION(e,ltrim,rtrim,base)
#define HIO_BCHARS_TO_INTMAX_GET_OPTION_E(option)              HIO_CHARS_TO_INT_GET_OPTION_E(option)
#define HIO_BCHARS_TO_INTMAX_GET_OPTION_LTRIM(option)          HIO_CHARS_TO_INT_GET_OPTION_LTRIM(option)
#define HIO_BCHARS_TO_INTMAX_GET_OPTION_RTRIM(option)          HIO_CHARS_TO_INT_GET_OPTION_RTRIM(option)
#define HIO_BCHARS_TO_INTMAX_GET_OPTION_BASE(option)           HIO_CHARS_TO_INT_GET_OPTION_BASE(option)

#define HIO_UCHARS_TO_UINTMAX_MAKE_OPTION(e,ltrim,rtrim,base)  HIO_CHARS_TO_UINT_MAKE_OPTION(e,ltrim,rtrim,base)
#define HIO_UCHARS_TO_UINTMAX_GET_OPTION_E(option)             HIO_CHARS_TO_UINT_GET_OPTION_E(option)
#define HIO_UCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option)         HIO_CHARS_TO_UINT_GET_OPTION_LTRIM(option)
#define HIO_UCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option)         HIO_CHARS_TO_UINT_GET_OPTION_RTRIM(option)
#define HIO_UCHARS_TO_UINTMAX_GET_OPTION_BASE(option)          HIO_CHARS_TO_UINT_GET_OPTION_BASE(option)

#define HIO_BCHARS_TO_UINTMAX_MAKE_OPTION(e,ltrim,rtrim,base)  HIO_CHARS_TO_UINT_MAKE_OPTION(e,ltrim,rtrim,base)
#define HIO_BCHARS_TO_UINTMAX_GET_OPTION_E(option)             HIO_CHARS_TO_UINT_GET_OPTION_E(option)
#define HIO_BCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option)         HIO_CHARS_TO_UINT_GET_OPTION_LTRIM(option)
#define HIO_BCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option)         HIO_CHARS_TO_UINT_GET_OPTION_RTRIM(option)
#define HIO_BCHARS_TO_UINTMAX_GET_OPTION_BASE(option)          HIO_CHARS_TO_UINT_GET_OPTION_BASE(option)

HIO_EXPORT hio_intmax_t hio_uchars_to_intmax (
	const hio_uch_t*  str,
	hio_oow_t         len,
	int               option,
	const hio_uch_t** endptr,
	int*              is_sober
);

HIO_EXPORT hio_intmax_t hio_bchars_to_intmax (
	const hio_bch_t*  str,
	hio_oow_t         len,
	int               option,
	const hio_bch_t** endptr,
	int*              is_sober
);

HIO_EXPORT hio_uintmax_t hio_uchars_to_uintmax (
	const hio_uch_t*  str,
	hio_oow_t         len,
	int               option,
	const hio_uch_t** endptr,
	int*              is_sober
);

HIO_EXPORT hio_uintmax_t hio_bchars_to_uintmax (
	const hio_bch_t*  str,
	hio_oow_t         len,
	int               option,
	const hio_bch_t** endptr,
	int*              is_sober
);
#if defined(HIO_OOCH_IS_UCH)
#	define hio_oochars_to_intmax hio_uchars_to_intmax
#	define hio_oochars_to_uintmax hio_uchars_to_uintmax
#else
#	define hio_oochars_to_intmax hio_bchars_to_intmax
#	define hio_oochars_to_uintmax hio_bchars_to_uintmax
#endif

/* ------------------------------------------------------------------------- */

#if defined(HIO_OOCH_IS_UCH)
#	define hio_conv_oocstr_to_bcstr_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr) hio_conv_ucstr_to_bcstr_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr)
#	define hio_conv_oochars_to_bchars_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr) hio_conv_uchars_to_bchars_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr)
#else
#	define hio_conv_oocstr_to_ucstr_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr) hio_conv_bcstr_to_ucstr_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr,0)
#	define hio_conv_oochars_to_uchars_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr) hio_conv_bchars_to_uchars_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr,0)
#endif


HIO_EXPORT int hio_conv_bcstr_to_ucstr_with_cmgr (
	const hio_bch_t* bcs,
	hio_oow_t*       bcslen,
	hio_uch_t*       ucs,
	hio_oow_t*       ucslen,
	hio_cmgr_t*      cmgr,
	int              all
);

HIO_EXPORT int hio_conv_bchars_to_uchars_with_cmgr (
	const hio_bch_t* bcs,
	hio_oow_t*       bcslen,
	hio_uch_t*       ucs,
	hio_oow_t*       ucslen,
	hio_cmgr_t*      cmgr,
	int              all
);

HIO_EXPORT int hio_conv_ucstr_to_bcstr_with_cmgr (
	const hio_uch_t* ucs,
	hio_oow_t*       ucslen,
	hio_bch_t*       bcs,
	hio_oow_t*       bcslen,
	hio_cmgr_t*      cmgr
);

HIO_EXPORT int hio_conv_uchars_to_bchars_with_cmgr (
	const hio_uch_t* ucs,
	hio_oow_t*       ucslen,
	hio_bch_t*       bcs,
	hio_oow_t*       bcslen,
	hio_cmgr_t*      cmgr
);

/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_cmgr_t* hio_get_utf8_cmgr (
	void
);

/**
 * The hio_conv_uchars_to_utf8() function converts a unicode character string \a ucs 
 * to a UTF8 string and writes it into the buffer pointed to by \a bcs, but
 * not more than \a bcslen bytes including the terminating null.
 *
 * Upon return, \a bcslen is modified to the actual number of bytes written to
 * \a bcs excluding the terminating null; \a ucslen is modified to the number of
 * wide characters converted.
 *
 * You may pass #HIO_NULL for \a bcs to dry-run conversion or to get the
 * required buffer size for conversion. -2 is never returned in this case.
 *
 * \return
 * - 0 on full conversion,
 * - -1 on no or partial conversion for an illegal character encountered,
 * - -2 on no or partial conversion for a small buffer.
 *
 * \code
 *   const hio_uch_t ucs[] = { 'H', 'e', 'l', 'l', 'o' };
 *   hio_bch_t bcs[10];
 *   hio_oow_t ucslen = 5;
 *   hio_oow_t bcslen = HIO_COUNTOF(bcs);
 *   n = hio_conv_uchars_to_utf8 (ucs, &ucslen, bcs, &bcslen);
 *   if (n <= -1)
 *   {
 *      // conversion error
 *   }
 * \endcode
 */
HIO_EXPORT int hio_conv_uchars_to_utf8 (
	const hio_uch_t*    ucs,
	hio_oow_t*          ucslen,
	hio_bch_t*          bcs,
	hio_oow_t*          bcslen
);

/**
 * The hio_conv_utf8_to_uchars() function converts a UTF8 string to a uncide string.
 *
 * It never returns -2 if \a ucs is #HIO_NULL.
 *
 * \code
 *  const hio_bch_t* bcs = "test string";
 *  hio_uch_t ucs[100];
 *  hio_oow_t ucslen = HIO_COUNTOF(buf), n;
 *  hio_oow_t bcslen = 11;
 *  int n;
 *  n = hio_conv_utf8_to_uchars (bcs, &bcslen, ucs, &ucslen);
 *  if (n <= -1) { invalid/incomplenete sequence or buffer to small }
 * \endcode
 * 
 * The resulting \a ucslen can still be greater than 0 even if the return
 * value is negative. The value indiates the number of characters converted
 * before the error has occurred.
 *
 * \return 0 on success.
 *         -1 if \a bcs contains an illegal character.
 *         -2 if the wide-character string buffer is too small.
 *         -3 if \a bcs is not a complete sequence.
 */
HIO_EXPORT int hio_conv_utf8_to_uchars (
	const hio_bch_t*   bcs,
	hio_oow_t*         bcslen,
	hio_uch_t*         ucs,
	hio_oow_t*         ucslen
);


HIO_EXPORT int hio_conv_ucstr_to_utf8 (
	const hio_uch_t*    ucs,
	hio_oow_t*          ucslen,
	hio_bch_t*          bcs,
	hio_oow_t*          bcslen
);

HIO_EXPORT int hio_conv_utf8_to_ucstr (
	const hio_bch_t*   bcs,
	hio_oow_t*         bcslen,
	hio_uch_t*         ucs,
	hio_oow_t*         ucslen
);


HIO_EXPORT hio_oow_t hio_uc_to_utf8 (
	hio_uch_t    uc,
	hio_bch_t*   utf8,
	hio_oow_t    size
);

HIO_EXPORT hio_oow_t hio_utf8_to_uc (
	const hio_bch_t* utf8,
	hio_oow_t        size,
	hio_uch_t*       uc
);

/* =========================================================================
 * TIME CALCULATION WITH OVERFLOW/UNDERFLOW DETECTION
 * ========================================================================= */

/** 
 * The hio_add_ntime() function adds two time structures pointed to by \a x and \a y
 * and stores the result in the structure pointed to by \a z. If it detects overflow/
 * underflow, it stores the largest/least possible value respectively.
 * You may use the HIO_ADD_NTIME() macro if overflow/underflow check isn't needed.
 */
HIO_EXPORT void hio_add_ntime (
	hio_ntime_t*       z, 
	const hio_ntime_t* x,
	const hio_ntime_t* y
);

/** 
 * The hio_sub_ntime() function subtracts the time value \a y from the time value \a x
 * and stores the result in the structure pointed to by \a z. If it detects overflow/
 * underflow, it stores the largest/least possible value respectively.
 * You may use the HIO_SUB_NTIME() macro if overflow/underflow check isn't needed.
 */
HIO_EXPORT void hio_sub_ntime (
	hio_ntime_t*       z,
	const hio_ntime_t* x,
	const hio_ntime_t* y
);


/* =========================================================================
 * BIT SWAP
 * ========================================================================= */
#if defined(HIO_HAVE_INLINE)

#if defined(HIO_HAVE_UINT16_T)
static HIO_INLINE hio_uint16_t hio_bswap16 (hio_uint16_t x)
{
#if defined(HIO_HAVE_BUILTIN_BSWAP16)
	return __builtin_bswap16(x);
#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64) || defined(__i386) || defined(i386))
	__asm__ volatile ("xchgb %b0, %h0" : "=Q"(x): "0"(x));
	return x;
#else
	return (x << 8) | (x >> 8);
#endif
}
#endif

#if defined(HIO_HAVE_UINT32_T)
static HIO_INLINE hio_uint32_t hio_bswap32 (hio_uint32_t x)
{
#if defined(HIO_HAVE_BUILTIN_BSWAP32)
	return __builtin_bswap32(x);
#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64) || defined(__i386) || defined(i386))
	__asm__ volatile ("bswapl %0" : "=r"(x) : "0"(x));
	return x;
#else
	return ((x >> 24)) | 
	       ((x >>  8) & ((hio_uint32_t)0xff << 8)) | 
	       ((x <<  8) & ((hio_uint32_t)0xff << 16)) | 
	       ((x << 24));
#endif
}
#endif

#if defined(HIO_HAVE_UINT64_T)
static HIO_INLINE hio_uint64_t hio_bswap64 (hio_uint64_t x)
{
#if defined(HIO_HAVE_BUILTIN_BSWAP64)
	return __builtin_bswap64(x);
#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64))
	__asm__ volatile ("bswapq %0" : "=r"(x) : "0"(x));
	return x;
#else
	return ((x >> 56)) | 
	       ((x >> 40) & ((hio_uint64_t)0xff << 8)) | 
	       ((x >> 24) & ((hio_uint64_t)0xff << 16)) | 
	       ((x >>  8) & ((hio_uint64_t)0xff << 24)) | 
	       ((x <<  8) & ((hio_uint64_t)0xff << 32)) | 
	       ((x << 24) & ((hio_uint64_t)0xff << 40)) | 
	       ((x << 40) & ((hio_uint64_t)0xff << 48)) | 
	       ((x << 56));
#endif
}
#endif

#if defined(HIO_HAVE_UINT128_T)
static HIO_INLINE hio_uint128_t hio_bswap128 (hio_uint128_t x)
{
	return ((x >> 120)) | 
	       ((x >> 104) & ((hio_uint128_t)0xff << 8)) |
	       ((x >>  88) & ((hio_uint128_t)0xff << 16)) |
	       ((x >>  72) & ((hio_uint128_t)0xff << 24)) |
	       ((x >>  56) & ((hio_uint128_t)0xff << 32)) |
	       ((x >>  40) & ((hio_uint128_t)0xff << 40)) |
	       ((x >>  24) & ((hio_uint128_t)0xff << 48)) |
	       ((x >>   8) & ((hio_uint128_t)0xff << 56)) |
	       ((x <<   8) & ((hio_uint128_t)0xff << 64)) |
	       ((x <<  24) & ((hio_uint128_t)0xff << 72)) |
	       ((x <<  40) & ((hio_uint128_t)0xff << 80)) |
	       ((x <<  56) & ((hio_uint128_t)0xff << 88)) |
	       ((x <<  72) & ((hio_uint128_t)0xff << 96)) |
	       ((x <<  88) & ((hio_uint128_t)0xff << 104)) |
	       ((x << 104) & ((hio_uint128_t)0xff << 112)) |
	       ((x << 120));
}
#endif

#else

#if defined(HIO_HAVE_UINT16_T)
#	define hio_bswap16(x) ((hio_uint16_t)(((hio_uint16_t)(x)) << 8) | (((hio_uint16_t)(x)) >> 8))
#endif

#if defined(HIO_HAVE_UINT32_T)
#	define hio_bswap32(x) ((hio_uint32_t)(((((hio_uint32_t)(x)) >> 24)) | \
	                                      ((((hio_uint32_t)(x)) >>  8) & ((hio_uint32_t)0xff << 8)) | \
	                                      ((((hio_uint32_t)(x)) <<  8) & ((hio_uint32_t)0xff << 16)) | \
	                                      ((((hio_uint32_t)(x)) << 24))))
#endif

#if defined(HIO_HAVE_UINT64_T)
#	define hio_bswap64(x) ((hio_uint64_t)(((((hio_uint64_t)(x)) >> 56)) | \
	                                      ((((hio_uint64_t)(x)) >> 40) & ((hio_uint64_t)0xff << 8)) | \
	                                      ((((hio_uint64_t)(x)) >> 24) & ((hio_uint64_t)0xff << 16)) | \
	                                      ((((hio_uint64_t)(x)) >>  8) & ((hio_uint64_t)0xff << 24)) | \
	                                      ((((hio_uint64_t)(x)) <<  8) & ((hio_uint64_t)0xff << 32)) | \
	                                      ((((hio_uint64_t)(x)) << 24) & ((hio_uint64_t)0xff << 40)) | \
	                                      ((((hio_uint64_t)(x)) << 40) & ((hio_uint64_t)0xff << 48)) | \
	                                      ((((hio_uint64_t)(x)) << 56))))
#endif

#if defined(HIO_HAVE_UINT128_T)
#	define hio_bswap128(x) ((hio_uint128_t)(((((hio_uint128_t)(x)) >> 120)) |  \
	                                        ((((hio_uint128_t)(x)) >> 104) & ((hio_uint128_t)0xff << 8)) | \
	                                        ((((hio_uint128_t)(x)) >>  88) & ((hio_uint128_t)0xff << 16)) | \
	                                        ((((hio_uint128_t)(x)) >>  72) & ((hio_uint128_t)0xff << 24)) | \
	                                        ((((hio_uint128_t)(x)) >>  56) & ((hio_uint128_t)0xff << 32)) | \
	                                        ((((hio_uint128_t)(x)) >>  40) & ((hio_uint128_t)0xff << 40)) | \
	                                        ((((hio_uint128_t)(x)) >>  24) & ((hio_uint128_t)0xff << 48)) | \
	                                        ((((hio_uint128_t)(x)) >>   8) & ((hio_uint128_t)0xff << 56)) | \
	                                        ((((hio_uint128_t)(x)) <<   8) & ((hio_uint128_t)0xff << 64)) | \
	                                        ((((hio_uint128_t)(x)) <<  24) & ((hio_uint128_t)0xff << 72)) | \
	                                        ((((hio_uint128_t)(x)) <<  40) & ((hio_uint128_t)0xff << 80)) | \
	                                        ((((hio_uint128_t)(x)) <<  56) & ((hio_uint128_t)0xff << 88)) | \
	                                        ((((hio_uint128_t)(x)) <<  72) & ((hio_uint128_t)0xff << 96)) | \
	                                        ((((hio_uint128_t)(x)) <<  88) & ((hio_uint128_t)0xff << 104)) | \
	                                        ((((hio_uint128_t)(x)) << 104) & ((hio_uint128_t)0xff << 112)) | \
	                                        ((((hio_uint128_t)(x)) << 120))))
#endif

#endif /* HIO_HAVE_INLINE */


#if defined(HIO_ENDIAN_LITTLE)

#	if defined(HIO_HAVE_UINT16_T)
#	define hio_hton16(x) hio_bswap16(x)
#	define hio_ntoh16(x) hio_bswap16(x)
#	define hio_htobe16(x) hio_bswap16(x)
#	define hio_be16toh(x) hio_bswap16(x)
#	define hio_htole16(x) ((hio_uint16_t)(x))
#	define hio_le16toh(x) ((hio_uint16_t)(x))
#	endif

#	if defined(HIO_HAVE_UINT32_T)
#	define hio_hton32(x) hio_bswap32(x)
#	define hio_ntoh32(x) hio_bswap32(x)
#	define hio_htobe32(x) hio_bswap32(x)
#	define hio_be32toh(x) hio_bswap32(x)
#	define hio_htole32(x) ((hio_uint32_t)(x))
#	define hio_le32toh(x) ((hio_uint32_t)(x))
#	endif

#	if defined(HIO_HAVE_UINT64_T)
#	define hio_hton64(x) hio_bswap64(x)
#	define hio_ntoh64(x) hio_bswap64(x)
#	define hio_htobe64(x) hio_bswap64(x)
#	define hio_be64toh(x) hio_bswap64(x)
#	define hio_htole64(x) ((hio_uint64_t)(x))
#	define hio_le64toh(x) ((hio_uint64_t)(x))
#	endif

#	if defined(HIO_HAVE_UINT128_T)

#	define hio_hton128(x) hio_bswap128(x)
#	define hio_ntoh128(x) hio_bswap128(x)
#	define hio_htobe128(x) hio_bswap128(x)
#	define hio_be128toh(x) hio_bswap128(x)
#	define hio_htole128(x) ((hio_uint128_t)(x))
#	define hio_le128toh(x) ((hio_uint128_t)(x))
#	endif

#elif defined(HIO_ENDIAN_BIG)

#	if defined(HIO_HAVE_UINT16_T)
#	define hio_hton16(x) ((hio_uint16_t)(x))
#	define hio_ntoh16(x) ((hio_uint16_t)(x))
#	define hio_htobe16(x) ((hio_uint16_t)(x))
#	define hio_be16toh(x) ((hio_uint16_t)(x))
#	define hio_htole16(x) hio_bswap16(x)
#	define hio_le16toh(x) hio_bswap16(x)
#	endif

#	if defined(HIO_HAVE_UINT32_T)
#	define hio_hton32(x) ((hio_uint32_t)(x))
#	define hio_ntoh32(x) ((hio_uint32_t)(x))
#	define hio_htobe32(x) ((hio_uint32_t)(x))
#	define hio_be32toh(x) ((hio_uint32_t)(x))
#	define hio_htole32(x) hio_bswap32(x)
#	define hio_le32toh(x) hio_bswap32(x)
#	endif

#	if defined(HIO_HAVE_UINT64_T)
#	define hio_hton64(x) ((hio_uint64_t)(x))
#	define hio_ntoh64(x) ((hio_uint64_t)(x))
#	define hio_htobe64(x) ((hio_uint64_t)(x))
#	define hio_be64toh(x) ((hio_uint64_t)(x))
#	define hio_htole64(x) hio_bswap64(x)
#	define hio_le64toh(x) hio_bswap64(x)
#	endif

#	if defined(HIO_HAVE_UINT128_T)
#	define hio_hton128(x) ((hio_uint128_t)(x))
#	define hio_ntoh128(x) ((hio_uint128_t)(x))
#	define hio_htobe128(x) ((hio_uint128_t)(x))
#	define hio_be128toh(x) ((hio_uint128_t)(x))
#	define hio_htole128(x) hio_bswap128(x)
#	define hio_le128toh(x) hio_bswap128(x)
#	endif

#else
#	error UNKNOWN ENDIAN
#endif


/* =========================================================================
 * SIP-HASH-PRF
 * ========================================================================= */
HIO_EXPORT void hio_sip_hash_24 (
	const hio_uint8_t   key[16],
	const void*         dptr,
	hio_oow_t           dlen,
	hio_uint8_t         out[8]
);

#if defined(__cplusplus)
}
#endif

/* Some C++ utilities below */
#if defined(__cplusplus)

/*
static inline bool is_space (char c) { return isspace(c); }
static inline bool is_wspace (wchar_t c) { return iswspace(c); }
unsigned x = hio_chars_to_uint<char,unsigned,is_space>("0x12345", 7, 0, NULL, NULL);
unsigned y = hio_chars_to_uint<wchar_t,unsigned,is_wspace>(L"0x12345", 7, 0, NULL, NULL);
int a = hio_chars_to_int<char,int,is_space>("-0x12345", 8, 0, NULL, NULL);
int b = hio_chars_to_int<wchar_t,int,is_wspace>(L"-0x12345", 8, 0, NULL, NULL);
*/
template<typename CHAR_TYPE, typename INT_TYPE, bool(*IS_SPACE)(CHAR_TYPE)>INT_TYPE hio_chars_to_int (const CHAR_TYPE* str, hio_oow_t len, int option, const CHAR_TYPE** endptr, int* is_sober)
{
	INT_TYPE n = 0;
	const CHAR_TYPE* p, * pp;
	const CHAR_TYPE* end;
	hio_oow_t rem;
	int digit, negative = 0;
	int base = HIO_CHARS_TO_INT_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if (HIO_CHARS_TO_INT_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && IS_SPACE(*p)) p++;
	}

	/* check for a sign */
	while (p < end)
	{
		if (*p == '-') 
		{
			negative = ~negative;
			p++;
		}
		else if (*p == '+') p++;
		else break;
	}

	/* check for a binary/octal/hexadecimal notation */
	rem = end - p;
	if (base == 0) 
	{
		if (rem >= 1 && *p == '0') 
		{
			p++;

			if (rem == 1) base = 8;
			else if (*p == 'x' || *p == 'X')
			{
				p++; base = 16;
			} 
			else if (*p == 'b' || *p == 'B')
			{
				p++; base = 2;
			}
			else base = 8;
		}
		else base = 10;
	} 
	else if (rem >= 2 && base == 16)
	{
		if (*p == '0' && 
		    (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && 
		    (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
	}

	/* process the digits */
	pp = p;
	while (p < end)
	{
		digit = HIO_ZDIGIT_TO_NUM(*p, base);
		if (digit >= base) break;
		n = n * base + digit;
		p++;
	}

	if (HIO_CHARS_TO_INT_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			INT_TYPE e = 0, i;
			int e_neg = 0;
			p++;
			if (*p == '+')
			{
				p++;
			}
			else if (*p == '-')
			{
				p++; e_neg = 1;
			}
			while (p < end)
			{
				digit = HIO_ZDIGIT_TO_NUM(*p, base);
				if (digit >= base) break;
				e = e * base + digit;
				p++;
			}
			if (e_neg)
				for (i = 0; i < e; i++) n /= 10;
			else
				for (i = 0; i < e; i++) n *= 10;
		}
	}

	/* base 8: at least a zero digit has been seen.
	 * other case: p > pp to be able to have at least 1 meaningful digit. */
	if (is_sober) *is_sober = (base == 8 || p > pp); 

	if (HIO_CHARS_TO_INT_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && IS_SPACE(*p)) p++;
	}

	if (endptr) *endptr = p;
	return (negative)? -n: n;
}

template<typename CHAR_TYPE, typename UINT_TYPE, bool(*IS_SPACE)(CHAR_TYPE)>UINT_TYPE hio_chars_to_uint (const CHAR_TYPE* str, hio_oow_t len, int option, const CHAR_TYPE** endptr, int* is_sober)
{
	UINT_TYPE n = 0;
	const CHAR_TYPE* p, * pp;
	const CHAR_TYPE* end;
	hio_oow_t rem;
	int digit;
	int base = HIO_CHARS_TO_UINT_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if (HIO_CHARS_TO_UINT_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && IS_SPACE(*p)) p++;
	}

	/* check for a sign */
	while (p < end)
	{
		if (*p == '+') p++;
		else break;
	}

	/* check for a binary/octal/hexadecimal notation */
	rem = end - p;
	if (base == 0) 
	{
		if (rem >= 1 && *p == '0') 
		{
			p++;

			if (rem == 1) base = 8;
			else if (*p == 'x' || *p == 'X')
			{
				p++; base = 16;
			} 
			else if (*p == 'b' || *p == 'B')
			{
				p++; base = 2;
			}
			else base = 8;
		}
		else base = 10;
	} 
	else if (rem >= 2 && base == 16)
	{
		if (*p == '0' && 
		    (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && 
		    (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
	}

	/* process the digits */
	pp = p;
	while (p < end)
	{
		digit = HIO_ZDIGIT_TO_NUM(*p, base);
		if (digit >= base) break;
		n = n * base + digit;
		p++;
	}

	if (HIO_CHARS_TO_UINT_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			UINT_TYPE e = 0, i;
			int e_neg = 0;
			p++;
			if (*p == '+')
			{
				p++;
			}
			else if (*p == '-')
			{
				p++; e_neg = 1;
			}
			while (p < end)
			{
				digit = HIO_ZDIGIT_TO_NUM(*p, base);
				if (digit >= base) break;
				e = e * base + digit;
				p++;
			}
			if (e_neg)
				for (i = 0; i < e; i++) n /= 10;
			else
				for (i = 0; i < e; i++) n *= 10;
		}
	}

	/* base 8: at least a zero digit has been seen.
	 * other case: p > pp to be able to have at least 1 meaningful digit. */
	if (is_sober) *is_sober = (base == 8 || p > pp); 

	if (HIO_CHARS_TO_UINT_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && IS_SPACE(*p)) p++;
	}

	if (endptr) *endptr = p;
	return n;
}


#endif

#endif
