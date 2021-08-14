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

/* 
 * Do NOT edit utl-str.c. Edit utl-str.c.m4 instead.
 * 
 * Generate utl-str.c with m4
 *   $ m4 utl-str.c.m4 > utl-str.c  
 */

#include "hio-prv.h"
#include <hio-chr.h>

dnl ---------------------------------------------------------------------------
include(`utl-str.m4')
dnl ---------------------------------------------------------------------------

fn_count_cstr(hio_count_ucstr, hio_uch_t)
fn_count_cstr(hio_count_bcstr, hio_bch_t)

fn_equal_chars(hio_equal_uchars, hio_uch_t)
fn_equal_chars(hio_equal_bchars, hio_bch_t)

fn_comp_chars(hio_comp_uchars, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_chars(hio_comp_bchars, hio_bch_t, hio_bchu_t, hio_to_bch_lower)

fn_comp_cstr(hio_comp_ucstr, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_cstr(hio_comp_bcstr, hio_bch_t, hio_bchu_t, hio_to_bch_lower)

fn_comp_cstr_limited(hio_comp_ucstr_limited, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_cstr_limited(hio_comp_bcstr_limited, hio_bch_t, hio_bchu_t, hio_to_bch_lower)

fn_concat_chars_to_cstr(hio_concat_uchars_to_ucstr, hio_uch_t, hio_count_ucstr)
fn_concat_chars_to_cstr(hio_concat_bchars_to_bcstr, hio_bch_t, hio_count_bcstr)

fn_concat_cstr(hio_concat_ucstr, hio_uch_t, hio_count_ucstr)
fn_concat_cstr(hio_concat_bcstr, hio_bch_t, hio_count_bcstr)

fn_fill_chars(hio_fill_uchars, hio_uch_t)
fn_fill_chars(hio_fill_bchars, hio_bch_t)

fn_find_char(hio_find_uchar, hio_uch_t)
fn_find_char(hio_find_bchar, hio_bch_t)

fn_rfind_char(hio_rfind_uchar, hio_uch_t)
fn_rfind_char(hio_rfind_bchar, hio_bch_t)

fn_find_char_in_cstr(hio_find_uchar_in_ucstr, hio_uch_t)
fn_find_char_in_cstr(hio_find_bchar_in_bcstr, hio_bch_t)

fn_trim_chars(hio_trim_uchars, hio_uch_t, hio_is_uch_space, HIO_TRIM_UCHARS)
fn_trim_chars(hio_trim_bchars, hio_bch_t, hio_is_bch_space, HIO_TRIM_BCHARS)

fn_split_cstr(hio_split_ucstr, hio_uch_t, hio_is_uch_space, hio_copy_ucstr_unlimited)
fn_split_cstr(hio_split_bcstr, hio_bch_t, hio_is_bch_space, hio_copy_bcstr_unlimited)

fn_chars_to_int(hio_uchars_to_intmax, hio_uch_t, hio_intmax_t, hio_is_uch_space, HIO_UCHARS_TO_INTMAX)
fn_chars_to_int(hio_bchars_to_intmax, hio_bch_t, hio_intmax_t, hio_is_bch_space, HIO_BCHARS_TO_INTMAX)

fn_chars_to_uint(hio_uchars_to_uintmax, hio_uch_t, hio_uintmax_t, hio_is_uch_space, HIO_UCHARS_TO_UINTMAX)
fn_chars_to_uint(hio_bchars_to_uintmax, hio_bch_t, hio_uintmax_t, hio_is_bch_space, HIO_BCHARS_TO_UINTMAX)
