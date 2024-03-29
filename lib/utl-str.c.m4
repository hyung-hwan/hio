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
 * Do NOT edit utl-str.c. Edit utl-str.c.m4 instead.
 *
 * Generate utl-str.c with m4
 *   $ m4 utl-str.c.m4 > utl-str.c
 */

#include "hio-prv.h"
#include <hio-chr.h>

static int match_uch_class (const hio_uch_t* pp, hio_uch_t sc, int* matched)
{
	if (hio_comp_ucstr_bcstr_limited(pp, "[:upper:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_upper(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:lower:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_lower(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:alpha:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_alpha(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:digit:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_digit(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:xdigit:]", 10, 0) == 0)
	{
		*matched = hio_is_uch_xdigit(sc);
		return 10;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:alnum:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_alnum(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:space:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_space(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:print:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_print(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:graph:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_graph(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:cntrl:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_cntrl(sc);
		return 9;
	}
	else if (hio_comp_ucstr_bcstr_limited(pp, "[:punct:]", 9, 0) == 0)
	{
		*matched = hio_is_uch_punct(sc);
		return 9;
	}

	return 0;
}

static int match_bch_class (const hio_bch_t* pp, hio_bch_t sc, int* matched)
{
	if (hio_comp_bcstr_limited(pp, "[:upper:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_upper(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:lower:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_lower(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:alpha:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_alpha(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:digit:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_digit(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:xdigit:]", 10, 0) == 0)
	{
		*matched = hio_is_bch_xdigit(sc);
		return 10;
	}
	else if (hio_comp_bcstr_limited(pp, "[:alnum:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_alnum(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:space:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_space(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:print:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_print(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:graph:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_graph(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:cntrl:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_cntrl(sc);
		return 9;
	}
	else if (hio_comp_bcstr_limited(pp, "[:punct:]", 9, 0) == 0)
	{
		*matched = hio_is_bch_punct(sc);
		return 9;
	}

	return 0;
}

dnl
dnl ---------------------------------------------------------------------------
include(`utl-str.m4')dnl
dnl ---------------------------------------------------------------------------
dnl --
fn_comp_chars(hio_comp_uchars, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_chars(hio_comp_bchars, hio_bch_t, hio_bchu_t, hio_to_bch_lower)
dnl --
fn_comp_cstr(hio_comp_ucstr, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_cstr(hio_comp_bcstr, hio_bch_t, hio_bchu_t, hio_to_bch_lower)
dnl --
fn_comp_cstr_limited(hio_comp_ucstr_limited, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_cstr_limited(hio_comp_bcstr_limited, hio_bch_t, hio_bchu_t, hio_to_bch_lower)
dnl --
fn_comp_chars_cstr(hio_comp_uchars_ucstr, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_chars_cstr(hio_comp_bchars_bcstr, hio_bch_t, hio_bchu_t, hio_to_bch_lower)
dnl --
fn_concat_chars_to_cstr(hio_concat_uchars_to_ucstr, hio_uch_t, hio_count_ucstr)
fn_concat_chars_to_cstr(hio_concat_bchars_to_bcstr, hio_bch_t, hio_count_bcstr)
dnl --
fn_concat_cstr(hio_concat_ucstr, hio_uch_t, hio_count_ucstr)
fn_concat_cstr(hio_concat_bcstr, hio_bch_t, hio_count_bcstr)
dnl --
fn_copy_chars(hio_copy_uchars, hio_uch_t)
fn_copy_chars(hio_copy_bchars, hio_bch_t)
dnl --
fn_copy_chars_to_cstr(hio_copy_uchars_to_ucstr, hio_uch_t)
fn_copy_chars_to_cstr(hio_copy_bchars_to_bcstr, hio_bch_t)
dnl --
fn_copy_chars_to_cstr_unlimited(hio_copy_uchars_to_ucstr_unlimited, hio_uch_t)
fn_copy_chars_to_cstr_unlimited(hio_copy_bchars_to_bcstr_unlimited, hio_bch_t)
dnl --
fn_copy_cstr_to_chars(hio_copy_ucstr_to_uchars, hio_uch_t)
fn_copy_cstr_to_chars(hio_copy_bcstr_to_bchars, hio_bch_t)
dnl --
fn_copy_cstr(hio_copy_ucstr, hio_uch_t)
fn_copy_cstr(hio_copy_bcstr, hio_bch_t)
dnl --
fn_copy_cstr_unlimited(hio_copy_ucstr_unlimited, hio_uch_t)
fn_copy_cstr_unlimited(hio_copy_bcstr_unlimited, hio_bch_t)
dnl --
fn_copy_fmt_cstrs_to_cstr(hio_copy_fmt_ucstrs_to_ucstr, hio_uch_t)
fn_copy_fmt_cstrs_to_cstr(hio_copy_fmt_bcstrs_to_bcstr, hio_bch_t)
dnl --
fn_copy_fmt_cses_to_cstr(hio_copy_fmt_ucses_to_ucstr, hio_uch_t, hio_ucs_t)
fn_copy_fmt_cses_to_cstr(hio_copy_fmt_bcses_to_bcstr, hio_bch_t, hio_bcs_t)
dnl --
fn_count_cstr(hio_count_ucstr, hio_uch_t)
fn_count_cstr(hio_count_bcstr, hio_bch_t)
dnl --
fn_count_cstr_limited(hio_count_ucstr_limited, hio_uch_t)
fn_count_cstr_limited(hio_count_bcstr_limited, hio_bch_t)
dnl --
fn_equal_chars(hio_equal_uchars, hio_uch_t)
fn_equal_chars(hio_equal_bchars, hio_bch_t)
dnl --
fn_fill_chars(hio_fill_uchars, hio_uch_t)
fn_fill_chars(hio_fill_bchars, hio_bch_t)
dnl --
fn_find_char_in_chars(hio_find_uchar_in_uchars, hio_uch_t)
fn_find_char_in_chars(hio_find_bchar_in_bchars, hio_bch_t)
dnl --
fn_rfind_char_in_chars(hio_rfind_uchar_in_uchars, hio_uch_t)
fn_rfind_char_in_chars(hio_rfind_bchar_in_bchars, hio_bch_t)
dnl --
fn_find_char_in_cstr(hio_find_uchar_in_ucstr, hio_uch_t)
fn_find_char_in_cstr(hio_find_bchar_in_bcstr, hio_bch_t)
dnl --
fn_rfind_char_in_cstr(hio_rfind_uchar_in_ucstr, hio_uch_t)
fn_rfind_char_in_cstr(hio_rfind_bchar_in_bcstr, hio_bch_t)
dnl --
fn_find_chars_in_chars(hio_find_uchars_in_uchars, hio_uch_t, hio_to_uch_lower)
fn_find_chars_in_chars(hio_find_bchars_in_bchars, hio_bch_t, hio_to_bch_lower)
dnl --
fn_rfind_chars_in_chars(hio_rfind_uchars_in_uchars, hio_uch_t, hio_to_uch_lower)
fn_rfind_chars_in_chars(hio_rfind_bchars_in_bchars, hio_bch_t, hio_to_bch_lower)
dnl --
fn_compact_chars(hio_compact_uchars, hio_uch_t)
fn_compact_chars(hio_compact_bchars, hio_bch_t)
dnl --
fn_rotate_chars(hio_rotate_uchars, hio_uch_t)
fn_rotate_chars(hio_rotate_bchars, hio_bch_t)
dnl --
fn_trim_chars(hio_trim_uchars, hio_uch_t, hio_is_uch_space, HIO_TRIM_UCHARS)
fn_trim_chars(hio_trim_bchars, hio_bch_t, hio_is_bch_space, HIO_TRIM_BCHARS)
dnl --
fn_split_cstr(hio_split_ucstr, hio_uch_t, hio_is_uch_space, hio_copy_ucstr_unlimited)
fn_split_cstr(hio_split_bcstr, hio_bch_t, hio_is_bch_space, hio_copy_bcstr_unlimited)
dnl --
fn_tokenize_chars(hio_tokenize_uchars, hio_uch_t, hio_ucs_t, hio_is_uch_space, hio_to_uch_lower)
fn_tokenize_chars(hio_tokenize_bchars, hio_bch_t, hio_bcs_t, hio_is_bch_space, hio_to_bch_lower)
dnl --
fn_byte_to_cstr(hio_byte_to_ucstr, hio_uch_t, HIO_BYTE_TO_UCSTR)
fn_byte_to_cstr(hio_byte_to_bcstr, hio_bch_t, HIO_BYTE_TO_BCSTR)
dnl --
fn_int_to_cstr(hio_intmax_to_ucstr, hio_uch_t, hio_intmax_t, hio_count_ucstr)
fn_int_to_cstr(hio_intmax_to_bcstr, hio_bch_t, hio_intmax_t, hio_count_bcstr)
fn_int_to_cstr(hio_uintmax_to_ucstr, hio_uch_t, hio_uintmax_t, hio_count_ucstr)
fn_int_to_cstr(hio_uintmax_to_bcstr, hio_bch_t, hio_uintmax_t, hio_count_bcstr)
dnl --
fn_chars_to_int(hio_uchars_to_intmax, hio_uch_t, hio_intmax_t, hio_is_uch_space, HIO_UCHARS_TO_INTMAX)
fn_chars_to_int(hio_bchars_to_intmax, hio_bch_t, hio_intmax_t, hio_is_bch_space, HIO_BCHARS_TO_INTMAX)
dnl --
fn_chars_to_uint(hio_uchars_to_uintmax, hio_uch_t, hio_uintmax_t, hio_is_uch_space, HIO_UCHARS_TO_UINTMAX)
fn_chars_to_uint(hio_bchars_to_uintmax, hio_bch_t, hio_uintmax_t, hio_is_bch_space, HIO_BCHARS_TO_UINTMAX)
dnl --
fn_fnmat(hio_fnmat_uchars_i, hio_uch_t, hio_to_uch_lower, match_uch_class)
fn_fnmat(hio_fnmat_bchars_i, hio_bch_t, hio_to_bch_lower, match_bch_class)
