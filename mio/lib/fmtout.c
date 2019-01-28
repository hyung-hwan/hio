/*
 * $Id$
 *
    Copyright (c) 2015-2016 Chung, Hyung-Hwan. All rights reserved.

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

#include "mio-prv.h"

/*#include <stdio.h>*/ /* for snprintf(). used for floating-point number formatting */

#if defined(_MSC_VER) || defined(__BORLANDC__) || (defined(__WATCOMC__) && (__WATCOMC__ < 1200))
#	define snprintf _snprintf 
#	if !defined(HAVE_SNPRINTF)
#		define HAVE_SNPRINTF
#	endif
#endif
#if defined(HAVE_QUADMATH_H)
#	include <quadmath.h> /* for quadmath_snprintf() */
#endif
/* TODO: remove stdio.h and quadmath.h once snprintf gets replaced by own 
floting-point conversion implementation*/

/* Max number conversion buffer length: 
 * mio_intmax_t in base 2, plus NUL byte. */
#define MAXNBUF (MIO_SIZEOF(mio_intmax_t) * 8 + 1)

enum
{
	/* integer */
	LF_C = (1 << 0),
	LF_H = (1 << 1),
	LF_J = (1 << 2),
	LF_L = (1 << 3),
	LF_Q = (1 << 4),
	LF_T = (1 << 5),
	LF_Z = (1 << 6),

	/* long double */
	LF_LD = (1 << 7),
	/* __float128 */
	LF_QD = (1 << 8)
};

static struct
{
	mio_uint8_t flag; /* for single occurrence */
	mio_uint8_t dflag; /* for double occurrence */
} lm_tab[26] = 
{
	{ 0,    0 }, /* a */
	{ 0,    0 }, /* b */
	{ 0,    0 }, /* c */
	{ 0,    0 }, /* d */
	{ 0,    0 }, /* e */
	{ 0,    0 }, /* f */
	{ 0,    0 }, /* g */
	{ LF_H, LF_C }, /* h */
	{ 0,    0 }, /* i */
	{ LF_J, 0 }, /* j */
	{ 0,    0 }, /* k */
	{ LF_L, LF_Q }, /* l */
	{ 0,    0 }, /* m */
	{ 0,    0 }, /* n */
	{ 0,    0 }, /* o */
	{ 0,    0 }, /* p */
	{ LF_Q, 0 }, /* q */
	{ 0,    0 }, /* r */
	{ 0,    0 }, /* s */
	{ LF_T, 0 }, /* t */
	{ 0,    0 }, /* u */
	{ 0,    0 }, /* v */
	{ 0,    0 }, /* w */
	{ 0,    0 }, /* z */
	{ 0,    0 }, /* y */
	{ LF_Z, 0 }, /* z */
};


enum 
{
	FLAGC_DOT       = (1 << 0),
	FLAGC_SPACE     = (1 << 1),
	FLAGC_SHARP     = (1 << 2),
	FLAGC_SIGN      = (1 << 3),
	FLAGC_LEFTADJ   = (1 << 4),
	FLAGC_ZEROPAD   = (1 << 5),
	FLAGC_WIDTH     = (1 << 6),
	FLAGC_PRECISION = (1 << 7),
	FLAGC_STAR1     = (1 << 8),
	FLAGC_STAR2     = (1 << 9),
	FLAGC_LENMOD    = (1 << 10) /* length modifier */
};

static const mio_bch_t hex2ascii_lower[] = 
{
	'0','1','2','3','4','5','6','7','8','9',
	'a','b','c','d','e','f','g','h','i','j','k','l','m',
	'n','o','p','q','r','s','t','u','v','w','x','y','z'
};

static const mio_bch_t hex2ascii_upper[] = 
{
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J','K','L','M',
	'N','O','P','Q','R','S','T','U','V','W','X','H','Z'
};

static mio_uch_t uch_nullstr[] = { '(','n','u','l','l', ')','\0' };
static mio_bch_t bch_nullstr[] = { '(','n','u','l','l', ')','\0' };

typedef int (*mio_fmtout_putch_t) (
	mio_t*          mio,
	mio_bitmask_t   mask,
	mio_ooch_t      c,
	mio_oow_t       len
);

typedef int (*mio_fmtout_putcs_t) (
	mio_t*            mio,
	mio_bitmask_t     mask,
	const mio_ooch_t* ptr,
	mio_oow_t         len
);

typedef struct mio_fmtout_data_t mio_fmtout_data_t;
struct mio_fmtout_data_t
{
	mio_oow_t            count; /* out */
	mio_bitmask_t        mask;  /* in */
	mio_fmtout_putch_t   putch; /* in */
	mio_fmtout_putcs_t   putcs; /* in */
};

/* ------------------------------------------------------------------------- */
/*
 * Put a NUL-terminated ASCII number (base <= 36) in a buffer in reverse
 * order; return an optional length and a pointer to the last character
 * written in the buffer (i.e., the first character of the string).
 * The buffer pointed to by `nbuf' must have length >= MAXNBUF.
 */

static mio_bch_t* sprintn_lower (mio_bch_t* nbuf, mio_uintmax_t num, int base, mio_ooi_t* lenp)
{
	mio_bch_t* p;

	p = nbuf;
	*p = '\0';
	do { *++p = hex2ascii_lower[num % base]; } while (num /= base);

	if (lenp) *lenp = p - nbuf;
	return p; /* returns the end */
}

static mio_bch_t* sprintn_upper (mio_bch_t* nbuf, mio_uintmax_t num, int base, mio_ooi_t* lenp)
{
	mio_bch_t* p;

	p = nbuf;
	*p = '\0';
	do { *++p = hex2ascii_upper[num % base]; } while (num /= base);

	if (lenp) *lenp = p - nbuf;
	return p; /* returns the end */
}

/* ------------------------------------------------------------------------- */
static int put_ooch (mio_t* mio, mio_bitmask_t mask, mio_ooch_t ch, mio_oow_t len)
{
	/* this is not equivalent to put_oocs(mio,mask,&ch, 1);
	 * this function is to emit a single character multiple times */
	mio_oow_t rem;

	if (len <= 0) return 1;

	if (mio->log.len > 0 && mio->log.last_mask != mask)
	{
		/* the mask has changed. commit the buffered text */

/* TODO: HANDLE LINE ENDING CONVENTION BETTER... */
		if (mio->log.ptr[mio->log.len - 1] != '\n')
		{
			/* no line ending - append a line terminator */
			mio->log.ptr[mio->log.len++] = '\n';
		}
		prim_write_log (mio, mio->log.last_mask, mio->log.ptr, mio->log.len);
		mio->log.len = 0;
	}

redo:
	rem = 0;
	if (len > mio->log.capa - mio->log.len)
	{
		mio_oow_t newcapa, max;
		mio_ooch_t* tmp;

		max = MIO_TYPE_MAX(mio_oow_t) - mio->log.len;
		if (len > max)
		{
			/* data too big. */
			rem += len - max;
			len = max;
		}

		newcapa = MIO_ALIGN_POW2(mio->log.len + len, MIO_LOG_CAPA_ALIGN); /* TODO: adjust this capacity */
		if (newcapa > mio->option.log_maxcapa) 
		{
			/* [NOTE]
			 * it doesn't adjust newcapa to mio->option.log_maxcapa.
			 * nor does it cut the input to fit it into the adjusted capacity.
			 * if maxcapa set is not aligned to MIO_LOG_CAPA_ALIGN,
			 * the largest buffer capacity may be suboptimal */
			goto make_do;
		}

		/* +1 to handle line ending injection more easily */
		tmp = mio_reallocmem(mio, mio->log.ptr, (newcapa + 1) * MIO_SIZEOF(*tmp)); 
		if (!tmp) 
		{
		make_do:
			if (mio->log.len > 0)
			{
				/* can't expand the buffer. just flush the existing contents */
				/* TODO: HANDLE LINE ENDING CONVENTION BETTER... */
				if (mio->log.ptr[mio->log.len - 1] != '\n')
				{
					/* no line ending - append a line terminator */
					mio->log.ptr[mio->log.len++] = '\n';
				}
				prim_write_log (mio, mio->log.last_mask, mio->log.ptr, mio->log.len);
				mio->log.len = 0;
			}

			if (len > mio->log.capa)
			{
				rem += len - mio->log.capa;
				len = mio->log.capa;
			}

		}
		else
		{
			mio->log.ptr = tmp;
			mio->log.capa = newcapa; 
		}
	}

	while (len > 0)
	{
		mio->log.ptr[mio->log.len++] = ch;
		len--;
	}
	mio->log.last_mask = mask;

	if (rem > 0)
	{
		len = rem;
		goto redo;
	}

	
	return 1; /* success */
}

static int put_oocs (mio_t* mio, mio_bitmask_t mask, const mio_ooch_t* ptr, mio_oow_t len)
{
	mio_oow_t rem;

	if (len <= 0) return 1;

	if (mio->log.len > 0 && mio->log.last_mask != mask)
	{
		/* the mask has changed. commit the buffered text */
/* TODO: HANDLE LINE ENDING CONVENTION BETTER... */
		if (mio->log.ptr[mio->log.len - 1] != '\n')
		{
			/* no line ending - append a line terminator */
			mio->log.ptr[mio->log.len++] = '\n';
		}

		prim_write_log (mio, mio->log.last_mask, mio->log.ptr, mio->log.len);
		mio->log.len = 0;
	}

redo:
	rem = 0;
	if (len > mio->log.capa - mio->log.len)
	{
		mio_oow_t newcapa, max;
		mio_ooch_t* tmp;

		max = MIO_TYPE_MAX(mio_oow_t) - mio->log.len;
		if (len > max) 
		{
			/* data too big. */
			rem += len - max;
			len = max;
		}

		newcapa = MIO_ALIGN_POW2(mio->log.len + len, 512); /* TODO: adjust this capacity */
		if (newcapa > mio->option.log_maxcapa)
		{
			/* [NOTE]
			 * it doesn't adjust newcapa to mio->option.log_maxcapa.
			 * nor does it cut the input to fit it into the adjusted capacity.
			 * if maxcapa set is not aligned to MIO_LOG_CAPA_ALIGN,
			 * the largest buffer capacity may be suboptimal */
			goto make_do;
		}

		/* +1 to handle line ending injection more easily */
		tmp = mio_reallocmem(mio, mio->log.ptr, (newcapa + 1) * MIO_SIZEOF(*tmp));
		if (!tmp) 
		{
		make_do:
			if (mio->log.len > 0)
			{
				/* can't expand the buffer. just flush the existing contents */
				/* TODO: HANDLE LINE ENDING CONVENTION BETTER... */
				if (mio->log.ptr[mio->log.len - 1] != '\n')
				{
					/* no line ending - append a line terminator */
					mio->log.ptr[mio->log.len++] = '\n';
				}
				prim_write_log (mio, mio->log.last_mask, mio->log.ptr, mio->log.len);
				mio->log.len = 0;
			}

			if (len > mio->log.capa)
			{
				rem += len - mio->log.capa;
				len = mio->log.capa;
			}
		}
		else
		{
			mio->log.ptr = tmp;
			mio->log.capa = newcapa;
		}
	}

	MIO_MEMCPY (&mio->log.ptr[mio->log.len], ptr, len * MIO_SIZEOF(*ptr));
	mio->log.len += len;
	mio->log.last_mask = mask;

	if (rem > 0)
	{
		ptr += len;
		len = rem;
		goto redo;
	}

	return 1; /* success */
}

/* ------------------------------------------------------------------------- */


/* ------------------------------------------------------------------------- */

#undef FMTCHAR_IS_BCH
#undef FMTCHAR_IS_UCH
#undef FMTCHAR_IS_OOCH
#undef fmtchar_t
#undef fmtoutv
#define fmtchar_t mio_bch_t
#define fmtoutv __logbfmtv
#define FMTCHAR_IS_BCH
#if defined(MIO_OOCH_IS_BCH)
#	define FMTCHAR_IS_OOCH
#endif
#include "fmtoutv.h"

#undef FMTCHAR_IS_BCH
#undef FMTCHAR_IS_UCH
#undef FMTCHAR_IS_OOCH
#undef fmtchar_t
#undef fmtoutv
#define fmtchar_t mio_uch_t
#define fmtoutv __logufmtv
#define FMTCHAR_IS_UCH
#if defined(MIO_OOCH_IS_UCH)
#	define FMTCHAR_IS_OOCH
#endif
#include "fmtoutv.h" 


static int _logbfmtv (mio_t* mio, const mio_bch_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	return __logbfmtv(mio, fmt, data, ap);
}

static int _logufmtv (mio_t* mio, const mio_uch_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	return __logufmtv(mio, fmt, data, ap);
}

mio_ooi_t mio_logbfmt (mio_t* mio, mio_bitmask_t mask, const mio_bch_t* fmt, ...)
{
	int x;
	va_list ap;
	mio_fmtout_data_t fo;

	if (mio->log.default_type_mask & MIO_LOG_ALL_TYPES) 
	{
		/* if a type is given, it's not untyped any more.
		 * mask off the UNTYPED bit */
		mask &= ~MIO_LOG_UNTYPED; 

		/* if the default_type_mask has the UNTYPED bit on,
		 * it'll get turned back on */
		mask |= (mio->log.default_type_mask & MIO_LOG_ALL_TYPES);
	}
	else if (!(mask & MIO_LOG_ALL_TYPES)) 
	{
		/* no type is set in the given mask and no default type is set.
		 * make it UNTYPED. */
		mask |= MIO_LOG_UNTYPED;
	}

	fo.mask = mask;
	fo.putch = put_ooch;
	fo.putcs = put_oocs;

	va_start (ap, fmt);
	x = _logbfmtv(mio, fmt, &fo, ap);
	va_end (ap);

	if (mio->log.len > 0 && mio->log.ptr[mio->log.len - 1] == '\n')
	{
		prim_write_log (mio, mio->log.last_mask, mio->log.ptr, mio->log.len);
		mio->log.len = 0;
	}
	return (x <= -1)? -1: fo.count;
}

mio_ooi_t mio_logufmt (mio_t* mio, mio_bitmask_t mask, const mio_uch_t* fmt, ...)
{
	int x;
	va_list ap;
	mio_fmtout_data_t fo;

	if (mio->log.default_type_mask & MIO_LOG_ALL_TYPES) 
	{
		mask &= ~MIO_LOG_UNTYPED;
		mask |= (mio->log.default_type_mask & MIO_LOG_ALL_TYPES);
	}
	else if (!(mask & MIO_LOG_ALL_TYPES)) 
	{
		mask |= MIO_LOG_UNTYPED;
	}

	fo.mask = mask;
	fo.putch = put_ooch;
	fo.putcs = put_oocs;

	va_start (ap, fmt);
	x = _logufmtv(mio, fmt, &fo, ap);
	va_end (ap);

	if (mio->log.len > 0 && mio->log.ptr[mio->log.len - 1] == '\n')
	{
		prim_write_log (mio, mio->log.last_mask, mio->log.ptr, mio->log.len);
		mio->log.len = 0;
	}

	return (x <= -1)? -1: fo.count;
}


/* -------------------------------------------------------------------------- 
 * ERROR MESSAGE FORMATTING
 * -------------------------------------------------------------------------- */

static int put_errch (mio_t* mio, mio_bitmask_t mask, mio_ooch_t ch, mio_oow_t len)
{
	mio_oow_t max;

	max = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len - 1;
	if (len > max) len = max;

	if (len <= 0) return 1;

	while (len > 0)
	{
		mio->errmsg.buf[mio->errmsg.len++] = ch;
		len--;
	}
	mio->errmsg.buf[mio->errmsg.len] = '\0';

	return 1; /* success */
}

static int put_errcs (mio_t* mio, mio_bitmask_t mask, const mio_ooch_t* ptr, mio_oow_t len)
{
	mio_oow_t max;

	max = MIO_COUNTOF(mio->errmsg.buf) - mio->errmsg.len - 1;
	if (len > max) len = max;

	if (len <= 0) return 1;

	MIO_MEMCPY (&mio->errmsg.buf[mio->errmsg.len], ptr, len * MIO_SIZEOF(*ptr));
	mio->errmsg.len += len;
	mio->errmsg.buf[mio->errmsg.len] = '\0';

	return 1; /* success */
}


static int _errbfmtv (mio_t* mio, const mio_bch_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	return __logbfmtv(mio, fmt, data, ap);
}

static int _errufmtv (mio_t* mio, const mio_uch_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	return __logufmtv(mio, fmt, data, ap);
}

void mio_seterrbfmt (mio_t* mio, mio_errnum_t errnum, const mio_bch_t* fmt, ...)
{
	va_list ap;
	mio_fmtout_data_t fo;

	if (mio->shuterr) return;
	mio->errmsg.len = 0;

	fo.mask = 0; /* not used */
	fo.putch = put_errch;
	fo.putcs = put_errcs;

	va_start (ap, fmt);
	_errbfmtv (mio, fmt, &fo, ap);
	va_end (ap);

	mio->errnum = errnum;
}

void mio_seterrufmt (mio_t* mio, mio_errnum_t errnum, const mio_uch_t* fmt, ...)
{
	va_list ap;
	mio_fmtout_data_t fo;

	if (mio->shuterr) return;
	mio->errmsg.len = 0;

	fo.mask = 0; /* not used */
	fo.putch = put_errch;
	fo.putcs = put_errcs;

	va_start (ap, fmt);
	_errufmtv (mio, fmt, &fo, ap);
	va_end (ap);

	mio->errnum = errnum;
}


void mio_seterrbfmtv (mio_t* mio, mio_errnum_t errnum, const mio_bch_t* fmt, va_list ap)
{
	mio_fmtout_data_t fo;

	if (mio->shuterr) return;

	mio->errmsg.len = 0;

	fo.mask = 0; /* not used */
	fo.putch = put_errch;
	fo.putcs = put_errcs;

	_errbfmtv (mio, fmt, &fo, ap);
	mio->errnum = errnum;
}

void mio_seterrufmtv (mio_t* mio, mio_errnum_t errnum, const mio_uch_t* fmt, va_list ap)
{
	mio_fmtout_data_t fo;

	if (mio->shuterr) return;

	mio->errmsg.len = 0;

	fo.mask = 0; /* not used */
	fo.putch = put_errch;
	fo.putcs = put_errcs;

	_errufmtv (mio, fmt, &fo, ap);
	mio->errnum = errnum;
}

/* -------------------------------------------------------------------------- 
 * SUPPORT FOR THE BUILTIN SPRINTF PRIMITIVE FUNCTION
 * -------------------------------------------------------------------------- */
static int put_sprcs (mio_t* mio, mio_bitmask_t mask, const mio_ooch_t* ptr, mio_oow_t len)
{
	if (len > mio->sprintf.xbuf.capa - mio->sprintf.xbuf.len)
	{
		mio_ooch_t* tmp;
		mio_oow_t newcapa;

		newcapa = mio->sprintf.xbuf.len + len + 1;
		newcapa = MIO_ALIGN_POW2(newcapa, 256);

		tmp = (mio_ooch_t*)mio_reallocmem(mio, mio->sprintf.xbuf.ptr, newcapa * MIO_SIZEOF(*tmp));
		if (!tmp) return -1;

		mio->sprintf.xbuf.ptr = tmp;
		mio->sprintf.xbuf.capa = newcapa;
	}

	MIO_MEMCPY (&mio->sprintf.xbuf.ptr[mio->sprintf.xbuf.len], ptr, len * MIO_SIZEOF(*ptr));
	mio->sprintf.xbuf.len += len;
	return 1; /* success */
}

static int put_sprch (mio_t* mio, mio_bitmask_t mask, mio_ooch_t ch, mio_oow_t len)
{
	if (len > mio->sprintf.xbuf.capa - mio->sprintf.xbuf.len)
	{
		mio_ooch_t* tmp;
		mio_oow_t newcapa;

		newcapa = mio->sprintf.xbuf.len + len + 1;
		newcapa = MIO_ALIGN_POW2(newcapa, 256);

		tmp = (mio_ooch_t*)mio_reallocmem(mio, mio->sprintf.xbuf.ptr, newcapa * MIO_SIZEOF(*tmp));
		if (!tmp) return -1;

		mio->sprintf.xbuf.ptr = tmp;
		mio->sprintf.xbuf.capa = newcapa;
	}

	while (len > 0)
	{
		--len;
		mio->sprintf.xbuf.ptr[mio->sprintf.xbuf.len++] = ch;
	}

	return 1; /* success */
}

static int _sprbfmtv (mio_t* mio, const mio_bch_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	return __logbfmtv (mio, fmt, data, ap);
}

/*
static int _sprufmtv (mio_t* mio, const mio_uch_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	return __logufmtv (mio, fmt, data, ap, __sprbfmtv);
}*/

mio_ooi_t mio_sproutbfmt (mio_t* mio, mio_bitmask_t mask, const mio_bch_t* fmt, ...)
{
	int x;
	va_list ap;
	mio_fmtout_data_t fo;

	fo.mask = mask;
	fo.putch = put_sprch;
	fo.putcs = put_sprcs;

	va_start (ap, fmt);
	x = _sprbfmtv(mio, fmt, &fo, ap);
	va_end (ap);

	return (x <= -1)? -1: fo.count;
}

/*
mio_ooi_t mio_sproutufmt (mio_t* mio, mio_bitmask_t mask, const mio_uch_t* fmt, ...)
{
	int x;
	va_list ap;
	mio_fmtout_data_t fo;

	fo.mask = mask;
	fo.putch = put_sprch;
	fo.putcs = put_sprcs;

	va_start (ap, fmt);
	x = _sprufmtv (mio, fmt, &fo, ap);
	va_end (ap);

	return (x <= -1)? -1: fo.count;
}*/

