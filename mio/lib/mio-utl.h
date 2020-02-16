/*
 * $Id$
 *
    Copyright (c) 2014-2018 Chung, Hyung-Hwan. All rights reserved.

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

#include "mio-cmn.h"
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
 * FORMATTED OUTPUT
 * ========================================================================= */
typedef struct mio_fmtout_t mio_fmtout_t;

typedef int (*mio_fmtout_putbcs_t) (
	mio_fmtout_t*     fmtout,
	const mio_bch_t*  ptr,
	mio_oow_t         len
);

typedef int (*mio_fmtout_putucs_t) (
	mio_fmtout_t*     fmtout,
	const mio_uch_t*  ptr,
	mio_oow_t         len
);

enum mio_fmtout_fmt_type_t 
{
	MIO_FMTOUT_FMT_TYPE_BCH = 0,
	MIO_FMTOUT_FMT_TYPE_UCH
};
typedef enum mio_fmtout_fmt_type_t mio_fmtout_fmt_type_t;


struct mio_fmtout_t
{
	mio_oow_t             count; /* out */

	mio_fmtout_putbcs_t   putbcs; /* in */
	mio_fmtout_putucs_t   putucs; /* in */
	mio_bitmask_t         mask;   /* in */
	void*                 ctx;    /* in */

	mio_fmtout_fmt_type_t fmt_type;
	const void*           fmt_str;
};


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
	mio_oow_t        len2
);

MIO_EXPORT int mio_comp_bchars (
	const mio_bch_t* str1,
	mio_oow_t        len1,
	const mio_bch_t* str2,
	mio_oow_t        len2
);

MIO_EXPORT int mio_comp_ucstr (
	const mio_uch_t* str1,
	const mio_uch_t* str2
);

MIO_EXPORT int mio_comp_bcstr (
	const mio_bch_t* str1,
	const mio_bch_t* str2
);

MIO_EXPORT int mio_comp_ucstr_bcstr (
	const mio_uch_t* str1,
	const mio_bch_t* str2
);

MIO_EXPORT int mio_comp_uchars_ucstr (
	const mio_uch_t* str1,
	mio_oow_t        len,
	const mio_uch_t* str2
);

MIO_EXPORT int mio_comp_uchars_bcstr (
	const mio_uch_t* str1,
	mio_oow_t        len,
	const mio_bch_t* str2
);

MIO_EXPORT int mio_comp_bchars_bcstr (
	const mio_bch_t* str1,
	mio_oow_t        len,
	const mio_bch_t* str2
);

MIO_EXPORT int mio_comp_bchars_ucstr (
	const mio_bch_t* str1,
	mio_oow_t        len,
	const mio_uch_t* str2
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
	mio_uch_t        dlen,
	const mio_uch_t* src,
	mio_oow_t        slen
);

MIO_EXPORT mio_oow_t mio_copy_bchars_to_bcstr (
	mio_bch_t*       dst,
	mio_bch_t        dlen,
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

MIO_EXPORT mio_oow_t mio_count_ucstr (
	const mio_uch_t* str
);

MIO_EXPORT mio_oow_t mio_count_bcstr (
	const mio_bch_t* str
);

#if defined(MIO_OOCH_IS_UCH)
#	define mio_equal_oochars(str1,str2,len) mio_equal_uchars(str1,str2,len)
#	define mio_comp_oochars(str1,len1,str2,len2) mio_comp_uchars(str1,len1,str2,len2)
#	define mio_comp_oocstr_bcstr(str1,str2) mio_comp_ucstr_bcstr(str1,str2)
#	define mio_comp_oochars_bcstr(str1,len1,str2) mio_comp_uchars_bcstr(str1,len1,str2)
#	define mio_comp_oochars_ucstr(str1,len1,str2) mio_comp_uchars_ucstr(str1,len1,str2)
#	define mio_comp_oochars_oocstr(str1,len1,str2) mio_comp_uchars_ucstr(str1,len1,str2)
#	define mio_comp_oocstr(str1,str2) mio_comp_ucstr(str1,str2)


#	define mio_copy_oochars mio_copy_uchars
#	define mio_copy_bchars_to_oochars mio_copy_bchars_to_uchars
#	define mio_copy_oochars_to_bchars mio_copy_uchars_to_bchars
#	define mio_copy_uchars_to_oochars mio_copy_uchars
#	define mio_copy_oochars_to_uchars mio_copy_uchars

#	define mio_copy_oochars_to_oocstr mio_copy_uchars_to_ucstr
#	define mio_copy_oochars_to_oocstr_unlimited mio_copy_uchars_to_ucstr_unlimited
#	define mio_copy_oocstr mio_copy_ucstr
#	define mio_copy_oocstr_unlimited mio_copy_ucstr_unlimited

#	define mio_fill_oochars(dst,ch,len) mio_fill_uchars(dst,ch,len)
#	define mio_find_oochar(ptr,len,c) mio_find_uchar(ptr,len,c)
#	define mio_rfind_oochar(ptr,len,c) mio_rfind_uchar(ptr,len,c)
#	define mio_find_oochar_in_oocstr(ptr,c) mio_find_uchar_in_ucstr(ptr,c)
#	define mio_count_oocstr(str) mio_count_ucstr(str)
#else
#	define mio_equal_oochars(str1,str2,len) mio_equal_bchars(str1,str2,len)
#	define mio_comp_oochars(str1,len1,str2,len2) mio_comp_bchars(str1,len1,str2,len2)
#	define mio_comp_oocstr_bcstr(str1,str2) mio_comp_bcstr(str1,str2)
#	define mio_comp_oochars_bcstr(str1,len1,str2) mio_comp_bchars_bcstr(str1,len1,str2)
#	define mio_comp_oochars_ucstr(str1,len1,str2) mio_comp_bchars_ucstr(str1,len1,str2)
#	define mio_comp_oochars_oocstr(str1,len1,str2) mio_comp_bchars_bcstr(str1,len1,str2)
#	define mio_comp_oocstr(str1,str2) mio_comp_bcstr(str1,str2)

#	define mio_copy_oochars mio_copy_bchars
#	define mio_copy_bchars_to_oochars mio_copy_bchars
#	define mio_copy_oochars_to_bchars mio_copy_bchars
#	define mio_copy_uchars_to_oochars mio_copy_uchars_to_bchars
#	define mio_copy_oochars_to_uchars mio_copy_bchars_to_uchars

#	define mio_copy_oochars_to_oocstr mio_copy_bchars_to_bcstr
#	define mio_copy_oochars_to_oocstr_unlimited mio_copy_bchars_to_bcstr_unlimited
#	define mio_copy_oocstr mio_copy_bcstr
#	define mio_copy_oocstr_unlimited mio_copy_bcstr_unlimited

#	define mio_fill_oochars(dst,ch,len) mio_fill_bchars(dst,ch,len)
#	define mio_find_oochar(ptr,len,c) mio_find_bchar(ptr,len,c)
#	define mio_rfind_oochar(ptr,len,c) mio_rfind_bchar(ptr,len,c)
#	define mio_find_oochar_in_oocstr(ptr,c) mio_find_bchar_in_bcstr(ptr,c)
#	define mio_count_oocstr(str) mio_count_bcstr(str)
#endif
/* ------------------------------------------------------------------------- */

#define MIO_BYTE_TO_BCSTR_RADIXMASK (0xFF)
#define MIO_BYTE_TO_BCSTR_LOWERCASE (1 << 8)
 
mio_oow_t mio_byte_to_bcstr (
	mio_uint8_t   byte,  
	mio_bch_t*    buf,
	mio_oow_t     size,
	int           flagged_radix,
	mio_bch_t     fill
);

/* ------------------------------------------------------------------------- */

MIO_EXPORT int mio_ucwidth (
	mio_uch_t uc
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
 * FORMATTED OUTPUT
 * ========================================================================= */
MIO_EXPORT int mio_bfmt_outv (
	mio_fmtout_t*    fmtout,
	const mio_bch_t* fmt,
	va_list          ap
);

MIO_EXPORT int mio_ufmt_outv (
	mio_fmtout_t*    fmtout,
	const mio_uch_t* fmt,
	va_list          ap
);


MIO_EXPORT int mio_bfmt_out (
	mio_fmtout_t*    fmtout,
	const mio_bch_t* fmt,
	...
);

MIO_EXPORT int mio_ufmt_out (
	mio_fmtout_t*    fmtout,
	const mio_uch_t* fmt,
	...
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
