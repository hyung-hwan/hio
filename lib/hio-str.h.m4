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
 * Do NOT edit hio-str.h. Edit hio-str.h.m4 instead.
 *
 * Generate hio-str.h.m4 with m4
 *   $ m4 hio-str.h.m4 > hio-str.h
 */

#ifndef _HIO_STR_H_
#define _HIO_STR_H_

#include <hio-cmn.h>

dnl ---------------------------------------------------------------------------
dnl include utl-str.m4 for c++ template functions far below
include(`utl-str.m4')
dnl ---------------------------------------------------------------------------

/* =========================================================================
 * STRING
 * ========================================================================= */
enum hio_trim_flag_t
{
	HIO_TRIM_LEFT  = (1 << 0), /**< trim leading spaces */
#define HIO_TRIM_LEFT HIO_TRIM_LEFT
#define HIO_TRIM_OOCHARS_LEFT HIO_TRIM_LEFT
#define HIO_TRIM_UCHARS_LEFT HIO_TRIM_LEFT
#define HIO_TRIM_BCHARS_LEFT HIO_TRIM_LEFT
	HIO_TRIM_RIGHT = (1 << 1)  /**< trim trailing spaces */
#define HIO_TRIM_RIGHT HIO_TRIM_RIGHT
#define HIO_TRIM_OOCHARS_RIGHT HIO_TRIM_RIGHT
#define HIO_TRIM_UCHARS_RIGHT HIO_TRIM_RIGHT
#define HIO_TRIM_BCHARS_RIGHT HIO_TRIM_RIGHT
};

enum hio_fnmat_flag_t
{
	HIO_FNMAT_PATHNAME   = (1 << 0),
#define HIO_FNMAT_PATHNAME   HIO_FNMAT_PATHNAME
	HIO_FNMAT_NOESCAPE   = (1 << 1),
#define HIO_FNMAT_NOESCAPE   HIO_FNMAT_NOESCAPE
	HIO_FNMAT_PERIOD     = (1 << 2),
#define HIO_FNMAT_PERIOD     HIO_FNMAT_PERIOD
	HIO_FNMAT_IGNORECASE = (1 << 3)
#define HIO_FNMAT_IGNORECASE HIO_FNMAT_IGNORECASE
};

#if defined(_WIN32) || defined(__OS2__) || defined(__DOS__)
	/* i don't support escaping in these systems */
#	define HIO_FNMAT_IS_ESC(c) (0)
#	define HIO_FNMAT_IS_SEP(c) ((c) == '/' || (c) == '\\')
#else
#	define HIO_FNMAT_IS_ESC(c) ((c) == '\\')
#	define HIO_FNMAT_IS_SEP(c) ((c) == '/')
#endif

#if defined(__cplusplus)
extern "C" {
#endif


/* ------------------------------------ */

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

HIO_EXPORT int hio_comp_uchars_ucstr (
	const hio_uch_t* str1,
	hio_oow_t        len,
	const hio_uch_t* str2,
	int              ignorecase
);

HIO_EXPORT int hio_comp_bchars_bcstr (
	const hio_bch_t* str1,
	hio_oow_t        len,
	const hio_bch_t* str2,
	int              ignorecase
);

/* ------------------------------------ */

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

/* ------------------------------------ */

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

HIO_EXPORT hio_oow_t hio_copy_ucstr_to_uchars (
	hio_uch_t*        dst,
	hio_oow_t         dlen,
	const hio_uch_t*  src
);

HIO_EXPORT hio_oow_t hio_copy_bcstr_to_bchars (
	hio_bch_t*        dst,
	hio_oow_t         dlen,
	const hio_bch_t*  src
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

HIO_EXPORT hio_oow_t hio_copy_ucstr_unlimited (
	hio_uch_t*       dst,
	const hio_uch_t* src
);

HIO_EXPORT hio_oow_t hio_copy_bcstr_unlimited (
	hio_bch_t*       dst,
	const hio_bch_t* src
);

HIO_EXPORT hio_oow_t hio_copy_fmt_ucstrs_to_ucstr (
	hio_uch_t*       buf,
	hio_oow_t        bsz,
	const hio_uch_t* fmt,
	const hio_uch_t* str[]
);

HIO_EXPORT hio_oow_t hio_copy_fmt_bcstrs_to_bcstr (
	hio_bch_t*       buf,
	hio_oow_t        bsz,
	const hio_bch_t* fmt,
	const hio_bch_t* str[]
);

HIO_EXPORT hio_oow_t hio_copy_fmt_ucses_to_ucstr (
	hio_uch_t*       buf,
	hio_oow_t        bsz,
	const hio_uch_t* fmt,
	const hio_ucs_t  str[]
);

HIO_EXPORT hio_oow_t hio_copy_fmt_bcses_to_bcstr (
	hio_bch_t*       buf,
	hio_oow_t        bsz,
	const hio_bch_t* fmt,
	const hio_bcs_t  str[]
);

/* ------------------------------------ */

HIO_EXPORT hio_oow_t hio_count_ucstr (
	const hio_uch_t* str
);

HIO_EXPORT hio_oow_t hio_count_bcstr (
	const hio_bch_t* str
);

HIO_EXPORT hio_oow_t hio_count_ucstr_limited (
	const hio_uch_t* str,
	hio_oow_t        maxlen
);

HIO_EXPORT hio_oow_t hio_count_bcstr_limited (
	const hio_bch_t* str,
	hio_oow_t        maxlen
);

/* ------------------------------------ */

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

/* ------------------------------------ */


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

/* ------------------------------------ */

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

HIO_EXPORT hio_uch_t* hio_find_uchar_in_uchars (
	const hio_uch_t* ptr,
	hio_oow_t        len,
	hio_uch_t        c
);

HIO_EXPORT hio_bch_t* hio_find_bchar_in_bchars (
	const hio_bch_t* ptr,
	hio_oow_t        len,
	hio_bch_t        c
);

HIO_EXPORT hio_uch_t* hio_rfind_uchar_in_uchars (
	const hio_uch_t* ptr,
	hio_oow_t        len,
	hio_uch_t        c
);

HIO_EXPORT hio_bch_t* hio_rfind_bchar_in_bchars (
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

HIO_EXPORT hio_uch_t* hio_rfind_uchar_in_ucstr (
	const hio_uch_t* ptr,
	hio_uch_t        c
);

HIO_EXPORT hio_bch_t* hio_rfind_bchar_in_bcstr (
	const hio_bch_t* ptr,
	hio_bch_t        c
);

HIO_EXPORT hio_uch_t* hio_find_uchars_in_uchars (
	const hio_uch_t* str,
	hio_oow_t        strsz,
	const hio_uch_t* sub,
	hio_oow_t        subsz,
	int              inorecase
);

HIO_EXPORT hio_bch_t* hio_find_bchars_in_bchars (
	const hio_bch_t* str,
	hio_oow_t        strsz,
	const hio_bch_t* sub,
	hio_oow_t        subsz,
	int              inorecase
);

HIO_EXPORT hio_uch_t* hio_rfind_uchars_in_uchars (
	const hio_uch_t* str,
	hio_oow_t        strsz,
	const hio_uch_t* sub,
	hio_oow_t        subsz,
	int              inorecase
);

HIO_EXPORT hio_bch_t* hio_rfind_bchars_in_bchars (
	const hio_bch_t* str,
	hio_oow_t        strsz,
	const hio_bch_t* sub,
	hio_oow_t        subsz,
	int              inorecase
);

/* ------------------------------------ */

HIO_EXPORT hio_oow_t hio_compact_uchars (
	hio_uch_t*       str,
	hio_oow_t        len
);

HIO_EXPORT hio_oow_t hio_compact_bchars (
	hio_bch_t*       str,
	hio_oow_t        len
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

HIO_EXPORT hio_uch_t* hio_tokenize_uchars (
	const hio_uch_t* s,
	hio_oow_t        len,
	const hio_uch_t* delim,
	hio_oow_t        delim_len,
	hio_ucs_t*       tok,
	int              ignorecase
);

HIO_EXPORT hio_bch_t* hio_tokenize_bchars (
	const hio_bch_t* s,
	hio_oow_t        len,
	const hio_bch_t* delim,
	hio_oow_t        delim_len,
	hio_bcs_t*       tok,
	int              ignorecase
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


HIO_EXPORT int hio_fnmat_uchars_i (
	const hio_uch_t* str,
	hio_oow_t        slen,
	const hio_uch_t* ptn,
	hio_oow_t        plen,
	int              flags,
	int              no_first_period
);

HIO_EXPORT int hio_fnmat_bchars_i (
	const hio_bch_t* str,
	hio_oow_t        slen,
	const hio_bch_t* ptn,
	hio_oow_t        plen,
	int              flags,
	int              no_first_period
);

#define hio_fnmat_uchars(str, slen, ptn, plen, flags) hio_fnmat_uchars_i(str, slen, ptn, plen, flags, 0)
#define hio_fnmat_ucstr(str, ptn, flags) hio_fnmat_uchars_i(str, hio_count_ucstr(str), ptn, hio_count_ucstr(ptn), flags, 0)
#define hio_fnmat_uchars_ucstr(str, slen, ptn, flags) hio_fnmat_uchars_i(str, slen, ptn, hio_count_ucstr(ptn), flags, 0)
#define hio_fnmat_ucstr_uchars(str, ptn, plen, flags) hio_fnmat_uchars_i(str, hio_count_ucstr(str), ptn, plen, flags, 0)

#define hio_fnmat_bchars(str, slen, ptn, plen, flags) hio_fnmat_bchars_i(str, slen, ptn, plen, flags, 0)
#define hio_fnmat_bcstr(str, ptn, flags) hio_fnmat_bchars_i(str, hio_count_bcstr(str), ptn, hio_count_bcstr(ptn), flags, 0)
#define hio_fnmat_bchars_bcstr(str, slen, ptn, flags) hio_fnmat_bchars_i(str, slen, ptn, hio_count_bcstr(ptn), flags, 0)
#define hio_fnmat_bcstr_bchars(str, ptn, plen, flags) hio_fnmat_bchars_i(str, hio_count_bcstr(str), ptn, plen, flags, 0)


#if defined(HIO_OOCH_IS_UCH)
#	define hio_count_oocstr hio_count_ucstr
#	define hio_count_oocstr_limited hio_count_ucstr_limited

#	define hio_equal_oochars hio_equal_uchars
#	define hio_comp_oochars hio_comp_uchars
#	define hio_comp_oocstr_bcstr hio_comp_ucstr_bcstr
#	define hio_comp_oochars_bcstr hio_comp_uchars_bcstr
#	define hio_comp_oochars_ucstr hio_comp_uchars_ucstr
#	define hio_comp_oochars_oocstr hio_comp_uchars_ucstr
#	define hio_comp_oocstr hio_comp_ucstr
#	define hio_comp_oocstr_limited hio_comp_ucstr_limited

#	define hio_copy_oochars hio_copy_uchars
#	define hio_copy_bchars_to_oochars hio_copy_bchars_to_uchars
#	define hio_copy_oochars_to_bchars hio_copy_uchars_to_bchars
#	define hio_copy_uchars_to_oochars hio_copy_uchars
#	define hio_copy_oochars_to_uchars hio_copy_uchars

#	define hio_copy_oochars_to_oocstr hio_copy_uchars_to_ucstr
#	define hio_copy_oochars_to_oocstr_unlimited hio_copy_uchars_to_ucstr_unlimited
#	define hio_copy_oocstr hio_copy_ucstr
#	define hio_copy_oocstr_unlimited hio_copy_ucstr_unlimited
#	define hio_copy_fmt_oocses_to_oocstr hio_copy_fmt_ucses_to_ucstr
#	define hio_copy_fmt_oocstr_to_oocstr hio_copy_fmt_ucstr_to_ucstr

#	define hio_concat_oochars_to_ucstr hio_concat_uchars_to_ucstr
#	define hio_concat_oocstr hio_concat_ucstr

#	define hio_fill_oochars hio_fill_uchars
#	define hio_find_oocstr_word_in_oocstr hio_find_ucstr_word_in_ucstr
#	define hio_find_oochar_in_oochars hio_find_uchar_in_uchars
#	define hio_rfind_oochar_in_oochars hio_rfind_uchar_in_uchars
#	define hio_find_oochar_in_oocstr hio_find_uchar_in_ucstr
#	define hio_find_oochars_in_oochars hio_find_uchars_in_uchars
#	define hio_rfind_oochars_in_oochars hio_rfind_uchars_in_uchars

#	define hio_compact_oochars hio_compact_uchars
#	define hio_rotate_oochars hio_rotate_uchars
#	define hio_tokenize_oochars hio_tokenize_uchars
#	define hio_trim_oochars hio_trim_uchars
#	define hio_split_oocstr hio_split_ucstr

#	define hawk_fnmat_oochars_i hawk_fnmat_uchars_i
#	define hawk_fnmat_oochars hawk_fnmat_uchars
#	define hawk_fnmat_oocstr hawk_fnmat_ucstr
#	define hawk_fnmat_oochars_oocstr hawk_fnmat_uchars_ucstr
#	define hawk_fnmat_oocstr_oochars hawk_fnmat_ucstr_uchars

#else
#	define hio_count_oocstr hio_count_bcstr
#	define hio_count_oocstr_limited hio_count_bcstr_limited

#	define hio_equal_oochars hio_equal_bchars
#	define hio_comp_oochars hio_comp_bchars
#	define hio_comp_oocstr_bcstr hio_comp_bcstr
#	define hio_comp_oochars_bcstr hio_comp_bchars_bcstr
#	define hio_comp_oochars_ucstr hio_comp_bchars_ucstr
#	define hio_comp_oochars_oocstr hio_comp_bchars_bcstr
#	define hio_comp_oocstr hio_comp_bcstr
#	define hio_comp_oocstr_limited hio_comp_bcstr_limited


#	define hio_copy_oochars hio_copy_bchars
#	define hio_copy_bchars_to_oochars hio_copy_bchars
#	define hio_copy_oochars_to_bchars hio_copy_bchars
#	define hio_copy_uchars_to_oochars hio_copy_uchars_to_bchars
#	define hio_copy_oochars_to_uchars hio_copy_bchars_to_uchars

#	define hio_copy_oochars_to_oocstr hio_copy_bchars_to_bcstr
#	define hio_copy_oochars_to_oocstr_unlimited hio_copy_bchars_to_bcstr_unlimited
#	define hio_copy_oocstr hio_copy_bcstr
#	define hio_copy_oocstr_unlimited hio_copy_bcstr_unlimited
#	define hio_copy_fmt_oocses_to_oocstr hio_copy_fmt_bcses_to_bcstr
#	define hio_copy_fmt_oocstr_to_oocstr hio_copy_fmt_bcstr_to_bcstr

#	define hio_concat_oochars_to_bcstr hio_concat_bchars_to_bcstr
#	define hio_concat_oocstr hio_concat_bcstr

#	define hio_fill_oochars hio_fill_bchars
#	define hio_find_oocstr_word_in_oocstr hio_find_bcstr_word_in_bcstr
#	define hio_find_oochar_in_oochars hio_find_bchar_in_bchars
#	define hio_rfind_oochar_in_oochars hio_rfind_bchar_in_bchars
#	define hio_find_oochar_in_oocstr hio_find_bchar_in_bcstr
#	define hio_find_oochars_in_oochars hio_find_bchars_in_bchars
#	define hio_rfind_oochars_in_oochars hio_rfind_bchars_in_bchars

#	define hio_compact_oochars hio_compact_bchars
#	define hio_rotate_oochars hio_rotate_bchars
#	define hio_tokenize_oochars hio_tokenize_bchars
#	define hio_trim_oochars hio_trim_bchars
#	define hio_split_oocstr hio_split_bcstr

#	define hawk_fnmat_oochars_i hawk_fnmat_bchars_i
#	define hawk_fnmat_oochars hawk_fnmat_bchars
#	define hawk_fnmat_oocstr hawk_fnmat_bcstr
#	define hawk_fnmat_oochars_oocstr hawk_fnmat_bchars_bcstr
#	define hawk_fnmat_oocstr_oochars hawk_fnmat_bcstr_bchars
#endif

/* ------------------------------------------------------------------------- */

#define HIO_BYTE_TO_OOCSTR_RADIXMASK (0xFF)
#define HIO_BYTE_TO_OOCSTR_LOWERCASE (1 << 8)

#define HIO_BYTE_TO_UCSTR_RADIXMASK HIO_BYTE_TO_OOCSTR_RADIXMASK
#define HIO_BYTE_TO_UCSTR_LOWERCASE HIO_BYTE_TO_OOCSTR_LOWERCASE

#define HIO_BYTE_TO_BCSTR_RADIXMASK HIO_BYTE_TO_OOCSTR_RADIXMASK
#define HIO_BYTE_TO_BCSTR_LOWERCASE HIO_BYTE_TO_OOCSTR_LOWERCASE

HIO_EXPORT hio_oow_t hio_byte_to_bcstr (
	hio_uint8_t   byte,
	hio_bch_t*    buf,
	hio_oow_t     size,
	int           flagged_radix,
	hio_bch_t     fill
);

HIO_EXPORT hio_oow_t hio_byte_to_ucstr (
	hio_uint8_t   byte,
	hio_uch_t*    buf,
	hio_oow_t     size,
	int           flagged_radix,
	hio_uch_t     fill
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_byte_to_oocstr hio_byte_to_ucstr
#else
#	define hio_byte_to_oocstr hio_byte_to_bcstr
#endif

/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_oow_t hio_intmax_to_ucstr (
	hio_intmax_t     value,
	int              radix,
	const hio_uch_t* prefix,
	hio_uch_t*       buf,
	hio_oow_t        size
);

HIO_EXPORT hio_oow_t hio_intmax_to_bcstr (
	hio_intmax_t     value,
	int              radix,
	const hio_bch_t* prefix,
	hio_bch_t*       buf,
	hio_oow_t        size
);

HIO_EXPORT hio_oow_t hio_uintmax_to_ucstr (
	hio_uintmax_t     value,
	int              radix,
	const hio_uch_t* prefix,
	hio_uch_t*       buf,
	hio_oow_t        size
);

HIO_EXPORT hio_oow_t hio_uintmax_to_bcstr (
	hio_uintmax_t     value,
	int              radix,
	const hio_bch_t* prefix,
	hio_bch_t*       buf,
	hio_oow_t        size
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_intmax_to_oocstr hio_intmax_to_ucstr
#	define hio_uintmax_to_oocstr hio_uintmax_to_ucstr
#else
#	define hio_intmax_to_oocstr hio_intmax_to_bcstr
#	define hio_uintmax_to_oocstr hio_uintmax_to_bcstr
#endif


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
template<typename CHAR_TYPE, typename INT_TYPE, bool(*IS_SPACE)(CHAR_TYPE)>fn_chars_to_int(hio_chars_to_int, CHAR_TYPE, INT_TYPE, IS_SPACE, HIO_CHARS_TO_INT)
template<typename CHAR_TYPE, typename UINT_TYPE, bool(*IS_SPACE)(CHAR_TYPE)>fn_chars_to_uint(hio_chars_to_uint, CHAR_TYPE, UINT_TYPE, IS_SPACE, HIO_CHARS_TO_UINT)

#endif

#endif
