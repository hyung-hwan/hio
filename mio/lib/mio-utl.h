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

#ifndef _MIO_UTL_H_
#define _MIO_UTL_H_

#include <mio-cmn.h>
#include <stdarg.h>

/* =========================================================================
 * ENDIAN CHANGE OF A CONSTANT
 * ========================================================================= */
#define MIO_CONST_BSWAP16(x) \
	((mio_uint16_t)((((mio_uint16_t)(x) & ((mio_uint16_t)0xff << 0)) << 8) | \
	                (((mio_uint16_t)(x) & ((mio_uint16_t)0xff << 8)) >> 8)))

#define MIO_CONST_BSWAP32(x) \
	((mio_uint32_t)((((mio_uint32_t)(x) & ((mio_uint32_t)0xff <<  0)) << 24) | \
	                (((mio_uint32_t)(x) & ((mio_uint32_t)0xff <<  8)) <<  8) | \
	                (((mio_uint32_t)(x) & ((mio_uint32_t)0xff << 16)) >>  8) | \
	                (((mio_uint32_t)(x) & ((mio_uint32_t)0xff << 24)) >> 24)))

#if defined(MIO_HAVE_UINT64_T)
#define MIO_CONST_BSWAP64(x) \
	((mio_uint64_t)((((mio_uint64_t)(x) & ((mio_uint64_t)0xff <<  0)) << 56) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff <<  8)) << 40) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff << 16)) << 24) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff << 24)) <<  8) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff << 32)) >>  8) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff << 40)) >> 24) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff << 48)) >> 40) | \
	                (((mio_uint64_t)(x) & ((mio_uint64_t)0xff << 56)) >> 56)))
#endif

#if defined(MIO_HAVE_UINT128_T)
#define MIO_CONST_BSWAP128(x) \
	((mio_uint128_t)((((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 0)) << 120) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 8)) << 104) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 16)) << 88) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 24)) << 72) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 32)) << 56) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 40)) << 40) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 48)) << 24) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 56)) << 8) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 64)) >> 8) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 72)) >> 24) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 80)) >> 40) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 88)) >> 56) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 96)) >> 72) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 104)) >> 88) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 112)) >> 104) | \
	                 (((mio_uint128_t)(x) & ((mio_uint128_t)0xff << 120)) >> 120)))
#endif

#if defined(MIO_ENDIAN_LITTLE)

#	if defined(MIO_HAVE_UINT16_T)
#	define MIO_CONST_NTOH16(x) MIO_CONST_BSWAP16(x)
#	define MIO_CONST_HTON16(x) MIO_CONST_BSWAP16(x)
#	define MIO_CONST_HTOBE16(x) MIO_CONST_BSWAP16(x)
#	define MIO_CONST_HTOLE16(x) (x)
#	define MIO_CONST_BE16TOH(x) MIO_CONST_BSWAP16(x)
#	define MIO_CONST_LE16TOH(x) (x)
#	endif

#	if defined(MIO_HAVE_UINT32_T)
#	define MIO_CONST_NTOH32(x) MIO_CONST_BSWAP32(x)
#	define MIO_CONST_HTON32(x) MIO_CONST_BSWAP32(x)
#	define MIO_CONST_HTOBE32(x) MIO_CONST_BSWAP32(x)
#	define MIO_CONST_HTOLE32(x) (x)
#	define MIO_CONST_BE32TOH(x) MIO_CONST_BSWAP32(x)
#	define MIO_CONST_LE32TOH(x) (x)
#	endif

#	if defined(MIO_HAVE_UINT64_T)
#	define MIO_CONST_NTOH64(x) MIO_CONST_BSWAP64(x)
#	define MIO_CONST_HTON64(x) MIO_CONST_BSWAP64(x)
#	define MIO_CONST_HTOBE64(x) MIO_CONST_BSWAP64(x)
#	define MIO_CONST_HTOLE64(x) (x)
#	define MIO_CONST_BE64TOH(x) MIO_CONST_BSWAP64(x)
#	define MIO_CONST_LE64TOH(x) (x)
#	endif

#	if defined(MIO_HAVE_UINT128_T)
#	define MIO_CONST_NTOH128(x) MIO_CONST_BSWAP128(x)
#	define MIO_CONST_HTON128(x) MIO_CONST_BSWAP128(x)
#	define MIO_CONST_HTOBE128(x) MIO_CONST_BSWAP128(x)
#	define MIO_CONST_HTOLE128(x) (x)
#	define MIO_CONST_BE128TOH(x) MIO_CONST_BSWAP128(x)
#	define MIO_CONST_LE128TOH(x) (x)
#endif

#elif defined(MIO_ENDIAN_BIG)

#	if defined(MIO_HAVE_UINT16_T)
#	define MIO_CONST_NTOH16(x) (x)
#	define MIO_CONST_HTON16(x) (x)
#	define MIO_CONST_HTOBE16(x) (x)
#	define MIO_CONST_HTOLE16(x) MIO_CONST_BSWAP16(x)
#	define MIO_CONST_BE16TOH(x) (x)
#	define MIO_CONST_LE16TOH(x) MIO_CONST_BSWAP16(x)
#	endif

#	if defined(MIO_HAVE_UINT32_T)
#	define MIO_CONST_NTOH32(x) (x)
#	define MIO_CONST_HTON32(x) (x)
#	define MIO_CONST_HTOBE32(x) (x)
#	define MIO_CONST_HTOLE32(x) MIO_CONST_BSWAP32(x)
#	define MIO_CONST_BE32TOH(x) (x)
#	define MIO_CONST_LE32TOH(x) MIO_CONST_BSWAP32(x)
#	endif

#	if defined(MIO_HAVE_UINT64_T)
#	define MIO_CONST_NTOH64(x) (x)
#	define MIO_CONST_HTON64(x) (x)
#	define MIO_CONST_HTOBE64(x) (x)
#	define MIO_CONST_HTOLE64(x) MIO_CONST_BSWAP64(x)
#	define MIO_CONST_BE64TOH(x) (x)
#	define MIO_CONST_LE64TOH(x) MIO_CONST_BSWAP64(x)
#	endif

#	if defined(MIO_HAVE_UINT128_T)
#	define MIO_CONST_NTOH128(x) (x)
#	define MIO_CONST_HTON128(x) (x)
#	define MIO_CONST_HTOBE128(x) (x)
#	define MIO_CONST_HTOLE128(x) MIO_CONST_BSWAP128(x)
#	define MIO_CONST_BE128TOH(x) (x)
#	define MIO_CONST_LE128TOH(x) MIO_CONST_BSWAP128(x)
#	endif

#else
#	error UNKNOWN ENDIAN
#endif


/* =========================================================================
 * HASH
 * ========================================================================= */
#if (MIO_SIZEOF_OOW_T == 4)
#	define MIO_HASH_FNV_MAGIC_INIT (0x811c9dc5)
#	define MIO_HASH_FNV_MAGIC_PRIME (0x01000193)
#elif (MIO_SIZEOF_OOW_T == 8)
#	define MIO_HASH_FNV_MAGIC_INIT (0xCBF29CE484222325)
#	define MIO_HASH_FNV_MAGIC_PRIME (0x100000001B3l)
#elif (MIO_SIZEOF_OOW_T == 16)
#	define MIO_HASH_FNV_MAGIC_INIT (0x6C62272E07BB014262B821756295C58D)
#	define MIO_HASH_FNV_MAGIC_PRIME (0x1000000000000000000013B)
#endif

#if defined(MIO_HASH_FNV_MAGIC_INIT)
	/* FNV-1 hash */
#	define MIO_HASH_INIT MIO_HASH_FNV_MAGIC_INIT
#	define MIO_HASH_VALUE(hv,v) (((hv) ^ (v)) * MIO_HASH_FNV_MAGIC_PRIME)

#else
	/* SDBM hash */
#	define MIO_HASH_INIT 0
#	define MIO_HASH_VALUE(hv,v) (((hv) << 6) + ((hv) << 16) - (hv) + (v))
#endif

#define MIO_HASH_VPTL(hv, ptr, len, type) do { \
	hv = MIO_HASH_INIT; \
	MIO_HASH_MORE_VPTL (hv, ptr, len, type); \
} while(0)

#define MIO_HASH_MORE_VPTL(hv, ptr, len, type) do { \
	type* __mio_hash_more_vptl_p = (type*)(ptr); \
	type* __mio_hash_more_vptl_q = (type*)__mio_hash_more_vptl_p + (len); \
	while (__mio_hash_more_vptl_p < __mio_hash_more_vptl_q) \
	{ \
		hv = MIO_HASH_VALUE(hv, *__mio_hash_more_vptl_p); \
		__mio_hash_more_vptl_p++; \
	} \
} while(0)

#define MIO_HASH_VPTR(hv, ptr, type) do { \
	hv = MIO_HASH_INIT; \
	MIO_HASH_MORE_VPTR (hv, ptr, type); \
} while(0)

#define MIO_HASH_MORE_VPTR(hv, ptr, type) do { \
	type* __mio_hash_more_vptr_p = (type*)(ptr); \
	while (*__mio_hash_more_vptr_p) \
	{ \
		hv = MIO_HASH_VALUE(hv, *__mio_hash_more_vptr_p); \
		__mio_hash_more_vptr_p++; \
	} \
} while(0)

#define MIO_HASH_BYTES(hv, ptr, len) MIO_HASH_VPTL(hv, ptr, len, const mio_uint8_t)
#define MIO_HASH_MORE_BYTES(hv, ptr, len) MIO_HASH_MORE_VPTL(hv, ptr, len, const mio_uint8_t)

#define MIO_HASH_BCHARS(hv, ptr, len) MIO_HASH_VPTL(hv, ptr, len, const mio_bch_t)
#define MIO_HASH_MORE_BCHARS(hv, ptr, len) MIO_HASH_MORE_VPTL(hv, ptr, len, const mio_bch_t)

#define MIO_HASH_UCHARS(hv, ptr, len) MIO_HASH_VPTL(hv, ptr, len, const mio_uch_t)
#define MIO_HASH_MORE_UCHARS(hv, ptr, len) MIO_HASH_MORE_VPTL(hv, ptr, len, const mio_uch_t)

#define MIO_HASH_BCSTR(hv, ptr) MIO_HASH_VPTR(hv, ptr, const mio_bch_t)
#define MIO_HASH_MORE_BCSTR(hv, ptr) MIO_HASH_MORE_VPTR(hv, ptr, const mio_bch_t)

#define MIO_HASH_UCSTR(hv, ptr) MIO_HASH_VPTR(hv, ptr, const mio_uch_t)
#define MIO_HASH_MORE_UCSTR(hv, ptr) MIO_HASH_MORE_VPTR(hv, ptr, const mio_uch_t)


#ifdef __cplusplus
extern "C" {
#endif

/**
 * The mio_equal_uchars() function determines equality of two strings
 * of the same length \a len.
 */
MIO_EXPORT int mio_equal_uchars (
	const mio_uch_t* str1,
	const mio_uch_t* str2,
	mio_oow_t        len
);

MIO_EXPORT int mio_equal_bchars (
	const mio_bch_t* str1,
	const mio_bch_t* str2,
	mio_oow_t        len
);

MIO_EXPORT int mio_comp_uchars (
	const mio_uch_t* str1,
	mio_oow_t        len1,
	const mio_uch_t* str2,
	mio_oow_t        len2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_bchars (
	const mio_bch_t* str1,
	mio_oow_t        len1,
	const mio_bch_t* str2,
	mio_oow_t        len2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_ucstr (
	const mio_uch_t* str1,
	const mio_uch_t* str2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_bcstr (
	const mio_bch_t* str1,
	const mio_bch_t* str2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_ucstr_limited (
	const mio_uch_t* str1,
	const mio_uch_t* str2,
	mio_oow_t        maxlen,
	int              ignorecase
);

MIO_EXPORT int mio_comp_bcstr_limited (
	const mio_bch_t* str1,
	const mio_bch_t* str2,
	mio_oow_t        maxlen,
	int              ignorecase
);

MIO_EXPORT int mio_comp_ucstr_bcstr (
	const mio_uch_t* str1,
	const mio_bch_t* str2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_uchars_ucstr (
	const mio_uch_t* str1,
	mio_oow_t        len,
	const mio_uch_t* str2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_uchars_bcstr (
	const mio_uch_t* str1,
	mio_oow_t        len,
	const mio_bch_t* str2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_bchars_bcstr (
	const mio_bch_t* str1,
	mio_oow_t        len,
	const mio_bch_t* str2,
	int              ignorecase
);

MIO_EXPORT int mio_comp_bchars_ucstr (
	const mio_bch_t* str1,
	mio_oow_t        len,
	const mio_uch_t* str2,
	int              ignorecase
);

MIO_EXPORT void mio_copy_uchars (
	mio_uch_t*       dst,
	const mio_uch_t* src,
	mio_oow_t        len
);

MIO_EXPORT void mio_copy_bchars (
	mio_bch_t*       dst,
	const mio_bch_t* src,
	mio_oow_t        len
);

MIO_EXPORT void mio_copy_bchars_to_uchars (
	mio_uch_t*       dst,
	const mio_bch_t* src,
	mio_oow_t        len
);
MIO_EXPORT void mio_copy_uchars_to_bchars (
	mio_bch_t*       dst,
	const mio_uch_t* src,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_copy_uchars_to_ucstr_unlimited (
	mio_uch_t*       dst,
	const mio_uch_t* src,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_copy_bchars_to_bcstr_unlimited (
	mio_bch_t*       dst,
	const mio_bch_t* src,
	mio_oow_t        len
);

MIO_EXPORT mio_oow_t mio_copy_ucstr (
	mio_uch_t*       dst,
	mio_oow_t        len,
	const mio_uch_t* src
);

MIO_EXPORT mio_oow_t mio_copy_bcstr (
	mio_bch_t*       dst,
	mio_oow_t        len,
	const mio_bch_t* src
);

MIO_EXPORT mio_oow_t mio_copy_uchars_to_ucstr (
	mio_uch_t*       dst,
	mio_oow_t        dlen,
	const mio_uch_t* src,
	mio_oow_t        slen
);

MIO_EXPORT mio_oow_t mio_copy_bchars_to_bcstr (
	mio_bch_t*       dst,
	mio_oow_t        dlen,
	const mio_bch_t* src,
	mio_oow_t        slen
);

MIO_EXPORT mio_oow_t mio_copy_ucstr_unlimited (
	mio_uch_t*       dst,
	const mio_uch_t* src
);

MIO_EXPORT mio_oow_t mio_copy_bcstr_unlimited (
	mio_bch_t*       dst,
	const mio_bch_t* src
);

MIO_EXPORT void mio_fill_uchars (
	mio_uch_t*       dst,
	const mio_uch_t  ch,
	mio_oow_t        len
);

MIO_EXPORT void mio_fill_bchars (
	mio_bch_t*       dst,
	const mio_bch_t  ch,
	mio_oow_t        len
);

MIO_EXPORT const mio_bch_t* mio_find_bcstr_word_in_bcstr (
	const mio_bch_t* str,
	const mio_bch_t* word,
	mio_bch_t        extra_delim,
	int              ignorecase
);

MIO_EXPORT const mio_uch_t* mio_find_ucstr_word_in_ucstr (
	const mio_uch_t* str,
	const mio_uch_t* word,
	mio_uch_t        extra_delim,
	int              ignorecase
);

MIO_EXPORT mio_uch_t* mio_find_uchar (
	const mio_uch_t* ptr,
	mio_oow_t        len,
	mio_uch_t        c
);

MIO_EXPORT mio_bch_t* mio_find_bchar (
	const mio_bch_t* ptr,
	mio_oow_t        len,
	mio_bch_t        c
);

MIO_EXPORT mio_uch_t* mio_rfind_uchar (
	const mio_uch_t* ptr,
	mio_oow_t        len,
	mio_uch_t        c
);

MIO_EXPORT mio_bch_t* mio_rfind_bchar (
	const mio_bch_t* ptr,
	mio_oow_t        len,
	mio_bch_t        c
);

MIO_EXPORT mio_uch_t* mio_find_uchar_in_ucstr (
	const mio_uch_t* ptr,
	mio_uch_t        c
);

MIO_EXPORT mio_bch_t* mio_find_bchar_in_bcstr (
	const mio_bch_t* ptr,
	mio_bch_t        c
);

MIO_EXPORT int mio_split_ucstr (
	mio_uch_t*       s,
	const mio_uch_t* delim,
	mio_uch_t        lquote,
	mio_uch_t        rquote,
	mio_uch_t        escape
);

MIO_EXPORT int mio_split_bcstr (
	mio_bch_t*       s,
	const mio_bch_t* delim,
	mio_bch_t        lquote,
	mio_bch_t        rquote,
	mio_bch_t        escape
);

MIO_EXPORT mio_oow_t mio_count_ucstr (
	const mio_uch_t* str
);

MIO_EXPORT mio_oow_t mio_count_bcstr (
	const mio_bch_t* str
);

#if defined(MIO_OOCH_IS_UCH)
#	define mio_equal_oochars mio_equal_uchars
#	define mio_comp_oochars mio_comp_uchars
#	define mio_comp_oocstr_bcstr mio_comp_ucstr_bcstr
#	define mio_comp_oochars_bcstr mio_comp_uchars_bcstr
#	define mio_comp_oochars_ucstr mio_comp_uchars_ucstr
#	define mio_comp_oochars_oocstr mio_comp_uchars_ucstr
#	define mio_comp_oocstr mio_comp_ucstr

#	define mio_copy_oochars mio_copy_uchars
#	define mio_copy_bchars_to_oochars mio_copy_bchars_to_uchars
#	define mio_copy_oochars_to_bchars mio_copy_uchars_to_bchars
#	define mio_copy_uchars_to_oochars mio_copy_uchars
#	define mio_copy_oochars_to_uchars mio_copy_uchars

#	define mio_copy_oochars_to_oocstr mio_copy_uchars_to_ucstr
#	define mio_copy_oochars_to_oocstr_unlimited mio_copy_uchars_to_ucstr_unlimited
#	define mio_copy_oocstr mio_copy_ucstr
#	define mio_copy_oocstr_unlimited mio_copy_ucstr_unlimited

#	define mio_fill_oochars mio_fill_uchars
#	define mio_find_oocstr_word_in_oocstr mio_find_ucstr_word_in_ucstr
#	define mio_find_oochar mio_find_uchar
#	define mio_rfind_oochar mio_rfind_uchar
#	define mio_find_oochar_in_oocstr mio_find_uchar_in_ucstr

#	define mio_split_oocstr mio_split_ucstr
#	define mio_count_oocstr mio_count_ucstr
#else
#	define mio_equal_oochars mio_equal_bchars
#	define mio_comp_oochars mio_comp_bchars
#	define mio_comp_oocstr_bcstr mio_comp_bcstr
#	define mio_comp_oochars_bcstr mio_comp_bchars_bcstr
#	define mio_comp_oochars_ucstr mio_comp_bchars_ucstr
#	define mio_comp_oochars_oocstr mio_comp_bchars_bcstr
#	define mio_comp_oocstr mio_comp_bcstr

#	define mio_copy_oochars mio_copy_bchars
#	define mio_copy_bchars_to_oochars mio_copy_bchars
#	define mio_copy_oochars_to_bchars mio_copy_bchars
#	define mio_copy_uchars_to_oochars mio_copy_uchars_to_bchars
#	define mio_copy_oochars_to_uchars mio_copy_bchars_to_uchars

#	define mio_copy_oochars_to_oocstr mio_copy_bchars_to_bcstr
#	define mio_copy_oochars_to_oocstr_unlimited mio_copy_bchars_to_bcstr_unlimited
#	define mio_copy_oocstr mio_copy_bcstr
#	define mio_copy_oocstr_unlimited mio_copy_bcstr_unlimited

#	define mio_fill_oochars mio_fill_bchars
#	define mio_find_oocstr_word_in_oocstr mio_find_bcstr_word_in_bcstr
#	define mio_find_oochar mio_find_bchar
#	define mio_rfind_oochar mio_rfind_bchar
#	define mio_find_oochar_in_oocstr mio_find_bchar_in_bcstr

#	define mio_split_oocstr mio_split_bcstr
#	define mio_count_oocstr mio_count_bcstr
#endif
/* ------------------------------------------------------------------------- */

#define MIO_BYTE_TO_BCSTR_RADIXMASK (0xFF)
#define MIO_BYTE_TO_BCSTR_LOWERCASE (1 << 8)
 
MIO_EXPORT mio_oow_t mio_byte_to_bcstr (
	mio_uint8_t   byte,  
	mio_bch_t*    buf,
	mio_oow_t     size,
	int           flagged_radix,
	mio_bch_t     fill
);

/* ------------------------------------------------------------------------- */

#define MIO_UCHARS_TO_INTMAX_MAKE_OPTION(ltrim,rtrim,base) (((!!(ltrim)) << 2) | ((!!(rtrim)) << 4) | ((base) << 8))
#define MIO_UCHARS_TO_INTMAX_GET_OPTION_LTRIM(option) ((option) & 4)
#define MIO_UCHARS_TO_INTMAX_GET_OPTION_RTRIM(option) ((option) & 8)
#define MIO_UCHARS_TO_INTMAX_GET_OPTION_BASE(option) ((option) >> 8)

#define MIO_BCHARS_TO_INTMAX_MAKE_OPTION(ltrim,rtrim,base) MIO_UCHARS_TO_INTMAX_MAKE_OPTION(ltrim,rtrim,base)
#define MIO_BCHARS_TO_INTMAX_GET_OPTION_LTRIM(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_LTRIM(option)
#define MIO_BCHARS_TO_INTMAX_GET_OPTION_RTRIM(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_RTRIM(option)
#define MIO_BCHARS_TO_INTMAX_GET_OPTION_BASE(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_BASE(option)

#define MIO_UCHARS_TO_UINTMAX_MAKE_OPTION(ltrim,rtrim,base) MIO_UCHARS_TO_INTMAX_MAKE_OPTION(ltrim,rtrim,base)
#define MIO_UCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_LTRIM(option)
#define MIO_UCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_RTRIM(option)
#define MIO_UCHARS_TO_UINTMAX_GET_OPTION_BASE(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_BASE(option)

#define MIO_BCHARS_TO_UINTMAX_MAKE_OPTION(ltrim,rtrim,base) MIO_UCHARS_TO_INTMAX_MAKE_OPTION(ltrim,rtrim,base)
#define MIO_BCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_LTRIM(option)
#define MIO_BCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_RTRIM(option)
#define MIO_BCHARS_TO_UINTMAX_GET_OPTION_BASE(option) MIO_UCHARS_TO_INTMAX_GET_OPTION_BASE(option)

MIO_EXPORT mio_intmax_t mio_uchars_to_intmax (
	const mio_uch_t*  str,
	mio_oow_t         len,
	int               option,
	const mio_uch_t** endptr,
	int*              is_sober
);

MIO_EXPORT mio_intmax_t mio_bchars_to_intmax (
	const mio_bch_t*  str,
	mio_oow_t         len,
	int               option,
	const mio_bch_t** endptr,
	int*              is_sober
);

MIO_EXPORT mio_uintmax_t mio_uchars_to_uintmax (
	const mio_uch_t*  str,
	mio_oow_t         len,
	int               option,
	const mio_uch_t** endptr,
	int*              is_sober
);

MIO_EXPORT mio_uintmax_t mio_bchars_to_uintmax (
	const mio_bch_t*  str,
	mio_oow_t         len,
	int               option,
	const mio_bch_t** endptr,
	int*              is_sober
);

/* ------------------------------------------------------------------------- */

#if defined(MIO_OOCH_IS_UCH)
#	define mio_conv_oocstr_to_bcstr_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr) mio_conv_ucstr_to_bcstr_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr)
#	define mio_conv_oochars_to_bchars_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr) mio_conv_uchars_to_bchars_with_cmgr(oocs,oocslen,bcs,bcslen,cmgr)
#else
#	define mio_conv_oocstr_to_ucstr_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr) mio_conv_bcstr_to_ucstr_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr,0)
#	define mio_conv_oochars_to_uchars_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr) mio_conv_bchars_to_uchars_with_cmgr(oocs,oocslen,ucs,ucslen,cmgr,0)
#endif


MIO_EXPORT int mio_conv_bcstr_to_ucstr_with_cmgr (
	const mio_bch_t* bcs,
	mio_oow_t*       bcslen,
	mio_uch_t*       ucs,
	mio_oow_t*       ucslen,
	mio_cmgr_t*      cmgr,
	int              all
);

MIO_EXPORT int mio_conv_bchars_to_uchars_with_cmgr (
	const mio_bch_t* bcs,
	mio_oow_t*       bcslen,
	mio_uch_t*       ucs,
	mio_oow_t*       ucslen,
	mio_cmgr_t*      cmgr,
	int              all
);

MIO_EXPORT int mio_conv_ucstr_to_bcstr_with_cmgr (
	const mio_uch_t* ucs,
	mio_oow_t*       ucslen,
	mio_bch_t*       bcs,
	mio_oow_t*       bcslen,
	mio_cmgr_t*      cmgr
);

MIO_EXPORT int mio_conv_uchars_to_bchars_with_cmgr (
	const mio_uch_t* ucs,
	mio_oow_t*       ucslen,
	mio_bch_t*       bcs,
	mio_oow_t*       bcslen,
	mio_cmgr_t*      cmgr
);

/* ------------------------------------------------------------------------- */

MIO_EXPORT mio_cmgr_t* mio_get_utf8_cmgr (
	void
);

/**
 * The mio_conv_uchars_to_utf8() function converts a unicode character string \a ucs 
 * to a UTF8 string and writes it into the buffer pointed to by \a bcs, but
 * not more than \a bcslen bytes including the terminating null.
 *
 * Upon return, \a bcslen is modified to the actual number of bytes written to
 * \a bcs excluding the terminating null; \a ucslen is modified to the number of
 * wide characters converted.
 *
 * You may pass #MIO_NULL for \a bcs to dry-run conversion or to get the
 * required buffer size for conversion. -2 is never returned in this case.
 *
 * \return
 * - 0 on full conversion,
 * - -1 on no or partial conversion for an illegal character encountered,
 * - -2 on no or partial conversion for a small buffer.
 *
 * \code
 *   const mio_uch_t ucs[] = { 'H', 'e', 'l', 'l', 'o' };
 *   mio_bch_t bcs[10];
 *   mio_oow_t ucslen = 5;
 *   mio_oow_t bcslen = MIO_COUNTOF(bcs);
 *   n = mio_conv_uchars_to_utf8 (ucs, &ucslen, bcs, &bcslen);
 *   if (n <= -1)
 *   {
 *      // conversion error
 *   }
 * \endcode
 */
MIO_EXPORT int mio_conv_uchars_to_utf8 (
	const mio_uch_t*    ucs,
	mio_oow_t*          ucslen,
	mio_bch_t*          bcs,
	mio_oow_t*          bcslen
);

/**
 * The mio_conv_utf8_to_uchars() function converts a UTF8 string to a uncide string.
 *
 * It never returns -2 if \a ucs is #MIO_NULL.
 *
 * \code
 *  const mio_bch_t* bcs = "test string";
 *  mio_uch_t ucs[100];
 *  mio_oow_t ucslen = MIO_COUNTOF(buf), n;
 *  mio_oow_t bcslen = 11;
 *  int n;
 *  n = mio_conv_utf8_to_uchars (bcs, &bcslen, ucs, &ucslen);
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
MIO_EXPORT int mio_conv_utf8_to_uchars (
	const mio_bch_t*   bcs,
	mio_oow_t*         bcslen,
	mio_uch_t*         ucs,
	mio_oow_t*         ucslen
);


MIO_EXPORT int mio_conv_ucstr_to_utf8 (
	const mio_uch_t*    ucs,
	mio_oow_t*          ucslen,
	mio_bch_t*          bcs,
	mio_oow_t*          bcslen
);

MIO_EXPORT int mio_conv_utf8_to_ucstr (
	const mio_bch_t*   bcs,
	mio_oow_t*         bcslen,
	mio_uch_t*         ucs,
	mio_oow_t*         ucslen
);


MIO_EXPORT mio_oow_t mio_uc_to_utf8 (
	mio_uch_t    uc,
	mio_bch_t*   utf8,
	mio_oow_t    size
);

MIO_EXPORT mio_oow_t mio_utf8_to_uc (
	const mio_bch_t* utf8,
	mio_oow_t        size,
	mio_uch_t*       uc
);

/* =========================================================================
 * BIT SWAP
 * ========================================================================= */
#if defined(MIO_HAVE_INLINE)

#if defined(MIO_HAVE_UINT16_T)
static MIO_INLINE mio_uint16_t mio_bswap16 (mio_uint16_t x)
{
#if defined(MIO_HAVE_BUILTIN_BSWAP16)
	return __builtin_bswap16(x);
#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64) || defined(__i386) || defined(i386))
	__asm__ volatile ("xchgb %b0, %h0" : "=Q"(x): "0"(x));
	return x;
#else
	return (x << 8) | (x >> 8);
#endif
}
#endif

#if defined(MIO_HAVE_UINT32_T)
static MIO_INLINE mio_uint32_t mio_bswap32 (mio_uint32_t x)
{
#if defined(MIO_HAVE_BUILTIN_BSWAP32)
	return __builtin_bswap32(x);
#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64) || defined(__i386) || defined(i386))
	__asm__ volatile ("bswapl %0" : "=r"(x) : "0"(x));
	return x;
#else
	return ((x >> 24)) | 
	       ((x >>  8) & ((mio_uint32_t)0xff << 8)) | 
	       ((x <<  8) & ((mio_uint32_t)0xff << 16)) | 
	       ((x << 24));
#endif
}
#endif

#if defined(MIO_HAVE_UINT64_T)
static MIO_INLINE mio_uint64_t mio_bswap64 (mio_uint64_t x)
{
#if defined(MIO_HAVE_BUILTIN_BSWAP64)
	return __builtin_bswap64(x);
#elif defined(__GNUC__) && (defined(__x86_64) || defined(__amd64))
	__asm__ volatile ("bswapq %0" : "=r"(x) : "0"(x));
	return x;
#else
	return ((x >> 56)) | 
	       ((x >> 40) & ((mio_uint64_t)0xff << 8)) | 
	       ((x >> 24) & ((mio_uint64_t)0xff << 16)) | 
	       ((x >>  8) & ((mio_uint64_t)0xff << 24)) | 
	       ((x <<  8) & ((mio_uint64_t)0xff << 32)) | 
	       ((x << 24) & ((mio_uint64_t)0xff << 40)) | 
	       ((x << 40) & ((mio_uint64_t)0xff << 48)) | 
	       ((x << 56));
#endif
}
#endif

#if defined(MIO_HAVE_UINT128_T)
static MIO_INLINE mio_uint128_t mio_bswap128 (mio_uint128_t x)
{
	return ((x >> 120)) | 
	       ((x >> 104) & ((mio_uint128_t)0xff << 8)) |
	       ((x >>  88) & ((mio_uint128_t)0xff << 16)) |
	       ((x >>  72) & ((mio_uint128_t)0xff << 24)) |
	       ((x >>  56) & ((mio_uint128_t)0xff << 32)) |
	       ((x >>  40) & ((mio_uint128_t)0xff << 40)) |
	       ((x >>  24) & ((mio_uint128_t)0xff << 48)) |
	       ((x >>   8) & ((mio_uint128_t)0xff << 56)) |
	       ((x <<   8) & ((mio_uint128_t)0xff << 64)) |
	       ((x <<  24) & ((mio_uint128_t)0xff << 72)) |
	       ((x <<  40) & ((mio_uint128_t)0xff << 80)) |
	       ((x <<  56) & ((mio_uint128_t)0xff << 88)) |
	       ((x <<  72) & ((mio_uint128_t)0xff << 96)) |
	       ((x <<  88) & ((mio_uint128_t)0xff << 104)) |
	       ((x << 104) & ((mio_uint128_t)0xff << 112)) |
	       ((x << 120));
}
#endif

#else

#if defined(MIO_HAVE_UINT16_T)
#	define mio_bswap16(x) ((mio_uint16_t)(((mio_uint16_t)(x)) << 8) | (((mio_uint16_t)(x)) >> 8))
#endif

#if defined(MIO_HAVE_UINT32_T)
#	define mio_bswap32(x) ((mio_uint32_t)(((((mio_uint32_t)(x)) >> 24)) | \
	                                      ((((mio_uint32_t)(x)) >>  8) & ((mio_uint32_t)0xff << 8)) | \
	                                      ((((mio_uint32_t)(x)) <<  8) & ((mio_uint32_t)0xff << 16)) | \
	                                      ((((mio_uint32_t)(x)) << 24))))
#endif

#if defined(MIO_HAVE_UINT64_T)
#	define mio_bswap64(x) ((mio_uint64_t)(((((mio_uint64_t)(x)) >> 56)) | \
	                                      ((((mio_uint64_t)(x)) >> 40) & ((mio_uint64_t)0xff << 8)) | \
	                                      ((((mio_uint64_t)(x)) >> 24) & ((mio_uint64_t)0xff << 16)) | \
	                                      ((((mio_uint64_t)(x)) >>  8) & ((mio_uint64_t)0xff << 24)) | \
	                                      ((((mio_uint64_t)(x)) <<  8) & ((mio_uint64_t)0xff << 32)) | \
	                                      ((((mio_uint64_t)(x)) << 24) & ((mio_uint64_t)0xff << 40)) | \
	                                      ((((mio_uint64_t)(x)) << 40) & ((mio_uint64_t)0xff << 48)) | \
	                                      ((((mio_uint64_t)(x)) << 56))))
#endif

#if defined(MIO_HAVE_UINT128_T)
#	define mio_bswap128(x) ((mio_uint128_t)(((((mio_uint128_t)(x)) >> 120)) |  \
	                                        ((((mio_uint128_t)(x)) >> 104) & ((mio_uint128_t)0xff << 8)) | \
	                                        ((((mio_uint128_t)(x)) >>  88) & ((mio_uint128_t)0xff << 16)) | \
	                                        ((((mio_uint128_t)(x)) >>  72) & ((mio_uint128_t)0xff << 24)) | \
	                                        ((((mio_uint128_t)(x)) >>  56) & ((mio_uint128_t)0xff << 32)) | \
	                                        ((((mio_uint128_t)(x)) >>  40) & ((mio_uint128_t)0xff << 40)) | \
	                                        ((((mio_uint128_t)(x)) >>  24) & ((mio_uint128_t)0xff << 48)) | \
	                                        ((((mio_uint128_t)(x)) >>   8) & ((mio_uint128_t)0xff << 56)) | \
	                                        ((((mio_uint128_t)(x)) <<   8) & ((mio_uint128_t)0xff << 64)) | \
	                                        ((((mio_uint128_t)(x)) <<  24) & ((mio_uint128_t)0xff << 72)) | \
	                                        ((((mio_uint128_t)(x)) <<  40) & ((mio_uint128_t)0xff << 80)) | \
	                                        ((((mio_uint128_t)(x)) <<  56) & ((mio_uint128_t)0xff << 88)) | \
	                                        ((((mio_uint128_t)(x)) <<  72) & ((mio_uint128_t)0xff << 96)) | \
	                                        ((((mio_uint128_t)(x)) <<  88) & ((mio_uint128_t)0xff << 104)) | \
	                                        ((((mio_uint128_t)(x)) << 104) & ((mio_uint128_t)0xff << 112)) | \
	                                        ((((mio_uint128_t)(x)) << 120))))
#endif

#endif /* MIO_HAVE_INLINE */


#if defined(MIO_ENDIAN_LITTLE)

#	if defined(MIO_HAVE_UINT16_T)
#	define mio_hton16(x) mio_bswap16(x)
#	define mio_ntoh16(x) mio_bswap16(x)
#	define mio_htobe16(x) mio_bswap16(x)
#	define mio_be16toh(x) mio_bswap16(x)
#	define mio_htole16(x) ((mio_uint16_t)(x))
#	define mio_le16toh(x) ((mio_uint16_t)(x))
#	endif

#	if defined(MIO_HAVE_UINT32_T)
#	define mio_hton32(x) mio_bswap32(x)
#	define mio_ntoh32(x) mio_bswap32(x)
#	define mio_htobe32(x) mio_bswap32(x)
#	define mio_be32toh(x) mio_bswap32(x)
#	define mio_htole32(x) ((mio_uint32_t)(x))
#	define mio_le32toh(x) ((mio_uint32_t)(x))
#	endif

#	if defined(MIO_HAVE_UINT64_T)
#	define mio_hton64(x) mio_bswap64(x)
#	define mio_ntoh64(x) mio_bswap64(x)
#	define mio_htobe64(x) mio_bswap64(x)
#	define mio_be64toh(x) mio_bswap64(x)
#	define mio_htole64(x) ((mio_uint64_t)(x))
#	define mio_le64toh(x) ((mio_uint64_t)(x))
#	endif

#	if defined(MIO_HAVE_UINT128_T)

#	define mio_hton128(x) mio_bswap128(x)
#	define mio_ntoh128(x) mio_bswap128(x)
#	define mio_htobe128(x) mio_bswap128(x)
#	define mio_be128toh(x) mio_bswap128(x)
#	define mio_htole128(x) ((mio_uint128_t)(x))
#	define mio_le128toh(x) ((mio_uint128_t)(x))
#	endif

#elif defined(MIO_ENDIAN_BIG)

#	if defined(MIO_HAVE_UINT16_T)
#	define mio_hton16(x) ((mio_uint16_t)(x))
#	define mio_ntoh16(x) ((mio_uint16_t)(x))
#	define mio_htobe16(x) ((mio_uint16_t)(x))
#	define mio_be16toh(x) ((mio_uint16_t)(x))
#	define mio_htole16(x) mio_bswap16(x)
#	define mio_le16toh(x) mio_bswap16(x)
#	endif

#	if defined(MIO_HAVE_UINT32_T)
#	define mio_hton32(x) ((mio_uint32_t)(x))
#	define mio_ntoh32(x) ((mio_uint32_t)(x))
#	define mio_htobe32(x) ((mio_uint32_t)(x))
#	define mio_be32toh(x) ((mio_uint32_t)(x))
#	define mio_htole32(x) mio_bswap32(x)
#	define mio_le32toh(x) mio_bswap32(x)
#	endif

#	if defined(MIO_HAVE_UINT64_T)
#	define mio_hton64(x) ((mio_uint64_t)(x))
#	define mio_ntoh64(x) ((mio_uint64_t)(x))
#	define mio_htobe64(x) ((mio_uint64_t)(x))
#	define mio_be64toh(x) ((mio_uint64_t)(x))
#	define mio_htole64(x) mio_bswap64(x)
#	define mio_le64toh(x) mio_bswap64(x)
#	endif

#	if defined(MIO_HAVE_UINT128_T)
#	define mio_hton128(x) ((mio_uint128_t)(x))
#	define mio_ntoh128(x) ((mio_uint128_t)(x))
#	define mio_htobe128(x) ((mio_uint128_t)(x))
#	define mio_be128toh(x) ((mio_uint128_t)(x))
#	define mio_htole128(x) mio_bswap128(x)
#	define mio_le128toh(x) mio_bswap128(x)
#	endif

#else
#	error UNKNOWN ENDIAN
#endif


#ifdef __cplusplus
}
#endif

#endif
