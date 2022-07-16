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

#include "hio-prv.h"
#include <hio-chr.h>

/* ========================================================================= */

int hio_comp_ucstr_bcstr (const hio_uch_t* str1, const hio_bch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		while (hio_to_uch_lower(*str1) == hio_to_bch_lower(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((hio_uchu_t)hio_to_uch_lower(*str1) > (hio_bchu_t)hio_to_bch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((hio_uchu_t)*str1 > (hio_bchu_t)*str2)? 1: -1;
	}
}

int hio_comp_ucstr_bcstr_limited (const hio_uch_t* str1, const hio_bch_t* str2, hio_oow_t maxlen, int ignorecase)
{
	if (maxlen == 0) return 0;

	if (ignorecase)
	{
		while (hio_to_uch_lower(*str1) == hio_to_bch_lower(*str2))
 		{
			if (*str1 == '\0' || maxlen == 1) return 0;

			str1++; str2++; maxlen--;
		}

		return ((hio_uchu_t)hio_to_uch_lower(*str1) > (hio_bchu_t)hio_to_bch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0' || maxlen == 1) return 0;
			str1++; str2++; maxlen--;
		}

		return ((hio_uchu_t)*str1 > (hio_bchu_t)*str2)? 1: -1;
	}
}

int hio_comp_uchars_bcstr (const hio_uch_t* str1, hio_oow_t len, const hio_bch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		const hio_uch_t* end = str1 + len;
		hio_uch_t c1;
		hio_bch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = hio_to_uch_lower(*str1);
			c2 = hio_to_bch_lower(*str2);
			if (c1 != c2) return ((hio_uchu_t)c1 > (hio_bchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const hio_uch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((hio_uchu_t)*str1 > (hio_bchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

int hio_comp_bchars_ucstr (const hio_bch_t* str1, hio_oow_t len, const hio_uch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		const hio_bch_t* end = str1 + len;
		hio_bch_t c1;
		hio_uch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = hio_to_bch_lower(*str1);
			c2 = hio_to_uch_lower(*str2);
			if (c1 != c2) return ((hio_bchu_t)c1 > (hio_uchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const hio_bch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((hio_bchu_t)*str1 > (hio_uchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

/* ========================================================================= */

void hio_copy_bchars_to_uchars (hio_uch_t* dst, const hio_bch_t* src, hio_oow_t len)
{
	/* copy without conversions.
	 * use hio_convbtouchars() for conversion encoding */
	hio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

void hio_copy_uchars_to_bchars (hio_bch_t* dst, const hio_uch_t* src, hio_oow_t len)
{
	/* copy without conversions.
	 * use hio_convutobchars() for conversion encoding */
	hio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

/* ========================================================================= */

#define IS_BCH_WORD_DELIM(x,delim) (hio_is_bch_space(x) || (x) == delim)
#define IS_UCH_WORD_DELIM(x,delim) (hio_is_uch_space(x) || (x) == delim)

const hio_bch_t* hio_find_bcstr_word_in_bcstr (const hio_bch_t* str, const hio_bch_t* word, hio_bch_t extra_delim, int ignorecase)
{
	/* find a full word in a string */

	const hio_bch_t* ptr = str;

	if (extra_delim == '\0') extra_delim = ' ';
	do
	{
		const hio_bch_t* s;

		while (IS_BCH_WORD_DELIM(*ptr,extra_delim)) ptr++;
		if (*ptr == '\0') return HIO_NULL;

		s = ptr;
		while (*ptr != '\0' && !IS_BCH_WORD_DELIM(*ptr,extra_delim)) ptr++;

		if (hio_comp_bchars_bcstr(s, ptr - s, word, ignorecase) == 0) return s;
	}
	while (*ptr != '\0');

	return HIO_NULL;
}

const hio_uch_t* hio_find_ucstr_word_in_ucstr (const hio_uch_t* str, const hio_uch_t* word, hio_uch_t extra_delim, int ignorecase)
{
	/* find a full word in a string */

	const hio_uch_t* ptr = str;

	if (extra_delim == '\0') extra_delim = ' ';
	do
	{
		const hio_uch_t* s;

		while (IS_UCH_WORD_DELIM(*ptr,extra_delim)) ptr++;
		if (*ptr == '\0') return HIO_NULL;

		s = ptr;
		while (*ptr != '\0' && !IS_UCH_WORD_DELIM(*ptr,extra_delim)) ptr++;

		if (hio_comp_uchars_ucstr(s, ptr - s, word, ignorecase) == 0) return s;
	}
	while (*ptr != '\0');

	return HIO_NULL;
}

/* ========================================================================= */

HIO_INLINE int hio_conv_bchars_to_uchars_with_cmgr (
	const hio_bch_t* bcs, hio_oow_t* bcslen,
	hio_uch_t* ucs, hio_oow_t* ucslen, hio_cmgr_t* cmgr, int all)
{
	const hio_bch_t* p;
	int ret = 0;
	hio_oow_t mlen;

	if (ucs)
	{
		/* destination buffer is specified. 
		 * copy the conversion result to the buffer */

		hio_uch_t* q, * qend;

		p = bcs;
		q = ucs;
		qend = ucs + *ucslen;
		mlen = *bcslen;

		while (mlen > 0)
		{
			hio_oow_t n;

			if (q >= qend)
			{
				/* buffer too small */
				ret = -2;
				break;
			}

			n = cmgr->bctouc(p, mlen, q);
			if (n == 0)
			{
				/* invalid sequence */
				if (all)
				{
					n = 1;
					*q = '?';
				}
				else
				{
					ret = -1;
					break;
				}
			}
			if (n > mlen)
			{
				/* incomplete sequence */
				if (all)
				{
					n = 1;
					*q = '?';
				}
				else
				{
					ret = -3;
					break;
				}
			}

			q++;
			p += n;
			mlen -= n;
		}

		*ucslen = q - ucs;
		*bcslen = p - bcs;
	}
	else
	{
		/* no destination buffer is specified. perform conversion
		 * but don't copy the result. the caller can call this function
		 * without a buffer to find the required buffer size, allocate
		 * a buffer with the size and call this function again with 
		 * the buffer. */

		hio_uch_t w;
		hio_oow_t wlen = 0;

		p = bcs;
		mlen = *bcslen;

		while (mlen > 0)
		{
			hio_oow_t n;

			n = cmgr->bctouc(p, mlen, &w);
			if (n == 0)
			{
				/* invalid sequence */
				if (all) n = 1;
				else
				{
					ret = -1;
					break;
				}
			}
			if (n > mlen)
			{
				/* incomplete sequence */
				if (all) n = 1;
				else
				{
					ret = -3;
					break;
				}
			}

			p += n;
			mlen -= n;
			wlen += 1;
		}

		*ucslen = wlen;
		*bcslen = p - bcs;
	}

	return ret;
}

HIO_INLINE int hio_conv_bcstr_to_ucstr_with_cmgr (
	const hio_bch_t* bcs, hio_oow_t* bcslen,
	hio_uch_t* ucs, hio_oow_t* ucslen, hio_cmgr_t* cmgr, int all)
{
	const hio_bch_t* bp;
	hio_oow_t mlen, wlen;
	int n;

	for (bp = bcs; *bp != '\0'; bp++) /* nothing */ ;

	mlen = bp - bcs; wlen = *ucslen;
	n = hio_conv_bchars_to_uchars_with_cmgr (bcs, &mlen, ucs, &wlen, cmgr, all);
	if (ucs)
	{
		/* null-terminate the target buffer if it has room for it. */
		if (wlen < *ucslen) ucs[wlen] = '\0';
		else n = -2; /* buffer too small */
	}
	*bcslen = mlen; *ucslen = wlen;

	return n;
}

HIO_INLINE int hio_conv_uchars_to_bchars_with_cmgr (
	const hio_uch_t* ucs, hio_oow_t* ucslen,
	hio_bch_t* bcs, hio_oow_t* bcslen, hio_cmgr_t* cmgr)
{
	const hio_uch_t* p = ucs;
	const hio_uch_t* end = ucs + *ucslen;
	int ret = 0; 

	if (bcs)
	{
		hio_oow_t rem = *bcslen;

		while (p < end) 
		{
			hio_oow_t n;

			if (rem <= 0)
			{
				ret = -2; /* buffer too small */
				break;
			}

			n = cmgr->uctobc(*p, bcs, rem);
			if (n == 0) 
			{
				ret = -1;
				break; /* illegal character */
			}
			if (n > rem) 
			{
				ret = -2; /* buffer too small */
				break;
			}
			bcs += n; rem -= n; p++;
		}

		*bcslen -= rem; 
	}
	else
	{
		hio_bch_t bcsbuf[HIO_BCSIZE_MAX];
		hio_oow_t mlen = 0;

		while (p < end)
		{
			hio_oow_t n;

			n = cmgr->uctobc(*p, bcsbuf, HIO_COUNTOF(bcsbuf));
			if (n == 0) 
			{
				ret = -1;
				break; /* illegal character */
			}

			/* it assumes that bcsbuf is large enough to hold a character */
			/*HIO_ASSERT (hio, n <= HIO_COUNTOF(bcsbuf));*/

			p++; mlen += n;
		}

		/* this length excludes the terminating null character. 
		 * this function doesn't even null-terminate the result. */
		*bcslen = mlen;
	}

	*ucslen = p - ucs;
	return ret;
}

HIO_INLINE int hio_conv_ucstr_to_bcstr_with_cmgr (
	const hio_uch_t* ucs, hio_oow_t* ucslen,
	hio_bch_t* bcs, hio_oow_t* bcslen, hio_cmgr_t* cmgr)
{
	const hio_uch_t* p = ucs;
	int ret = 0;

	if (bcs)
	{
		hio_oow_t rem = *bcslen;

		while (*p != '\0')
		{
			hio_oow_t n;

			if (rem <= 0)
			{
				ret = -2;
				break;
			}
			
			n = cmgr->uctobc(*p, bcs, rem);
			if (n == 0) 
			{
				ret = -1;
				break; /* illegal character */
			}
			if (n > rem) 
			{
				ret = -2;
				break; /* buffer too small */
			}

			bcs += n; rem -= n; p++;
		}

		/* update bcslen to the length of the bcs string converted excluding
		 * terminating null */
		*bcslen -= rem; 

		/* null-terminate the multibyte sequence if it has sufficient space */
		if (rem > 0) *bcs = '\0';
		else 
		{
			/* if ret is -2 and cs[cslen] == '\0', 
			 * this means that the bcs buffer was lacking one
			 * slot for the terminating null */
			ret = -2; /* buffer too small */
		}
	}
	else
	{
		hio_bch_t bcsbuf[HIO_BCSIZE_MAX];
		hio_oow_t mlen = 0;

		while (*p != '\0')
		{
			hio_oow_t n;

			n = cmgr->uctobc(*p, bcsbuf, HIO_COUNTOF(bcsbuf));
			if (n == 0) 
			{
				ret = -1;
				break; /* illegal character */
			}

			/* it assumes that bcs is large enough to hold a character */
			/*HIO_ASSERT (hio, n <= HIO_COUNTOF(bcs));*/

			p++; mlen += n;
		}

		/* this length holds the number of resulting multi-byte characters 
		 * excluding the terminating null character */
		*bcslen = mlen;
	}

	*ucslen = p - ucs;  /* the number of wide characters handled. */
	return ret;
}

/* ----------------------------------------------------------------------- */

static hio_cmgr_t utf8_cmgr =
{
	hio_utf8_to_uc,
	hio_uc_to_utf8
};

hio_cmgr_t* hio_get_utf8_cmgr (void)
{
	return &utf8_cmgr;
}

int hio_conv_utf8_to_uchars (const hio_bch_t* bcs, hio_oow_t* bcslen, hio_uch_t* ucs, hio_oow_t* ucslen)
{
	/* the source is length bound */
	return hio_conv_bchars_to_uchars_with_cmgr(bcs, bcslen, ucs, ucslen, &utf8_cmgr, 0);
}

int hio_conv_uchars_to_utf8 (const hio_uch_t* ucs, hio_oow_t* ucslen, hio_bch_t* bcs, hio_oow_t* bcslen)
{
	/* length bound */
	return hio_conv_uchars_to_bchars_with_cmgr(ucs, ucslen, bcs, bcslen, &utf8_cmgr);
}

int hio_conv_utf8_to_ucstr (const hio_bch_t* bcs, hio_oow_t* bcslen, hio_uch_t* ucs, hio_oow_t* ucslen)
{
	/* null-terminated. */
	return hio_conv_bcstr_to_ucstr_with_cmgr(bcs, bcslen, ucs, ucslen, &utf8_cmgr, 0);
}

int hio_conv_ucstr_to_utf8 (const hio_uch_t* ucs, hio_oow_t* ucslen, hio_bch_t* bcs, hio_oow_t* bcslen)
{
	/* null-terminated */
	return hio_conv_ucstr_to_bcstr_with_cmgr(ucs, ucslen, bcs, bcslen, &utf8_cmgr);
}

/* ----------------------------------------------------------------------- */

int hio_convbtouchars (hio_t* hio, const hio_bch_t* bcs, hio_oow_t* bcslen, hio_uch_t* ucs, hio_oow_t* ucslen, int all)
{
	/* length bound */
	int n;

	n = hio_conv_bchars_to_uchars_with_cmgr(bcs, bcslen, ucs, ucslen, hio_getcmgr(hio), all);

	if (n <= -1)
	{
		/* -1: illegal character, -2: buffer too small, -3: incomplete sequence */
		hio_seterrnum (hio, (n == -2)? HIO_EBUFFULL: HIO_EECERR);
	}

	return n;
}

int hio_convutobchars (hio_t* hio, const hio_uch_t* ucs, hio_oow_t* ucslen, hio_bch_t* bcs, hio_oow_t* bcslen)
{
	/* length bound */
	int n;

	n = hio_conv_uchars_to_bchars_with_cmgr(ucs, ucslen, bcs, bcslen, hio_getcmgr(hio));

	if (n <= -1)
	{
		hio_seterrnum (hio, (n == -2)? HIO_EBUFFULL: HIO_EECERR);
	}

	return n;
}

int hio_convbtoucstr (hio_t* hio, const hio_bch_t* bcs, hio_oow_t* bcslen, hio_uch_t* ucs, hio_oow_t* ucslen, int all)
{
	/* null-terminated. */
	int n;

	n = hio_conv_bcstr_to_ucstr_with_cmgr(bcs, bcslen, ucs, ucslen, hio_getcmgr(hio), all);

	if (n <= -1)
	{
		hio_seterrnum (hio, (n == -2)? HIO_EBUFFULL: HIO_EECERR);
	}

	return n;
}

int hio_convutobcstr (hio_t* hio, const hio_uch_t* ucs, hio_oow_t* ucslen, hio_bch_t* bcs, hio_oow_t* bcslen)
{
	/* null-terminated */
	int n;

	n = hio_conv_ucstr_to_bcstr_with_cmgr(ucs, ucslen, bcs, bcslen, hio_getcmgr(hio));

	if (n <= -1)
	{
		hio_seterrnum (hio, (n == -2)? HIO_EBUFFULL: HIO_EECERR);
	}

	return n;
}

/* ----------------------------------------------------------------------- */

HIO_INLINE hio_uch_t* hio_dupbtoucharswithheadroom (hio_t* hio, hio_oow_t headroom_bytes, const hio_bch_t* bcs, hio_oow_t bcslen, hio_oow_t* ucslen, int all)
{
	hio_oow_t inlen, outlen;
	hio_uch_t* ptr;

	inlen = bcslen;
	if (hio_convbtouchars(hio, bcs, &inlen, HIO_NULL, &outlen, all) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return HIO_NULL;
	}

	ptr = (hio_uch_t*)hio_allocmem(hio, headroom_bytes + ((outlen + 1) * HIO_SIZEOF(hio_uch_t)));
	if (!ptr) return HIO_NULL;

	inlen = bcslen;

	ptr = (hio_uch_t*)((hio_oob_t*)ptr + headroom_bytes);
	hio_convbtouchars (hio, bcs, &inlen, ptr, &outlen, all);

	/* hio_convbtouchars() doesn't null-terminate the target. 
	 * but in hio_dupbtouchars(), i allocate space. so i don't mind
	 * null-terminating it with 1 extra character overhead */
	ptr[outlen] = '\0'; 
	if (ucslen) *ucslen = outlen;
	return ptr;
}

hio_uch_t* hio_dupbtouchars (hio_t* hio, const hio_bch_t* bcs, hio_oow_t bcslen, hio_oow_t* ucslen, int all)
{
	return hio_dupbtoucharswithheadroom (hio, 0, bcs, bcslen, ucslen, all);
}

HIO_INLINE hio_bch_t* hio_duputobcharswithheadroom (hio_t* hio, hio_oow_t headroom_bytes, const hio_uch_t* ucs, hio_oow_t ucslen, hio_oow_t* bcslen)
{
	hio_oow_t inlen, outlen;
	hio_bch_t* ptr;

	inlen = ucslen;
	if (hio_convutobchars(hio, ucs, &inlen, HIO_NULL, &outlen) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return HIO_NULL;
	}

	ptr = (hio_bch_t*)hio_allocmem(hio, headroom_bytes + ((outlen + 1) * HIO_SIZEOF(hio_bch_t)));
	if (!ptr) return HIO_NULL;

	inlen = ucslen;
	ptr = (hio_bch_t*)((hio_oob_t*)ptr + headroom_bytes);
	hio_convutobchars (hio, ucs, &inlen, ptr, &outlen);

	ptr[outlen] = '\0';
	if (bcslen) *bcslen = outlen;
	return ptr;
}

hio_bch_t* hio_duputobchars (hio_t* hio, const hio_uch_t* ucs, hio_oow_t ucslen, hio_oow_t* bcslen)
{
	return hio_duputobcharswithheadroom (hio, 0, ucs, ucslen, bcslen);
}


/* ----------------------------------------------------------------------- */

HIO_INLINE hio_uch_t* hio_dupbtoucstrwithheadroom (hio_t* hio, hio_oow_t headroom_bytes, const hio_bch_t* bcs, hio_oow_t* ucslen, int all)
{
	hio_oow_t inlen, outlen;
	hio_uch_t* ptr;

	if (hio_convbtoucstr(hio, bcs, &inlen, HIO_NULL, &outlen, all) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return HIO_NULL;
	}

	outlen++;
	ptr = (hio_uch_t*)hio_allocmem(hio, headroom_bytes + (outlen * HIO_SIZEOF(hio_uch_t)));
	if (!ptr) return HIO_NULL;

	hio_convbtoucstr (hio, bcs, &inlen, ptr, &outlen, all);
	if (ucslen) *ucslen = outlen;
	return ptr;
}

hio_uch_t* hio_dupbtoucstr (hio_t* hio, const hio_bch_t* bcs, hio_oow_t* ucslen, int all)
{
	return hio_dupbtoucstrwithheadroom (hio, 0, bcs, ucslen, all);
}

HIO_INLINE hio_bch_t* hio_duputobcstrwithheadroom (hio_t* hio, hio_oow_t headroom_bytes, const hio_uch_t* ucs, hio_oow_t* bcslen)
{
	hio_oow_t inlen, outlen;
	hio_bch_t* ptr;

	if (hio_convutobcstr(hio, ucs, &inlen, HIO_NULL, &outlen) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return HIO_NULL;
	}

	outlen++;
	ptr = (hio_bch_t*)hio_allocmem(hio, headroom_bytes + (outlen * HIO_SIZEOF(hio_bch_t)));
	if (!ptr) return HIO_NULL;

	ptr = (hio_bch_t*)((hio_oob_t*)ptr + headroom_bytes);

	hio_convutobcstr (hio, ucs, &inlen, ptr, &outlen);
	if (bcslen) *bcslen = outlen;
	return ptr;
}

hio_bch_t* hio_duputobcstr (hio_t* hio, const hio_uch_t* ucs, hio_oow_t* bcslen)
{
	return hio_duputobcstrwithheadroom (hio, 0, ucs, bcslen);
}
/* ----------------------------------------------------------------------- */

hio_uch_t* hio_dupuchars (hio_t* hio, const hio_uch_t* ucs, hio_oow_t ucslen)
{
	hio_uch_t* ptr;

	ptr = (hio_uch_t*)hio_allocmem(hio, (ucslen + 1) * HIO_SIZEOF(hio_uch_t));
	if (!ptr) return HIO_NULL;

	hio_copy_uchars (ptr, ucs, ucslen);
	ptr[ucslen] = '\0';
	return ptr;
}

hio_bch_t* hio_dupbchars (hio_t* hio, const hio_bch_t* bcs, hio_oow_t bcslen)
{
	hio_bch_t* ptr;

	ptr = (hio_bch_t*)hio_allocmem(hio, (bcslen + 1) * HIO_SIZEOF(hio_bch_t));
	if (!ptr) return HIO_NULL;

	hio_copy_bchars (ptr, bcs, bcslen);
	ptr[bcslen] = '\0';
	return ptr;
}


/* ========================================================================= */
hio_uch_t* hio_dupucstr (hio_t* hio, const hio_uch_t* ucs, hio_oow_t* ucslen)
{
	hio_uch_t* ptr;
	hio_oow_t len;

	len = hio_count_ucstr(ucs);

	ptr = (hio_uch_t*)hio_allocmem(hio, (len + 1) * HIO_SIZEOF(hio_uch_t));
	if (!ptr) return HIO_NULL;

	hio_copy_uchars (ptr, ucs, len);
	ptr[len] = '\0';

	if (ucslen) *ucslen = len;
	return ptr;
}

hio_bch_t* hio_dupbcstr (hio_t* hio, const hio_bch_t* bcs, hio_oow_t* bcslen)
{
	hio_bch_t* ptr;
	hio_oow_t len;

	len = hio_count_bcstr(bcs);

	ptr = (hio_bch_t*)hio_allocmem(hio, (len + 1) * HIO_SIZEOF(hio_bch_t));
	if (!ptr) return HIO_NULL;

	hio_copy_bchars (ptr, bcs, len);
	ptr[len] = '\0';

	if (bcslen) *bcslen = len;
	return ptr;
}

hio_uch_t* hio_dupucstrs (hio_t* hio, const hio_uch_t* ucs[], hio_oow_t* ucslen)
{
	hio_uch_t* ptr;
	hio_oow_t len, i;

	for (i = 0, len = 0; ucs[i]; i++) len += hio_count_ucstr(ucs[i]);

	ptr = (hio_uch_t*)hio_allocmem(hio, (len + 1) * HIO_SIZEOF(hio_uch_t));
	if (!ptr) return HIO_NULL;

	for (i = 0, len = 0; ucs[i]; i++) 
		len += hio_copy_ucstr_unlimited(&ptr[len], ucs[i]);
	ptr[len] = '\0';

	if (ucslen) *ucslen = len;
	return ptr;
}

hio_bch_t* hio_dupbcstrs (hio_t* hio, const hio_bch_t* bcs[], hio_oow_t* bcslen)
{
	hio_bch_t* ptr;
	hio_oow_t len, i;

	for (i = 0, len = 0; bcs[i]; i++) len += hio_count_bcstr(bcs[i]);

	ptr = (hio_bch_t*)hio_allocmem(hio, (len + 1) * HIO_SIZEOF(hio_bch_t));
	if (!ptr) return HIO_NULL;

	for (i = 0, len = 0; bcs[i]; i++) 
		len += hio_copy_bcstr_unlimited(&ptr[len], bcs[i]);
	ptr[len] = '\0';

	if (bcslen) *bcslen = len;
	return ptr;
}
/* ========================================================================= */

void hio_add_ntime (hio_ntime_t* z, const hio_ntime_t* x, const hio_ntime_t* y)
{
	hio_ntime_sec_t xs, ys;
	hio_ntime_nsec_t ns;

	/*HIO_ASSERT (x->nsec >= 0 && x->nsec < HIO_NSECS_PER_SEC);
	HIO_ASSERT (y->nsec >= 0 && y->nsec < HIO_NSECS_PER_SEC);*/

	ns = x->nsec + y->nsec;
	if (ns >= HIO_NSECS_PER_SEC)
	{
		ns = ns - HIO_NSECS_PER_SEC;
		if (x->sec == HIO_TYPE_MAX(hio_ntime_sec_t))
		{
			if (y->sec >= 0) goto overflow;
			xs = x->sec;
			ys = y->sec + 1; /* this won't overflow */
		}
		else
		{
			xs = x->sec + 1; /* this won't overflow */
			ys = y->sec;
		}
	}
	else
	{
		xs = x->sec;
		ys = y->sec;
	}

	if ((ys >= 1 && xs > HIO_TYPE_MAX(hio_ntime_sec_t) - ys) ||
	    (ys <= -1 && xs < HIO_TYPE_MIN(hio_ntime_sec_t) - ys))
	{
		if (xs >= 0)
		{
		overflow:
			xs = HIO_TYPE_MAX(hio_ntime_sec_t);
			ns = HIO_NSECS_PER_SEC - 1;
		}
		else
		{
			xs = HIO_TYPE_MIN(hio_ntime_sec_t);
			ns = 0;
		}
	}
	else
	{
		xs = xs + ys;
	}

	z->sec = xs;
	z->nsec = ns;
}

void hio_sub_ntime (hio_ntime_t* z, const hio_ntime_t* x, const hio_ntime_t* y)
{
	hio_ntime_sec_t xs, ys;
	hio_ntime_nsec_t ns;

	/*HIO_ASSERT (x->nsec >= 0 && x->nsec < HIO_NSECS_PER_SEC);
	HIO_ASSERT (y->nsec >= 0 && y->nsec < HIO_NSECS_PER_SEC);*/

	ns = x->nsec - y->nsec;
	if (ns < 0)
	{
		ns = ns + HIO_NSECS_PER_SEC;
		if (x->sec == HIO_TYPE_MIN(hio_ntime_sec_t))
		{
			if (y->sec <= 0) goto underflow;
			xs = x->sec;
			ys = y->sec - 1; /* this won't underflow */
		}
		else
		{
			xs = x->sec - 1; /* this won't underflow */
			ys = y->sec;
		}
	}
	else
	{
		xs = x->sec;
		ys = y->sec;
	}

	if ((ys >= 1 && xs < HIO_TYPE_MIN(hio_ntime_sec_t) + ys) ||
	    (ys <= -1 && xs > HIO_TYPE_MAX(hio_ntime_sec_t) + ys))
	{
		if (xs >= 0)
		{
			xs = HIO_TYPE_MAX(hio_ntime_sec_t);
			ns = HIO_NSECS_PER_SEC - 1;
		}
		else
		{
		underflow:
			xs = HIO_TYPE_MIN(hio_ntime_sec_t);
			ns = 0;
		}
	} 
	else
	{
		xs = xs - ys;
	}

	z->sec = xs;
	z->nsec = ns;
}

/* ========================================================================= */
