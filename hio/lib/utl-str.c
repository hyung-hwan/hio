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


int hio_comp_uchars (const hio_uch_t* str1, hio_oow_t len1, const hio_uch_t* str2, hio_oow_t len2, int ignorecase)
{
	hio_uchu_t c1, c2;
	const hio_uch_t* end1 = str1 + len1;
	const hio_uch_t* end2 = str2 + len2;

	if (ignorecase)
	{
		while (str1 < end1)
		{
			c1 = hio_to_uch_lower(*str1);
			if (str2 < end2) 
			{
				c2 = hio_to_uch_lower(*str2);
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

int hio_comp_bchars (const hio_bch_t* str1, hio_oow_t len1, const hio_bch_t* str2, hio_oow_t len2, int ignorecase)
{
	hio_bchu_t c1, c2;
	const hio_bch_t* end1 = str1 + len1;
	const hio_bch_t* end2 = str2 + len2;

	if (ignorecase)
	{
		while (str1 < end1)
		{
			c1 = hio_to_bch_lower(*str1);
			if (str2 < end2) 
			{
				c2 = hio_to_bch_lower(*str2);
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

int hio_comp_ucstr (const hio_uch_t* str1, const hio_uch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		while (hio_to_uch_lower(*str1) == hio_to_uch_lower(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((hio_uchu_t)hio_to_uch_lower(*str1) > (hio_uchu_t)hio_to_uch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((hio_uchu_t)*str1 > (hio_uchu_t)*str2)? 1: -1;
	}
}

int hio_comp_bcstr (const hio_bch_t* str1, const hio_bch_t* str2, int ignorecase)
{
	if (ignorecase)
	{
		while (hio_to_bch_lower(*str1) == hio_to_bch_lower(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((hio_bchu_t)hio_to_bch_lower(*str1) > (hio_bchu_t)hio_to_bch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((hio_bchu_t)*str1 > (hio_bchu_t)*str2)? 1: -1;
	}
}

int hio_comp_ucstr_limited (const hio_uch_t* str1, const hio_uch_t* str2, hio_oow_t maxlen, int ignorecase)
{
	if (maxlen == 0) return 0;

	if (ignorecase)
	{
		while (hio_to_uch_lower(*str1) == hio_to_uch_lower(*str2))
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((hio_uchu_t)hio_to_uch_lower(*str1) > (hio_uchu_t)hio_to_uch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((hio_uchu_t)*str1 > (hio_uchu_t)*str2)? 1: -1;
	}
}

int hio_comp_bcstr_limited (const hio_bch_t* str1, const hio_bch_t* str2, hio_oow_t maxlen, int ignorecase)
{
	if (maxlen == 0) return 0;

	if (ignorecase)
	{
		while (hio_to_bch_lower(*str1) == hio_to_bch_lower(*str2))
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((hio_bchu_t)hio_to_bch_lower(*str1) > (hio_bchu_t)hio_to_bch_lower(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((hio_bchu_t)*str1 > (hio_bchu_t)*str2)? 1: -1;
	}
}

int hio_comp_uchars_ucstr (const hio_uch_t* str1, hio_oow_t len, const hio_uch_t* str2, int ignorecase)
{
	/* for "abc\0" of length 4 vs "abc", the fourth character
	 * of the first string is equal to the terminating null of
	 * the second string. the first string is still considered 
	 * bigger */
	if (ignorecase)
	{
		const hio_uch_t* end = str1 + len;
		hio_uch_t c1;
		hio_uch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = hio_to_uch_lower(*str1);
			c2 = hio_to_uch_lower(*str2);
			if (c1 != c2) return ((hio_uchu_t)c1 > (hio_uchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const hio_uch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((hio_uchu_t)*str1 > (hio_uchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

int hio_comp_bchars_bcstr (const hio_bch_t* str1, hio_oow_t len, const hio_bch_t* str2, int ignorecase)
{
	/* for "abc\0" of length 4 vs "abc", the fourth character
	 * of the first string is equal to the terminating null of
	 * the second string. the first string is still considered 
	 * bigger */
	if (ignorecase)
	{
		const hio_bch_t* end = str1 + len;
		hio_bch_t c1;
		hio_bch_t c2;
		while (str1 < end && *str2 != '\0') 
		{
			c1 = hio_to_bch_lower(*str1);
			c2 = hio_to_bch_lower(*str2);
			if (c1 != c2) return ((hio_bchu_t)c1 > (hio_bchu_t)c2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
	else
	{
		const hio_bch_t* end = str1 + len;
		while (str1 < end && *str2 != '\0') 
		{
			if (*str1 != *str2) return ((hio_bchu_t)*str1 > (hio_bchu_t)*str2)? 1: -1;
			str1++; str2++;
		}
		return (str1 < end)? 1: (*str2 == '\0'? 0: -1);
	}
}

hio_oow_t hio_concat_uchars_to_ucstr (hio_uch_t* buf, hio_oow_t bsz, const hio_uch_t* str, hio_oow_t len)
{
	hio_uch_t* p, * p2;
	const hio_uch_t* end;
	hio_oow_t blen;

	blen = hio_count_ucstr(buf);
	if (blen >= bsz) return blen; /* something wrong */

	p = buf + blen;
	p2 = buf + bsz - 1;

	end = str + len;

	while (p < p2) 
	{
		if (str >= end) break;
		*p++ = *str++;
	}

	if (bsz > 0) *p = '\0';
	return p - buf;
}

hio_oow_t hio_concat_bchars_to_bcstr (hio_bch_t* buf, hio_oow_t bsz, const hio_bch_t* str, hio_oow_t len)
{
	hio_bch_t* p, * p2;
	const hio_bch_t* end;
	hio_oow_t blen;

	blen = hio_count_bcstr(buf);
	if (blen >= bsz) return blen; /* something wrong */

	p = buf + blen;
	p2 = buf + bsz - 1;

	end = str + len;

	while (p < p2) 
	{
		if (str >= end) break;
		*p++ = *str++;
	}

	if (bsz > 0) *p = '\0';
	return p - buf;
}

hio_oow_t hio_concat_ucstr (hio_uch_t* buf, hio_oow_t bsz, const hio_uch_t* str)
{
	hio_uch_t* p, * p2;
	hio_oow_t blen;

	blen = hio_count_ucstr(buf);
	if (blen >= bsz) return blen; /* something wrong */

	p = buf + blen;
	p2 = buf + bsz - 1;

	while (p < p2) 
	{
		if (*str == '\0') break;
		*p++ = *str++;
	}

	if (bsz > 0) *p = '\0';
	return p - buf;
}

hio_oow_t hio_concat_bcstr (hio_bch_t* buf, hio_oow_t bsz, const hio_bch_t* str)
{
	hio_bch_t* p, * p2;
	hio_oow_t blen;

	blen = hio_count_bcstr(buf);
	if (blen >= bsz) return blen; /* something wrong */

	p = buf + blen;
	p2 = buf + bsz - 1;

	while (p < p2) 
	{
		if (*str == '\0') break;
		*p++ = *str++;
	}

	if (bsz > 0) *p = '\0';
	return p - buf;
}

void hio_copy_uchars (hio_uch_t* dst, const hio_uch_t* src, hio_oow_t len)
{
	/* take note of no forced null termination */
	hio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

void hio_copy_bchars (hio_bch_t* dst, const hio_bch_t* src, hio_oow_t len)
{
	/* take note of no forced null termination */
	hio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
}

hio_oow_t hio_copy_uchars_to_ucstr (hio_uch_t* dst, hio_oow_t dlen, const hio_uch_t* src, hio_oow_t slen)
{
	hio_oow_t i;
	if (dlen <= 0) return 0;
	if (dlen <= slen) slen = dlen - 1;
	for (i = 0; i < slen; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

hio_oow_t hio_copy_bchars_to_bcstr (hio_bch_t* dst, hio_oow_t dlen, const hio_bch_t* src, hio_oow_t slen)
{
	hio_oow_t i;
	if (dlen <= 0) return 0;
	if (dlen <= slen) slen = dlen - 1;
	for (i = 0; i < slen; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

hio_oow_t hio_copy_uchars_to_ucstr_unlimited (hio_uch_t* dst, const hio_uch_t* src, hio_oow_t len)
{
	hio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

hio_oow_t hio_copy_bchars_to_bcstr_unlimited (hio_bch_t* dst, const hio_bch_t* src, hio_oow_t len)
{
	hio_oow_t i;
	for (i = 0; i < len; i++) dst[i] = src[i];
	dst[i] = '\0';
	return i;
}

hio_oow_t hio_copy_ucstr_to_uchars (hio_uch_t* dst, hio_oow_t len, const hio_uch_t* src)
{
	/* no null termination */
	hio_uch_t* p, * p2;

	p = dst; p2 = dst + len - 1;

	while (p < p2)
	{
		if (*src == '\0') break;
		*p++ = *src++;
	}

	return p - dst;
}

hio_oow_t hio_copy_bcstr_to_bchars (hio_bch_t* dst, hio_oow_t len, const hio_bch_t* src)
{
	/* no null termination */
	hio_bch_t* p, * p2;

	p = dst; p2 = dst + len - 1;

	while (p < p2)
	{
		if (*src == '\0') break;
		*p++ = *src++;
	}

	return p - dst;
}

hio_oow_t hio_copy_ucstr (hio_uch_t* dst, hio_oow_t len, const hio_uch_t* src)
{
	hio_uch_t* p, * p2;

	p = dst; p2 = dst + len - 1;

	while (p < p2)
	{
		if (*src == '\0') break;
		*p++ = *src++;
	}

	if (len > 0) *p = '\0';
	return p - dst;
}

hio_oow_t hio_copy_bcstr (hio_bch_t* dst, hio_oow_t len, const hio_bch_t* src)
{
	hio_bch_t* p, * p2;

	p = dst; p2 = dst + len - 1;

	while (p < p2)
	{
		if (*src == '\0') break;
		*p++ = *src++;
	}

	if (len > 0) *p = '\0';
	return p - dst;
}

hio_oow_t hio_copy_ucstr_unlimited (hio_uch_t* dst, const hio_uch_t* src)
{
	hio_uch_t* org = dst;
	while ((*dst++ = *src++) != '\0');
	return dst - org - 1;
}

hio_oow_t hio_copy_bcstr_unlimited (hio_bch_t* dst, const hio_bch_t* src)
{
	hio_bch_t* org = dst;
	while ((*dst++ = *src++) != '\0');
	return dst - org - 1;
}

hio_oow_t hio_copy_fmt_ucstrs_to_ucstr (hio_uch_t* buf, hio_oow_t bsz, const hio_uch_t* fmt, const hio_uch_t* str[])
{
	hio_uch_t* b = buf;
	hio_uch_t* end = buf + bsz - 1;
	const hio_uch_t* f = fmt;

	if (bsz <= 0) return 0;

	while (*f != '\0')
	{
		if (*f == '\\')
		{
			/* get the escaped character and treat it normally.
			 * if the escaper is the last character, treat it
			 * normally also. */
			if (f[1] != '\0') f++;
		}
		else if (*f == '$')
		{
			if (f[1] == '{' && (f[2] >= '0' && f[2] <= '9'))
			{
				const hio_uch_t* tmp;
				hio_oow_t idx = 0;

				tmp = f;
				f += 2;

				do idx = idx * 10 + (*f++ - '0');
				while (*f >= '0' && *f <= '9');

				if (*f != '}')
				{
					f = tmp;
					goto normal;
				}

				f++;

				tmp = str[idx];
				while (*tmp != '\0')
				{
					if (b >= end) goto fini;
					*b++ = *tmp++;
				}
				continue;
			}
			else if (f[1] == '$') f++;
		}

	normal:
		if (b >= end) break;
		*b++ = *f++;
	}

fini:
	*b = '\0';
	return b - buf;
}

hio_oow_t hio_copy_fmt_bcstrs_to_bcstr (hio_bch_t* buf, hio_oow_t bsz, const hio_bch_t* fmt, const hio_bch_t* str[])
{
	hio_bch_t* b = buf;
	hio_bch_t* end = buf + bsz - 1;
	const hio_bch_t* f = fmt;

	if (bsz <= 0) return 0;

	while (*f != '\0')
	{
		if (*f == '\\')
		{
			/* get the escaped character and treat it normally.
			 * if the escaper is the last character, treat it
			 * normally also. */
			if (f[1] != '\0') f++;
		}
		else if (*f == '$')
		{
			if (f[1] == '{' && (f[2] >= '0' && f[2] <= '9'))
			{
				const hio_bch_t* tmp;
				hio_oow_t idx = 0;

				tmp = f;
				f += 2;

				do idx = idx * 10 + (*f++ - '0');
				while (*f >= '0' && *f <= '9');

				if (*f != '}')
				{
					f = tmp;
					goto normal;
				}

				f++;

				tmp = str[idx];
				while (*tmp != '\0')
				{
					if (b >= end) goto fini;
					*b++ = *tmp++;
				}
				continue;
			}
			else if (f[1] == '$') f++;
		}

	normal:
		if (b >= end) break;
		*b++ = *f++;
	}

fini:
	*b = '\0';
	return b - buf;
}

hio_oow_t hio_copy_fmt_ucses_to_ucstr (hio_uch_t* buf, hio_oow_t bsz, const hio_uch_t* fmt, const hio_ucs_t str[])
{
	hio_uch_t* b = buf;
	hio_uch_t* end = buf + bsz - 1;
	const hio_uch_t* f = fmt;
 
	if (bsz <= 0) return 0;
 
	while (*f != '\0')
	{
		if (*f == '\\')
		{
			/* get the escaped character and treat it normally.
			 * if the escaper is the last character, treat it 
			 * normally also. */
			if (f[1] != '\0') f++;
		}
		else if (*f == '$')
		{
			if (f[1] == '{' && (f[2] >= '0' && f[2] <= '9'))
			{
				const hio_uch_t* tmp, * tmpend;
				hio_oow_t idx = 0;
 
				tmp = f;
				f += 2;
 
				do idx = idx * 10 + (*f++ - '0');
				while (*f >= '0' && *f <= '9');
	
				if (*f != '}')
				{
					f = tmp;
					goto normal;
				}
 
				f++;
				
				tmp = str[idx].ptr;
				tmpend = tmp + str[idx].len;
 
				while (tmp < tmpend)
				{
					if (b >= end) goto fini;
					*b++ = *tmp++;
				}
				continue;
			}
			else if (f[1] == '$') f++;
		}
 
	normal:
		if (b >= end) break;
		*b++ = *f++;
	}
 
fini:
	*b = '\0';
	return b - buf;
}

hio_oow_t hio_copy_fmt_bcses_to_bcstr (hio_bch_t* buf, hio_oow_t bsz, const hio_bch_t* fmt, const hio_bcs_t str[])
{
	hio_bch_t* b = buf;
	hio_bch_t* end = buf + bsz - 1;
	const hio_bch_t* f = fmt;
 
	if (bsz <= 0) return 0;
 
	while (*f != '\0')
	{
		if (*f == '\\')
		{
			/* get the escaped character and treat it normally.
			 * if the escaper is the last character, treat it 
			 * normally also. */
			if (f[1] != '\0') f++;
		}
		else if (*f == '$')
		{
			if (f[1] == '{' && (f[2] >= '0' && f[2] <= '9'))
			{
				const hio_bch_t* tmp, * tmpend;
				hio_oow_t idx = 0;
 
				tmp = f;
				f += 2;
 
				do idx = idx * 10 + (*f++ - '0');
				while (*f >= '0' && *f <= '9');
	
				if (*f != '}')
				{
					f = tmp;
					goto normal;
				}
 
				f++;
				
				tmp = str[idx].ptr;
				tmpend = tmp + str[idx].len;
 
				while (tmp < tmpend)
				{
					if (b >= end) goto fini;
					*b++ = *tmp++;
				}
				continue;
			}
			else if (f[1] == '$') f++;
		}
 
	normal:
		if (b >= end) break;
		*b++ = *f++;
	}
 
fini:
	*b = '\0';
	return b - buf;
}

hio_oow_t hio_count_ucstr (const hio_uch_t* str)
{
	const hio_uch_t* ptr = str;
	while (*ptr != '\0') ptr++;
	return ptr - str;
} 

hio_oow_t hio_count_bcstr (const hio_bch_t* str)
{
	const hio_bch_t* ptr = str;
	while (*ptr != '\0') ptr++;
	return ptr - str;
} 

hio_oow_t hio_count_ucstr_limited (const hio_uch_t* str, hio_oow_t maxlen)
{
	hio_oow_t i;
	for (i = 0; i < maxlen; i++)
	{
		if (str[i] == '\0') break;
	}
	return i;
}

hio_oow_t hio_count_bcstr_limited (const hio_bch_t* str, hio_oow_t maxlen)
{
	hio_oow_t i;
	for (i = 0; i < maxlen; i++)
	{
		if (str[i] == '\0') break;
	}
	return i;
}

int hio_equal_uchars (const hio_uch_t* str1, const hio_uch_t* str2, hio_oow_t len)
{
	hio_oow_t i;

	/* NOTE: you should call this function after having ensured that
	 *       str1 and str2 are in the same length */

	for (i = 0; i < len; i++)
	{
		if (str1[i] != str2[i]) return 0;
	}

	return 1;
}

int hio_equal_bchars (const hio_bch_t* str1, const hio_bch_t* str2, hio_oow_t len)
{
	hio_oow_t i;

	/* NOTE: you should call this function after having ensured that
	 *       str1 and str2 are in the same length */

	for (i = 0; i < len; i++)
	{
		if (str1[i] != str2[i]) return 0;
	}

	return 1;
}

void hio_fill_uchars (hio_uch_t* dst, hio_uch_t ch, hio_oow_t len)
{
        hio_oow_t i;
        for (i = 0; i < len; i++) dst[i] = ch;
}

void hio_fill_bchars (hio_bch_t* dst, hio_bch_t ch, hio_oow_t len)
{
        hio_oow_t i;
        for (i = 0; i < len; i++) dst[i] = ch;
}

hio_uch_t* hio_find_uchar_in_uchars (const hio_uch_t* ptr, hio_oow_t len, hio_uch_t c)
{
	const hio_uch_t* end;

	end = ptr + len;
	while (ptr < end)
	{
		if (*ptr == c) return (hio_uch_t*)ptr;
		ptr++;
	}

	return HIO_NULL;
}

hio_bch_t* hio_find_bchar_in_bchars (const hio_bch_t* ptr, hio_oow_t len, hio_bch_t c)
{
	const hio_bch_t* end;

	end = ptr + len;
	while (ptr < end)
	{
		if (*ptr == c) return (hio_bch_t*)ptr;
		ptr++;
	}

	return HIO_NULL;
}

hio_uch_t* hio_rfind_uchar_in_uchars (const hio_uch_t* ptr, hio_oow_t len, hio_uch_t c)
{
	const hio_uch_t* cur;

	cur = ptr + len;
	while (cur > ptr)
	{
		--cur;
		if (*cur == c) return (hio_uch_t*)cur;
	}

	return HIO_NULL;
}

hio_bch_t* hio_rfind_bchar_in_bchars (const hio_bch_t* ptr, hio_oow_t len, hio_bch_t c)
{
	const hio_bch_t* cur;

	cur = ptr + len;
	while (cur > ptr)
	{
		--cur;
		if (*cur == c) return (hio_bch_t*)cur;
	}

	return HIO_NULL;
}

hio_uch_t* hio_find_uchar_in_ucstr (const hio_uch_t* ptr, hio_uch_t c)
{
	while (*ptr != '\0')
	{
		if (*ptr == c) return (hio_uch_t*)ptr;
		ptr++;
	}

	return HIO_NULL;
}

hio_bch_t* hio_find_bchar_in_bcstr (const hio_bch_t* ptr, hio_bch_t c)
{
	while (*ptr != '\0')
	{
		if (*ptr == c) return (hio_bch_t*)ptr;
		ptr++;
	}

	return HIO_NULL;
}

hio_uch_t* hio_rfind_uchar_in_ucstr (const hio_uch_t* str, hio_uch_t c)
{
	const hio_uch_t* ptr = str;
	while (*ptr != '\0') ptr++;

	while (ptr > str)
	{
		--ptr;
		if (*ptr == c) return (hio_uch_t*)ptr;
	}

	return HIO_NULL;
}

hio_bch_t* hio_rfind_bchar_in_bcstr (const hio_bch_t* str, hio_bch_t c)
{
	const hio_bch_t* ptr = str;
	while (*ptr != '\0') ptr++;

	while (ptr > str)
	{
		--ptr;
		if (*ptr == c) return (hio_bch_t*)ptr;
	}

	return HIO_NULL;
}

hio_uch_t* hio_find_uchars_in_uchars (const hio_uch_t* str, hio_oow_t strsz, const hio_uch_t* sub, hio_oow_t subsz, int ignorecase)
{
	const hio_uch_t* end, * subp;

	if (subsz == 0) return (hio_uch_t*)str;
	if (strsz < subsz) return HIO_NULL;
	
	end = str + strsz - subsz;
	subp = sub + subsz;

	if (HIO_UNLIKELY(ignorecase))
	{
		while (str <= end) 
		{
			const hio_uch_t* x = str;
			const hio_uch_t* y = sub;

			while (1)
			{
				if (y >= subp) return (hio_uch_t*)str;
				if (hio_to_uch_lower(*x) != hio_to_uch_lower(*y)) break;
				x++; y++;
			}

			str++;
		}
	}
	else
	{
		while (str <= end) 
		{
			const hio_uch_t* x = str;
			const hio_uch_t* y = sub;

			while (1)
			{
				if (y >= subp) return (hio_uch_t*)str;
				if (*x != *y) break;
				x++; y++;
			}

			str++;
		}
	}

	return HIO_NULL;
}

hio_bch_t* hio_find_bchars_in_bchars (const hio_bch_t* str, hio_oow_t strsz, const hio_bch_t* sub, hio_oow_t subsz, int ignorecase)
{
	const hio_bch_t* end, * subp;

	if (subsz == 0) return (hio_bch_t*)str;
	if (strsz < subsz) return HIO_NULL;
	
	end = str + strsz - subsz;
	subp = sub + subsz;

	if (HIO_UNLIKELY(ignorecase))
	{
		while (str <= end) 
		{
			const hio_bch_t* x = str;
			const hio_bch_t* y = sub;

			while (1)
			{
				if (y >= subp) return (hio_bch_t*)str;
				if (hio_to_bch_lower(*x) != hio_to_bch_lower(*y)) break;
				x++; y++;
			}

			str++;
		}
	}
	else
	{
		while (str <= end) 
		{
			const hio_bch_t* x = str;
			const hio_bch_t* y = sub;

			while (1)
			{
				if (y >= subp) return (hio_bch_t*)str;
				if (*x != *y) break;
				x++; y++;
			}

			str++;
		}
	}

	return HIO_NULL;
}

hio_uch_t* hio_rfind_uchars_in_uchars (const hio_uch_t* str, hio_oow_t strsz, const hio_uch_t* sub, hio_oow_t subsz, int ignorecase)
{
	const hio_uch_t* p = str + strsz;
	const hio_uch_t* subp = sub + subsz;

	if (subsz == 0) return (hio_uch_t*)p;
	if (strsz < subsz) return HIO_NULL;

	p = p - subsz;

	if (HIO_UNLIKELY(ignorecase))
	{
		while (p >= str) 
		{
			const hio_uch_t* x = p;
			const hio_uch_t* y = sub;

			while (1) 
			{
				if (y >= subp) return (hio_uch_t*)p;
				if (hio_to_uch_lower(*x) != hio_to_uch_lower(*y)) break;
				x++; y++;
			}

			p--;
		}
	}
	else
	{
		while (p >= str) 
		{
			const hio_uch_t* x = p;
			const hio_uch_t* y = sub;

			while (1) 
			{
				if (y >= subp) return (hio_uch_t*)p;
				if (*x != *y) break;
				x++; y++;
			}	

			p--;
		}
	}

	return HIO_NULL;
}

hio_bch_t* hio_rfind_bchars_in_bchars (const hio_bch_t* str, hio_oow_t strsz, const hio_bch_t* sub, hio_oow_t subsz, int ignorecase)
{
	const hio_bch_t* p = str + strsz;
	const hio_bch_t* subp = sub + subsz;

	if (subsz == 0) return (hio_bch_t*)p;
	if (strsz < subsz) return HIO_NULL;

	p = p - subsz;

	if (HIO_UNLIKELY(ignorecase))
	{
		while (p >= str) 
		{
			const hio_bch_t* x = p;
			const hio_bch_t* y = sub;

			while (1) 
			{
				if (y >= subp) return (hio_bch_t*)p;
				if (hio_to_bch_lower(*x) != hio_to_bch_lower(*y)) break;
				x++; y++;
			}

			p--;
		}
	}
	else
	{
		while (p >= str) 
		{
			const hio_bch_t* x = p;
			const hio_bch_t* y = sub;

			while (1) 
			{
				if (y >= subp) return (hio_bch_t*)p;
				if (*x != *y) break;
				x++; y++;
			}	

			p--;
		}
	}

	return HIO_NULL;
}

hio_oow_t hio_compact_uchars (hio_uch_t* str, hio_oow_t len)
{
	hio_uch_t* p = str, * q = str, * end = str + len;
	int followed_by_space = 0;
	int state = 0;

	while (p < end) 
	{
		if (state == 0) 
		{
			if (!(*p)) 
			{
				*q++ = *p;
				state = 1;
			}
		}
		else if (state == 1) 
		{
			if ((*p)) 
			{
				if (!followed_by_space) 
				{
					followed_by_space = 1;
					*q++ = *p;
				}
			}
			else 
			{
				followed_by_space = 0;
				*q++ = *p;	
			}
		}

		p++;
	}

	return (followed_by_space) ? (q - str -1): (q - str);
}

hio_oow_t hio_compact_bchars (hio_bch_t* str, hio_oow_t len)
{
	hio_bch_t* p = str, * q = str, * end = str + len;
	int followed_by_space = 0;
	int state = 0;

	while (p < end) 
	{
		if (state == 0) 
		{
			if (!(*p)) 
			{
				*q++ = *p;
				state = 1;
			}
		}
		else if (state == 1) 
		{
			if ((*p)) 
			{
				if (!followed_by_space) 
				{
					followed_by_space = 1;
					*q++ = *p;
				}
			}
			else 
			{
				followed_by_space = 0;
				*q++ = *p;	
			}
		}

		p++;
	}

	return (followed_by_space) ? (q - str -1): (q - str);
}

hio_oow_t hio_rotate_uchars (hio_uch_t* str, hio_oow_t len, int dir, hio_oow_t n)
{
	hio_oow_t first, last, count, index, nk;
	hio_uch_t c;

	if (dir == 0 || len == 0) return len;
	if ((n %= len) == 0) return len;

	if (dir > 0) n = len - n;
	first = 0; nk = len - n; count = 0;

	while (count < n)
	{
		last = first + nk;
		index = first;
		c = str[first];
		do
		{
			count++;
			while (index < nk)
			{
				str[index] = str[index + n];
				index += n;
			}
			if (index == last) break;
			str[index] = str[index - nk];
			index -= nk;
		}
		while (1);
		str[last] = c; first++;
	}
	return len;
}

hio_oow_t hio_rotate_bchars (hio_bch_t* str, hio_oow_t len, int dir, hio_oow_t n)
{
	hio_oow_t first, last, count, index, nk;
	hio_bch_t c;

	if (dir == 0 || len == 0) return len;
	if ((n %= len) == 0) return len;

	if (dir > 0) n = len - n;
	first = 0; nk = len - n; count = 0;

	while (count < n)
	{
		last = first + nk;
		index = first;
		c = str[first];
		do
		{
			count++;
			while (index < nk)
			{
				str[index] = str[index + n];
				index += n;
			}
			if (index == last) break;
			str[index] = str[index - nk];
			index -= nk;
		}
		while (1);
		str[last] = c; first++;
	}
	return len;
}

hio_uch_t* hio_trim_uchars (const hio_uch_t* str, hio_oow_t* len, int flags)
{
	const hio_uch_t* p = str, * end = str + *len;

	if (p < end)
	{
		const hio_uch_t* s = HIO_NULL, * e = HIO_NULL;

		do
		{
			if (!hio_is_uch_space(*p))
			{
				if (s == HIO_NULL) s = p;
				e = p;
			}
			p++;
		}
		while (p < end);

		if (e)
		{
			if (flags & HIO_TRIM_UCHARS_RIGHT) 
			{
				*len -= end - e - 1;
			}
			if (flags & HIO_TRIM_UCHARS_LEFT) 
			{
				*len -= s - str;
				str = s;
			}
		}
		else
		{
			/* the entire string need to be deleted */
			if ((flags & HIO_TRIM_UCHARS_RIGHT) || 
			    (flags & HIO_TRIM_UCHARS_LEFT)) *len = 0;
		}
	}

	return (hio_uch_t*)str;
}

hio_bch_t* hio_trim_bchars (const hio_bch_t* str, hio_oow_t* len, int flags)
{
	const hio_bch_t* p = str, * end = str + *len;

	if (p < end)
	{
		const hio_bch_t* s = HIO_NULL, * e = HIO_NULL;

		do
		{
			if (!hio_is_bch_space(*p))
			{
				if (s == HIO_NULL) s = p;
				e = p;
			}
			p++;
		}
		while (p < end);

		if (e)
		{
			if (flags & HIO_TRIM_BCHARS_RIGHT) 
			{
				*len -= end - e - 1;
			}
			if (flags & HIO_TRIM_BCHARS_LEFT) 
			{
				*len -= s - str;
				str = s;
			}
		}
		else
		{
			/* the entire string need to be deleted */
			if ((flags & HIO_TRIM_BCHARS_RIGHT) || 
			    (flags & HIO_TRIM_BCHARS_LEFT)) *len = 0;
		}
	}

	return (hio_bch_t*)str;
}

int hio_split_ucstr (hio_uch_t* s, const hio_uch_t* delim, hio_uch_t lquote, hio_uch_t rquote, hio_uch_t escape)
{
	hio_uch_t* p = s, *d;
	hio_uch_t* sp = HIO_NULL, * ep = HIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == HIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (hio_uch_t*)delim; *d != '\0'; d++)
			if (!hio_is_uch_space(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (hio_is_uch_space(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != '\0' && *p == lquote) 
		{
			hio_copy_ucstr_unlimited (p, p + 1);

			for (;;) 
			{
				if (*p == '\0') return -1;

				if (escape != '\0' && *p == escape) 
				{
					hio_copy_ucstr_unlimited (p, p + 1);
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
			while (hio_is_uch_space(*p)) p++;
			if (*p != '\0') return -1;

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (hio_uch_t*)sp) hio_copy_ucstr_unlimited (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!hio_is_uch_space(*p)) 
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
				if (s != (hio_uch_t*)sp) hio_copy_ucstr_unlimited (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		hio_uch_t* o;

		while (*p) 
		{
			o = p;
			while (hio_is_uch_space(*p)) p++;
			if (o != p) { hio_copy_ucstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				hio_copy_ucstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						hio_copy_ucstr_unlimited (p, p + 1);
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
					if (hio_is_uch_space(*p)) 
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
		hio_uch_t* o;
		int ok;

		while (*p != '\0') 
		{
			o = p;
			while (hio_is_uch_space(*p)) p++;
			if (o != p) { hio_copy_ucstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				hio_copy_ucstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						hio_copy_ucstr_unlimited (p, p + 1);
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
				while (hio_is_uch_space(*p)) p++;
				if (*p == '\0') ok = 1;
				for (d = (hio_uch_t*)delim; *d != '\0'; d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						hio_copy_ucstr_unlimited (p, p + 1);
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
					for (d = (hio_uch_t*)delim; *d != '\0'; d++) 
					{
						if (*p == *d)  
						{
							if (sp == HIO_NULL) 
							{
								hio_copy_ucstr_unlimited (o, p); p = o;
								*p++ = '\0';
							}
							else 
							{
								hio_copy_ucstr_unlimited (&ep[1], p);
								hio_copy_ucstr_unlimited (o, sp);
								o[ep - sp + 1] = '\0';
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == '\0') cnt++;
							goto exit_point;
						}
					}

					if (!hio_is_uch_space(*p)) 
					{
						if (sp == HIO_NULL) sp = p;
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

int hio_split_bcstr (hio_bch_t* s, const hio_bch_t* delim, hio_bch_t lquote, hio_bch_t rquote, hio_bch_t escape)
{
	hio_bch_t* p = s, *d;
	hio_bch_t* sp = HIO_NULL, * ep = HIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == HIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (hio_bch_t*)delim; *d != '\0'; d++)
			if (!hio_is_bch_space(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while (hio_is_bch_space(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != '\0' && *p == lquote) 
		{
			hio_copy_bcstr_unlimited (p, p + 1);

			for (;;) 
			{
				if (*p == '\0') return -1;

				if (escape != '\0' && *p == escape) 
				{
					hio_copy_bcstr_unlimited (p, p + 1);
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
			while (hio_is_bch_space(*p)) p++;
			if (*p != '\0') return -1;

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (hio_bch_t*)sp) hio_copy_bcstr_unlimited (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!hio_is_bch_space(*p)) 
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
				if (s != (hio_bch_t*)sp) hio_copy_bcstr_unlimited (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		hio_bch_t* o;

		while (*p) 
		{
			o = p;
			while (hio_is_bch_space(*p)) p++;
			if (o != p) { hio_copy_bcstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				hio_copy_bcstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						hio_copy_bcstr_unlimited (p, p + 1);
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
					if (hio_is_bch_space(*p)) 
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
		hio_bch_t* o;
		int ok;

		while (*p != '\0') 
		{
			o = p;
			while (hio_is_bch_space(*p)) p++;
			if (o != p) { hio_copy_bcstr_unlimited (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				hio_copy_bcstr_unlimited (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						hio_copy_bcstr_unlimited (p, p + 1);
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
				while (hio_is_bch_space(*p)) p++;
				if (*p == '\0') ok = 1;
				for (d = (hio_bch_t*)delim; *d != '\0'; d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						hio_copy_bcstr_unlimited (p, p + 1);
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
					for (d = (hio_bch_t*)delim; *d != '\0'; d++) 
					{
						if (*p == *d)  
						{
							if (sp == HIO_NULL) 
							{
								hio_copy_bcstr_unlimited (o, p); p = o;
								*p++ = '\0';
							}
							else 
							{
								hio_copy_bcstr_unlimited (&ep[1], p);
								hio_copy_bcstr_unlimited (o, sp);
								o[ep - sp + 1] = '\0';
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == '\0') cnt++;
							goto exit_point;
						}
					}

					if (!hio_is_bch_space(*p)) 
					{
						if (sp == HIO_NULL) sp = p;
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

hio_uch_t* hio_tokenize_uchars (const hio_uch_t* s, hio_oow_t len, const hio_uch_t* delim, hio_oow_t delim_len, hio_ucs_t* tok, int ignorecase)
{
	const hio_uch_t* p = s, *d;
	const hio_uch_t* end = s + len;
	const hio_uch_t* sp = HIO_NULL, * ep = HIO_NULL;
	const hio_uch_t* delim_end = delim + delim_len;
	hio_uch_t c; 
	int delim_mode;

#define __DELIM_NULL      0
#define __DELIM_EMPTY     1
#define __DELIM_SPACES    2
#define __DELIM_NOSPACES  3
#define __DELIM_COMPOSITE 4
	if (delim == HIO_NULL) delim_mode = __DELIM_NULL;
	else 
	{
		delim_mode = __DELIM_EMPTY;

		for (d = delim; d < delim_end; d++) 
		{
			if (hio_is_uch_space(*d)) 
			{
				if (delim_mode == __DELIM_EMPTY)
					delim_mode = __DELIM_SPACES;
				else if (delim_mode == __DELIM_NOSPACES)
				{
					delim_mode = __DELIM_COMPOSITE;
					break;
				}
			}
			else
			{
				if (delim_mode == __DELIM_EMPTY)
					delim_mode = __DELIM_NOSPACES;
				else if (delim_mode == __DELIM_SPACES)
				{
					delim_mode = __DELIM_COMPOSITE;
					break;
				}
			}
		}

		/* TODO: verify the following statement... */
		if (delim_mode == __DELIM_SPACES && delim_len == 1 && delim[0] != ' ') delim_mode = __DELIM_NOSPACES;
	}		
	
	if (delim_mode == __DELIM_NULL) 
	{ 
		/* when HIO_NULL is given as "delim", it trims off the 
		 * leading and trailing spaces characters off the source
		 * string "s" eventually. */

		while (p < end && hio_is_uch_space(*p)) p++;
		while (p < end) 
		{
			c = *p;

			if (!hio_is_uch_space(c)) 
			{
				if (sp == HIO_NULL) sp = p;
				ep = p;
			}
			p++;
		}
	}
	else if (delim_mode == __DELIM_EMPTY)
	{
		/* each character in the source string "s" becomes a token. */
		if (p < end)
		{
			c = *p;
			sp = p;
			ep = p++;
		}
	}
	else if (delim_mode == __DELIM_SPACES) 
	{
		/* each token is delimited by space characters. all leading
		 * and trailing spaces are removed. */

		while (p < end && hio_is_uch_space(*p)) p++;
		while (p < end) 
		{
			c = *p;
			if (hio_is_uch_space(c)) break;
			if (sp == HIO_NULL) sp = p;
			ep = p++;
		}
		while (p < end && hio_is_uch_space(*p)) p++;
	}
	else if (delim_mode == __DELIM_NOSPACES)
	{
		/* each token is delimited by one of charaters 
		 * in the delimeter set "delim". */
		if (ignorecase)
		{
			while (p < end) 
			{
				c = hio_to_uch_lower(*p);
				for (d = delim; d < delim_end; d++) 
				{
					if (c == hio_to_uch_lower(*d)) goto exit_loop;
				}

				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
		else
		{
			while (p < end) 
			{
				c = *p;
				for (d = delim; d < delim_end; d++) 
				{
					if (c == *d) goto exit_loop;
				}

				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
	}
	else /* if (delim_mode == __DELIM_COMPOSITE) */ 
	{
		/* each token is delimited by one of non-space charaters
		 * in the delimeter set "delim". however, all space characters
		 * surrounding the token are removed */
		while (p < end && hio_is_uch_space(*p)) p++;
		if (ignorecase)
		{
			while (p < end) 
			{
				c = hio_to_uch_lower(*p);
				if (hio_is_uch_space(c)) 
				{
					p++;
					continue;
				}
				for (d = delim; d < delim_end; d++) 
				{
					if (c == hio_to_uch_lower(*d)) goto exit_loop;
				}
				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
		else
		{
			while (p < end) 
			{
				c = *p;
				if (hio_is_uch_space(c)) 
				{
					p++;
					continue;
				}
				for (d = delim; d < delim_end; d++) 
				{
					if (c == *d) goto exit_loop;
				}
				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
	}

exit_loop:
	if (sp == HIO_NULL) 
	{
		tok->ptr = HIO_NULL;
		tok->len = (hio_oow_t)0;
	}
	else 
	{
		tok->ptr = (hio_uch_t*)sp;
		tok->len = ep - sp + 1;
	}

	/* if HIO_NULL is returned, this function should not be called again */
	if (p >= end) return HIO_NULL;
	if (delim_mode == __DELIM_EMPTY || delim_mode == __DELIM_SPACES) return (hio_uch_t*)p;
	return (hio_uch_t*)++p;
}

hio_bch_t* hio_tokenize_bchars (const hio_bch_t* s, hio_oow_t len, const hio_bch_t* delim, hio_oow_t delim_len, hio_bcs_t* tok, int ignorecase)
{
	const hio_bch_t* p = s, *d;
	const hio_bch_t* end = s + len;
	const hio_bch_t* sp = HIO_NULL, * ep = HIO_NULL;
	const hio_bch_t* delim_end = delim + delim_len;
	hio_bch_t c; 
	int delim_mode;

#define __DELIM_NULL      0
#define __DELIM_EMPTY     1
#define __DELIM_SPACES    2
#define __DELIM_NOSPACES  3
#define __DELIM_COMPOSITE 4
	if (delim == HIO_NULL) delim_mode = __DELIM_NULL;
	else 
	{
		delim_mode = __DELIM_EMPTY;

		for (d = delim; d < delim_end; d++) 
		{
			if (hio_is_bch_space(*d)) 
			{
				if (delim_mode == __DELIM_EMPTY)
					delim_mode = __DELIM_SPACES;
				else if (delim_mode == __DELIM_NOSPACES)
				{
					delim_mode = __DELIM_COMPOSITE;
					break;
				}
			}
			else
			{
				if (delim_mode == __DELIM_EMPTY)
					delim_mode = __DELIM_NOSPACES;
				else if (delim_mode == __DELIM_SPACES)
				{
					delim_mode = __DELIM_COMPOSITE;
					break;
				}
			}
		}

		/* TODO: verify the following statement... */
		if (delim_mode == __DELIM_SPACES && delim_len == 1 && delim[0] != ' ') delim_mode = __DELIM_NOSPACES;
	}		
	
	if (delim_mode == __DELIM_NULL) 
	{ 
		/* when HIO_NULL is given as "delim", it trims off the 
		 * leading and trailing spaces characters off the source
		 * string "s" eventually. */

		while (p < end && hio_is_bch_space(*p)) p++;
		while (p < end) 
		{
			c = *p;

			if (!hio_is_bch_space(c)) 
			{
				if (sp == HIO_NULL) sp = p;
				ep = p;
			}
			p++;
		}
	}
	else if (delim_mode == __DELIM_EMPTY)
	{
		/* each character in the source string "s" becomes a token. */
		if (p < end)
		{
			c = *p;
			sp = p;
			ep = p++;
		}
	}
	else if (delim_mode == __DELIM_SPACES) 
	{
		/* each token is delimited by space characters. all leading
		 * and trailing spaces are removed. */

		while (p < end && hio_is_bch_space(*p)) p++;
		while (p < end) 
		{
			c = *p;
			if (hio_is_bch_space(c)) break;
			if (sp == HIO_NULL) sp = p;
			ep = p++;
		}
		while (p < end && hio_is_bch_space(*p)) p++;
	}
	else if (delim_mode == __DELIM_NOSPACES)
	{
		/* each token is delimited by one of charaters 
		 * in the delimeter set "delim". */
		if (ignorecase)
		{
			while (p < end) 
			{
				c = hio_to_bch_lower(*p);
				for (d = delim; d < delim_end; d++) 
				{
					if (c == hio_to_bch_lower(*d)) goto exit_loop;
				}

				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
		else
		{
			while (p < end) 
			{
				c = *p;
				for (d = delim; d < delim_end; d++) 
				{
					if (c == *d) goto exit_loop;
				}

				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
	}
	else /* if (delim_mode == __DELIM_COMPOSITE) */ 
	{
		/* each token is delimited by one of non-space charaters
		 * in the delimeter set "delim". however, all space characters
		 * surrounding the token are removed */
		while (p < end && hio_is_bch_space(*p)) p++;
		if (ignorecase)
		{
			while (p < end) 
			{
				c = hio_to_bch_lower(*p);
				if (hio_is_bch_space(c)) 
				{
					p++;
					continue;
				}
				for (d = delim; d < delim_end; d++) 
				{
					if (c == hio_to_bch_lower(*d)) goto exit_loop;
				}
				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
		else
		{
			while (p < end) 
			{
				c = *p;
				if (hio_is_bch_space(c)) 
				{
					p++;
					continue;
				}
				for (d = delim; d < delim_end; d++) 
				{
					if (c == *d) goto exit_loop;
				}
				if (sp == HIO_NULL) sp = p;
				ep = p++;
			}
		}
	}

exit_loop:
	if (sp == HIO_NULL) 
	{
		tok->ptr = HIO_NULL;
		tok->len = (hio_oow_t)0;
	}
	else 
	{
		tok->ptr = (hio_bch_t*)sp;
		tok->len = ep - sp + 1;
	}

	/* if HIO_NULL is returned, this function should not be called again */
	if (p >= end) return HIO_NULL;
	if (delim_mode == __DELIM_EMPTY || delim_mode == __DELIM_SPACES) return (hio_bch_t*)p;
	return (hio_bch_t*)++p;
}

hio_oow_t hio_byte_to_ucstr (hio_uint8_t byte, hio_uch_t* buf, hio_oow_t size, int flagged_radix, hio_uch_t fill)
{
	hio_uch_t tmp[(HIO_SIZEOF(hio_uint8_t) * HIO_BITS_PER_BYTE)];
	hio_uch_t* p = tmp, * bp = buf, * be = buf + size - 1;
	int radix;
	hio_uch_t radix_char;

	radix = (flagged_radix & HIO_BYTE_TO_UCSTR_RADIXMASK);
	radix_char = (flagged_radix & HIO_BYTE_TO_UCSTR_LOWERCASE)? 'a': 'A';
	if (radix < 2 || radix > 36 || size <= 0) return 0;

	do 
	{
		hio_uint8_t digit = byte % radix;
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

hio_oow_t hio_byte_to_bcstr (hio_uint8_t byte, hio_bch_t* buf, hio_oow_t size, int flagged_radix, hio_bch_t fill)
{
	hio_bch_t tmp[(HIO_SIZEOF(hio_uint8_t) * HIO_BITS_PER_BYTE)];
	hio_bch_t* p = tmp, * bp = buf, * be = buf + size - 1;
	int radix;
	hio_bch_t radix_char;

	radix = (flagged_radix & HIO_BYTE_TO_BCSTR_RADIXMASK);
	radix_char = (flagged_radix & HIO_BYTE_TO_BCSTR_LOWERCASE)? 'a': 'A';
	if (radix < 2 || radix > 36 || size <= 0) return 0;

	do 
	{
		hio_uint8_t digit = byte % radix;
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

hio_oow_t hio_intmax_to_ucstr (hio_intmax_t value, int radix, const hio_uch_t* prefix, hio_uch_t* buf, hio_oow_t size)
{
	hio_intmax_t t, rem;
	hio_oow_t len, ret, i;
	hio_oow_t prefix_len;

	prefix_len = (prefix != HIO_NULL)? hio_count_ucstr(prefix): 0;

	t = value;
	if (t == 0)
	{
		/* zero */
		if (buf == HIO_NULL) 
		{
			/* if buf is not given, 
			 * return the number of bytes required */
			return prefix_len + 1;
		}

		if (size < prefix_len+1) 
		{
			/* buffer too small */
			return (hio_oow_t)-1;
		}

		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
		buf[prefix_len] = '0';
		if (size > prefix_len+1) buf[prefix_len+1] = '\0';
		return prefix_len+1;
	}

	/* non-zero values */
	len = prefix_len;
	if (t < 0) { t = -t; len++; }
	while (t > 0) { len++; t /= radix; }

	if (buf == HIO_NULL)
	{
		/* if buf is not given, return the number of bytes required */
		return len;
	}

	if (size < len) return (hio_oow_t)-1; /* buffer too small */
	if (size > len) buf[len] = '\0';
	ret = len;

	t = value;
	if (t < 0) t = -t;

	while (t > 0) 
	{
		rem = t % radix;
		if (rem >= 10)
			buf[--len] = (hio_uch_t)rem + 'a' - 10;
		else
			buf[--len] = (hio_uch_t)rem + '0';
		t /= radix;
	}

	if (value < 0) 
	{
		for (i = 1; i <= prefix_len; i++) 
		{
			buf[i] = prefix[i-1];
			len--;
		}
		buf[--len] = '-';
	}
	else
	{
		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
	}

	return ret;
}

hio_oow_t hio_intmax_to_bcstr (hio_intmax_t value, int radix, const hio_bch_t* prefix, hio_bch_t* buf, hio_oow_t size)
{
	hio_intmax_t t, rem;
	hio_oow_t len, ret, i;
	hio_oow_t prefix_len;

	prefix_len = (prefix != HIO_NULL)? hio_count_bcstr(prefix): 0;

	t = value;
	if (t == 0)
	{
		/* zero */
		if (buf == HIO_NULL) 
		{
			/* if buf is not given, 
			 * return the number of bytes required */
			return prefix_len + 1;
		}

		if (size < prefix_len+1) 
		{
			/* buffer too small */
			return (hio_oow_t)-1;
		}

		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
		buf[prefix_len] = '0';
		if (size > prefix_len+1) buf[prefix_len+1] = '\0';
		return prefix_len+1;
	}

	/* non-zero values */
	len = prefix_len;
	if (t < 0) { t = -t; len++; }
	while (t > 0) { len++; t /= radix; }

	if (buf == HIO_NULL)
	{
		/* if buf is not given, return the number of bytes required */
		return len;
	}

	if (size < len) return (hio_oow_t)-1; /* buffer too small */
	if (size > len) buf[len] = '\0';
	ret = len;

	t = value;
	if (t < 0) t = -t;

	while (t > 0) 
	{
		rem = t % radix;
		if (rem >= 10)
			buf[--len] = (hio_bch_t)rem + 'a' - 10;
		else
			buf[--len] = (hio_bch_t)rem + '0';
		t /= radix;
	}

	if (value < 0) 
	{
		for (i = 1; i <= prefix_len; i++) 
		{
			buf[i] = prefix[i-1];
			len--;
		}
		buf[--len] = '-';
	}
	else
	{
		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
	}

	return ret;
}

hio_oow_t hio_uintmax_to_ucstr (hio_uintmax_t value, int radix, const hio_uch_t* prefix, hio_uch_t* buf, hio_oow_t size)
{
	hio_uintmax_t t, rem;
	hio_oow_t len, ret, i;
	hio_oow_t prefix_len;

	prefix_len = (prefix != HIO_NULL)? hio_count_ucstr(prefix): 0;

	t = value;
	if (t == 0)
	{
		/* zero */
		if (buf == HIO_NULL) 
		{
			/* if buf is not given, 
			 * return the number of bytes required */
			return prefix_len + 1;
		}

		if (size < prefix_len+1) 
		{
			/* buffer too small */
			return (hio_oow_t)-1;
		}

		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
		buf[prefix_len] = '0';
		if (size > prefix_len+1) buf[prefix_len+1] = '\0';
		return prefix_len+1;
	}

	/* non-zero values */
	len = prefix_len;
	if (t < 0) { t = -t; len++; }
	while (t > 0) { len++; t /= radix; }

	if (buf == HIO_NULL)
	{
		/* if buf is not given, return the number of bytes required */
		return len;
	}

	if (size < len) return (hio_oow_t)-1; /* buffer too small */
	if (size > len) buf[len] = '\0';
	ret = len;

	t = value;
	if (t < 0) t = -t;

	while (t > 0) 
	{
		rem = t % radix;
		if (rem >= 10)
			buf[--len] = (hio_uch_t)rem + 'a' - 10;
		else
			buf[--len] = (hio_uch_t)rem + '0';
		t /= radix;
	}

	if (value < 0) 
	{
		for (i = 1; i <= prefix_len; i++) 
		{
			buf[i] = prefix[i-1];
			len--;
		}
		buf[--len] = '-';
	}
	else
	{
		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
	}

	return ret;
}

hio_oow_t hio_uintmax_to_bcstr (hio_uintmax_t value, int radix, const hio_bch_t* prefix, hio_bch_t* buf, hio_oow_t size)
{
	hio_uintmax_t t, rem;
	hio_oow_t len, ret, i;
	hio_oow_t prefix_len;

	prefix_len = (prefix != HIO_NULL)? hio_count_bcstr(prefix): 0;

	t = value;
	if (t == 0)
	{
		/* zero */
		if (buf == HIO_NULL) 
		{
			/* if buf is not given, 
			 * return the number of bytes required */
			return prefix_len + 1;
		}

		if (size < prefix_len+1) 
		{
			/* buffer too small */
			return (hio_oow_t)-1;
		}

		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
		buf[prefix_len] = '0';
		if (size > prefix_len+1) buf[prefix_len+1] = '\0';
		return prefix_len+1;
	}

	/* non-zero values */
	len = prefix_len;
	if (t < 0) { t = -t; len++; }
	while (t > 0) { len++; t /= radix; }

	if (buf == HIO_NULL)
	{
		/* if buf is not given, return the number of bytes required */
		return len;
	}

	if (size < len) return (hio_oow_t)-1; /* buffer too small */
	if (size > len) buf[len] = '\0';
	ret = len;

	t = value;
	if (t < 0) t = -t;

	while (t > 0) 
	{
		rem = t % radix;
		if (rem >= 10)
			buf[--len] = (hio_bch_t)rem + 'a' - 10;
		else
			buf[--len] = (hio_bch_t)rem + '0';
		t /= radix;
	}

	if (value < 0) 
	{
		for (i = 1; i <= prefix_len; i++) 
		{
			buf[i] = prefix[i-1];
			len--;
		}
		buf[--len] = '-';
	}
	else
	{
		for (i = 0; i < prefix_len; i++) buf[i] = prefix[i];
	}

	return ret;
}

hio_intmax_t hio_uchars_to_intmax (const hio_uch_t* str, hio_oow_t len, int option, const hio_uch_t** endptr, int* is_sober)
{
	hio_intmax_t n = 0;
	const hio_uch_t* p, * pp;
	const hio_uch_t* end;
	hio_oow_t rem;
	int digit, negative = 0;
	int base = HIO_UCHARS_TO_INTMAX_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if (HIO_UCHARS_TO_INTMAX_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && hio_is_uch_space(*p)) p++;
	}

	/* check for a sign */
	while (p < end)
	{
		if (*p == '-') 
		{
			negative = ~negative;
			p++;
		}
		else if (*p == '+') p++;
		else break;
	}

	/* check for a binary/octal/hexadecimal notation */
	rem = end - p;
	if (base == 0) 
	{
		if (rem >= 1 && *p == '0') 
		{
			p++;

			if (rem == 1) base = 8;
			else if (*p == 'x' || *p == 'X')
			{
				p++; base = 16;
			} 
			else if (*p == 'b' || *p == 'B')
			{
				p++; base = 2;
			}
			else base = 8;
		}
		else base = 10;
	} 
	else if (rem >= 2 && base == 16)
	{
		if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
	}

	/* process the digits */
	pp = p;
	while (p < end)
	{
		digit = HIO_ZDIGIT_TO_NUM(*p, base);
		if (digit >= base) break;
		n = n * base + digit;
		p++;
	}

	if (HIO_UCHARS_TO_INTMAX_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			hio_intmax_t e = 0, i;
			int e_neg = 0;
			p++;
			if (*p == '+')
			{
				p++;
			}
			else if (*p == '-')
			{
				p++; e_neg = 1;
			}
			while (p < end)
			{
				digit = HIO_ZDIGIT_TO_NUM(*p, base);
				if (digit >= base) break;
				e = e * base + digit;
				p++;
			}
			if (e_neg)
				for (i = 0; i < e; i++) n /= 10;
			else
				for (i = 0; i < e; i++) n *= 10;
		}
	}

	/* base 8: at least a zero digit has been seen.
	 * other case: p > pp to be able to have at least 1 meaningful digit. */
	if (is_sober) *is_sober = (base == 8 || p > pp); 

	if (HIO_UCHARS_TO_INTMAX_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && hio_is_uch_space(*p)) p++;
	}

	if (endptr) *endptr = p;
	return (negative)? -n: n;
}

hio_intmax_t hio_bchars_to_intmax (const hio_bch_t* str, hio_oow_t len, int option, const hio_bch_t** endptr, int* is_sober)
{
	hio_intmax_t n = 0;
	const hio_bch_t* p, * pp;
	const hio_bch_t* end;
	hio_oow_t rem;
	int digit, negative = 0;
	int base = HIO_BCHARS_TO_INTMAX_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if (HIO_BCHARS_TO_INTMAX_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && hio_is_bch_space(*p)) p++;
	}

	/* check for a sign */
	while (p < end)
	{
		if (*p == '-') 
		{
			negative = ~negative;
			p++;
		}
		else if (*p == '+') p++;
		else break;
	}

	/* check for a binary/octal/hexadecimal notation */
	rem = end - p;
	if (base == 0) 
	{
		if (rem >= 1 && *p == '0') 
		{
			p++;

			if (rem == 1) base = 8;
			else if (*p == 'x' || *p == 'X')
			{
				p++; base = 16;
			} 
			else if (*p == 'b' || *p == 'B')
			{
				p++; base = 2;
			}
			else base = 8;
		}
		else base = 10;
	} 
	else if (rem >= 2 && base == 16)
	{
		if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
	}

	/* process the digits */
	pp = p;
	while (p < end)
	{
		digit = HIO_ZDIGIT_TO_NUM(*p, base);
		if (digit >= base) break;
		n = n * base + digit;
		p++;
	}

	if (HIO_BCHARS_TO_INTMAX_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			hio_intmax_t e = 0, i;
			int e_neg = 0;
			p++;
			if (*p == '+')
			{
				p++;
			}
			else if (*p == '-')
			{
				p++; e_neg = 1;
			}
			while (p < end)
			{
				digit = HIO_ZDIGIT_TO_NUM(*p, base);
				if (digit >= base) break;
				e = e * base + digit;
				p++;
			}
			if (e_neg)
				for (i = 0; i < e; i++) n /= 10;
			else
				for (i = 0; i < e; i++) n *= 10;
		}
	}

	/* base 8: at least a zero digit has been seen.
	 * other case: p > pp to be able to have at least 1 meaningful digit. */
	if (is_sober) *is_sober = (base == 8 || p > pp); 

	if (HIO_BCHARS_TO_INTMAX_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && hio_is_bch_space(*p)) p++;
	}

	if (endptr) *endptr = p;
	return (negative)? -n: n;
}

hio_uintmax_t hio_uchars_to_uintmax (const hio_uch_t* str, hio_oow_t len, int option, const hio_uch_t** endptr, int* is_sober)
{
	hio_uintmax_t n = 0;
	const hio_uch_t* p, * pp;
	const hio_uch_t* end;
	hio_oow_t rem;
	int digit;
	int base = HIO_UCHARS_TO_UINTMAX_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if (HIO_UCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && hio_is_uch_space(*p)) p++;
	}

	/* check for a sign */
	while (p < end)
	{
		if (*p == '+') p++;
		else break;
	}

	/* check for a binary/octal/hexadecimal notation */
	rem = end - p;
	if (base == 0) 
	{
		if (rem >= 1 && *p == '0') 
		{
			p++;

			if (rem == 1) base = 8;
			else if (*p == 'x' || *p == 'X')
			{
				p++; base = 16;
			} 
			else if (*p == 'b' || *p == 'B')
			{
				p++; base = 2;
			}
			else base = 8;
		}
		else base = 10;
	} 
	else if (rem >= 2 && base == 16)
	{
		if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
	}

	/* process the digits */
	pp = p;
	while (p < end)
	{
		digit = HIO_ZDIGIT_TO_NUM(*p, base);
		if (digit >= base) break;
		n = n * base + digit;
		p++;
	}

	if (HIO_UCHARS_TO_UINTMAX_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			hio_uintmax_t e = 0, i;
			int e_neg = 0;
			p++;
			if (*p == '+')
			{
				p++;
			}
			else if (*p == '-')
			{
				p++; e_neg = 1;
			}
			while (p < end)
			{
				digit = HIO_ZDIGIT_TO_NUM(*p, base);
				if (digit >= base) break;
				e = e * base + digit;
				p++;
			}
			if (e_neg)
				for (i = 0; i < e; i++) n /= 10;
			else
				for (i = 0; i < e; i++) n *= 10;
		}
	}

	/* base 8: at least a zero digit has been seen.
	 * other case: p > pp to be able to have at least 1 meaningful digit. */
	if (is_sober) *is_sober = (base == 8 || p > pp); 

	if (HIO_UCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && hio_is_uch_space(*p)) p++;
	}

	if (endptr) *endptr = p;
	return n;
}

hio_uintmax_t hio_bchars_to_uintmax (const hio_bch_t* str, hio_oow_t len, int option, const hio_bch_t** endptr, int* is_sober)
{
	hio_uintmax_t n = 0;
	const hio_bch_t* p, * pp;
	const hio_bch_t* end;
	hio_oow_t rem;
	int digit;
	int base = HIO_BCHARS_TO_UINTMAX_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if (HIO_BCHARS_TO_UINTMAX_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && hio_is_bch_space(*p)) p++;
	}

	/* check for a sign */
	while (p < end)
	{
		if (*p == '+') p++;
		else break;
	}

	/* check for a binary/octal/hexadecimal notation */
	rem = end - p;
	if (base == 0) 
	{
		if (rem >= 1 && *p == '0') 
		{
			p++;

			if (rem == 1) base = 8;
			else if (*p == 'x' || *p == 'X')
			{
				p++; base = 16;
			} 
			else if (*p == 'b' || *p == 'B')
			{
				p++; base = 2;
			}
			else base = 8;
		}
		else base = 10;
	} 
	else if (rem >= 2 && base == 16)
	{
		if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
	}

	/* process the digits */
	pp = p;
	while (p < end)
	{
		digit = HIO_ZDIGIT_TO_NUM(*p, base);
		if (digit >= base) break;
		n = n * base + digit;
		p++;
	}

	if (HIO_BCHARS_TO_UINTMAX_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			hio_uintmax_t e = 0, i;
			int e_neg = 0;
			p++;
			if (*p == '+')
			{
				p++;
			}
			else if (*p == '-')
			{
				p++; e_neg = 1;
			}
			while (p < end)
			{
				digit = HIO_ZDIGIT_TO_NUM(*p, base);
				if (digit >= base) break;
				e = e * base + digit;
				p++;
			}
			if (e_neg)
				for (i = 0; i < e; i++) n /= 10;
			else
				for (i = 0; i < e; i++) n *= 10;
		}
	}

	/* base 8: at least a zero digit has been seen.
	 * other case: p > pp to be able to have at least 1 meaningful digit. */
	if (is_sober) *is_sober = (base == 8 || p > pp); 

	if (HIO_BCHARS_TO_UINTMAX_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && hio_is_bch_space(*p)) p++;
	}

	if (endptr) *endptr = p;
	return n;
}

