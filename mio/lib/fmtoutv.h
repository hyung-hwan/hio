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


/* NOTE: data output is aborted if the data limit is reached or 
 *       I/O error occurs  */

#undef PUT_OOCH
#undef PUT_OOCS
#undef PUT_BYTE_IN_HEX
#undef BYTE_PRINTABLE

#define PUT_OOCH(c,n) do { \
	if (n > 0) { \
		int xx; \
		if ((xx = data->putch(mio, data->mask, c, n)) <= -1) goto oops; \
		if (xx == 0) goto done; \
		data->count += n; \
	} \
} while (0)

#define PUT_OOCS(ptr,len) do { \
	if (len > 0) { \
		int xx; \
		if ((xx = data->putcs(mio, data->mask, ptr, len)) <= -1) goto oops; \
		if (xx == 0) goto done; \
		data->count += len; \
	} \
} while (0)

#define PUT_BYTE_IN_HEX(byte,extra_flags) do { \
	mio_bch_t __xbuf[3]; \
	mio_byte_to_bcstr ((byte), __xbuf, MIO_COUNTOF(__xbuf), (16 | (extra_flags)), '0'); \
	PUT_OOCH(__xbuf[0], 1); \
	PUT_OOCH(__xbuf[1], 1); \
} while (0)
 
/* TODO: redefine this */
#define BYTE_PRINTABLE(x) ((x >= 'a' && x <= 'z') || (x >= 'A' &&  x <= 'Z') || (x >= '0' && x <= '9') || (x == ' '))
 
static int fmtoutv (mio_t* mio, const fmtchar_t* fmt, mio_fmtout_data_t* data, va_list ap)
{
	const fmtchar_t* percent;
	const fmtchar_t* checkpoint;
	mio_bch_t nbuf[MAXNBUF], bch;
	const mio_bch_t* nbufp;
	int n, base, neg, sign;
	mio_ooi_t tmp, width, precision;
	mio_ooch_t ch, padc;
#if !defined(FMTCHAR_IS_OOCH)
	fmtchar_t fch;
#endif
	int lm_flag, lm_dflag, flagc, numlen;
	mio_uintmax_t num = 0;
	int stop = 0;

#if 0
	mio_bchbuf_t* fltfmt;
	mio_oochbuf_t* fltout;
#endif
	mio_bch_t* (*sprintn) (mio_bch_t* nbuf, mio_uintmax_t num, int base, mio_ooi_t* lenp);

	data->count = 0;

#if 0
	fltfmt = &mio->d->fltfmt;
	fltout = &mio->d->fltout;

	fltfmt->ptr  = fltfmt->buf;
	fltfmt->capa = MIO_COUNTOF(fltfmt->buf) - 1;

	fltout->ptr  = fltout->buf;
	fltout->capa = MIO_COUNTOF(fltout->buf) - 1;
#endif

	while (1)
	{
	#if defined(FMTCHAR_IS_OOCH)
		checkpoint = fmt;
		while ((ch = *fmt++) != '%' || stop) 
		{
			if (ch == '\0') 
			{
				PUT_OOCS (checkpoint, fmt - checkpoint - 1);
				goto done;
			}
		}
		PUT_OOCS (checkpoint, fmt - checkpoint - 1);
	#else
		#if defined(MIO_OOCH_IS_UCH)
		/* fmtchar is bch. ooch is uch. convert bch to uch */
		checkpoint = fmt;
		while ((fch = *fmt++) != '%' || stop) 
		{
			if (fch == '\0') break;
		}
		while (checkpoint < fmt - 1)
		{
			mio_oow_t cvlen, bclen;
			bclen = fmt - checkpoint - 1;
			cvlen = mio->cmgr->bctouc(checkpoint, bclen, &ch);
			if (cvlen == 0 || cvlen > bclen) goto oops;
			checkpoint += cvlen;
			PUT_OOCH (ch, 1);
		}
		if (fch == '\0') goto done;
		#else
		while ((fch = *fmt++) != '%' || stop) 
		{
			mio_bch_t bcsbuf[MIO_MBLEN_MAX + 1];
			mio_oow_t ucslen, bcslen;

			if (fch == '\0') goto done;

			/* fmtchar is uch. ooch is bch. convert uch to bch */
			ucslen = 1;
			bcslen = MIO_COUNTOF(bcsbuf);
			if (mio_conv_uchars_to_bchars_with_cmgr(&fch, &ucslen, bcsbuf, &bcslen, mio->cmgr) <= -1) goto oops;
			PUT_OOCS (bcsbuf, bcslen);
		}
		#endif
	#endif
		percent = fmt - 1;

		padc = ' '; 
		width = 0; precision = 0;
		neg = 0; sign = 0;

		lm_flag = 0; lm_dflag = 0; flagc = 0; 
		sprintn = sprintn_lower;

	reswitch:
		switch (ch = *fmt++) 
		{
		case '%': /* %% */
			bch = ch;
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

				precision = va_arg(ap, mio_ooi_t); /* this deviates from the standard printf that accepts 'int' */
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

				width = va_arg(ap, mio_ooi_t); /* it deviates from the standard printf that accepts 'int' */
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
			if (flagc & FLAGC_LENMOD) goto invalid_format;
			for (n = 0;; ++fmt) 
			{
				n = n * 10 + ch - '0';
				ch = *fmt;
				if (ch < '0' || ch > '9') break;
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

		/* length modifiers */
		case 'h': /* short int */
		case 'l': /* long int */
		case 'q': /* long long int */
		case 'j': /* mio_intmax_t/mio_uintmax_t */
		case 'z': /* mio_ooi_t/mio_oow_t */
		case 't': /* ptrdiff_t */
			if (lm_flag & (LF_LD | LF_QD)) goto invalid_format;

			flagc |= FLAGC_LENMOD;
			if (lm_dflag)
			{
				/* error */
				goto invalid_format;
			}
			else if (lm_flag)
			{
				if (lm_tab[ch - 'a'].dflag && lm_flag == lm_tab[ch - 'a'].flag)
				{
					lm_flag &= ~lm_tab[ch - 'a'].flag;
					lm_flag |= lm_tab[ch - 'a'].dflag;
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
				lm_flag |= lm_tab[ch - 'a'].flag;
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
				*(va_arg(ap, mio_intmax_t*)) = data->count;
			else if (lm_flag & LF_Z) /* z */
				*(va_arg(ap, mio_ooi_t*)) = data->count;
		#if (MIO_SIZEOF_LONG_LONG > 0)
			else if (lm_flag & LF_Q) /* ll */
				*(va_arg(ap, long long int*)) = data->count;
		#endif
			else if (lm_flag & LF_L) /* l */
				*(va_arg(ap, long int*)) = data->count;
			else if (lm_flag & LF_H) /* h */
				*(va_arg(ap, short int*)) = data->count;
			else if (lm_flag & LF_C) /* hh */
				*(va_arg(ap, char*)) = data->count;
			else if (flagc & FLAGC_LENMOD) 
				goto invalid_format;
			else
				*(va_arg(ap, int*)) = data->count;
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

			num = (mio_uintptr_t)va_arg(ap, void*);
			goto number;

		case 'c':
		{
			/* zeropad must not take effect for 'c' */
			if (flagc & FLAGC_ZEROPAD) padc = ' '; 
			if (lm_flag & LF_L) goto uppercase_c;
		#if defined(MIO_OOCH_IS_UCH)
			if (lm_flag & LF_J) goto uppercase_c;
		#endif
		lowercase_c:
			bch = MIO_SIZEOF(mio_bch_t) < MIO_SIZEOF(int)? va_arg(ap, int): va_arg(ap, mio_bch_t);

		print_lowercase_c:
			/* precision 0 doesn't kill the letter */
			width--;
			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			PUT_OOCH (bch, 1);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			break;
		}

		case 'C':
		{
			mio_uch_t ooch;

			/* zeropad must not take effect for 'C' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_H) goto lowercase_c;
		#if defined(MIO_OOCH_IS_BCH)
			if (lm_flag & LF_J) goto lowercase_c;
		#endif
		uppercase_c:
			ooch = MIO_SIZEOF(mio_uch_t) < MIO_SIZEOF(int)? va_arg(ap, int): va_arg(ap, mio_uch_t);

			/* precision 0 doesn't kill the letter */
			width--;
			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			PUT_OOCH (ooch, 1);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			break;
		}

		case 's':
		{
			const mio_bch_t* bsp;
			mio_oow_t bslen, slen;

			/* zeropad must not take effect for 'S' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_L) goto uppercase_s;
		#if defined(MIO_OOCH_IS_UCH)
			if (lm_flag & LF_J) goto uppercase_s;
		#endif
		lowercase_s:

			bsp = va_arg(ap, mio_bch_t*);
			if (bsp == MIO_NULL) bsp = bch_nullstr;

		#if defined(MIO_OOCH_IS_UCH)
			/* get the length */
			if (flagc & FLAGC_DOT)
			{
				for (bslen = 0; bslen < precision && bsp[bslen]; bslen++);
			}
			else
			{
				for (bslen = 0; bsp[bslen]; bslen++);
			}

			if (mio_conv_bchars_to_uchars_with_cmgr(bsp, &bslen, MIO_NULL, &slen, mio->cmgr, 0) <= -1) goto oops;

			/* slen holds the length after conversion */
			n = slen;
			if ((flagc & FLAGC_DOT) && precision < slen) n = precision;
			width -= n;

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);

			{
				mio_ooch_t conv_buf[32]; 
				mio_oow_t conv_len, src_len, tot_len = 0;
				while (n > 0)
				{
					MIO_ASSERT (mio, bslen > tot_len);

					src_len = bslen - tot_len;
					conv_len = MIO_COUNTOF(conv_buf);

					/* this must not fail since the dry-run above was successful */
					mio_conv_bchars_to_uchars_with_cmgr(&bsp[tot_len], &src_len, conv_buf, &conv_len, mio->cmgr, 0);
					tot_len += src_len;

					if (conv_len > n) conv_len = n;
					PUT_OOCS (conv_buf, conv_len);

					n -= conv_len;
				}
			}
			
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
		#else
			if (flagc & FLAGC_DOT)
			{
				for (n = 0; n < precision && bsp[n]; n++);
			}
			else
			{
				for (n = 0; bsp[n]; n++);
			}

			width -= n;

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			PUT_OOCS (bsp, n);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
		#endif
			break;
		}

		case 'S':
		{
			const mio_uch_t* usp;
			mio_oow_t uslen, slen;

			/* zeropad must not take effect for 's' */
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			if (lm_flag & LF_H) goto lowercase_s;
		#if defined(MIO_OOCH_IS_UCH)
			if (lm_flag & LF_J) goto lowercase_s;
		#endif
		uppercase_s:
			usp = va_arg (ap, mio_uch_t*);
			if (usp == MIO_NULL) usp = uch_nullstr;

		#if defined(MIO_OOCH_IS_BCH)
			/* get the length */
			if (flagc & FLAGC_DOT)
			{
				for (uslen = 0; uslen < precision && usp[uslen]; uslen++);
			}
			else
			{
				for (uslen = 0; usp[uslen]; uslen++);
			}

			if (mio_conv_uchars_to_bchars_with_cmgr(usp, &uslen, MIO_NULL, &slen, mio->cmgr) <= -1) goto oops;

			/* slen holds the length after conversion */
			n = slen;
			if ((flagc & FLAGC_DOT) && precision < slen) n = precision;
			width -= n;

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			{
				mio_ooch_t conv_buf[32]; 
				mio_oow_t conv_len, src_len, tot_len = 0;
				while (n > 0)
				{
					MIO_ASSERT (mio, uslen > tot_len);

					src_len = uslen - tot_len;
					conv_len = MIO_COUNTOF(conv_buf);

					/* this must not fail since the dry-run above was successful */
					mio_conv_uchars_to_bchars_with_cmgr (&usp[tot_len], &src_len, conv_buf, &conv_len, mio->cmgr);
					tot_len += src_len;

					if (conv_len > n) conv_len = n;
					PUT_OOCS (conv_buf, conv_len);

					n -= conv_len;
				}
			}
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
		#else
			if (flagc & FLAGC_DOT)
			{
				for (n = 0; n < precision && usp[n]; n++);
			}
			else
			{
				for (n = 0; usp[n]; n++);
			}

			width -= n;

			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			PUT_OOCS (usp, n);
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
		#endif
			break;
		}

		case 'k':
		case 'K':
		{
			/* byte or multibyte character string in escape sequence */
 
			const mio_uint8_t* bsp;
			mio_oow_t k_hex_width;
 
			/* zeropad must not take effect for 'k' and 'K' 
			 * 
 			 * 'h' & 'l' is not used to differentiate qse_mchar_t and qse_wchar_t
			 * because 'k' means qse_byte_t. 
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
 
			bsp = va_arg(ap, mio_uint8_t*);
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
					for (n = 0; n < precision; n++) /* nothing */;
				}
				else
				{
					for (n = 0; bsp[n]; n++) /* nothing */;
				}
				width -= (n * k_hex_width);
			}
 
			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
 
			while (n--) 
			{
				if ((lm_flag & LF_H) && BYTE_PRINTABLE(*bsp)) 
				{
					PUT_OOCH(*bsp, 1);
				}
				else
				{
					mio_bch_t xbuf[3];
					mio_byte_to_bcstr (*bsp, xbuf, MIO_COUNTOF(xbuf), (16 | (ch == 'k'? MIO_BYTE_TO_BCSTR_LOWERCASE: 0)), '0');
					if (lm_flag & (LF_H | LF_L))
					{
						PUT_OOCH('\\', 1);
						PUT_OOCH('x', 1);
					}
					PUT_OOCH(xbuf[0], 1);
					PUT_OOCH(xbuf[1], 1);
				}
				bsp++;
			}
 
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
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
			const mio_uch_t* usp;
			mio_oow_t uwid;
 
			if (flagc & FLAGC_ZEROPAD) padc = ' ';
			usp = va_arg(ap, mio_uch_t*);
 
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
 
			if (!(flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
 
			while (n--) 
			{
				if ((lm_flag & LF_H) && BYTE_PRINTABLE(*usp)) 
				{
					PUT_OOCH(*usp, 1);
				}
				else if (!(lm_flag & LF_L) && *usp <= 0xFFFF) 
				{
					mio_uint16_t u16 = *usp;
					int extra_flags = ((ch) == 'w'? MIO_BYTE_TO_BCSTR_LOWERCASE: 0);
					PUT_OOCH('\\', 1);
					PUT_OOCH('u', 1);
					PUT_BYTE_IN_HEX((u16 >> 8) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX(u16 & 0xFF, extra_flags);
				}
				else
				{
					mio_uint32_t u32 = *usp;
					int extra_flags = ((ch) == 'w'? MIO_BYTE_TO_BCSTR_LOWERCASE: 0);
					PUT_OOCH('\\', 1);
					PUT_OOCH('U', 1);
					PUT_BYTE_IN_HEX((u32 >> 24) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX((u32 >> 16) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX((u32 >> 8) & 0xFF, extra_flags);
					PUT_BYTE_IN_HEX(u32 & 0xFF, extra_flags);
				}
				usp++;
			}
 
			if ((flagc & FLAGC_LEFTADJ) && width > 0) PUT_OOCH (padc, width);
			break;
		}
#if 0
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
			mio_oow_t fmtlen;
		#if (MIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF)
			__float128 v_qd;
		#endif
			long double v_ld;
			double v_d;
			int dtype = 0;
			mio_oow_t newcapa;

			if (lm_flag & LF_J)
			{
			#if (MIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF) && (MIO_SIZEOF_FLTMAX_T == MIO_SIZEOF___FLOAT128)
				v_qd = va_arg (ap, mio_fltmax_t);
				dtype = LF_QD;
			#elif MIO_SIZEOF_FLTMAX_T == MIO_SIZEOF_DOUBLE
				v_d = va_arg (ap, mio_fltmax_t);
			#elif MIO_SIZEOF_FLTMAX_T == MIO_SIZEOF_LONG_DOUBLE
				v_ld = va_arg (ap, mio_fltmax_t);
				dtype = LF_LD;
			#else
				#error Unsupported mio_flt_t
			#endif
			}
			else if (lm_flag & LF_Z)
			{
				/* mio_flt_t is limited to double or long double */

				/* precedence goes to double if sizeof(double) == sizeof(long double) 
				 * for example, %Lf didn't work on some old platforms.
				 * so i prefer the format specifier with no modifier.
				 */
			#if MIO_SIZEOF_FLT_T == MIO_SIZEOF_DOUBLE
				v_d = va_arg (ap, mio_flt_t);
			#elif MIO_SIZEOF_FLT_T == MIO_SIZEOF_LONG_DOUBLE
				v_ld = va_arg (ap, mio_flt_t);
				dtype = LF_LD;
			#else
				#error Unsupported mio_flt_t
			#endif
			}
			else if (lm_flag & (LF_LD | LF_L))
			{
				v_ld = va_arg (ap, long double);
				dtype = LF_LD;
			}
		#if (MIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF)
			else if (lm_flag & (LF_QD | LF_Q))
			{
				v_qd = va_arg (ap, __float128);
				dtype = LF_QD;
			}
		#endif
			else if (flagc & FLAGC_LENMOD)
			{
				goto invalid_format;
			}
			else
			{
				v_d = va_arg (ap, double);
			}

			fmtlen = fmt - percent;
			if (fmtlen > fltfmt->capa)
			{
				if (fltfmt->ptr == fltfmt->buf)
				{
					fltfmt->ptr = MIO_MMGR_ALLOC (MIO_MMGR_GETDFL(), MIO_SIZEOF(*fltfmt->ptr) * (fmtlen + 1));
					if (fltfmt->ptr == MIO_NULL) goto oops;
				}
				else
				{
					mio_mchar_t* tmpptr;

					tmpptr = MIO_MMGR_REALLOC (MIO_MMGR_GETDFL(), fltfmt->ptr, MIO_SIZEOF(*fltfmt->ptr) * (fmtlen + 1));
					if (tmpptr == MIO_NULL) goto oops;
					fltfmt->ptr = tmpptr;
				}

				fltfmt->capa = fmtlen;
			}

			/* compose back the format specifier */
			fmtlen = 0;
			fltfmt->ptr[fmtlen++] = '%';
			if (flagc & FLAGC_SPACE) fltfmt->ptr[fmtlen++] = ' ';
			if (flagc & FLAGC_SHARP) fltfmt->ptr[fmtlen++] = '#';
			if (flagc & FLAGC_SIGN) fltfmt->ptr[fmtlen++] = '+';
			if (flagc & FLAGC_LEFTADJ) fltfmt->ptr[fmtlen++] = '-';
			if (flagc & FLAGC_ZEROPAD) fltfmt->ptr[fmtlen++] = '0';

			if (flagc & FLAGC_STAR1) fltfmt->ptr[fmtlen++] = '*';
			else if (flagc & FLAGC_WIDTH) 
			{
				fmtlen += mio_fmtuintmaxtombs (
					&fltfmt->ptr[fmtlen], fltfmt->capa - fmtlen, 
					width, 10, -1, '\0', MIO_NULL);
			}
			if (flagc & FLAGC_DOT) fltfmt->ptr[fmtlen++] = '.';
			if (flagc & FLAGC_STAR2) fltfmt->ptr[fmtlen++] = '*';
			else if (flagc & FLAGC_PRECISION) 
			{
				fmtlen += mio_fmtuintmaxtombs (
					&fltfmt->ptr[fmtlen], fltfmt->capa - fmtlen, 
					precision, 10, -1, '\0', MIO_NULL);
			}

			if (dtype == LF_LD)
				fltfmt->ptr[fmtlen++] = 'L';
		#if (MIO_SIZEOF___FLOAT128 > 0)
			else if (dtype == LF_QD)
				fltfmt->ptr[fmtlen++] = 'Q';
		#endif

			fltfmt->ptr[fmtlen++] = ch;
			fltfmt->ptr[fmtlen] = '\0';

		#if defined(HAVE_SNPRINTF)
			/* nothing special here */
		#else
			/* best effort to avoid buffer overflow when no snprintf is available. 
			 * i really can't do much if it happens. */
			newcapa = precision + width + 32;
			if (fltout->capa < newcapa)
			{
				MIO_ASSERT (mio, fltout->ptr == fltout->buf);

				fltout->ptr = MIO_MMGR_ALLOC (MIO_MMGR_GETDFL(), MIO_SIZEOF(char_t) * (newcapa + 1));
				if (fltout->ptr == MIO_NULL) goto oops;
				fltout->capa = newcapa;
			}
		#endif

			while (1)
			{

				if (dtype == LF_LD)
				{
				#if defined(HAVE_SNPRINTF)
					q = snprintf ((mio_mchar_t*)fltout->ptr, fltout->capa + 1, fltfmt->ptr, v_ld);
				#else
					q = sprintf ((mio_mchar_t*)fltout->ptr, fltfmt->ptr, v_ld);
				#endif
				}
			#if (MIO_SIZEOF___FLOAT128 > 0) && defined(HAVE_QUADMATH_SNPRINTF)
				else if (dtype == LF_QD)
				{
					q = quadmath_snprintf ((mio_mchar_t*)fltout->ptr, fltout->capa + 1, fltfmt->ptr, v_qd);
				}
			#endif
				else
				{
				#if defined(HAVE_SNPRINTF)
					q = snprintf ((mio_mchar_t*)fltout->ptr, fltout->capa + 1, fltfmt->ptr, v_d);
				#else
					q = sprintf ((mio_mchar_t*)fltout->ptr, fltfmt->ptr, v_d);
				#endif
				}
				if (q <= -1) goto oops;
				if (q <= fltout->capa) break;

				newcapa = fltout->capa * 2;
				if (newcapa < q) newcapa = q;

				if (fltout->ptr == fltout->sbuf)
				{
					fltout->ptr = MIO_MMGR_ALLOC (MIO_MMGR_GETDFL(), MIO_SIZEOF(char_t) * (newcapa + 1));
					if (fltout->ptr == MIO_NULL) goto oops;
				}
				else
				{
					char_t* tmpptr;

					tmpptr = MIO_MMGR_REALLOC (MIO_MMGR_GETDFL(), fltout->ptr, MIO_SIZEOF(char_t) * (newcapa + 1));
					if (tmpptr == MIO_NULL) goto oops;
					fltout->ptr = tmpptr;
				}
				fltout->capa = newcapa;
			}

			if (MIO_SIZEOF(char_t) != MIO_SIZEOF(mio_mchar_t))
			{
				fltout->ptr[q] = '\0';
				while (q > 0)
				{
					q--;
					fltout->ptr[q] = ((mio_mchar_t*)fltout->ptr)[q];
				}
			}

			sp = fltout->ptr;
			flagc &= ~FLAGC_DOT;
			width = 0;
			precision = 0;
			goto print_lowercase_s;
		}
#endif


		handle_nosign:
			sign = 0;
			if (lm_flag & LF_J)
			{
			#if defined(__GNUC__) && \
			    (MIO_SIZEOF_UINTMAX_T > MIO_SIZEOF_OOW_T) && \
			    (MIO_SIZEOF_UINTMAX_T != MIO_SIZEOF_LONG_LONG) && \
			    (MIO_SIZEOF_UINTMAX_T != MIO_SIZEOF_LONG)
				/* GCC-compiled binaries crashed when getting mio_uintmax_t with va_arg.
				 * This is just a work-around for it */
				int i;
				for (i = 0, num = 0; i < MIO_SIZEOF(mio_uintmax_t) / MIO_SIZEOF(mio_oow_t); i++)
				{
				#if defined(MIO_ENDIAN_BIG)
					num = num << (8 * MIO_SIZEOF(mio_oow_t)) | (va_arg (ap, mio_oow_t));
				#else
					register int shift = i * MIO_SIZEOF(mio_oow_t);
					mio_oow_t x = va_arg (ap, mio_oow_t);
					num |= (mio_uintmax_t)x << (shift * MIO_BITS_PER_BYTE);
				#endif
				}
			#else
				num = va_arg (ap, mio_uintmax_t);
			#endif
			}
#if 0
			else if (lm_flag & LF_T)
				num = va_arg (ap, mio_ptrdiff_t);
#endif
			else if (lm_flag & LF_Z)
				num = va_arg (ap, mio_oow_t);
			#if (MIO_SIZEOF_LONG_LONG > 0)
			else if (lm_flag & LF_Q)
				num = va_arg (ap, unsigned long long int);
			#endif
			else if (lm_flag & (LF_L | LF_LD))
				num = va_arg (ap, unsigned long int);
			else if (lm_flag & LF_H)
				num = (unsigned short int)va_arg (ap, int);
			else if (lm_flag & LF_C)
				num = (unsigned char)va_arg (ap, int);
			else
				num = va_arg (ap, unsigned int);
			goto number;

		handle_sign:
			if (lm_flag & LF_J)
			{
			#if defined(__GNUC__) && \
			    (MIO_SIZEOF_INTMAX_T > MIO_SIZEOF_OOI_T) && \
			    (MIO_SIZEOF_UINTMAX_T != MIO_SIZEOF_LONG_LONG) && \
			    (MIO_SIZEOF_UINTMAX_T != MIO_SIZEOF_LONG)
				/* GCC-compiled binraries crashed when getting mio_uintmax_t with va_arg.
				 * This is just a work-around for it */
				int i;
				for (i = 0, num = 0; i < MIO_SIZEOF(mio_intmax_t) / MIO_SIZEOF(mio_oow_t); i++)
				{
				#if defined(MIO_ENDIAN_BIG)
					num = num << (8 * MIO_SIZEOF(mio_oow_t)) | (va_arg (ap, mio_oow_t));
				#else
					register int shift = i * MIO_SIZEOF(mio_oow_t);
					mio_oow_t x = va_arg (ap, mio_oow_t);
					num |= (mio_uintmax_t)x << (shift * MIO_BITS_PER_BYTE);
				#endif
				}
			#else
				num = va_arg (ap, mio_intmax_t);
			#endif
			}

#if 0
			else if (lm_flag & LF_T)
				num = va_arg(ap, mio_ptrdiff_t);
#endif
			else if (lm_flag & LF_Z)
				num = va_arg (ap, mio_ooi_t);
			#if (MIO_SIZEOF_LONG_LONG > 0)
			else if (lm_flag & LF_Q)
				num = va_arg (ap, long long int);
			#endif
			else if (lm_flag & (LF_L | LF_LD))
				num = va_arg (ap, long int);
			else if (lm_flag & LF_H)
				num = (short int)va_arg (ap, int);
			else if (lm_flag & LF_C)
				num = (char)va_arg (ap, int);
			else
				num = va_arg (ap, int);

		number:
			if (sign && (mio_intmax_t)num < 0) 
			{
				neg = 1;
				num = -(mio_intmax_t)num;
			}

			nbufp = sprintn (nbuf, num, base, &tmp);
			if ((flagc & FLAGC_SHARP) && num != 0) 
			{
				if (base == 2 || base == 8) tmp += 2;
				else if (base == 16) tmp += 3;
			}
			if (neg) tmp++;
			else if (flagc & FLAGC_SIGN) tmp++;
			else if (flagc & FLAGC_SPACE) tmp++;

			numlen = (int)((const mio_bch_t*)nbufp - (const mio_bch_t*)nbuf);
			if ((flagc & FLAGC_DOT) && precision > numlen) 
			{
				/* extra zeros for precision specified */
				tmp += (precision - numlen);
			}

			if (!(flagc & FLAGC_LEFTADJ) && !(flagc & FLAGC_ZEROPAD) && width > 0 && (width -= tmp) > 0)
			{
				PUT_OOCH (padc, width);
				width = 0;
			}

			if (neg) PUT_OOCH ('-', 1);
			else if (flagc & FLAGC_SIGN) PUT_OOCH ('+', 1);
			else if (flagc & FLAGC_SPACE) PUT_OOCH (' ', 1);

			if ((flagc & FLAGC_SHARP) && num != 0) 
			{
				if (base == 2) 
				{
					PUT_OOCH ('2', 1);
					PUT_OOCH ('r', 1);
				}
				if (base == 8) 
				{
					PUT_OOCH ('8', 1);
					PUT_OOCH ('r', 1);
				} 
				else if (base == 16) 
				{
					PUT_OOCH ('1', 1);
					PUT_OOCH ('6', 1);
					PUT_OOCH ('r', 1);
				}
			}

			if ((flagc & FLAGC_DOT) && precision > numlen)
			{
				/* extra zeros for precision specified */
				PUT_OOCH ('0', precision - numlen);
			}

			if (!(flagc & FLAGC_LEFTADJ) && width > 0 && (width -= tmp) > 0)
			{
				PUT_OOCH (padc, width);
			}

			while (*nbufp) PUT_OOCH (*nbufp--, 1); /* output actual digits */

			if ((flagc & FLAGC_LEFTADJ) && width > 0 && (width -= tmp) > 0)
			{
				PUT_OOCH (padc, width);
			}
			break;

		invalid_format:
		#if defined(FMTCHAR_IS_OOCH)
			PUT_OOCS (percent, fmt - percent);
		#else
			while (percent < fmt) PUT_OOCH (*percent++, 1);
		#endif
			break;

		default:
		#if defined(FMTCHAR_IS_OOCH)
			PUT_OOCS (percent, fmt - percent);
		#else
			while (percent < fmt) PUT_OOCH (*percent++, 1);
		#endif
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
	return 0;

oops:
	return -1;
}
#undef PUT_OOCH
