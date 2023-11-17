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
 * This file contains a formatted output routine derived from kvprintf()
 * of FreeBSD. It has been heavily modified and bug-fixed.
 */

/*
 * Copyright (c) 1986, 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <hio-fmt.h>
#include "hio-prv.h"


#if defined(HIO_ENABLE_FLTFMT)

#include <stdio.h> /* for snrintf(). used for floating-point number formatting */
#if defined(_MSC_VER) || defined(__BORLANDC__) || (defined(__WATCOMC__) && (__WATCOMC__ < 1200))
#	define snprintf _snprintf
#	if !defined(HAVE_SNPRINTF)
#		define HAVE_SNPRINTF
#	endif
#endif
#if defined(HAVE_QUADMATH_H)
#	include <quadmath.h> /* for quadmath_snprintf() */
#endif

#endif

/* Max number conversion buffer length:
 * hio_intmax_t in base 2, plus NUL byte. */
#define MAXNBUF (HIO_SIZEOF(hio_intmax_t) * HIO_BITS_PER_BYTE + 1)

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
	hio_uint8_t flag; /* for single occurrence */
	hio_uint8_t dflag; /* for double occurrence */
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

static const hio_bch_t hex2ascii_lower[] =
{
	'0','1','2','3','4','5','6','7','8','9',
	'a','b','c','d','e','f','g','h','i','j','k','l','m',
	'n','o','p','q','r','s','t','u','v','w','x','y','z'
};

static const hio_bch_t hex2ascii_upper[] =
{
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J','K','L','M',
	'N','O','P','Q','R','S','T','U','V','W','X','H','Z'
};

static hio_uch_t uch_nullstr[] = { '(','n','u','l','l', ')','\0' };
static hio_bch_t bch_nullstr[] = { '(','n','u','l','l', ')','\0' };


/* ------------------------------------------------------------------------- */
/*
 * Put a NUL-terminated ASCII number (base <= 36) in a buffer in reverse
 * order; return an optional length and a pointer to the last character
 * written in the buffer (i.e., the first character of the string).
 * The buffer pointed to by `nbuf' must have length >= MAXNBUF.
 */

static hio_bch_t* sprintn_lower (hio_bch_t* nbuf, hio_uintmax_t num, int base, hio_ooi_t* lenp)
{
	hio_bch_t* p;

	p = nbuf;
	*p = '\0';
	do { *++p = hex2ascii_lower[num % base]; } while (num /= base);

	if (lenp) *lenp = p - nbuf;
	return p; /* returns the end */
}

static hio_bch_t* sprintn_upper (hio_bch_t* nbuf, hio_uintmax_t num, int base, hio_ooi_t* lenp)
{
	hio_bch_t* p;

	p = nbuf;
	*p = '\0';
	do { *++p = hex2ascii_upper[num % base]; } while (num /= base);

	if (lenp) *lenp = p - nbuf;
	return p; /* returns the end */
}

/* ------------------------------------------------------------------------- */

#define PUT_BCH(fmtout,c,n) do { \
	if (n > 0) { \
		hio_oow_t _yy; \
		hio_bch_t _cc = c; \
		for (_yy = 0; _yy < n; _yy++) \
		{ \
			int _xx; \
			if ((_xx = fmtout->putbchars(fmtout, &_cc, 1)) <= -1) goto oops; \
			if (_xx == 0) goto done; \
			fmtout->count++; \
		} \
	} \
} while (0)

#define PUT_BCS(fmtout,ptr,len) do { \
	if (len > 0) { \
		int _xx; \
		if ((_xx = fmtout->putbchars(fmtout, ptr, len)) <= -1) goto oops; \
		if (_xx == 0) goto done; \
		fmtout->count += len; \
	} \
} while (0)

#define PUT_UCH(fmtout,c,n) do { \
	if (n > 0) { \
		hio_oow_t _yy; \
		hio_uch_t _cc = c; \
		for (_yy = 0; _yy < n; _yy++) \
		{ \
			int _xx; \
			if ((_xx = fmtout->putuchars(fmtout, &_cc, 1)) <= -1) goto oops; \
			if (_xx == 0) goto done; \
			fmtout->count++; \
		} \
	} \
} while (0)

#define PUT_UCS(fmtout,ptr,len) do { \
	if (len > 0) { \
		int _xx; \
		if ((_xx = fmtout->putuchars(fmtout, ptr, len)) <= -1) goto oops; \
		if (_xx == 0) goto done; \
		fmtout->count += len; \
	} \
} while (0)


#if defined(HIO_OOCH_IS_BCH)
#	define PUT_OOCH(fmtout,c,n) PUT_BCH(fmtout,c,n)
#	define PUT_OOCS(fmtout,ptr,len) PUT_BCS(fmtout,ptr,len)
#else
#	define PUT_OOCH(fmtout,c,n) PUT_UCH(fmtout,c,n)
#	define PUT_OOCS(fmtout,ptr,len) PUT_UCS(fmtout,ptr,len)
#endif

#define BYTE_PRINTABLE(x) ((x >= 'a' && x <= 'z') || (x >= 'A' &&  x <= 'Z') || (x >= '0' && x <= '9') || (x == ' '))


#define PUT_BYTE_IN_HEX(fmtout,byte,extra_flags) do { \
	hio_bch_t __xbuf[3]; \
	hio_byte_to_bcstr ((byte), __xbuf, HIO_COUNTOF(__xbuf), (16 | (extra_flags)), '0'); \
	PUT_BCH(fmtout, __xbuf[0], 1); \
	PUT_BCH(fmtout, __xbuf[1], 1); \
} while (0)

/* ------------------------------------------------------------------------- */
static int fmt_outv (hio_fmtout_t* fmtout, va_list ap)
{
	const hio_uint8_t* fmtptr, * percent;
	int fmtchsz;

	hio_uch_t uch;
	hio_bch_t bch;
	hio_ooch_t padc;

	int n, base, neg, sign;
	hio_ooi_t tmp, width, precision;
	int lm_flag, lm_dflag, flagc, numlen;

	hio_uintmax_t num = 0;
	hio_bch_t nbuf[MAXNBUF];
	const hio_bch_t* nbufp;
	int stop = 0;

#if defined(HIO_ENABLE_FLTFMT)
	struct
	{
		struct
		{
			hio_bch_t  sbuf[32];
			hio_bch_t* ptr;
			hio_oow_t  capa;
		} fmt;
		struct
		{
			hio_bch_t  sbuf[64];
			hio_bch_t* ptr;
			hio_oow_t  capa;
		} out;
	} fb; /* some buffers for handling float-point number formatting */
#endif

	hio_bch_t* (*sprintn) (hio_bch_t* nbuf, hio_uintmax_t num, int base, hio_ooi_t* lenp);

	fmtptr = (const hio_uint8_t*)fmtout->fmt_str;
	switch (fmtout->fmt_type)
	{
		case HIO_FMTOUT_FMT_TYPE_BCH:
			fmtchsz = HIO_SIZEOF_BCH_T;
			break;
		case HIO_FMTOUT_FMT_TYPE_UCH:
			fmtchsz = HIO_SIZEOF_UCH_T;
			break;
	}

	/* this is an internal function. it doesn't reset count to 0 */
	/* fmtout->count = 0; */
#if defined(HIO_ENABLE_FLTFMT)
	fb.fmt.ptr = fb.fmt.sbuf;
	fb.fmt.capa = HIO_COUNTOF(fb.fmt.sbuf) - 1;
	fb.out.ptr = fb.out.sbuf;
	fb.out.capa = HIO_COUNTOF(fb.out.sbuf) - 1;
#endif

	while (1)
	{
	#if defined(HAVE_LABELS_AS_VALUES)
		static void* before_percent_tab[] = { &&before_percent_bch, &&before_percent_uch };
		goto *before_percent_tab[fmtout->fmt_type];
	#else
		switch (fmtout->fmt_type)
		{
			case HIO_FMTOUT_FMT_TYPE_BCH:
				goto before_percent_bch;
			case HIO_FMTOUT_FMT_TYPE_UCH:
				goto before_percent_uch;
		}
	#endif

	before_percent_bch:
		{
			const hio_bch_t* start, * end;
			start = end = (const hio_bch_t*)fmtptr;
			while ((bch = *end++) != '%' || stop)
			{
				if (bch == '\0')
				{
					PUT_BCS (fmtout, start, end - start - 1);
					goto done;
				}
			}
			PUT_BCS (fmtout, start, end - start - 1);
			fmtptr = (const hio_uint8_t*)end;
			percent = (const hio_uint8_t*)(end - 1);
		}
		goto handle_percent;

	before_percent_uch:
		{
			const hio_uch_t* start, * end;
			start = end = (const hio_uch_t*)fmtptr;
			while ((uch = *end++) != '%' || stop)
			{
				if (uch == '\0')
				{
					PUT_UCS (fmtout, start, end - start - 1);
					goto done;
				}
			}
			PUT_UCS (fmtout, start, end - start - 1);
			fmtptr = (const hio_uint8_t*)end;
			percent = (const hio_uint8_t*)(end - 1);
		}
		goto handle_percent;

	handle_percent:
		padc = ' ';
		width = 0; precision = 0; neg = 0; sign = 0;
		lm_flag = 0; lm_dflag = 0; flagc = 0;
		sprintn = sprintn_lower;

	reswitch:
		switch (fmtout->fmt_type)
		{
			case HIO_FMTOUT_FMT_TYPE_BCH:
				uch = *(const hio_bch_t*)fmtptr;
				break;
			case HIO_FMTOUT_FMT_TYPE_UCH:
				uch = *(const hio_uch_t*)fmtptr;
				break;
		}
		fmtptr += fmtchsz;

		switch (uch)
		{
		case '%': /* %% */
			bch = uch;
			goto print_lowercase_c;

		/* flag characters */
		case '.':
			if (flagc & FLAGC_DOT) goto invalid_format;
			flagc |= FLAGC_DOT;
			goto reswitch;

		case '#':
			if (flagc & (FLAGC_WIDTH | FLAGC_DOT | FLAGC_LENMOD)) goto invalid_format;
			flagc |= FLAGC_SHARP;
			goto reswitch;

		case ' ':
			if (flagc & (FLAGC_WIDTH | FLAGC_DOT | FLAGC_LENMOD)) goto invalid_format;
			flagc |= FLAGC_SPACE;
			goto reswitch;

		case '+': /* place sign for signed conversion */
			if (flagc & (FLAGC_WIDTH | FLAGC_DOT | FLAGC_LENMOD)) goto invalid_format;
			flagc |= FLAGC_SIGN;
			goto reswitch;

		case '-': /* left adjusted */
			if (flagc & (FLAGC_WIDTH | FLAGC_DOT | FLAGC_LENMOD)) goto invalid_format;
			if (flagc & FLAGC_DOT)
			{
				goto invalid_format;
			}
			else
			{
				flagc |= FLAGC_LEFTADJ;
				if (flagc & FLAGC_ZEROPAD)
				{
					padc = ' ';
					flagc &= ~FLAGC_ZEROPAD;
				}
			}
			goto reswitch;

		case '*': /* take the length from the parameter */
			if (flagc & FLAGC_DOT)
			{
				if (flagc & (FLAGC_STAR2 | FLAGC_PRECISION)) goto invalid_format;
				flagc |= FLAGC_STAR2;

				precision = va_arg(ap, hio_ooi_t); /* this deviates from the standard printf that accepts 'int' */
				if (precision < 0)
				{
					/* if precision is less than 0,
					 * treat it as if no .precision is specified */
					flagc &= ~FLAGC_DOT;
					precision = 0;
				}
			}
			else
			{
				if (flagc & (FLAGC_STAR1 | FLAGC_WIDTH)) goto invalid_format;
				flagc |= FLAGC_STAR1;

				width = va_arg(ap, hio_ooi_t); /* it deviates from the standard printf that accepts 'int' */
				if (width < 0)
				{
					/*
					if (flagc & FLAGC_LEFTADJ)
						flagc  &= ~FLAGC_LEFTADJ;
					else
					*/
						flagc |= FLAGC_LEFTADJ;
					width = -width;
				}
			}
			goto reswitch;

		case '0': /* zero pad */
			if (flagc & FLAGC_LENMOD) goto invalid_format;
			if (!(flagc & (FLAGC_DOT | FLAGC_LEFTADJ)))
			{
				padc = '0';
				flagc |= FLAGC_ZEROPAD;
				goto reswitch;
			}
		/* end of flags characters */

		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		{
			if (flagc & FLAGC_LENMOD) goto invalid_format;
			for (n = 0;; fmtptr += fmtchsz)
			{
				n = n * 10 + uch - '0';
				switch (fmtout->fmt_type)
				{
					case HIO_FMTOUT_FMT_TYPE_BCH:
						uch = *(const hio_bch_t*)fmtptr;
						break;
					case HIO_FMTOUT_FMT_TYPE_UCH:
						uch = *(const hio_uch_t*)fmtptr;
						break;
				}
				if (uch < '0' || uch > '9') break;
			}
			if (flagc & FLAGC_DOT)
			{
				if (flagc & FLAGC_STAR2) goto invalid_format;
				precision = n;
				flagc |= FLAGC_PRECISION;
			}
			else
			{
				if (flagc & FLAGC_STAR1) goto invalid_format;
				width = n;
				flagc |= FLAGC_WIDTH;
			}
			goto reswitch;
		}

		/* length modifiers */
		case 'h': /* short int */
		case 'l': /* long int */
		case 'q': /* long long int */
		case 'j': /* hio_intmax_t/hio_uintmax_t */
		case 'z': /* hio_ooi_t/hio_oow_t */
		case 't': /* ptrdiff_t - usually hio_intptr_t */
			if (lm_flag & (LF_LD | LF_QD)) goto invalid_format;

			flagc |= FLAGC_LENMOD;
			if (lm_dflag)
			{
				/* error */
				goto invalid_format;
			}
			else if (lm_flag)
			{
				if (lm_tab[uch - 'a'].dflag && lm_flag == lm_tab[uch - 'a'].flag)
				{
					lm_flag &= ~lm_tab[uch - 'a'].flag;
					lm_flag |= lm_tab[uch - 'a'].dflag;
					lm_dflag |= lm_flag;
					goto reswitch;
				}
				else
				{
					/* error */
					goto invalid_format;
				}
			}
			else
			{
				lm_flag |= lm_tab[uch - 'a'].flag;
				goto reswitch;
			}
			break;

		case 'L': /* long double */
			if (flagc & FLAGC_LENMOD)
			{
				/* conflict with other length modifier */
				goto invalid_format;
			}
			flagc |= FLAGC_LENMOD;
			lm_flag |= LF_LD;
			goto reswitch;

		case 'Q': /* __float128 */
			if (flagc & FLAGC_LENMOD)
			{
				/* conflict with other length modifier */
				goto invalid_format;
			}
			flagc |= FLAGC_LENMOD;
			lm_flag |= LF_QD;
			goto reswitch;
		/* end of length modifiers */

		case 'n': /* number of characters printed so far */
			if (lm_flag & LF_J) /* j */
				*(va_arg(ap, hio_intmax_t*)) = fmtout->count;
			else if (lm_flag & LF_Z) /* z */
				*(va_arg(ap, hio_ooi_t*)) = fmtout->count;
		#if (HIO_SIZEOF_LONG_LONG > 0)
			else if (lm_flag & LF_Q) /* ll */
				*(va_arg(ap, long long int*)) = fmtout->count;
		#endif
			else if (lm_flag & LF_L) /* l */
				*(va_arg(ap, long int*)) = fmtout->count;
			else if (lm_flag & LF_H) /* h */
				*(va_arg(ap, short int*)) = fmtout->count;
			else if (lm_flag & LF_C) /* hh */
				*(va_arg(ap, char*)) = fmtout->count;
			else if (flagc & FLAGC_LENMOD)
				goto invalid_format;
			else
				*(va_arg(ap, int*)) = fmtout->count;
			break;

		/* signed integer conversions */
		case 'd':
		case 'i': /* signed conversion */
			base = 10;
			sign = 1;
			goto handle_sign;
		/* end of signed integer conversions */

		/* unsigned integer conversions */
		case 'o':
			base = 8;
			goto handle_nosign;
		case 'u':
			base = 10;
			goto handle_nosign;
		case 'X':
			sprintn = sprintn_upper;
		case 'x':
			base = 16;
			goto handle_nosign;
		case 'b':
			base = 2;
			goto handle_nosign;
		/* end of unsigned integer conversions */

		case 'p': /* pointer */
			base = 16;

			if (width == 0) flagc |= FLAGC_SHARP;
			else flagc &= ~FLAGC_SHARP;

			num = (hio_uintptr_t)va_arg(ap, void*);
			goto number;

		case 'c':
		{
			/* zeropad must not take effect for 'c' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_L) goto uppercase_c;
		#if defined(HIO_OOCH_IS_UCH)
			if (lm_flag & LF_J) goto uppercase_c;
		#endif
		lowercase_c:
			bch = HIO_SIZEOF(hio_bch_t) < HIO_SIZEOF(int)? va_arg(ap, int): va_arg(ap, hio_bch_t);

		print_lowercase_c:
			/* precision 0 doesn't kill the letter */
			width--;
			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_BCH (fmtout, padc, width);
			PUT_BCH (fmtout, bch, 1);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_BCH (fmtout, padc, width);
			break;
		}

		case 'C':
		{
			/* zeropad must not take effect for 'C' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_H) goto lowercase_c;
		#if defined(HIO_OOCH_IS_BCH)
			if (lm_flag & LF_J) goto lowercase_c;
		#endif
		uppercase_c:
			uch = HIO_SIZEOF(hio_uch_t) < HIO_SIZEOF(int)? va_arg(ap, int): va_arg(ap, hio_uch_t);

			/* precision 0 doesn't kill the letter */
			width--;
			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_UCH (fmtout, padc, width);
			PUT_UCH (fmtout, uch, 1);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_UCH (fmtout, padc, width);
			break;
		}

		case 's':
		{
			const hio_bch_t* bsp;

			/* zeropad must not take effect for 'S' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_L) goto uppercase_s;
		#if defined(HIO_OOCH_IS_UCH)
			if (lm_flag & LF_J) goto uppercase_s;
		#endif
		lowercase_s:
			bsp = va_arg(ap, hio_bch_t*);
			if (!bsp) bsp = bch_nullstr;

			n = 0;
			if (flagc & FLAGC_DOT)
			{
				while (n < precision && bsp[n]) n++;
			}
			else
			{
				while (bsp[n]) n++;
			}

			width -= n;

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_BCH (fmtout, padc, width);
			PUT_BCS (fmtout, bsp, n);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_BCH (fmtout, padc, width);
			break;
		}

		case 'S':
		{
			const hio_uch_t* usp;

			/* zeropad must not take effect for 's' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_H) goto lowercase_s;
		#if defined(HIO_OOCH_IS_BCH)
			if (lm_flag & LF_J) goto lowercase_s;
		#endif
		uppercase_s:
			usp = va_arg(ap, hio_uch_t*);
			if (!usp) usp = uch_nullstr;

			n = 0;
			if (flagc & FLAGC_DOT)
			{
				while (n < precision && usp[n]) n++;
			}
			else
			{
				while (usp[n]) n++;
			}

			width -= n;

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_UCH (fmtout, padc, width);
			PUT_UCS (fmtout, usp, n);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_UCH (fmtout, padc, width);

			break;
		}

		case 'k':
		case 'K':
		{
			/* byte or multibyte character string in escape sequence */
			const hio_uint8_t* bsp;
			hio_oow_t k_hex_width;

			/* zeropad must not take effect for 'k' and 'K'
			 *
 			 * 'h' & 'l' is not used to differentiate hio_bch_t and hio_uch_t
			 * because 'k' means hio_byte_t.
			 * 'l', results in uppercase hexadecimal letters.
			 * 'h' drops the leading \x in the output
			 * --------------------------------------------------------
			 * hk -> \x + non-printable in lowercase hex
			 * k -> all in lowercase hex
			 * lk -> \x +  all in lowercase hex
			 * --------------------------------------------------------
			 * hK -> \x + non-printable in uppercase hex
			 * K -> all in uppercase hex
			 * lK -> \x +  all in uppercase hex
			 * --------------------------------------------------------
			 * with 'k' or 'K', i don't substitute "(null)" for the NULL pointer
			 */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';

			bsp = va_arg(ap, hio_uint8_t*);
			k_hex_width = (lm_flag & (LF_H | LF_L))? 4: 2;

			if (lm_flag& LF_H)
			{
				if (flagc & FLAGC_DOT)
				{
					/* if precision is specifed, it doesn't stop at the value of zero unlike 's' or 'S' */
					for (n = 0; n < precision; n++) width -= BYTE_PRINTABLE(bsp[n])? 1: k_hex_width;
				}
				else
				{
					for (n = 0; bsp[n]; n++) width -= BYTE_PRINTABLE(bsp[n])? 1: k_hex_width;
				}
			}
			else
			{
				if (flagc & FLAGC_DOT)
				{
					/* if precision is specifed, it doesn't stop at the value of zero unlike 's' or 'S' */
					n = precision;
				}
				else
				{
					for (n = 0; bsp[n]; n++) /* nothing */;
				}
				width -= (n * k_hex_width);
			}

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (fmtout, padc, width);

			while (n--)
			{
				if ((lm_flag & LF_H) && BYTE_PRINTABLE(*bsp))
				{
					PUT_BCH (fmtout, *bsp, 1);
				}
				else
				{
					hio_bch_t xbuf[3];
					hio_byte_to_bcstr (*bsp, xbuf, HIO_COUNTOF(xbuf), (16 | (uch == 'k'? HIO_BYTE_TO_BCSTR_LOWERCASE: 0)), '0');
					if (lm_flag & (LF_H | LF_L)) PUT_BCS (fmtout, "\\x", 2);
					PUT_BCS (fmtout, xbuf, 2);
				}
				bsp++;
			}

			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (fmtout, padc, width);
			break;
		}

		case 'w':
		case 'W':
		{
			/* unicode string in unicode escape sequence.
			 *
			 * hw -> \uXXXX, \UXXXXXXXX, printable-byte(only in ascii range)
			 * w -> \uXXXX, \UXXXXXXXX
			 * lw -> all in \UXXXXXXXX
			 */
			const hio_uch_t* usp;
			hio_oow_t uwid;

			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			usp = va_arg(ap, hio_uch_t*);

			if (flagc & FLAGC_DOT)
			{
				/* if precision is specifed, it doesn't stop at the value of zero unlike 's' or 'S' */
				for (n = 0; n < precision; n++)
				{
					if ((lm_flag & LF_H) && BYTE_PRINTABLE(usp[n])) uwid = 1;
					else if (!(lm_flag & LF_L) && usp[n] <= 0xFFFF) uwid = 6;
					else uwid = 10;
					width -= uwid;
				}
			}
			else
			{
				for (n = 0; usp[n]; n++)
				{
					if ((lm_flag & LF_H) && BYTE_PRINTABLE(usp[n])) uwid = 1;
					else if (!(lm_flag & LF_L) && usp[n] <= 0xFFFF) uwid = 6;
					else uwid = 10;
					width -= uwid;
				}
			}

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (fmtout, padc, width);

			while (n--)
			{
				if ((lm_flag & LF_H) && BYTE_PRINTABLE(*usp))
				{
					PUT_OOCH(fmtout, *usp, 1);
				}
				else if (!(lm_flag & LF_L) && *usp <= 0xFFFF)
				{
					hio_uint16_t u16 = *usp;
					int extra_flags = ((uch) == 'w'? HIO_BYTE_TO_BCSTR_LOWERCASE: 0);
					PUT_BCS(fmtout, "\\u", 2);
					PUT_BYTE_IN_HEX(fmtout, (u16 >> 8) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX(fmtout, u16 & 0xFF, extra_flags);
				}
				else
				{
					hio_uint32_t u32 = *usp;
					int extra_flags = ((uch) == 'w'? HIO_BYTE_TO_BCSTR_LOWERCASE: 0);
					PUT_BCS(fmtout, "\\u", 2);
					PUT_BYTE_IN_HEX(fmtout, (u32 >> 24) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX(fmtout, (u32 >> 16) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX(fmtout, (u32 >> 8) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX(fmtout, u32 & 0xFF, extra_flags);
				}
				usp++;
			}

			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (fmtout, padc, width);
			break;
		}

#if 0
		case 'O': /* object - ignore precision, width, adjustment */
			if (!fmtout->putobj) goto invalid_format;
			if (fmtout->putobj(fmtout, va_arg(ap, hio_oop_t)) <= -1) goto oops;
			break;
#endif

#if defined(HIO_ENABLE_FLTFMT)
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
		/*
		case 'a':
		case 'A':
		*/
		{
			/* let me rely on snprintf until i implement float-point to string conversion */
			int q;
			hio_oow_t fmtlen;
			union
			{
			#if (HIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF)
				__float128 qd;
			#endif
				long double ld;
				double d;
			} v;
			int dtype = 0;
			hio_oow_t newcapa;
			hio_bch_t* bsp;

			if (lm_flag & LF_J)
			{
			#if (HIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF) && (HIO_SIZEOF_FLTMAX_T == HIO_SIZEOF___FLOAT128)
				v.qd = va_arg(ap, hio_fltmax_t);
				dtype = LF_QD;
			#elif HIO_SIZEOF_FLTMAX_T == HIO_SIZEOF_DOUBLE
				v.d = va_arg(ap, hio_fltmax_t);
			#elif HIO_SIZEOF_FLTMAX_T == HIO_SIZEOF_LONG_DOUBLE
				v.ld = va_arg(ap, hio_fltmax_t);
				dtype = LF_LD;
			#else
				#error Unsupported hio_flt_t
			#endif
			}
			else if (lm_flag & LF_Z)
			{
				/* hio_flt_t is limited to double or long double */

				/* precedence goes to double if sizeof(double) == sizeof(long double)
				 * for example, %Lf didn't work on some old platforms.
				 * so i prefer the format specifier with no modifier.
				 */
			#if HIO_SIZEOF_FLT_T == HIO_SIZEOF_DOUBLE
				v.d = va_arg(ap, hio_flt_t);
			#elif HIO_SIZEOF_FLT_T == HIO_SIZEOF_LONG_DOUBLE
				v.ld = va_arg(ap, hio_flt_t);
				dtype = LF_LD;
			#else
				#error Unsupported hio_flt_t
			#endif
			}
			else if (lm_flag & (LF_LD | LF_L))
			{
				v.ld = va_arg(ap, long double);
				dtype = LF_LD;
			}
		#if (HIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF)
			else if (lm_flag & (LF_QD | LF_Q))
			{
				v.qd = va_arg(ap, __float128);
				dtype = LF_QD;
			}
		#endif
			else if (flagc & FLAGC_LENMOD)
			{
				goto invalid_format;
			}
			else
			{
				v.d = va_arg(ap, double);
			}

			fmtlen = fmtptr - percent;
			if (fmtlen > fb.fmt.capa)
			{
				if (fb.fmt.ptr == fb.fmt.sbuf)
				{
					fb.fmt.ptr = (hio_bch_t*)HIO_MMGR_ALLOC(fmtout->mmgr, HIO_SIZEOF(*fb.fmt.ptr) * (fmtlen + 1));
					if (!fb.fmt.ptr) goto oops;
				}
				else
				{
					hio_bch_t* tmpptr;

					tmpptr = (hio_bch_t*)HIO_MMGR_REALLOC(fmtout->mmgr, fb.fmt.ptr, HIO_SIZEOF(*fb.fmt.ptr) * (fmtlen + 1));
					if (!tmpptr) goto oops;
					fb.fmt.ptr = tmpptr;
				}

				fb.fmt.capa = fmtlen;
			}

			/* compose back the format specifier */
			fmtlen = 0;
			fb.fmt.ptr[fmtlen++] = '%';
			if (flagc & FLAGC_SPACE) fb.fmt.ptr[fmtlen++] = ' ';
			if (flagc & FLAGC_SHARP) fb.fmt.ptr[fmtlen++] = '#';
			if (flagc & FLAGC_SIGN) fb.fmt.ptr[fmtlen++] = '+';
			if (flagc & FLAGC_LEFTADJ) fb.fmt.ptr[fmtlen++] = '-';
			if (flagc & FLAGC_ZEROPAD) fb.fmt.ptr[fmtlen++] = '0';

			if (flagc & FLAGC_STAR1) fb.fmt.ptr[fmtlen++] = '*';
			else if (flagc & FLAGC_WIDTH)
			{
				fmtlen += hio_fmt_uintmax_to_bcstr(
					&fb.fmt.ptr[fmtlen], fb.fmt.capa - fmtlen,
					width, 10, -1, '\0', HIO_NULL);
			}
			if (flagc & FLAGC_DOT) fb.fmt.ptr[fmtlen++] = '.';
			if (flagc & FLAGC_STAR2) fb.fmt.ptr[fmtlen++] = '*';
			else if (flagc & FLAGC_PRECISION)
			{
				fmtlen += hio_fmt_uintmax_to_bcstr(
					&fb.fmt.ptr[fmtlen], fb.fmt.capa - fmtlen,
					precision, 10, -1, '\0', HIO_NULL);
			}

			if (dtype == LF_LD)
				fb.fmt.ptr[fmtlen++] = 'L';
		#if (HIO_SIZEOF___FLOAT128 > 0)
			else if (dtype == LF_QD)
				fb.fmt.ptr[fmtlen++] = 'Q';
		#endif

			fb.fmt.ptr[fmtlen++] = uch;
			fb.fmt.ptr[fmtlen] = '\0';

		#if defined(HAVE_SNPRINTF)
			/* nothing special here */
		#else
			/* best effort to avoid buffer overflow when no snprintf is available.
			 * i really can't do much if it happens. */
			newcapa = precision + width + 32;
			if (fb.out.capa < newcapa)
			{
				/*HIO_ASSERT (hio, fb.out.ptr == fb.out.sbuf);*/
				fb.out.ptr = HIO_MMGR_ALLOC(fmtout->mmgr, HIO_SIZEOF(hio_bch_t) * (newcapa + 1));
				if (!fb.out.ptr) goto oops;
				fb.out.capa = newcapa;
			}
		#endif

			while (1)
			{
				if (dtype == LF_LD)
				{
				#if defined(HAVE_SNPRINTF)
					q = snprintf((hio_bch_t*)fb.out.ptr, fb.out.capa + 1, fb.fmt.ptr, v.ld);
				#else
					q = sprintf((hio_bch_t*)fb.out.ptr, fb.fmt.ptr, v.ld);
				#endif
				}
			#if (HIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF)
				else if (dtype == LF_QD)
				{
					q = quadmath_snprintf((hio_bch_t*)fb.out.ptr, fb.out.capa + 1, fb.fmt.ptr, v.qd);
				}
			#endif
				else
				{
				#if defined(HAVE_SNPRINTF)
					q = snprintf((hio_bch_t*)fb.out.ptr, fb.out.capa + 1, fb.fmt.ptr, v.d);
				#else
					q = sprintf((hio_bch_t*)fb.out.ptr, fb.fmt.ptr, v.d);
				#endif
				}
				if (q <= -1) goto oops;
				if (q <= fb.out.capa) break;

				newcapa = fb.out.capa * 2;
				if (newcapa < q) newcapa = q;

				if (fb.out.ptr == fb.out.sbuf)
				{
					fb.out.ptr = (hio_bch_t*)HIO_MMGR_ALLOC(fmtout->mmgr, HIO_SIZEOF(hio_bch_t) * (newcapa + 1));
					if (!fb.out.ptr) goto oops;
				}
				else
				{
					hio_bch_t* tmpptr;
					tmpptr = (hio_bch_t*)HIO_MMGR_REALLOC(fmtout->mmgr, fb.out.ptr, HIO_SIZEOF(hio_bch_t) * (newcapa + 1));
					if (!tmpptr) goto oops;
					fb.out.ptr = tmpptr;
				}
				fb.out.capa = newcapa;
			}

			bsp = fb.out.ptr;
			n = 0; while (bsp[n] != '\0') n++;
			PUT_BCS (fmtout, bsp, n);
			break;
		}
#endif

		handle_nosign:
			sign = 0;
			if (lm_flag & LF_J)
			{
			#if 1 && !defined(__clang__) && defined(__GNUC__) && \
			    (HIO_SIZEOF_UINTMAX_T > HIO_SIZEOF_OOW_T) && \
			    (HIO_SIZEOF_UINTMAX_T != HIO_SIZEOF_LONG_LONG) && \
			    (HIO_SIZEOF_UINTMAX_T != HIO_SIZEOF_LONG)
				/* GCC-compiled binaries crashed when getting hio_uintmax_t with va_arg.
				 * This is just a work-around for it */
				int i;
				for (i = 0, num = 0; i < HIO_SIZEOF(hio_uintmax_t) / HIO_SIZEOF(hio_oow_t); i++)
				{
				#if defined(HIO_ENDIAN_BIG)
					num = num << (8 * HIO_SIZEOF(hio_oow_t)) | (va_arg(ap, hio_oow_t));
				#else
					int shift = i * HIO_SIZEOF(hio_oow_t);
					hio_oow_t x = va_arg(ap, hio_oow_t);
					num |= (hio_uintmax_t)x << (shift * HIO_BITS_PER_BYTE);
				#endif
				}
			#else
				num = va_arg(ap, hio_uintmax_t);
			#endif
			}
			else if (lm_flag & LF_T)
				num = va_arg(ap, hio_intptr_t/*hio_ptrdiff_t*/);
			else if (lm_flag & LF_Z)
				num = va_arg(ap, hio_oow_t);
			#if (HIO_SIZEOF_LONG_LONG > 0)
			else if (lm_flag & LF_Q)
				num = va_arg(ap, unsigned long long int);
			#endif
			else if (lm_flag & (LF_L | LF_LD))
				num = va_arg(ap, unsigned long int);
			else if (lm_flag & LF_H)
				num = (unsigned short int)va_arg(ap, int);
			else if (lm_flag & LF_C)
				num = (unsigned char)va_arg(ap, int);
			else
				num = va_arg(ap, unsigned int);
			goto number;

		handle_sign:
			if (lm_flag & LF_J)
			{
			#if 1 && !defined(__clang__) && defined(__GNUC__) && \
			    (HIO_SIZEOF_INTMAX_T > HIO_SIZEOF_OOI_T) && \
			    (HIO_SIZEOF_UINTMAX_T != HIO_SIZEOF_LONG_LONG) && \
			    (HIO_SIZEOF_UINTMAX_T != HIO_SIZEOF_LONG)
				/* GCC-compiled binraries crashed when getting hio_uintmax_t with va_arg.
				 * This is just a work-around for it */
				int i;
				for (i = 0, num = 0; i < HIO_SIZEOF(hio_intmax_t) / HIO_SIZEOF(hio_oow_t); i++)
				{
				#if defined(HIO_ENDIAN_BIG)
					num = num << (8 * HIO_SIZEOF(hio_oow_t)) | (va_arg(ap, hio_oow_t));
				#else
					int shift = i * HIO_SIZEOF(hio_oow_t);
					hio_oow_t x = va_arg(ap, hio_oow_t);
					num |= (hio_uintmax_t)x << (shift * HIO_BITS_PER_BYTE);
				#endif
				}
			#else
				num = va_arg(ap, hio_intmax_t);
			#endif
			}

			else if (lm_flag & LF_T)
				num = va_arg(ap, hio_intptr_t/*hio_ptrdiff_t*/);
			else if (lm_flag & LF_Z)
				num = va_arg(ap, hio_ooi_t);
			#if (HIO_SIZEOF_LONG_LONG > 0)
			else if (lm_flag & LF_Q)
				num = va_arg(ap, long long int);
			#endif
			else if (lm_flag & (LF_L | LF_LD))
				num = va_arg(ap, long int);
			else if (lm_flag & LF_H)
				num = (short int)va_arg(ap, int);
			else if (lm_flag & LF_C)
				num = (char)va_arg(ap, int);
			else
				num = va_arg(ap, int);

		number:
			if (sign && (hio_intmax_t)num < 0)
			{
				neg = 1;
				num = -(hio_intmax_t)num;
			}

			nbufp = sprintn(nbuf, num, base, &tmp);
			if ((flagc & FLAGC_SHARP) && num != 0)
			{
				if (base == 2 || base == 16) tmp += 2;
				else if (base == 8) tmp += 1;
			}
			if (neg) tmp++;
			else if (flagc & FLAGC_SIGN) tmp++;
			else if (flagc & FLAGC_SPACE) tmp++;

			numlen = (int)((const hio_bch_t*)nbufp - (const hio_bch_t*)nbuf);
			if ((flagc & FLAGC_DOT) && precision > numlen)
			{
				/* extra zeros for precision specified */
				tmp += (precision - numlen);
			}

			if (!(flagc & FLAGC_LEFTADJ) && !(flagc & FLAGC_ZEROPAD) && width > 0 && (width -= tmp) > 0)
			{
				PUT_OOCH (fmtout, padc, width);
				width = 0;
			}

			if (neg) PUT_OOCH (fmtout, '-', 1);
			else if (flagc & FLAGC_SIGN) PUT_OOCH (fmtout, '+', 1);
			else if (flagc & FLAGC_SPACE) PUT_OOCH (fmtout, ' ', 1);

			if ((flagc & FLAGC_SHARP) && num != 0)
			{
				if (base == 2)
				{
					PUT_OOCH (fmtout, '0', 1);
					PUT_OOCH (fmtout, 'b', 1);
				}
				if (base == 8)
				{
					PUT_OOCH (fmtout, '0', 1);
				}
				else if (base == 16)
				{
					PUT_OOCH (fmtout, '0', 1);
					PUT_OOCH (fmtout, 'x', 1);
				}
			}

			if ((flagc & FLAGC_DOT) && precision > numlen)
			{
				/* extra zeros for precision specified */
				PUT_OOCH (fmtout, '0', precision - numlen);
			}

			if (!(flagc & FLAGC_LEFTADJ) && width > 0 && (width -= tmp) > 0)
			{
				PUT_OOCH (fmtout, padc, width);
			}

			while (*nbufp) PUT_OOCH (fmtout, *nbufp--, 1); /* output actual digits */

			if ((flagc & FLAGC_LEFTADJ) && width > 0 && (width -= tmp) > 0)
			{
				PUT_OOCH (fmtout, padc, width);
			}
			break;

		invalid_format:
			switch (fmtout->fmt_type)
			{
				case HIO_FMTOUT_FMT_TYPE_BCH:
					PUT_BCS (fmtout, (const hio_bch_t*)percent, (fmtptr - percent) / fmtchsz);
					break;
				case HIO_FMTOUT_FMT_TYPE_UCH:
					PUT_UCS (fmtout, (const hio_uch_t*)percent, (fmtptr - percent) / fmtchsz);
					break;
			}
			break;

		default:
			switch (fmtout->fmt_type)
			{
				case HIO_FMTOUT_FMT_TYPE_BCH:
					PUT_BCS (fmtout, (const hio_bch_t*)percent, (fmtptr - percent) / fmtchsz);
					break;
				case HIO_FMTOUT_FMT_TYPE_UCH:
					PUT_UCS (fmtout, (const hio_uch_t*)percent, (fmtptr - percent) / fmtchsz);
					break;
			}
			/*
			 * Since we ignore an formatting argument it is no
			 * longer safe to obey the remaining formatting
			 * arguments as the arguments will no longer match
			 * the format specs.
			 */
			stop = 1;
			break;
		}
	}

done:
#if defined(HIO_ENABLE_FLTFMT)
	if (fb.fmt.ptr != fb.fmt.sbuf) HIO_MMGR_FREE (fmtout->mmgr, fb.fmt.ptr);
	if (fb.out.ptr != fb.out.sbuf) HIO_MMGR_FREE (fmtout->mmgr, fb.out.ptr);
#endif
	return 0;

oops:
#if defined(HIO_ENABLE_FLTFMT)
	if (fb.fmt.ptr != fb.fmt.sbuf) HIO_MMGR_FREE (fmtout->mmgr, fb.fmt.ptr);
	if (fb.out.ptr != fb.out.sbuf) HIO_MMGR_FREE (fmtout->mmgr, fb.out.ptr);
#endif
	return -1;
}

int hio_bfmt_outv (hio_fmtout_t* fmtout, const hio_bch_t* fmt, va_list ap)
{
	int n;
	const void* fmt_str;
	hio_fmtout_fmt_type_t fmt_type;

	fmt_str = fmtout->fmt_str;
	fmt_type = fmtout->fmt_type;

	fmtout->fmt_type = HIO_FMTOUT_FMT_TYPE_BCH;
	fmtout->fmt_str = fmt;

	n = fmt_outv(fmtout, ap);

	fmtout->fmt_str = fmt_str;
	fmtout->fmt_type = fmt_type;
	return n;
}

int hio_ufmt_outv (hio_fmtout_t* fmtout, const hio_uch_t* fmt, va_list ap)
{
	int n;
	const void* fmt_str;
	hio_fmtout_fmt_type_t fmt_type;

	fmt_str = fmtout->fmt_str;
	fmt_type = fmtout->fmt_type;

	fmtout->fmt_type = HIO_FMTOUT_FMT_TYPE_UCH;
	fmtout->fmt_str = fmt;

	n = fmt_outv(fmtout, ap);

	fmtout->fmt_str = fmt_str;
	fmtout->fmt_type = fmt_type;
	return n;
}

int hio_bfmt_out (hio_fmtout_t* fmtout, const hio_bch_t* fmt, ...)
{
	va_list ap;
	int n;
	const void* fmt_str;
	hio_fmtout_fmt_type_t fmt_type;

	fmt_str = fmtout->fmt_str;
	fmt_type = fmtout->fmt_type;

	fmtout->fmt_type = HIO_FMTOUT_FMT_TYPE_BCH;
	fmtout->fmt_str = fmt;

	va_start (ap, fmt);
	n = fmt_outv(fmtout, ap);
	va_end (ap);

	fmtout->fmt_str = fmt_str;
	fmtout->fmt_type = fmt_type;
	return n;
}

int hio_ufmt_out (hio_fmtout_t* fmtout, const hio_uch_t* fmt, ...)
{
	va_list ap;
	int n;
	const void* fmt_str;
	hio_fmtout_fmt_type_t fmt_type;

	fmt_str = fmtout->fmt_str;
	fmt_type = fmtout->fmt_type;

	fmtout->fmt_type = HIO_FMTOUT_FMT_TYPE_UCH;
	fmtout->fmt_str = fmt;

	va_start (ap, fmt);
	n = fmt_outv(fmtout, ap);
	va_end (ap);

	fmtout->fmt_str = fmt_str;
	fmtout->fmt_type = fmt_type;
	return n;
}

/* --------------------------------------------------------------------------
 * FORMATTED LOG OUTPUT
 * -------------------------------------------------------------------------- */

static int log_oocs (hio_fmtout_t* fmtout, const hio_ooch_t* ptr, hio_oow_t len)
{
	hio_t* hio = (hio_t*)fmtout->ctx;
	hio_oow_t rem;

	if (len <= 0) return 1;

	if (hio->log.len > 0 && hio->log.last_mask != fmtout->mask)
	{
		/* the mask has changed. commit the buffered text */
/* TODO: HANDLE LINE ENDING CONVENTION BETTER... */
		if (hio->log.ptr[hio->log.len - 1] != HIO_EOL)
		{
			/* no line ending - append a line terminator */
			hio->log.ptr[hio->log.len++] = HIO_EOL;
		}

		HIO_SYS_WRITE_LOG (hio, hio->log.last_mask, hio->log.ptr, hio->log.len);
		hio->log.len = 0;
	}

redo:
	rem = 0;
	if (len > hio->log.capa - hio->log.len)
	{
		hio_oow_t newcapa, max;
		hio_ooch_t* tmp;

		max = HIO_TYPE_MAX(hio_oow_t) - hio->log.len;
		if (len > max)
		{
			/* data too big. */
			rem += len - max;
			len = max;
		}

		newcapa = HIO_ALIGN_POW2(hio->log.len + len, 512); /* TODO: adjust this capacity */
		if (newcapa > hio->option.log_maxcapa)
		{
			/* [NOTE]
			 * it doesn't adjust newcapa to hio->option.log_maxcapa.
			 * nor does it cut the input to fit it into the adjusted capacity.
			 * if maxcapa set is not aligned to HIO_LOG_CAPA_ALIGN,
			 * the largest buffer capacity may be suboptimal */
			goto make_do;
		}

		/* +1 to handle line ending injection more easily */
		tmp = hio_reallocmem(hio, hio->log.ptr, (newcapa + 1) * HIO_SIZEOF(*tmp));
		if (!tmp)
		{
		make_do:
			if (hio->log.len > 0)
			{
				/* can't expand the buffer. just flush the existing contents */
				/* TODO: HANDLE LINE ENDING CONVENTION BETTER... */
				if (hio->log.ptr[hio->log.len - 1] != HIO_EOL)
				{
					/* no line ending - append a line terminator */
					hio->log.ptr[hio->log.len++] = HIO_EOL;
				}
				HIO_SYS_WRITE_LOG (hio, hio->log.last_mask, hio->log.ptr, hio->log.len);
				hio->log.len = 0;
			}

			if (len > hio->log.capa)
			{
				rem += len - hio->log.capa;
				len = hio->log.capa;
			}
		}
		else
		{
			hio->log.ptr = tmp;
			hio->log.capa = newcapa;
		}
	}

	HIO_MEMCPY (&hio->log.ptr[hio->log.len], ptr, len * HIO_SIZEOF(*ptr));
	hio->log.len += len;
	hio->log.last_mask = fmtout->mask;

	if (rem > 0)
	{
		ptr += len;
		len = rem;
		goto redo;
	}

	return 1; /* success */
}

#if defined(HIO_OOCH_IS_BCH)
#define log_bcs log_oocs

static int log_ucs (hio_fmtout_t* fmtout, const hio_uch_t* ptr, hio_oow_t len)
{
	hio_t* hio = (hio_t*)fmtout->ctx;
	hio_bch_t bcs[128];
	hio_oow_t bcslen, rem;

	rem = len;
	while (rem > 0)
	{
		len = rem;
		bcslen = HIO_COUNTOF(bcs);
		hio_conv_uchars_to_bchars_with_cmgr(ptr, &len, bcs, &bcslen, hio_getcmgr(hio));
		log_bcs(fmtout, bcs, bcslen);
		rem -= len;
		ptr += len;
	}
	return 1;
}


#else

#define log_ucs log_oocs

static int log_bcs (hio_fmtout_t* fmtout, const hio_bch_t* ptr, hio_oow_t len)
{
	hio_t* hio = (hio_t*)fmtout->ctx;
	hio_uch_t ucs[64];
	hio_oow_t ucslen, rem;

	rem = len;
	while (rem > 0)
	{
		len = rem;
		ucslen = HIO_COUNTOF(ucs);
		hio_conv_bchars_to_uchars_with_cmgr(ptr, &len, ucs, &ucslen, hio_getcmgr(hio), 1);
		log_ucs(fmtout, ucs, ucslen);
		rem -= len;
		ptr += len;
	}
	return 1;
}

#endif

hio_ooi_t hio_logbfmtv (hio_t* hio, hio_bitmask_t mask, const hio_bch_t* fmt, va_list ap)
{
	int x;
	hio_fmtout_t fo;

	/* there may internal log calls even if this feature is disabled.
	 * the explicit check is required */
	if (HIO_UNLIKELY(!(hio->_features & HIO_FEATURE_LOG))) return -1;

	if (hio->log.default_type_mask & HIO_LOG_ALL_TYPES)
	{
		/* if a type is given, it's not untyped any more.
		 * mask off the UNTYPED bit */
		mask &= ~HIO_LOG_UNTYPED;

		/* if the default_type_mask has the UNTYPED bit on,
		 * it'll get turned back on */
		mask |= (hio->log.default_type_mask & HIO_LOG_ALL_TYPES);
	}
	else if (!(mask & HIO_LOG_ALL_TYPES))
	{
		/* no type is set in the given mask and no default type is set.
		 * make it UNTYPED. */
		mask |= HIO_LOG_UNTYPED;
	}

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.fmt_type = HIO_FMTOUT_FMT_TYPE_BCH;
	fo.fmt_str = fmt;
	fo.ctx = hio;
	fo.mask = mask;
	fo.mmgr = hio_getmmgr(hio);
	fo.putbchars = log_bcs;
	fo.putuchars = log_ucs;

	if (hio->option.log_mask & HIO_LOG_GUARDED)
	{
		hio_sys_locklog (hio);
		x = fmt_outv(&fo, ap);
		if (hio->log.len > 0 && hio->log.ptr[hio->log.len - 1] == HIO_EOL)
		{
			HIO_SYS_WRITE_LOG (hio, hio->log.last_mask, hio->log.ptr, hio->log.len);
			hio->log.len = 0;
		}
		hio_sys_unlocklog (hio);
	}
	else
	{
		x = fmt_outv(&fo, ap);
		if (hio->log.len > 0 && hio->log.ptr[hio->log.len - 1] == HIO_EOL)
		{
			HIO_SYS_WRITE_LOG (hio, hio->log.last_mask, hio->log.ptr, hio->log.len);
			hio->log.len = 0;
		}
	}

	return (x <= -1)? -1: fo.count;
}

hio_ooi_t hio_logbfmt (hio_t* hio, hio_bitmask_t mask, const hio_bch_t* fmt, ...)
{
	hio_ooi_t x;
	va_list ap;

	va_start (ap, fmt);
	x = hio_logbfmtv(hio, mask, fmt, ap);
	va_end (ap);

	return x;
}

hio_ooi_t hio_logufmtv (hio_t* hio, hio_bitmask_t mask, const hio_uch_t* fmt, va_list ap)
{
	int x;
	hio_fmtout_t fo;

	/* there may internal log calls even if this feature is disabled.
	 * the explicit check is required */
	if (HIO_UNLIKELY(!(hio->_features & HIO_FEATURE_LOG))) return -1;

	if (hio->log.default_type_mask & HIO_LOG_ALL_TYPES)
	{
		/* if a type is given, it's not untyped any more.
		 * mask off the UNTYPED bit */
		mask &= ~HIO_LOG_UNTYPED;

		/* if the default_type_mask has the UNTYPED bit on,
		 * it'll get turned back on */
		mask |= (hio->log.default_type_mask & HIO_LOG_ALL_TYPES);
	}
	else if (!(mask & HIO_LOG_ALL_TYPES))
	{
		/* no type is set in the given mask and no default type is set.
		 * make it UNTYPED. */
		mask |= HIO_LOG_UNTYPED;
	}

	HIO_MEMSET (&fo, 0, HIO_SIZEOF(fo));
	fo.fmt_type = HIO_FMTOUT_FMT_TYPE_UCH;
	fo.fmt_str = fmt;
	fo.ctx = hio;
	fo.mask = mask;
	fo.mmgr = hio_getmmgr(hio);
	fo.putbchars = log_bcs;
	fo.putuchars = log_ucs;

	if (hio->option.log_mask & HIO_LOG_GUARDED)
	{
		hio_sys_locklog (hio);
		x = fmt_outv(&fo, ap);
		if (hio->log.len > 0 && hio->log.ptr[hio->log.len - 1] == HIO_EOL)
		{
			HIO_SYS_WRITE_LOG (hio, hio->log.last_mask, hio->log.ptr, hio->log.len);
			hio->log.len = 0;
		}
		hio_sys_unlocklog (hio);
	}
	else
	{
		x = fmt_outv(&fo, ap);
		if (hio->log.len > 0 && hio->log.ptr[hio->log.len - 1] == HIO_EOL)
		{
			HIO_SYS_WRITE_LOG (hio, hio->log.last_mask, hio->log.ptr, hio->log.len);
			hio->log.len = 0;
		}
	}

	return (x <= -1)? -1: fo.count;
}

hio_ooi_t hio_logufmt (hio_t* hio, hio_bitmask_t mask, const hio_uch_t* fmt, ...)
{
	hio_ooi_t x;
	va_list ap;

	va_start (ap, fmt);
	x = hio_logufmtv(hio, mask, fmt, ap);
	va_end (ap);

	return x;
}


/* ------------------------------------------------------------------------------------- */

/*define static int fmt_uintmax_to_bcstr(...)*/
#undef char_t
#undef fmt_uintmax
#define char_t hio_bch_t
#define fmt_uintmax fmt_uintmax_to_bcstr
#include "fmt-imp.h"

/*define static int fmt_uintmax_to_ucstr(...)*/
#undef char_t
#undef fmt_uintmax
#define char_t hio_uch_t
#define fmt_uintmax fmt_uintmax_to_ucstr
#include "fmt-imp.h"

/* ------------------------------------------------------------------------------------- */

int hio_fmt_intmax_to_bcstr (
	hio_bch_t* buf, int size,
	hio_intmax_t value, int base_and_flags, int prec,
	hio_bch_t fillchar, const hio_bch_t* prefix)
{
	hio_bch_t signchar;
	hio_uintmax_t absvalue;

	if (value < 0)
	{
		signchar = '-';
		absvalue = -value;
	}
	else if (base_and_flags & HIO_FMT_INTMAX_TO_BCSTR_PLUSSIGN)
	{
		signchar = '+';
		absvalue = value;
	}
	else if (base_and_flags & HIO_FMT_INTMAX_TO_BCSTR_EMPTYSIGN)
	{
		signchar = ' ';
		absvalue = value;
	}
	else
	{
		signchar = '\0';
		absvalue = value;
	}

	return fmt_uintmax_to_bcstr(buf, size, absvalue, base_and_flags, prec, fillchar, signchar, prefix);
}

int hio_fmt_uintmax_to_bcstr (
	hio_bch_t* buf, int size,
	hio_uintmax_t value, int base_and_flags, int prec,
	hio_bch_t fillchar, const hio_bch_t* prefix)
{
	hio_bch_t signchar;

	/* determine if a sign character is needed */
	if (base_and_flags & HIO_FMT_INTMAX_TO_BCSTR_PLUSSIGN)
	{
		signchar = '+';
	}
	else if (base_and_flags & HIO_FMT_INTMAX_TO_BCSTR_EMPTYSIGN)
	{
		signchar = ' ';
	}
	else
	{
		signchar = '\0';
	}

	return fmt_uintmax_to_bcstr(buf, size, value, base_and_flags, prec, fillchar, signchar, prefix);
}

/* ------------------------------------------------------------------------------------- */

int hio_fmt_intmax_to_ucstr (
	hio_uch_t* buf, int size,
	hio_intmax_t value, int base_and_flags, int prec,
	hio_uch_t fillchar, const hio_uch_t* prefix)
{
	hio_uch_t signchar;
	hio_uintmax_t absvalue;

	if (value < 0)
	{
		signchar = '-';
		absvalue = -value;
	}
	else if (base_and_flags & HIO_FMT_INTMAX_TO_UCSTR_PLUSSIGN)
	{
		signchar = '+';
		absvalue = value;
	}
	else if (base_and_flags & HIO_FMT_INTMAX_TO_UCSTR_EMPTYSIGN)
	{
		signchar = ' ';
		absvalue = value;
	}
	else
	{
		signchar = '\0';
		absvalue = value;
	}

	return fmt_uintmax_to_ucstr(buf, size, absvalue, base_and_flags, prec, fillchar, signchar, prefix);
}

int hio_fmt_uintmax_to_ucstr (
	hio_uch_t* buf, int size,
	hio_uintmax_t value, int base_and_flags, int prec,
	hio_uch_t fillchar, const hio_uch_t* prefix)
{
	hio_uch_t signchar;

	/* determine if a sign character is needed */
	if (base_and_flags & HIO_FMT_INTMAX_TO_UCSTR_PLUSSIGN)
	{
		signchar = '+';
	}
	else if (base_and_flags & HIO_FMT_INTMAX_TO_UCSTR_EMPTYSIGN)
	{
		signchar = ' ';
	}
	else
	{
		signchar = '\0';
	}

	return fmt_uintmax_to_ucstr(buf, size, value, base_and_flags, prec, fillchar, signchar, prefix);
}

