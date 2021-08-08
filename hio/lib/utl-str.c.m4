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

/* 
 * Do NOT edit utl-str.c.
 * 
 * Generate utl-str.c with m4
 *   $ m4 utl-str.c.m4 > utl-str.c  
 */

#include "hio-prv.h"
#include <hio-chr.h>

dnl ---------------------------------------------------------------------------
changequote(`[[', `]]')

dnl ---------------------------------------------------------------------------
define([[fn_count_cstr]], [[ define([[fn_name]], $1) define([[char_type]], $2)
hio_oow_t fn_name (const char_type* str)
{
	const char_type* ptr = str;
	while (*ptr != '\0') ptr++;
	return ptr - str;
} 
]])
fn_count_cstr(hio_count_ucstr, hio_uch_t)
fn_count_cstr(hio_count_bcstr, hio_bch_t)

dnl ---------------------------------------------------------------------------
define([[fn_equal_chars]], [[ define([[fn_name]], $1) define([[char_type]], $2)
int fn_name (const char_type* str1, const char_type* str2, hio_oow_t len)
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
]])
fn_equal_chars(hio_equal_uchars, hio_uch_t)
fn_equal_chars(hio_equal_bchars, hio_bch_t)

dnl ---------------------------------------------------------------------------
define([[fn_comp_chars]], [[ define([[fn_name]], $1) define([[char_type]], $2) define([[chau_type]], $3)
int fn_name (const char_type* str1, hio_oow_t len1, const char_type* str2, hio_oow_t len2, int ignorecase)
{
	chau_type c1, c2;
	const char_type* end1 = str1 + len1;
	const char_type* end2 = str2 + len2;

	if (ignorecase)
	{
		while (str1 < end1)
		{
			c1 = $4(*str1);
			if (str2 < end2) 
			{
				c2 = $4(*str2);
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
]])
fn_comp_chars(hio_comp_uchars, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_chars(hio_comp_bchars, hio_bch_t, hio_bchu_t, hio_to_bch_lower)


dnl ---------------------------------------------------------------------------
define([[fn_comp_cstr]], [[ define([[fn_name]], $1) define([[char_type]], $2) define([[chau_type]], $3)
int fn_name (const char_type* str1, const char_type* str2, int ignorecase)
{
	if (ignorecase)
	{
		while ($4(*str1) == $4(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((chau_type)$4(*str1) > (chau_type)$4(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((chau_type)*str1 > (chau_type)*str2)? 1: -1;
	}
}
]])
fn_comp_cstr(hio_comp_ucstr, hio_uch_t, hio_uchu_t, hio_to_uch_lower)
fn_comp_cstr(hio_comp_bcstr, hio_bch_t, hio_bchu_t, hio_to_bch_lower)

dnl ---------------------------------------------------------------------------
define([[fn_concat_chars_to_cstr]], [[ define([[fn_name]], $1) define([[char_type]], $2) dnl: $3 count_str
hio_oow_t fn_name (char_type* buf, hio_oow_t bsz, const char_type* str, hio_oow_t len)
{
	char_type* p, * p2;
	const char_type* end;
	hio_oow_t blen;

	blen = $3(buf);
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
]])
fn_concat_chars_to_cstr(hio_concat_uchars_to_ucstr, hio_uch_t, hio_count_ucstr)
fn_concat_chars_to_cstr(hio_concat_bchars_to_bcstr, hio_bch_t, hio_count_bcstr)

dnl ---------------------------------------------------------------------------
define([[fn_concat_cstr]], [[ define([[fn_name]], $1) define([[char_type]], $2) dnl: $3 count_str
hio_oow_t fn_name (char_type* buf, hio_oow_t bsz, const char_type* str)
{
	char_type* p, * p2;
	hio_oow_t blen;

	blen = $3(buf);
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
]])
fn_concat_cstr(hio_concat_ucstr, hio_uch_t, hio_count_ucstr)
fn_concat_cstr(hio_concat_bcstr, hio_bch_t, hio_count_bcstr)


dnl ---------------------------------------------------------------------------
define([[fn_fill_chars]], [[ define([[fn_name]], $1) define([[char_type]], $2)
void fn_name (char_type* dst, char_type ch, hio_oow_t len)
{
        hio_oow_t i;
        for (i = 0; i < len; i++) dst[i] = ch;
}
]])
fn_fill_chars(hio_fill_uchars, hio_uch_t)
fn_fill_chars(hio_fill_bchars, hio_bch_t)

dnl ---------------------------------------------------------------------------
define([[fn_find_char]], [[ define([[fn_name]], $1) define([[char_type]], $2)
char_type* fn_name (const char_type* ptr, hio_oow_t len, char_type c)
{
	const char_type* end;

	end = ptr + len;
	while (ptr < end)
	{
		if (*ptr == c) return (char_type*)ptr;
		ptr++;
	}

	return HIO_NULL;
}
]])
fn_find_char(hio_find_uchar, hio_uch_t)
fn_find_char(hio_find_bchar, hio_bch_t)

dnl ---------------------------------------------------------------------------
define([[fn_rfind_char]], [[ define([[fn_name]], $1) define([[char_type]], $2)
char_type* fn_name (const char_type* ptr, hio_oow_t len, char_type c)
{
	const char_type* cur;

	cur = ptr + len;
	while (cur > ptr)
	{
		--cur;
		if (*cur == c) return (char_type*)cur;
	}

	return HIO_NULL;
}
]])
fn_rfind_char(hio_rfind_uchar, hio_uch_t)
fn_rfind_char(hio_rfind_bchar, hio_bch_t)


dnl ---------------------------------------------------------------------------
define([[fn_find_char_in_cstr]], [[ define([[fn_name]], $1) define([[char_type]], $2)
char_type* fn_name (const char_type* ptr, char_type c)
{
	while (*ptr != '\0')
	{
		if (*ptr == c) return (char_type*)ptr;
		ptr++;
	}

	return HIO_NULL;
}
]])
fn_find_char_in_cstr(hio_find_uchar_in_ucstr, hio_uch_t)
fn_find_char_in_cstr(hio_find_bchar_in_bcstr, hio_bch_t)


dnl ---------------------------------------------------------------------------
define([[fn_trim_chars]], [[define([[fn_name]], $1) define([[char_type]], $2) dnl $3: is_space $4: prefix for option values

char_type* fn_name (const char_type* str, hio_oow_t* len, int flags)
{
	const char_type* p = str, * end = str + *len;

	if (p < end)
	{
		const char_type* s = HIO_NULL, * e = HIO_NULL;

		do
		{
			if (!$3(*p))
			{
				if (s == HIO_NULL) s = p;
				e = p;
			}
			p++;
		}
		while (p < end);

		if (e)
		{
			if (flags & $4_RIGHT) 
			{
				*len -= end - e - 1;
			}
			if (flags & $4_LEFT) 
			{
				*len -= s - str;
				str = s;
			}
		}
		else
		{
			/* the entire string need to be deleted */
			if ((flags & $4_RIGHT) || 
			    (flags & $4_LEFT)) *len = 0;
		}
	}

	return (char_type*)str;
}
]])
fn_trim_chars(hio_trim_uchars, hio_uch_t, hio_is_uch_space, HIO_TRIM_UCHARS)
fn_trim_chars(hio_trim_bchars, hio_bch_t, hio_is_bch_space, HIO_TRIM_BCHARS)


dnl ---------------------------------------------------------------------------
define([[fn_split_cstr]], [[ define([[fn_name]], $1) define([[char_type]], $2) dnl: $3 is_space $4: copy_str_unlimited

int fn_name (char_type* s, const char_type* delim, char_type lquote, char_type rquote, char_type escape)
{
	char_type* p = s, *d;
	char_type* sp = HIO_NULL, * ep = HIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == HIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (char_type*)delim; *d != '\0'; d++)
			if (!$3(*d)) delim_mode = 2;
	}

	if (delim_mode == 0) 
	{
		/* skip preceding space characters */
		while ($3(*p)) p++;

		/* when 0 is given as "delim", it has an effect of cutting
		   preceding and trailing space characters off "s". */
		if (lquote != '\0' && *p == lquote) 
		{
			$4 (p, p + 1);

			for (;;) 
			{
				if (*p == '\0') return -1;

				if (escape != '\0' && *p == escape) 
				{
					$4 (p, p + 1);
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
			while ($3(*p)) p++;
			if (*p != '\0') return -1;

			if (sp == 0 && ep == 0) s[0] = '\0';
			else 
			{
				ep[1] = '\0';
				if (s != (char_type*)sp) $4 (s, sp);
				cnt++;
			}
		}
		else 
		{
			while (*p) 
			{
				if (!$3(*p)) 
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
				if (s != (char_type*)sp) $4 (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		char_type* o;

		while (*p) 
		{
			o = p;
			while ($3(*p)) p++;
			if (o != p) { $4 (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				$4 (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						$4 (p, p + 1);
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
					if ($3(*p)) 
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
		char_type* o;
		int ok;

		while (*p != '\0') 
		{
			o = p;
			while ($3(*p)) p++;
			if (o != p) { $4 (o, p); p = o; }

			if (lquote != '\0' && *p == lquote) 
			{
				$4 (p, p + 1);

				for (;;) 
				{
					if (*p == '\0') return -1;

					if (escape != '\0' && *p == escape) 
					{
						$4 (p, p + 1);
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
				while ($3(*p)) p++;
				if (*p == '\0') ok = 1;
				for (d = (char_type*)delim; *d != '\0'; d++) 
				{
					if (*p == *d) 
					{
						ok = 1;
						$4 (p, p + 1);
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
					for (d = (char_type*)delim; *d != '\0'; d++) 
					{
						if (*p == *d)  
						{
							if (sp == HIO_NULL) 
							{
								$4 (o, p); p = o;
								*p++ = '\0';
							}
							else 
							{
								$4 (&ep[1], p);
								$4 (o, sp);
								o[ep - sp + 1] = '\0';
								p = &o[ep - sp + 2];
							}
							cnt++;
							/* last empty field after delim */
							if (*p == '\0') cnt++;
							goto exit_point;
						}
					}

					if (!$3(*p)) 
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
]])
fn_split_cstr(hio_split_ucstr, hio_uch_t, hio_is_uch_space, hio_copy_ucstr_unlimited)
fn_split_cstr(hio_split_bcstr, hio_bch_t, hio_is_bch_space, hio_copy_bcstr_unlimited)

dnl ---------------------------------------------------------------------------
define([[fn_chars_to_int]], [[ define([[fn_name]], $1) define([[char_type]], $2) define([[int_type]], $3)
int_type fn_name (const char_type* str, hio_oow_t len, int option, const char_type** endptr, int* is_sober)
{
	int_type n = 0;
	const char_type* p, * pp;
	const char_type* end;
	hio_oow_t rem;
	int digit, negative = 0;
	int base = $5_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if ($5_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && $4(*p)) p++;
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
		if (*p == '0' && 
		    (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && 
		    (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
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

	if ($5_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			int_type e = 0, i;
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

	if ($5_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && $4(*p)) p++;
	}

	if (endptr) *endptr = p;
	return (negative)? -n: n;
}
]])
fn_chars_to_int(hio_uchars_to_intmax, hio_uch_t, hio_intmax_t, hio_is_uch_space, HIO_UCHARS_TO_INTMAX)
fn_chars_to_int(hio_bchars_to_intmax, hio_bch_t, hio_intmax_t, hio_is_bch_space, HIO_BCHARS_TO_INTMAX)



dnl ---------------------------------------------------------------------------
define([[fn_chars_to_uint]], [[ define([[fn_name]], $1) define([[char_type]], $2) define([[int_type]], $3)
int_type fn_name (const char_type* str, hio_oow_t len, int option, const char_type** endptr, int* is_sober)
{
	int_type n = 0;
	const char_type* p, * pp;
	const char_type* end;
	hio_oow_t rem;
	int digit;
	int base = $5_GET_OPTION_BASE(option);

	p = str; 
	end = str + len;

	if ($5_GET_OPTION_LTRIM(option))
	{
		/* strip off leading spaces */
		while (p < end && $4(*p)) p++;
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
		if (*p == '0' && 
		    (*(p + 1) == 'x' || *(p + 1) == 'X')) p += 2; 
	}
	else if (rem >= 2 && base == 2)
	{
		if (*p == '0' && 
		    (*(p + 1) == 'b' || *(p + 1) == 'B')) p += 2; 
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

	if ($5_GET_OPTION_E(option))
	{
		if (*p == 'E' || *p == 'e')
		{
			int_type e = 0, i;
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

	if ($5_GET_OPTION_RTRIM(option))
	{
		/* consume trailing spaces */
		while (p < end && $4(*p)) p++;
	}

	if (endptr) *endptr = p;
	return n;
}
]])
fn_chars_to_uint(hio_uchars_to_uintmax, hio_uch_t, hio_uintmax_t, hio_is_uch_space, HIO_UCHARS_TO_UINTMAX)
fn_chars_to_uint(hio_bchars_to_uintmax, hio_bch_t, hio_uintmax_t, hio_is_bch_space, HIO_BCHARS_TO_UINTMAX)
