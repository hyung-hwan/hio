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

#ifndef _HIO_CHR_H_
#define _HIO_CHR_H_

#include <hio-cmn.h>

enum hio_ooch_prop_t
{
	HIO_OOCH_PROP_UPPER  = (1 << 0),
#define HIO_UCH_PROP_UPPER HIO_OOCH_PROP_UPPER
#define HIO_BCH_PROP_UPPER HIO_OOCH_PROP_UPPER

	HIO_OOCH_PROP_LOWER  = (1 << 1),
#define HIO_UCH_PROP_LOWER HIO_OOCH_PROP_LOWER
#define HIO_BCH_PROP_LOWER HIO_OOCH_PROP_LOWER

	HIO_OOCH_PROP_ALPHA  = (1 << 2),
#define HIO_UCH_PROP_ALPHA HIO_OOCH_PROP_ALPHA
#define HIO_BCH_PROP_ALPHA HIO_OOCH_PROP_ALPHA

	HIO_OOCH_PROP_DIGIT  = (1 << 3),
#define HIO_UCH_PROP_DIGIT HIO_OOCH_PROP_DIGIT
#define HIO_BCH_PROP_DIGIT HIO_OOCH_PROP_DIGIT

	HIO_OOCH_PROP_XDIGIT = (1 << 4),
#define HIO_UCH_PROP_XDIGIT HIO_OOCH_PROP_XDIGIT
#define HIO_BCH_PROP_XDIGIT HIO_OOCH_PROP_XDIGIT

	HIO_OOCH_PROP_ALNUM  = (1 << 5),
#define HIO_UCH_PROP_ALNUM HIO_OOCH_PROP_ALNUM
#define HIO_BCH_PROP_ALNUM HIO_OOCH_PROP_ALNUM

	HIO_OOCH_PROP_SPACE  = (1 << 6),
#define HIO_UCH_PROP_SPACE HIO_OOCH_PROP_SPACE
#define HIO_BCH_PROP_SPACE HIO_OOCH_PROP_SPACE

	HIO_OOCH_PROP_PRINT  = (1 << 8),
#define HIO_UCH_PROP_PRINT HIO_OOCH_PROP_PRINT
#define HIO_BCH_PROP_PRINT HIO_OOCH_PROP_PRINT

	HIO_OOCH_PROP_GRAPH  = (1 << 9),
#define HIO_UCH_PROP_GRAPH HIO_OOCH_PROP_GRAPH
#define HIO_BCH_PROP_GRAPH HIO_OOCH_PROP_GRAPH

	HIO_OOCH_PROP_CNTRL  = (1 << 10),
#define HIO_UCH_PROP_CNTRL HIO_OOCH_PROP_CNTRL
#define HIO_BCH_PROP_CNTRL HIO_OOCH_PROP_CNTRL

	HIO_OOCH_PROP_PUNCT  = (1 << 11),
#define HIO_UCH_PROP_PUNCT HIO_OOCH_PROP_PUNCT
#define HIO_BCH_PROP_PUNCT HIO_OOCH_PROP_PUNCT

	HIO_OOCH_PROP_BLANK  = (1 << 12)
#define HIO_UCH_PROP_BLANK HIO_OOCH_PROP_BLANK
#define HIO_BCH_PROP_BLANK HIO_OOCH_PROP_BLANK
};

typedef enum hio_ooch_prop_t hio_ooch_prop_t;
typedef enum hio_ooch_prop_t hio_uch_prop_t;
typedef enum hio_ooch_prop_t hio_bch_prop_t;

#if defined(__cplusplus)
extern "C" {
#endif

HIO_EXPORT int hio_is_uch_type (hio_uch_t c, hio_uch_prop_t type);
HIO_EXPORT int hio_is_uch_upper (hio_uch_t c);
HIO_EXPORT int hio_is_uch_lower (hio_uch_t c);
HIO_EXPORT int hio_is_uch_alpha (hio_uch_t c);
HIO_EXPORT int hio_is_uch_digit (hio_uch_t c);
HIO_EXPORT int hio_is_uch_xdigit (hio_uch_t c);
HIO_EXPORT int hio_is_uch_alnum (hio_uch_t c);
HIO_EXPORT int hio_is_uch_space (hio_uch_t c);
HIO_EXPORT int hio_is_uch_print (hio_uch_t c);
HIO_EXPORT int hio_is_uch_graph (hio_uch_t c);
HIO_EXPORT int hio_is_uch_cntrl (hio_uch_t c);
HIO_EXPORT int hio_is_uch_punct (hio_uch_t c);
HIO_EXPORT int hio_is_uch_blank (hio_uch_t c);
HIO_EXPORT hio_uch_t hio_to_uch_upper (hio_uch_t c);
HIO_EXPORT hio_uch_t hio_to_uch_lower (hio_uch_t c);

/* ------------------------------------------------------------------------- */

HIO_EXPORT int hio_is_bch_type (hio_bch_t c, hio_bch_prop_t type);

#if defined(__has_builtin)
#	if __has_builtin(__builtin_isupper)
#		define hio_is_bch_upper __builtin_isupper
#	endif
#	if __has_builtin(__builtin_islower)
#		define hio_is_bch_lower __builtin_islower
#	endif
#	if __has_builtin(__builtin_isalpha)
#		define hio_is_bch_alpha __builtin_isalpha
#	endif
#	if __has_builtin(__builtin_isdigit)
#		define hio_is_bch_digit __builtin_isdigit
#	endif
#	if __has_builtin(__builtin_isxdigit)
#		define hio_is_bch_xdigit __builtin_isxdigit
#	endif
#	if __has_builtin(__builtin_isalnum)
#		define hio_is_bch_alnum __builtin_isalnum
#	endif
#	if __has_builtin(__builtin_isspace)
#		define hio_is_bch_space __builtin_isspace
#	endif
#	if __has_builtin(__builtin_isprint)
#		define hio_is_bch_print __builtin_isprint
#	endif
#	if __has_builtin(__builtin_isgraph)
#		define hio_is_bch_graph __builtin_isgraph
#	endif
#	if __has_builtin(__builtin_iscntrl)
#		define hio_is_bch_cntrl __builtin_iscntrl
#	endif
#	if __has_builtin(__builtin_ispunct)
#		define hio_is_bch_punct __builtin_ispunct
#	endif
#	if __has_builtin(__builtin_isblank)
#		define hio_is_bch_blank __builtin_isblank
#	endif
#	if __has_builtin(__builtin_toupper)
#		define hio_to_bch_upper __builtin_toupper
#	endif
#	if __has_builtin(__builtin_tolower)
#		define hio_to_bch_lower __builtin_tolower
#	endif
#elif (__GNUC__ >= 14)
#	define hio_is_bch_upper __builtin_isupper
#	define hio_is_bch_lower __builtin_islower
#	define hio_is_bch_alpha __builtin_isalpha
#	define hio_is_bch_digit __builtin_isdigit
#	define hio_is_bch_xdigit __builtin_isxdigit
#	define hio_is_bch_alnum __builtin_isalnum
#	define hio_is_bch_space __builtin_isspace
#	define hio_is_bch_print __builtin_isprint
#	define hio_is_bch_graph __builtin_isgraph
#	define hio_is_bch_cntrl __builtin_iscntrl
#	define hio_is_bch_punct __builtin_ispunct
#	define hio_is_bch_blank __builtin_isblank
#	define hio_to_bch_upper __builtin_toupper
#	define hio_to_bch_lower __builtin_tolower
#endif

/* the bch class functions support no locale.
 * these implemenent latin-1 only */

#if !defined(hio_is_bch_upper) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_upper (hio_bch_t c) { return (hio_bcu_t)c - 'A' < 26; }
#elif !defined(hio_is_bch_upper)
#	define hio_is_bch_upper(c) ((hio_bcu_t)(c) - 'A' < 26)
#endif

#if !defined(hio_is_bch_lower) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_lower (hio_bch_t c) { return (hio_bcu_t)c - 'a' < 26; }
#elif !defined(hio_is_bch_lower)
#	define hio_is_bch_lower(c) ((hio_bcu_t)(c) - 'a' < 26)
#endif

#if !defined(hio_is_bch_alpha) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_alpha (hio_bch_t c) { return ((hio_bcu_t)c | 32) - 'a' < 26; }
#elif !defined(hio_is_bch_alpha)
#	define hio_is_bch_alpha(c) (((hio_bcu_t)(c) | 32) - 'a' < 26)
#endif

#if !defined(hio_is_bch_digit) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_digit (hio_bch_t c) { return (hio_bcu_t)c - '0' < 10; }
#elif !defined(hio_is_bch_digit)
#	define hio_is_bch_digit(c) ((hio_bcu_t)(c) - '0' < 10)
#endif

#if !defined(hio_is_bch_xdigit) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_xdigit (hio_bch_t c) { return hio_is_bch_digit(c) || ((hio_bcu_t)c | 32) - 'a' < 6; }
#elif !defined(hio_is_bch_xdigit)
#	define hio_is_bch_xdigit(c) (hio_is_bch_digit(c) || ((hio_bcu_t)(c) | 32) - 'a' < 6)
#endif

#if !defined(hio_is_bch_alnum) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_alnum (hio_bch_t c) { return hio_is_bch_alpha(c) || hio_is_bch_digit(c); }
#elif !defined(hio_is_bch_alnum)
#	define hio_is_bch_alnum(c) (hio_is_bch_alpha(c) || hio_is_bch_digit(c))
#endif

#if !defined(hio_is_bch_space) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_space (hio_bch_t c) { return c == ' ' || (hio_bcu_t)c - '\t' < 5; }
#elif !defined(hio_is_bch_space)
#	define hio_is_bch_space(c) ((c) == ' ' || (hio_bcu_t)(c) - '\t' < 5)
#endif

#if !defined(hio_is_bch_print) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_print (hio_bch_t c) { return (hio_bcu_t)c - ' ' < 95; }
#elif !defined(hio_is_bch_print)
#	define hio_is_bch_print(c) ((hio_bcu_t)(c) - ' ' < 95)
#endif

#if !defined(hio_is_bch_graph) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_graph (hio_bch_t c) { return (hio_bcu_t)c - '!' < 94; }
#elif !defined(hio_is_bch_graph)
#	define hio_is_bch_graph(c) ((hio_bcu_t)(c) - '!' < 94)
#endif

#if !defined(hio_is_bch_cntrl) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_cntrl (hio_bch_t c) { return (hio_bcu_t)c < ' ' || (hio_bcu_t)c == 127; }
#elif !defined(hio_is_bch_cntrl)
#	define hio_is_bch_cntrl(c) ((hio_bcu_t)(c) < ' ' || (hio_bcu_t)(c) == 127)
#endif

#if !defined(hio_is_bch_punct) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_punct (hio_bch_t c) { return hio_is_bch_graph(c) && !hio_is_bch_alnum(c); }
#elif !defined(hio_is_bch_punct)
#	define hio_is_bch_punct(c) (hio_is_bch_graph(c) && !hio_is_bch_alnum(c))
#endif

#if !defined(hio_is_bch_blank) && defined(HIO_HAVE_INLINE)
static HIO_INLINE int hio_is_bch_blank (hio_bch_t c) { return c == ' ' || c == '\t'; }
#elif !defined(hio_is_bch_blank)
#	define hio_is_bch_blank(c) ((c) == ' ' || (c) == '\t')
#endif

#if !defined(hio_to_bch_upper)
HIO_EXPORT hio_bch_t hio_to_bch_upper (hio_bch_t c);
#endif
#if !defined(hio_to_bch_lower)
HIO_EXPORT hio_bch_t hio_to_bch_lower (hio_bch_t c);
#endif

#if defined(HIO_OOCH_IS_UCH)
#	define hio_is_ooch_type hio_is_uch_type
#	define hio_is_ooch_upper hio_is_uch_upper
#	define hio_is_ooch_lower hio_is_uch_lower
#	define hio_is_ooch_alpha hio_is_uch_alpha
#	define hio_is_ooch_digit hio_is_uch_digit
#	define hio_is_ooch_xdigit hio_is_uch_xdigit
#	define hio_is_ooch_alnum hio_is_uch_alnum
#	define hio_is_ooch_space hio_is_uch_space
#	define hio_is_ooch_print hio_is_uch_print
#	define hio_is_ooch_graph hio_is_uch_graph
#	define hio_is_ooch_cntrl hio_is_uch_cntrl
#	define hio_is_ooch_punct hio_is_uch_punct
#	define hio_is_ooch_blank hio_is_uch_blank
#	define hio_to_ooch_upper hio_to_uch_upper
#	define hio_to_ooch_lower hio_to_uch_lower
#else
#	define hio_is_ooch_type hio_is_bch_type
#	define hio_is_ooch_upper hio_is_bch_upper
#	define hio_is_ooch_lower hio_is_bch_lower
#	define hio_is_ooch_alpha hio_is_bch_alpha
#	define hio_is_ooch_digit hio_is_bch_digit
#	define hio_is_ooch_xdigit hio_is_bch_xdigit
#	define hio_is_ooch_alnum hio_is_bch_alnum
#	define hio_is_ooch_space hio_is_bch_space
#	define hio_is_ooch_print hio_is_bch_print
#	define hio_is_ooch_graph hio_is_bch_graph
#	define hio_is_ooch_cntrl hio_is_bch_cntrl
#	define hio_is_ooch_punct hio_is_bch_punct
#	define hio_is_ooch_blank hio_is_bch_blank
#	define hio_to_ooch_upper hio_to_bch_upper
#	define hio_to_ooch_lower hio_to_bch_lower
#endif

HIO_EXPORT int hio_ucstr_to_uch_prop (
	const hio_uch_t*  name,
	hio_uch_prop_t*   id
);

HIO_EXPORT int hio_uchars_to_uch_prop (
	const hio_uch_t*  name,
	hio_oow_t         len,
	hio_uch_prop_t*   id
);

HIO_EXPORT int hio_bcstr_to_bch_prop (
	const hio_bch_t*  name,
	hio_bch_prop_t*   id
);

HIO_EXPORT int hio_bchars_to_bch_prop (
	const hio_bch_t*  name,
	hio_oow_t         len,
	hio_bch_prop_t*   id
);

#if defined(HIO_OOCH_IS_UCH)
#	define hio_oocstr_to_ooch_prop hio_ucstr_to_uch_prop
#	define hio_oochars_to_ooch_prop hio_uchars_to_uch_prop
#else
#	define hio_oocstr_to_ooch_prop hio_bcstr_to_bch_prop
#	define hio_oochars_to_ooch_prop hio_bchars_to_bch_prop
#endif

/* ------------------------------------------------------------------------- */

HIO_EXPORT int hio_get_ucwidth (
        hio_uch_t uc
);

/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_oow_t hio_uc_to_utf16 (
	hio_uch_t    uc,
	hio_bch_t*   utf16,
	hio_oow_t    size
);

HIO_EXPORT hio_oow_t hio_utf16_to_uc (
	const hio_bch_t* utf16,
	hio_oow_t        size,
	hio_uch_t*       uc
);

/* ------------------------------------------------------------------------- */

HIO_EXPORT hio_oow_t hio_uc_to_mb8 (
	hio_uch_t    uc,
	hio_bch_t*   mb8,
	hio_oow_t    size
);

HIO_EXPORT hio_oow_t hio_mb8_to_uc (
	const hio_bch_t* mb8,
	hio_oow_t        size,
	hio_uch_t*       uc
);


#if defined(__cplusplus)
}
#endif

#endif
