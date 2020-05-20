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

#include "mio-prv.h"
#include "mio-chr.h"

/* ========================================================================= */

int mio_equal_uchars (const mio_uch_t* str1, const mio_uch_t* str2, mio_oow_t len)
{
	mio_oow_t i;

	/* NOTE: you should call this function after having ensured that
	 *       str1 and str2 are in the same length */

	for (i = 0; i < len; i++)
	{
		if (str1[i] != str2[i]) return 0;
	}

	return 1;
}

int mio_equal_bchars (const mio_bch_t* str1, const mio_bch_t* str2, mio_oow_t len)
{
	mio_oow_t i;

	/* NOTE: you should call this function after having ensured that
	 *       str1 and str2 are in the same length */

	for (i = 0; i < len; i++)
	{
		if (str1[i] != str2[i]) return 0;
	}

	return 1;
}

int mio_comp_uchars (const mio_uch_t* str1, mio_oow_t len1, const mio_uch_t* str2, mio_oow_t len2, int ignorecase)
{
	mio_uchu_t c1, c2;
	const mio_uch_t* end1 = str1 + len1;
	const mio_uch_t* end2 = str2 + len2;

	if (ignorecase)
	{
		while (str1 < end1)
		{
			c1 = mio_to_uch_lower(*str1);
			if (str2 < end2) 
			{
				c2 = mio_to_uch_lower(*str2);
				if (c1 > c2) return 1;
				if (c1 < c2) return -1;
			}
			else return 1;
			str1++; str2++;
		}
	}
	else
	{
		while (str1 < end1)
		{
			c1 = *str1;
			if (str2 < end2) 
			{
				c2 = *str2;
				if (c1 > c2) return 1;
				if (c1 < c2) return -1;
			}
			else return 1;
			str1++; str2++;
		}
	}

	return (str2 < end2)? -1: 0;
}

int mio_comp_bchars (const mio_bch_t* str1, mio_oow_t len1, const mio_bch_t* str2, mio_oow_t len2, int ignorecase)
{
	mio_bchu_t c1, c2;
	const mio_bch_t* end1 = str1 + len1;
	const mio_bch_t* end2 = str2 + len2;

	if (ignorecase)
	{
		while (str1 < end1)
		{
			c1 = mio_to_bch_lower(*str1);
			if (str2 < end2) 
			{
				c2 = mio_to_bch_lower(*str2);
				if (c1 > c2) return 1;
				if (c1 < c2) return -1;
			}
			else return 1;
			str1++; str2++;
		}
	}
	else
	{
		while (str1 < end1)
		{
			c1 = *str1;
			if (str2 < end2) 
			{
				c2 = *str2;
				if (c1 > c2) return 1;
				if (c1 < c2) return -1;
			}
			else return 1;
			str1++; str2++;
		}
	}

	return (str2 < end2)? -1: 0;
}

int mio_comp_ucstr (const mio_uch_t* str1, const mio_uch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		while (mio_to_uch_lower(*str1) == mio_to_uch_lower(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((mio_uchu_t)mio_to_uch_lower(*str1) > (mio_uchu_t)mio_to_uch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((mio_uchu_t)*str1 > (mio_uchu_t)*str2)? 1: -1;
	}
}

int mio_comp_bcstr (const mio_bch_t* str1, const mio_bch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		while (mio_to_bch_lower(*str1) == mio_to_bch_lower(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((mio_bchu_t)mio_to_bch_lower(*str1) > (mio_bchu_t)mio_to_bch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((mio_bchu_t)*str1 > (mio_bchu_t)*str2)? 1: -1;
	}
}

int mio_comp_ucstr_limited (const mio_uch_t* str1, const mio_uch_t* str2, mio_oow_t maxlen, int ignorecase)
{
	if (maxlen == 0) return 0;

	if (ignorecase)
	{
		while (mio_to_uch_lower(*str1) == mio_to_uch_lower(*str2))
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((mio_uchu_t)mio_to_uch_lower(*str1) > (mio_uchu_t)mio_to_uch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((mio_uchu_t)*str1 > (mio_uchu_t)*str2)? 1: -1;
	}
}

int mio_comp_bcstr_limited (const mio_bch_t* str1, const mio_bch_t* str2, mio_oow_t maxlen, int ignorecase)
{
	if (maxlen == 0) return 0;

	if (ignorecase)
	{
		while (mio_to_uch_lower(*str1) == mio_to_uch_lower(*str2))
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((mio_bchu_t)mio_to_uch_lower(*str1) > (mio_bchu_t)mio_to_uch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((mio_bchu_t)*str1 > (mio_bchu_t)*str2)? 1: -1;
	}
}

int mio_comp_ucstr_bcstr (const mio_uch_t* str1, const mio_bch_t* str2, int ignorecase)
{
	while (*str1 == *str2)
	{
		if (*str1 == '\0') return 0;
		str1++; str2++;
	}

	return ((mio_uchu_t)*str1 > (mio_bchu_t)*str2)? 1: -1;
}

int mio_comp_uchars_ucstr (const mio_uch_t* str1, mio_oow_t len, const mio_uch_t* str2, int ignorecase)
{
	/* for "abc\0" of length 4 vs "abc", the fourth character
	 * of the first string is equal to the terminating null of
	 * the second string. the first string is still considered 
	 * bigger */
	if (ignorecase)
	{
		const mio_uch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((mio_uchu_t)*str1 > (mio_uchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const mio_uch_t* end = str1 + len;
		mio_uch_t c1;
		mio_uch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = mio_to_uch_lower(*str1);
			c2 = mio_to_uch_lower(*str2);
			if (c1 != c2) return ((mio_uchu_t)c1 > (mio_uchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

int mio_comp_uchars_bcstr (const mio_uch_t* str1, mio_oow_t len, const mio_bch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		const mio_uch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((mio_uchu_t)*str1 > (mio_bchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const mio_uch_t* end = str1 + len;
		mio_uch_t c1;
		mio_bch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = mio_to_uch_lower(*str1);
			c2 = mio_to_bch_lower(*str2);
			if (c1 != c2) return ((mio_uchu_t)c1 > (mio_bchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

int mio_comp_bchars_bcstr (const mio_bch_t* str1, mio_oow_t len, const mio_bch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		const mio_bch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((mio_bchu_t)*str1 > (mio_bchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const mio_bch_t* end = str1 + len;
		mio_bch_t c1;
		mio_bch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = mio_to_bch_lower(*str1);
			c2 = mio_to_bch_lower(*str2);
			if (c1 != c2) return ((mio_bchu_t)c1 > (mio_bchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

int mio_comp_bchars_ucstr (const mio_bch_t* str1, mio_oow_t len, const mio_uch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		const mio_bch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((mio_bchu_t)*str1 > (mio_uchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const mio_bch_t* end = str1 + len;
		mio_bch_t c1;
		mio_uch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = mio_to_bch_lower(*str1);
			c2 = mio_to_uch_lower(*str2);
			if (c1 != c2) return ((mio_bchu_t)c1 > (mio_uchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

/* ========================================================================= */

void mio_copy_uchars (mio_uch_t* dst, const mio_uch_t* src, mio_oow_t len)
{
	/* take note of no forced null termination */
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

void mio_copy_bchars (mio_bch_t* dst, const mio_bch_t* src, mio_oow_t len)
{
	/* take note of no forced null termination */
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

void mio_copy_bchars_to_uchars (mio_uch_t* dst, const mio_bch_t* src, mio_oow_t len)
{
	/* copy without conversions.
	 * use mio_convbtouchars() for conversion encoding */
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

void mio_copy_uchars_to_bchars (mio_bch_t* dst, const mio_uch_t* src, mio_oow_t len)
{
	/* copy without conversions.
	 * use mio_convutobchars() for conversion encoding */
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

mio_oow_t mio_copy_uchars_to_ucstr (mio_uch_t* dst, mio_oow_t dlen, const mio_uch_t* src, mio_oow_t slen)
{
	mio_oow_t i;
	if (dlen <= 0) return 0;
	if (dlen <= slen) slen = dlen - 1;
	for (i = 0; i < slen; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

mio_oow_t mio_copy_bchars_to_bcstr (mio_bch_t* dst, mio_oow_t dlen, const mio_bch_t* src, mio_oow_t slen)
{
	mio_oow_t i;
	if (dlen <= 0) return 0;
	if (dlen <= slen) slen = dlen - 1;
	for (i = 0; i < slen; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

mio_oow_t mio_copy_uchars_to_ucstr_unlimited (mio_uch_t* dst, const mio_uch_t* src, mio_oow_t len)
{
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

mio_oow_t mio_copy_bchars_to_bcstr_unlimited (mio_bch_t* dst, const mio_bch_t* src, mio_oow_t len)
{
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

mio_oow_t mio_copy_ucstr (mio_uch_t* dst, mio_oow_t len, const mio_uch_t* src)
{
	mio_uch_t* p, * p2;

	p = dst; p2 = dst + len - 1;

	while (p < p2)
	{
		 if (*src == '\0') break;
		 *p++ = *src++;
	}

	if (len > 0) *p = '\0';
	return p - dst;
}

mio_oow_t mio_copy_bcstr (mio_bch_t* dst, mio_oow_t len, const mio_bch_t* src)
{
	mio_bch_t* p, * p2;

	p = dst; p2 = dst + len - 1;

	while (p < p2)
	{
		 if (*src == '\0') break;
		 *p++ = *src++;
	}

	if (len > 0) *p = '\0';
	return p - dst;
}


mio_oow_t mio_copy_ucstr_unlimited (mio_uch_t* dst, const mio_uch_t* src)
{
	mio_uch_t* org = dst;
	while ((*dst++ = *src++) != '\0');
	return dst - org - 1;
}

mio_oow_t mio_copy_bcstr_unlimited (mio_bch_t* dst, const mio_bch_t* src)
{
	mio_bch_t* org = dst;
	while ((*dst++ = *src++) != '\0');
	return dst - org - 1;
}

/* ========================================================================= */
void mio_fill_uchars (mio_uch_t* dst, mio_uch_t ch, mio_oow_t len)
{
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = ch;
}

void mio_fill_bchars (mio_bch_t* dst, mio_bch_t ch, mio_oow_t len)
{
	mio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = ch;
}

mio_oow_t mio_count_ucstr (const mio_uch_t* str)
{
	const mio_uch_t* ptr = str;
	while (*ptr != '\0') ptr++;
	return ptr - str;
}

mio_oow_t mio_count_bcstr (const mio_bch_t* str)
{
	const mio_bch_t* ptr = str;
	while (*ptr != '\0') ptr++;
	return ptr - str;
}

mio_uch_t* mio_find_uchar (const mio_uch_t* ptr, mio_oow_t len, mio_uch_t c)
{
	const mio_uch_t* end;

	end = ptr + len;
	while (ptr < end)
	{
		if (*ptr == c) return (mio_uch_t*)ptr;
		ptr++;
	}

	return MIO_NULL;
}

mio_bch_t* mio_find_bchar (const mio_bch_t* ptr, mio_oow_t len, mio_bch_t c)
{
	const mio_bch_t* end;

	end = ptr + len;
	while (ptr < end)
	{
		if (*ptr == c) return (mio_bch_t*)ptr;
		ptr++;
	}

	return MIO_NULL;
}

mio_uch_t* mio_rfind_uchar (const mio_uch_t* ptr, mio_oow_t len, mio_uch_t c)
{
	const mio_uch_t* cur;

	cur = ptr + len;
	while (cur > ptr)
	{
		--cur;
		if (*cur == c) return (mio_uch_t*)cur;
	}

	return MIO_NULL;
}

mio_bch_t* mio_rfind_bchar (const mio_bch_t* ptr, mio_oow_t len, mio_bch_t c)
{
	const mio_bch_t* cur;

	cur = ptr + len;
	while (cur > ptr)
	{
		--cur;
		if (*cur == c) return (mio_bch_t*)cur;
	}

	return MIO_NULL;
}

mio_uch_t* mio_find_uchar_in_ucstr (const mio_uch_t* ptr, mio_uch_t c)
{
	while (*ptr != '\0')
	{
		if (*ptr == c) return (mio_uch_t*)ptr;
		ptr++;
	}

	return MIO_NULL;
}

mio_bch_t* mio_find_bchar_in_bcstr (const mio_bch_t* ptr, mio_bch_t c)
{
	while (*ptr != '\0')
	{
		if (*ptr == c) return (mio_bch_t*)ptr;
		ptr++;
	}

	return MIO_NULL;
}

#define IS_BCH_WORD_DELIM(x,delim) (mio_is_bch_space(x) || (x) == delim)
#define IS_UCH_WORD_DELIM(x,delim) (mio_is_uch_space(x) || (x) == delim)

const mio_bch_t* mio_find_bcstr_word_in_bcstr (const mio_bch_t* str, const mio_bch_t* word, mio_bch_t extra_delim, int ignorecase)
{
	/* find a full word in a string */

	const mio_bch_t* ptr = str;

	if (extra_delim == '\0') extra_delim = ' ';
	do
	{
		const mio_bch_t* s;

		while (IS_BCH_WORD_DELIM(*ptr,extra_delim)) ptr++;
		if (*ptr == '\0') return MIO_NULL;

		s = ptr;
		while (*ptr != '\0' && !IS_BCH_WORD_DELIM(*ptr,extra_delim)) ptr++;

		if (mio_comp_bchars_bcstr(s, ptr - s, word, ignorecase) == 0) return s;
	}
	while (*ptr != '\0');

	return MIO_NULL;
}

const mio_uch_t* mio_find_ucstr_word_in_ucstr (const mio_uch_t* str, const mio_uch_t* word, mio_uch_t extra_delim, int ignorecase)
{
	/* find a full word in a string */

	const mio_uch_t* ptr = str;

	if (extra_delim == '\0') extra_delim = ' ';
	do
	{
		const mio_uch_t* s;

		while (IS_UCH_WORD_DELIM(*ptr,extra_delim)) ptr++;
		if (*ptr == '\0') return MIO_NULL;

		s = ptr;
		while (*ptr != '\0' && !IS_UCH_WORD_DELIM(*ptr,extra_delim)) ptr++;

		if (mio_comp_uchars_ucstr(s, ptr - s, word, ignorecase) == 0) return s;
	}
	while (*ptr != '\0');

	return MIO_NULL;
}

/* ========================================================================= */
#define IS_UCH_SPACE(x) ((x) ==' ' || (x) == '\t' || (x) == '\n' || (x) == '\r')
#define IS_BCH_SPACE(x) ((x) ==' ' || (x) == '\t' || (x) == '\n' || (x) == '\r')

int mio_split_ucstr (mio_uch_t* s, const mio_uch_t* delim, mio_uch_t lquote, mio_uch_t rquote, mio_uch_t escape)
{
	mio_uch_t* p = s, *d;
	mio_uch_t* sp = MIO_NULL, * ep = MIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == MIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (mio_uch_t*)delim; *d != '\0'; d++)
			if (!IS_UCH_SPACE(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (IS_UCH_SPACE(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != '\0' && *p == lquote) 
		{
			mio_copy_ucstr_unlimited (p, p + 1);

			for (;;) 
			{
				if (*p == '\0') return -1;

				if (escape != '\0' && *p == escape) 
				{
					mio_copy_ucstr_unlimited (p, p + 1);
				}
				else 
				{
					if (*p == rquote) 
					{
						p++;
						break;
					}
				}

				if (sp == 0) sp = p;
				ep = p;
				p++;
			}
			while (IS_UCH_SPACE(*p)) p++;
			if (*p != '\0') return -1;

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (mio_uch_t*)sp) mio_copy_ucstr_unlimited (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!IS_UCH_SPACE(*p)) 
				{
					if (sp == 0) sp = p;
					ep = p;
				}
				p++;
			}

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (mio_uch_t*)sp) mio_copy_ucstr_unlimited (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		mio_uch_t* o;

		while (*p) 
		{
			o = p;
			while (IS_UCH_SPACE(*p)) p++;
			if (o != p) { mio_copy_ucstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				mio_copy_ucstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						mio_copy_ucstr_unlimited (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = '\0';
							cnt++;
							break;
						}
					}
					p++;
				}
			}
			else 
			{
				o = p;
				for (;;) 
				{
					if (*p == '\0') 
					{
						if (o != p) cnt++;
						break;
					}
					if (IS_UCH_SPACE (*p)) 
					{
						*p++ = '\0';
						cnt++;
						break;
					}
					p++;
				}
			}
		}
	}
	else /* if (delim_mode == 2) */
	{
		mio_uch_t* o;
		int ok;

		while (*p != '\0') 
		{
			o = p;
			while (IS_UCH_SPACE(*p)) p++;
			if (o != p) { mio_copy_ucstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				mio_copy_ucstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						mio_copy_ucstr_unlimited (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = '\0';
							cnt++;
							break;
						}
					}
					p++;
				}

				ok = 0;
				while (IS_UCH_SPACE(*p)) p++;
				if (*p == '\0') ok = 1;
				for (d = (mio_uch_t*)delim; *d != '\0'; d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						mio_copy_ucstr_unlimited (p, p + 1);
						break;
					}
				}
				if (ok == 0) return -1;
			}
			else 
			{
				o = p; sp = ep = 0;

				for (;;) 
				{
					if (*p == '\0') 
					{
						if (ep) 
						{
							ep[1] = '\0';
							p = &ep[1];
						}
						cnt++;
						break;
					}
					for (d = (mio_uch_t*)delim; *d != '\0'; d++) 
					{
						if (*p == *d)  
						{
							if (sp == MIO_NULL) 
							{
								mio_copy_ucstr_unlimited (o, p); p = o;
								*p++ = '\0';
							}
							else 
							{
								mio_copy_ucstr_unlimited (&ep[1], p);
								mio_copy_ucstr_unlimited (o, sp);
								o[ep - sp + 1] = '\0';
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == '\0') cnt++;
							goto exit_point;
						}
					}

					if (!IS_UCH_SPACE (*p)) 
					{
						if (sp == MIO_NULL) sp = p;
						ep = p;
					}
					p++;
				}
exit_point:
				;
			}
		}
	}

	return cnt;
}

int mio_split_bcstr (mio_bch_t* s, const mio_bch_t* delim, mio_bch_t lquote, mio_bch_t rquote, mio_bch_t escape)
{
	mio_bch_t* p = s, *d;
	mio_bch_t* sp = MIO_NULL, * ep = MIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == MIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (mio_bch_t*)delim; *d != '\0'; d++)
			if (!IS_BCH_SPACE(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (IS_BCH_SPACE(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != '\0' && *p == lquote) 
		{
			mio_copy_bcstr_unlimited (p, p + 1);

			for (;;) 
			{
				if (*p == '\0') return -1;

				if (escape != '\0' && *p == escape) 
				{
					mio_copy_bcstr_unlimited (p, p + 1);
				}
				else 
				{
					if (*p == rquote) 
					{
						p++;
						break;
					}
				}

				if (sp == 0) sp = p;
				ep = p;
				p++;
			}
			while (IS_BCH_SPACE(*p)) p++;
			if (*p != '\0') return -1;

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (mio_bch_t*)sp) mio_copy_bcstr_unlimited (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!IS_BCH_SPACE(*p)) 
				{
					if (sp == 0) sp = p;
					ep = p;
				}
				p++;
			}

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (mio_bch_t*)sp) mio_copy_bcstr_unlimited (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		mio_bch_t* o;

		while (*p) 
		{
			o = p;
			while (IS_BCH_SPACE(*p)) p++;
			if (o != p) { mio_copy_bcstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				mio_copy_bcstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						mio_copy_bcstr_unlimited (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = '\0';
							cnt++;
							break;
						}
					}
					p++;
				}
			}
			else 
			{
				o = p;
				for (;;) 
				{
					if (*p == '\0') 
					{
						if (o != p) cnt++;
						break;
					}
					if (IS_BCH_SPACE (*p)) 
					{
						*p++ = '\0';
						cnt++;
						break;
					}
					p++;
				}
			}
		}
	}
	else /* if (delim_mode == 2) */
	{
		mio_bch_t* o;
		int ok;

		while (*p != '\0') 
		{
			o = p;
			while (IS_BCH_SPACE(*p)) p++;
			if (o != p) { mio_copy_bcstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				mio_copy_bcstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						mio_copy_bcstr_unlimited (p, p + 1);
					}
					else 
					{
						if (*p == rquote) 
						{
							*p++ = '\0';
							cnt++;
							break;
						}
					}
					p++;
				}

				ok = 0;
				while (IS_BCH_SPACE(*p)) p++;
				if (*p == '\0') ok = 1;
				for (d = (mio_bch_t*)delim; *d != '\0'; d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						mio_copy_bcstr_unlimited (p, p + 1);
						break;
					}
				}
				if (ok == 0) return -1;
			}
			else 
			{
				o = p; sp = ep = 0;

				for (;;) 
				{
					if (*p == '\0') 
					{
						if (ep) 
						{
							ep[1] = '\0';
							p = &ep[1];
						}
						cnt++;
						break;
					}
					for (d = (mio_bch_t*)delim; *d != '\0'; d++) 
					{
						if (*p == *d)  
						{
							if (sp == MIO_NULL) 
							{
								mio_copy_bcstr_unlimited (o, p); p = o;
								*p++ = '\0';
							}
							else 
							{
								mio_copy_bcstr_unlimited (&ep[1], p);
								mio_copy_bcstr_unlimited (o, sp);
								o[ep - sp + 1] = '\0';
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == '\0') cnt++;
							goto exit_point;
						}
					}

					if (!IS_BCH_SPACE (*p)) 
					{
						if (sp == MIO_NULL) sp = p;
						ep = p;
					}
					p++;
				}
exit_point:
				;
			}
		}
	}

	return cnt;
}

/* ========================================================================= */

mio_oow_t mio_byte_to_bcstr (mio_uint8_t byte, mio_bch_t* buf, mio_oow_t size, int flagged_radix, mio_bch_t fill)
{
	mio_bch_t tmp[(MIO_SIZEOF(mio_uint8_t) * MIO_BITS_PER_BYTE)];
	mio_bch_t* p = tmp, * bp = buf, * be = buf + size - 1;
	int radix;
	mio_bch_t radix_char;
 
	radix = (flagged_radix & MIO_BYTE_TO_BCSTR_RADIXMASK);
	radix_char = (flagged_radix & MIO_BYTE_TO_BCSTR_LOWERCASE)? 'a': 'A';
	if (radix < 2 || radix > 36 || size <= 0) return 0;
 
	do 
	{
		mio_uint8_t digit = byte % radix;	
		if (digit < 10) *p++ = digit + '0';
		else *p++ = digit + radix_char - 10;
		byte /= radix;
	}
	while (byte > 0);
 
	if (fill != '\0') 
	{
		while (size - 1 > p - tmp) 
		{
			*bp++ = fill;
			size--;
		}
	}
 
	while (p > tmp && bp < be) *bp++ = *--p;
	*bp = '\0';
	return bp - buf;
}
 
/* ========================================================================= */

MIO_INLINE int mio_conv_bchars_to_uchars_with_cmgr (
	const mio_bch_t* bcs, mio_oow_t* bcslen,
	mio_uch_t* ucs, mio_oow_t* ucslen, mio_cmgr_t* cmgr, int all)
{
	const mio_bch_t* p;
	int ret = 0;
	mio_oow_t mlen;

	if (ucs)
	{
		/* destination buffer is specified. 
		 * copy the conversion result to the buffer */

		mio_uch_t* q, * qend;

		p = bcs;
		q = ucs;
		qend = ucs + *ucslen;
		mlen = *bcslen;

		while (mlen > 0)
		{
			mio_oow_t n;

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

		mio_uch_t w;
		mio_oow_t wlen = 0;

		p = bcs;
		mlen = *bcslen;

		while (mlen > 0)
		{
			mio_oow_t n;

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

MIO_INLINE int mio_conv_bcstr_to_ucstr_with_cmgr (
	const mio_bch_t* bcs, mio_oow_t* bcslen,
	mio_uch_t* ucs, mio_oow_t* ucslen, mio_cmgr_t* cmgr, int all)
{
	const mio_bch_t* bp;
	mio_oow_t mlen, wlen;
	int n;

	for (bp = bcs; *bp != '\0'; bp++) /* nothing */ ;

	mlen = bp - bcs; wlen = *ucslen;
	n = mio_conv_bchars_to_uchars_with_cmgr (bcs, &mlen, ucs, &wlen, cmgr, all);
	if (ucs)
	{
		/* null-terminate the target buffer if it has room for it. */
		if (wlen < *ucslen) ucs[wlen] = '\0';
		else n = -2; /* buffer too small */
	}
	*bcslen = mlen; *ucslen = wlen;

	return n;
}

MIO_INLINE int mio_conv_uchars_to_bchars_with_cmgr (
	const mio_uch_t* ucs, mio_oow_t* ucslen,
	mio_bch_t* bcs, mio_oow_t* bcslen, mio_cmgr_t* cmgr)
{
	const mio_uch_t* p = ucs;
	const mio_uch_t* end = ucs + *ucslen;
	int ret = 0; 

	if (bcs)
	{
		mio_oow_t rem = *bcslen;

		while (p < end) 
		{
			mio_oow_t n;

			if (rem <= 0)
			{
				ret = -2; /* buffer too small */
				break;
			}

			n = cmgr->uctobc (*p, bcs, rem);
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
		mio_bch_t bcsbuf[MIO_BCSIZE_MAX];
		mio_oow_t mlen = 0;

		while (p < end)
		{
			mio_oow_t n;

			n = cmgr->uctobc (*p, bcsbuf, MIO_COUNTOF(bcsbuf));
			if (n == 0) 
			{
				ret = -1;
				break; /* illegal character */
			}

			/* it assumes that bcsbuf is large enough to hold a character */
			/*MIO_ASSERT (mio, n <= MIO_COUNTOF(bcsbuf));*/

			p++; mlen += n;
		}

		/* this length excludes the terminating null character. 
		 * this function doesn't even null-terminate the result. */
		*bcslen = mlen;
	}

	*ucslen = p - ucs;
	return ret;
}

MIO_INLINE int mio_conv_ucstr_to_bcstr_with_cmgr (
	const mio_uch_t* ucs, mio_oow_t* ucslen,
	mio_bch_t* bcs, mio_oow_t* bcslen, mio_cmgr_t* cmgr)
{
	const mio_uch_t* p = ucs;
	int ret = 0;

	if (bcs)
	{
		mio_oow_t rem = *bcslen;

		while (*p != '\0')
		{
			mio_oow_t n;

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
		mio_bch_t bcsbuf[MIO_BCSIZE_MAX];
		mio_oow_t mlen = 0;

		while (*p != '\0')
		{
			mio_oow_t n;

			n = cmgr->uctobc(*p, bcsbuf, MIO_COUNTOF(bcsbuf));
			if (n == 0) 
			{
				ret = -1;
				break; /* illegal character */
			}

			/* it assumes that bcs is large enough to hold a character */
			/*MIO_ASSERT (mio, n <= MIO_COUNTOF(bcs));*/

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

static mio_cmgr_t utf8_cmgr =
{
	mio_utf8_to_uc,
	mio_uc_to_utf8
};

mio_cmgr_t* mio_get_utf8_cmgr (void)
{
	return &utf8_cmgr;
}

int mio_conv_utf8_to_uchars (const mio_bch_t* bcs, mio_oow_t* bcslen, mio_uch_t* ucs, mio_oow_t* ucslen)
{
	/* the source is length bound */
	return mio_conv_bchars_to_uchars_with_cmgr(bcs, bcslen, ucs, ucslen, &utf8_cmgr, 0);
}

int mio_conv_uchars_to_utf8 (const mio_uch_t* ucs, mio_oow_t* ucslen, mio_bch_t* bcs, mio_oow_t* bcslen)
{
	/* length bound */
	return mio_conv_uchars_to_bchars_with_cmgr(ucs, ucslen, bcs, bcslen, &utf8_cmgr);
}

int mio_conv_utf8_to_ucstr (const mio_bch_t* bcs, mio_oow_t* bcslen, mio_uch_t* ucs, mio_oow_t* ucslen)
{
	/* null-terminated. */
	return mio_conv_bcstr_to_ucstr_with_cmgr(bcs, bcslen, ucs, ucslen, &utf8_cmgr, 0);
}

int mio_conv_ucstr_to_utf8 (const mio_uch_t* ucs, mio_oow_t* ucslen, mio_bch_t* bcs, mio_oow_t* bcslen)
{
	/* null-terminated */
	return mio_conv_ucstr_to_bcstr_with_cmgr(ucs, ucslen, bcs, bcslen, &utf8_cmgr);
}

/* ----------------------------------------------------------------------- */

int mio_convbtouchars (mio_t* mio, const mio_bch_t* bcs, mio_oow_t* bcslen, mio_uch_t* ucs, mio_oow_t* ucslen, int all)
{
	/* length bound */
	int n;

	n = mio_conv_bchars_to_uchars_with_cmgr(bcs, bcslen, ucs, ucslen, mio_getcmgr(mio), all);

	if (n <= -1)
	{
		/* -1: illegal character, -2: buffer too small, -3: incomplete sequence */
		mio_seterrnum (mio, (n == -2)? MIO_EBUFFULL: MIO_EECERR);
	}

	return n;
}

int mio_convutobchars (mio_t* mio, const mio_uch_t* ucs, mio_oow_t* ucslen, mio_bch_t* bcs, mio_oow_t* bcslen)
{
	/* length bound */
	int n;

	n = mio_conv_uchars_to_bchars_with_cmgr(ucs, ucslen, bcs, bcslen, mio_getcmgr(mio));

	if (n <= -1)
	{
		mio_seterrnum (mio, (n == -2)? MIO_EBUFFULL: MIO_EECERR);
	}

	return n;
}

int mio_convbtoucstr (mio_t* mio, const mio_bch_t* bcs, mio_oow_t* bcslen, mio_uch_t* ucs, mio_oow_t* ucslen, int all)
{
	/* null-terminated. */
	int n;

	n = mio_conv_bcstr_to_ucstr_with_cmgr(bcs, bcslen, ucs, ucslen, mio_getcmgr(mio), all);

	if (n <= -1)
	{
		mio_seterrnum (mio, (n == -2)? MIO_EBUFFULL: MIO_EECERR);
	}

	return n;
}

int mio_convutobcstr (mio_t* mio, const mio_uch_t* ucs, mio_oow_t* ucslen, mio_bch_t* bcs, mio_oow_t* bcslen)
{
	/* null-terminated */
	int n;

	n = mio_conv_ucstr_to_bcstr_with_cmgr(ucs, ucslen, bcs, bcslen, mio_getcmgr(mio));

	if (n <= -1)
	{
		mio_seterrnum (mio, (n == -2)? MIO_EBUFFULL: MIO_EECERR);
	}

	return n;
}

/* ----------------------------------------------------------------------- */

MIO_INLINE mio_uch_t* mio_dupbtoucharswithheadroom (mio_t* mio, mio_oow_t headroom_bytes, const mio_bch_t* bcs, mio_oow_t bcslen, mio_oow_t* ucslen, int all)
{
	mio_oow_t inlen, outlen;
	mio_uch_t* ptr;

	inlen = bcslen;
	if (mio_convbtouchars(mio, bcs, &inlen, MIO_NULL, &outlen, all) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return MIO_NULL;
	}

	ptr = (mio_uch_t*)mio_allocmem(mio, headroom_bytes + ((outlen + 1) * MIO_SIZEOF(mio_uch_t)));
	if (!ptr) return MIO_NULL;

	inlen = bcslen;

	ptr = (mio_uch_t*)((mio_oob_t*)ptr + headroom_bytes);
	mio_convbtouchars (mio, bcs, &inlen, ptr, &outlen, all);

	/* mio_convbtouchars() doesn't null-terminate the target. 
	 * but in mio_dupbtouchars(), i allocate space. so i don't mind
	 * null-terminating it with 1 extra character overhead */
	ptr[outlen] = '\0'; 
	if (ucslen) *ucslen = outlen;
	return ptr;
}

mio_uch_t* mio_dupbtouchars (mio_t* mio, const mio_bch_t* bcs, mio_oow_t bcslen, mio_oow_t* ucslen, int all)
{
	return mio_dupbtoucharswithheadroom (mio, 0, bcs, bcslen, ucslen, all);
}

MIO_INLINE mio_bch_t* mio_duputobcharswithheadroom (mio_t* mio, mio_oow_t headroom_bytes, const mio_uch_t* ucs, mio_oow_t ucslen, mio_oow_t* bcslen)
{
	mio_oow_t inlen, outlen;
	mio_bch_t* ptr;

	inlen = ucslen;
	if (mio_convutobchars(mio, ucs, &inlen, MIO_NULL, &outlen) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return MIO_NULL;
	}

	ptr = (mio_bch_t*)mio_allocmem(mio, headroom_bytes + ((outlen + 1) * MIO_SIZEOF(mio_bch_t)));
	if (!ptr) return MIO_NULL;

	inlen = ucslen;
	ptr = (mio_bch_t*)((mio_oob_t*)ptr + headroom_bytes);
	mio_convutobchars (mio, ucs, &inlen, ptr, &outlen);

	ptr[outlen] = '\0';
	if (bcslen) *bcslen = outlen;
	return ptr;
}

mio_bch_t* mio_duputobchars (mio_t* mio, const mio_uch_t* ucs, mio_oow_t ucslen, mio_oow_t* bcslen)
{
	return mio_duputobcharswithheadroom (mio, 0, ucs, ucslen, bcslen);
}


/* ----------------------------------------------------------------------- */

MIO_INLINE mio_uch_t* mio_dupbtoucstrwithheadroom (mio_t* mio, mio_oow_t headroom_bytes, const mio_bch_t* bcs, mio_oow_t* ucslen, int all)
{
	mio_oow_t inlen, outlen;
	mio_uch_t* ptr;

	if (mio_convbtoucstr(mio, bcs, &inlen, MIO_NULL, &outlen, all) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return MIO_NULL;
	}

	outlen++;
	ptr = (mio_uch_t*)mio_allocmem(mio, headroom_bytes + (outlen * MIO_SIZEOF(mio_uch_t)));
	if (!ptr) return MIO_NULL;

	mio_convbtoucstr (mio, bcs, &inlen, ptr, &outlen, all);
	if (ucslen) *ucslen = outlen;
	return ptr;
}

mio_uch_t* mio_dupbtoucstr (mio_t* mio, const mio_bch_t* bcs, mio_oow_t* ucslen, int all)
{
	return mio_dupbtoucstrwithheadroom (mio, 0, bcs, ucslen, all);
}

MIO_INLINE mio_bch_t* mio_duputobcstrwithheadroom (mio_t* mio, mio_oow_t headroom_bytes, const mio_uch_t* ucs, mio_oow_t* bcslen)
{
	mio_oow_t inlen, outlen;
	mio_bch_t* ptr;

	if (mio_convutobcstr(mio, ucs, &inlen, MIO_NULL, &outlen) <= -1) 
	{
		/* note it's also an error if no full conversion is made in this function */
		return MIO_NULL;
	}

	outlen++;
	ptr = (mio_bch_t*)mio_allocmem(mio, headroom_bytes + (outlen * MIO_SIZEOF(mio_bch_t)));
	if (!ptr) return MIO_NULL;

	ptr = (mio_bch_t*)((mio_oob_t*)ptr + headroom_bytes);

	mio_convutobcstr (mio, ucs, &inlen, ptr, &outlen);
	if (bcslen) *bcslen = outlen;
	return ptr;
}

mio_bch_t* mio_duputobcstr (mio_t* mio, const mio_uch_t* ucs, mio_oow_t* bcslen)
{
	return mio_duputobcstrwithheadroom (mio, 0, ucs, bcslen);
}
/* ----------------------------------------------------------------------- */

mio_uch_t* mio_dupuchars (mio_t* mio, const mio_uch_t* ucs, mio_oow_t ucslen)
{
	mio_uch_t* ptr;

	ptr = (mio_uch_t*)mio_allocmem(mio, (ucslen + 1) * MIO_SIZEOF(mio_uch_t));
	if (!ptr) return MIO_NULL;

	mio_copy_uchars (ptr, ucs, ucslen);
	ptr[ucslen] = '\0';
	return ptr;
}

mio_bch_t* mio_dupbchars (mio_t* mio, const mio_bch_t* bcs, mio_oow_t bcslen)
{
	mio_bch_t* ptr;

	ptr = (mio_bch_t*)mio_allocmem(mio, (bcslen + 1) * MIO_SIZEOF(mio_bch_t));
	if (!ptr) return MIO_NULL;

	mio_copy_bchars (ptr, bcs, bcslen);
	ptr[bcslen] = '\0';
	return ptr;
}


/* ========================================================================= */
mio_uch_t* mio_dupucstr (mio_t* mio, const mio_uch_t* ucs, mio_oow_t* ucslen)
{
	mio_uch_t* ptr;
	mio_oow_t len;

	len = mio_count_ucstr(ucs);

	ptr = (mio_uch_t*)mio_allocmem(mio, (len + 1) * MIO_SIZEOF(mio_uch_t));
	if (!ptr) return MIO_NULL;

	mio_copy_uchars (ptr, ucs, len);
	ptr[len] = '\0';

	if (ucslen) *ucslen = len;
	return ptr;
}

mio_bch_t* mio_dupbcstr (mio_t* mio, const mio_bch_t* bcs, mio_oow_t* bcslen)
{
	mio_bch_t* ptr;
	mio_oow_t len;

	len = mio_count_bcstr(bcs);

	ptr = (mio_bch_t*)mio_allocmem(mio, (len + 1) * MIO_SIZEOF(mio_bch_t));
	if (!ptr) return MIO_NULL;

	mio_copy_bchars (ptr, bcs, len);
	ptr[len] = '\0';

	if (bcslen) *bcslen = len;
	return ptr;
}


/* ========================================================================= */
