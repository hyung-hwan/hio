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

#ifndef _MIO_CHR_H_
#define _MIO_CHR_H_

#include <mio-cmn.h>

enum mio_ooch_prop_t
{
	MIO_OOCH_PROP_UPPER  = (1 << 0),
#define MIO_UCH_PROP_UPPER MIO_OOCH_PROP_UPPER
#define MIO_BCH_PROP_UPPER MIO_OOCH_PROP_UPPER

	MIO_OOCH_PROP_LOWER  = (1 << 1),
#define MIO_UCH_PROP_LOWER MIO_OOCH_PROP_LOWER
#define MIO_BCH_PROP_LOWER MIO_OOCH_PROP_LOWER

	MIO_OOCH_PROP_ALPHA  = (1 << 2),
#define MIO_UCH_PROP_ALPHA MIO_OOCH_PROP_ALPHA
#define MIO_BCH_PROP_ALPHA MIO_OOCH_PROP_ALPHA

	MIO_OOCH_PROP_DIGIT  = (1 << 3),
#define MIO_UCH_PROP_DIGIT MIO_OOCH_PROP_DIGIT
#define MIO_BCH_PROP_DIGIT MIO_OOCH_PROP_DIGIT

	MIO_OOCH_PROP_XDIGIT = (1 << 4),
#define MIO_UCH_PROP_XDIGIT MIO_OOCH_PROP_XDIGIT
#define MIO_BCH_PROP_XDIGIT MIO_OOCH_PROP_XDIGIT

	MIO_OOCH_PROP_ALNUM  = (1 << 5),
#define MIO_UCH_PROP_ALNUM MIO_OOCH_PROP_ALNUM
#define MIO_BCH_PROP_ALNUM MIO_OOCH_PROP_ALNUM

	MIO_OOCH_PROP_SPACE  = (1 << 6),
#define MIO_UCH_PROP_SPACE MIO_OOCH_PROP_SPACE
#define MIO_BCH_PROP_SPACE MIO_OOCH_PROP_SPACE

	MIO_OOCH_PROP_PRINT  = (1 << 8),
#define MIO_UCH_PROP_PRINT MIO_OOCH_PROP_PRINT
#define MIO_BCH_PROP_PRINT MIO_OOCH_PROP_PRINT

	MIO_OOCH_PROP_GRAPH  = (1 << 9),
#define MIO_UCH_PROP_GRAPH MIO_OOCH_PROP_GRAPH
#define MIO_BCH_PROP_GRAPH MIO_OOCH_PROP_GRAPH

	MIO_OOCH_PROP_CNTRL  = (1 << 10),
#define MIO_UCH_PROP_CNTRL MIO_OOCH_PROP_CNTRL
#define MIO_BCH_PROP_CNTRL MIO_OOCH_PROP_CNTRL

	MIO_OOCH_PROP_PUNCT  = (1 << 11),
#define MIO_UCH_PROP_PUNCT MIO_OOCH_PROP_PUNCT
#define MIO_BCH_PROP_PUNCT MIO_OOCH_PROP_PUNCT

	MIO_OOCH_PROP_BLANK  = (1 << 12)
#define MIO_UCH_PROP_BLANK MIO_OOCH_PROP_BLANK
#define MIO_BCH_PROP_BLANK MIO_OOCH_PROP_BLANK
};

typedef enum mio_ooch_prop_t mio_ooch_prop_t;
typedef enum mio_ooch_prop_t mio_uch_prop_t;
typedef enum mio_ooch_prop_t mio_bch_prop_t;

#define MIO_DIGIT_TO_NUM(c) (((c) >= '0' && (c) <= '9')? ((c) - '0'): -1)

#define MIO_XDIGIT_TO_NUM(c) \
	(((c) >= '0' && (c) <= '9')? ((c) - '0'): \
	 ((c) >= 'A' && (c) <= 'F')? ((c) - 'A' + 10): \
	 ((c) >= 'a' && (c) <= 'f')? ((c) - 'a' + 10): -1)

#if defined(__cplusplus)
extern "C" {
#endif

MIO_EXPORT int mio_is_uch_type (mio_uch_t c, mio_uch_prop_t type);
MIO_EXPORT int mio_is_uch_upper (mio_uch_t c);
MIO_EXPORT int mio_is_uch_lower (mio_uch_t c);
MIO_EXPORT int mio_is_uch_alpha (mio_uch_t c);
MIO_EXPORT int mio_is_uch_digit (mio_uch_t c);
MIO_EXPORT int mio_is_uch_xdigit (mio_uch_t c);
MIO_EXPORT int mio_is_uch_alnum (mio_uch_t c);
MIO_EXPORT int mio_is_uch_space (mio_uch_t c);
MIO_EXPORT int mio_is_uch_print (mio_uch_t c);
MIO_EXPORT int mio_is_uch_graph (mio_uch_t c);
MIO_EXPORT int mio_is_uch_cntrl (mio_uch_t c);
MIO_EXPORT int mio_is_uch_punct (mio_uch_t c);
MIO_EXPORT int mio_is_uch_blank (mio_uch_t c);
MIO_EXPORT mio_uch_t mio_to_uch_upper (mio_uch_t c);
MIO_EXPORT mio_uch_t mio_to_uch_lower (mio_uch_t c);

/* ------------------------------------------------------------------------- */

MIO_EXPORT int mio_is_bch_type (mio_bch_t c, mio_bch_prop_t type);

#if defined(__has_builtin)
#	if __has_builtin(__builtin_isupper)
#		define mio_is_bch_upper __builtin_isupper
#	endif
#	if __has_builtin(__builtin_islower)
#		define mio_is_bch_lower __builtin_islower
#	endif
#	if __has_builtin(__builtin_isalpha)
#		define mio_is_bch_alpha __builtin_isalpha
#	endif
#	if __has_builtin(__builtin_isdigit)
#		define mio_is_bch_digit __builtin_isdigit
#	endif
#	if __has_builtin(__builtin_isxdigit)
#		define mio_is_bch_xdigit __builtin_isxdigit
#	endif
#	if __has_builtin(__builtin_isalnum)
#		define mio_is_bch_alnum __builtin_isalnum
#	endif
#	if __has_builtin(__builtin_isspace)
#		define mio_is_bch_space __builtin_isspace
#	endif
#	if __has_builtin(__builtin_isprint)
#		define mio_is_bch_print __builtin_isprint
#	endif
#	if __has_builtin(__builtin_isgraph)
#		define mio_is_bch_graph __builtin_isgraph
#	endif
#	if __has_builtin(__builtin_iscntrl)
#		define mio_is_bch_cntrl __builtin_iscntrl
#	endif
#	if __has_builtin(__builtin_ispunct)
#		define mio_is_bch_punct __builtin_ispunct
#	endif
#	if __has_builtin(__builtin_isblank)
#		define mio_is_bch_blank __builtin_isblank
#	endif
#	if __has_builtin(__builtin_toupper)
#		define mio_to_bch_upper __builtin_toupper
#	endif
#	if __has_builtin(__builtin_tolower)
#		define mio_to_bch_lower __builtin_tolower
#	endif
#elif (__GNUC__ >= 14) 
#	define mio_is_bch_upper __builtin_isupper
#	define mio_is_bch_lower __builtin_islower
#	define mio_is_bch_alpha __builtin_isalpha
#	define mio_is_bch_digit __builtin_isdigit
#	define mio_is_bch_xdigit __builtin_isxdigit
#	define mio_is_bch_alnum __builtin_isalnum
#	define mio_is_bch_space __builtin_isspace
#	define mio_is_bch_print __builtin_isprint
#	define mio_is_bch_graph __builtin_isgraph
#	define mio_is_bch_cntrl __builtin_iscntrl
#	define mio_is_bch_punct __builtin_ispunct
#	define mio_is_bch_blank __builtin_isblank
#	define mio_to_bch_upper __builtin_toupper
#	define mio_to_bch_lower __builtin_tolower
#endif

/* the bch class functions support no locale.
 * these implemenent latin-1 only */

#if !defined(mio_is_bch_upper) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_upper (mio_bch_t c) { return (mio_bcu_t)c - 'A' < 26; }
#elif !defined(mio_is_bch_upper)
#	define mio_is_bch_upper(c) ((mio_bcu_t)(c) - 'A' < 26)
#endif

#if !defined(mio_is_bch_lower) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_lower (mio_bch_t c) { return (mio_bcu_t)c - 'a' < 26; }
#elif !defined(mio_is_bch_lower)
#	define mio_is_bch_lower(c) ((mio_bcu_t)(c) - 'a' < 26)
#endif

#if !defined(mio_is_bch_alpha) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_alpha (mio_bch_t c) { return ((mio_bcu_t)c | 32) - 'a' < 26; }
#elif !defined(mio_is_bch_alpha)
#	define mio_is_bch_alpha(c) (((mio_bcu_t)(c) | 32) - 'a' < 26)
#endif

#if !defined(mio_is_bch_digit) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_digit (mio_bch_t c) { return (mio_bcu_t)c - '0' < 10; }
#elif !defined(mio_is_bch_digit)
#	define mio_is_bch_digit(c) ((mio_bcu_t)(c) - '0' < 10)
#endif

#if !defined(mio_is_bch_xdigit) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_xdigit (mio_bch_t c) { return mio_is_bch_digit(c) || ((mio_bcu_t)c | 32) - 'a' < 6; }
#elif !defined(mio_is_bch_xdigit)
#	define mio_is_bch_xdigit(c) (mio_is_bch_digit(c) || ((mio_bcu_t)(c) | 32) - 'a' < 6)
#endif

#if !defined(mio_is_bch_alnum) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_alnum (mio_bch_t c) { return mio_is_bch_alpha(c) || mio_is_bch_digit(c); }
#elif !defined(mio_is_bch_alnum)
#	define mio_is_bch_alnum(c) (mio_is_bch_alpha(c) || mio_is_bch_digit(c))
#endif

#if !defined(mio_is_bch_space) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_space (mio_bch_t c) { return c == ' ' || (mio_bcu_t)c - '\t' < 5; }
#elif !defined(mio_is_bch_space)
#	define mio_is_bch_space(c) ((c) == ' ' || (mio_bcu_t)(c) - '\t' < 5)
#endif

#if !defined(mio_is_bch_print) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_print (mio_bch_t c) { return (mio_bcu_t)c - ' ' < 95; }
#elif !defined(mio_is_bch_print)
#	define mio_is_bch_print(c) ((mio_bcu_t)(c) - ' ' < 95)
#endif

#if !defined(mio_is_bch_graph) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_graph (mio_bch_t c) { return (mio_bcu_t)c - '!' < 94; }
#elif !defined(mio_is_bch_graph)
#	define mio_is_bch_graph(c) ((mio_bcu_t)(c) - '!' < 94)
#endif

#if !defined(mio_is_bch_cntrl) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_cntrl (mio_bch_t c) { return (mio_bcu_t)c < ' ' || (mio_bcu_t)c == 127; }
#elif !defined(mio_is_bch_cntrl)
#	define mio_is_bch_cntrl(c) ((mio_bcu_t)(c) < ' ' || (mio_bcu_t)(c) == 127)
#endif

#if !defined(mio_is_bch_punct) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_punct (mio_bch_t c) { return mio_is_bch_graph(c) && !mio_is_bch_alnum(c); }
#elif !defined(mio_is_bch_punct)
#	define mio_is_bch_punct(c) (mio_is_bch_graph(c) && !mio_is_bch_alnum(c))
#endif

#if !defined(mio_is_bch_blank) && defined(MIO_HAVE_INLINE)
static MIO_INLINE int mio_is_bch_blank (mio_bch_t c) { return c == ' ' || c == '\t'; }
#elif !defined(mio_is_bch_blank)
#	define mio_is_bch_blank(c) ((c) == ' ' || (c) == '\t')
#endif

#if !defined(mio_to_bch_upper)
MIO_EXPORT mio_bch_t mio_to_bch_upper (mio_bch_t c);
#endif
#if !defined(mio_to_bch_lower)
MIO_EXPORT mio_bch_t mio_to_bch_lower (mio_bch_t c);
#endif

#if defined(MIO_OOCH_IS_UCH)
#	define mio_is_ooch_type mio_is_uch_type
#	define mio_is_ooch_upper mio_is_uch_upper
#	define mio_is_ooch_lower mio_is_uch_lower
#	define mio_is_ooch_alpha mio_is_uch_alpha
#	define mio_is_ooch_digit mio_is_uch_digit
#	define mio_is_ooch_xdigit mio_is_uch_xdigit
#	define mio_is_ooch_alnum mio_is_uch_alnum
#	define mio_is_ooch_space mio_is_uch_space
#	define mio_is_ooch_print mio_is_uch_print
#	define mio_is_ooch_graph mio_is_uch_graph
#	define mio_is_ooch_cntrl mio_is_uch_cntrl
#	define mio_is_ooch_punct mio_is_uch_punct
#	define mio_is_ooch_blank mio_is_uch_blank
#	define mio_to_ooch_upper mio_to_uch_upper
#	define mio_to_ooch_lower mio_to_uch_lower
#else
#	define mio_is_ooch_type mio_is_bch_type
#	define mio_is_ooch_upper mio_is_bch_upper
#	define mio_is_ooch_lower mio_is_bch_lower
#	define mio_is_ooch_alpha mio_is_bch_alpha
#	define mio_is_ooch_digit mio_is_bch_digit
#	define mio_is_ooch_xdigit mio_is_bch_xdigit
#	define mio_is_ooch_alnum mio_is_bch_alnum
#	define mio_is_ooch_space mio_is_bch_space
#	define mio_is_ooch_print mio_is_bch_print
#	define mio_is_ooch_graph mio_is_bch_graph
#	define mio_is_ooch_cntrl mio_is_bch_cntrl
#	define mio_is_ooch_punct mio_is_bch_punct
#	define mio_is_ooch_blank mio_is_bch_blank
#	define mio_to_ooch_upper mio_to_bch_upper
#	define mio_to_ooch_lower mio_to_bch_lower
#endif

MIO_EXPORT int mio_ucstr_to_uch_prop (
	const mio_uch_t*  name,
	mio_uch_prop_t*   id
);

MIO_EXPORT int mio_uchars_to_uch_prop (
	const mio_uch_t*  name,
	mio_oow_t         len,
	mio_uch_prop_t*   id
);

MIO_EXPORT int mio_bcstr_to_bch_prop (
	const mio_bch_t*  name,
	mio_bch_prop_t*   id
);

MIO_EXPORT int mio_bchars_to_bch_prop (
	const mio_bch_t*  name,
	mio_oow_t         len,
	mio_bch_prop_t*   id
);

#if defined(MIO_OOCH_IS_UCH)
#	define mio_oocstr_to_ooch_prop mio_ucstr_to_uch_prop
#	define mio_oochars_to_ooch_prop mio_uchars_to_uch_prop
#else
#	define mio_oocstr_to_ooch_prop mio_bcstr_to_bch_prop
#	define mio_oochars_to_ooch_prop mio_bchars_to_bch_prop
#endif

/* ------------------------------------------------------------------------- */

MIO_EXPORT int mio_get_ucwidth (
        mio_uch_t uc
);

/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */

MIO_EXPORT mio_oow_t mio_uc_to_utf16 (
	mio_uch_t    uc,
	mio_bch_t*   utf16,
	mio_oow_t    size
);

MIO_EXPORT mio_oow_t mio_utf16_to_uc (
	const mio_bch_t* utf16,
	mio_oow_t        size,
	mio_uch_t*       uc
);

/* ------------------------------------------------------------------------- */

MIO_EXPORT mio_oow_t mio_uc_to_mb8 (
	mio_uch_t    uc,
	mio_bch_t*   mb8,
	mio_oow_t    size
);

MIO_EXPORT mio_oow_t mio_mb8_to_uc (
	const mio_bch_t* mb8,
	mio_oow_t        size,
	mio_uch_t*       uc
);


#if defined(__cplusplus)
}
#endif

#endif
