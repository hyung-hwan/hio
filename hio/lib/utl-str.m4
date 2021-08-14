dnl ---------------------------------------------------------------------------
changequote(`[[', `]]')

dnl ---------------------------------------------------------------------------
define([[fn_count_cstr]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
hio_oow_t _fn_name_ (const _char_type_* str)
{
	const _char_type_* ptr = str;
	while (*ptr != '\0') ptr++;
	return ptr - str;
} 
]])

dnl ---------------------------------------------------------------------------
define([[fn_equal_chars]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
int _fn_name_ (const _char_type_* str1, const _char_type_* str2, hio_oow_t len)
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

dnl ---------------------------------------------------------------------------
define([[fn_comp_chars]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2) define([[_chau_type_]], $3)
int _fn_name_ (const _char_type_* str1, hio_oow_t len1, const _char_type_* str2, hio_oow_t len2, int ignorecase)
{
	_chau_type_ c1, c2;
	const _char_type_* end1 = str1 + len1;
	const _char_type_* end2 = str2 + len2;

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

dnl ---------------------------------------------------------------------------
define([[fn_comp_cstr]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2) define([[_chau_type_]], $3)
int _fn_name_ (const _char_type_* str1, const _char_type_* str2, int ignorecase)
{
	if (ignorecase)
	{
		while ($4(*str1) == $4(*str2))
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((_chau_type_)$4(*str1) > (_chau_type_)$4(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			if (*str1 == '\0') return 0;
			str1++; str2++;
		}

		return ((_chau_type_)*str1 > (_chau_type_)*str2)? 1: -1;
	}
}
]])

dnl ---------------------------------------------------------------------------
define([[fn_comp_cstr_limited]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2) define([[_chau_type_]], $3)
int _fn_name_ (const _char_type_* str1, const _char_type_* str2, hio_oow_t maxlen, int ignorecase)
{
	if (maxlen == 0) return 0;

	if (ignorecase)
	{
		while ($4(*str1) == $4(*str2))
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((_chau_type_)$4(*str1) > (_chau_type_)$4(*str2))? 1: -1;
	}
	else
	{
		while (*str1 == *str2)
		{
			 if (*str1 == '\0' || maxlen == 1) return 0;
			 str1++; str2++; maxlen--;
		}

		return ((_chau_type_)*str1 > (_chau_type_)*str2)? 1: -1;
	}
}
]])

dnl ---------------------------------------------------------------------------
define([[fn_concat_chars_to_cstr]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2) dnl: $3 count_str
hio_oow_t _fn_name_ (_char_type_* buf, hio_oow_t bsz, const _char_type_* str, hio_oow_t len)
{
	_char_type_* p, * p2;
	const _char_type_* end;
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

dnl ---------------------------------------------------------------------------
dnl $3: count_str
define([[fn_concat_cstr]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
hio_oow_t _fn_name_ (_char_type_* buf, hio_oow_t bsz, const _char_type_* str)
{
	_char_type_* p, * p2;
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

dnl ---------------------------------------------------------------------------
define([[fn_fill_chars]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
void _fn_name_ (_char_type_* dst, _char_type_ ch, hio_oow_t len)
{
        hio_oow_t i;
        for (i = 0; i < len; i++) dst[i] = ch;
}
]])

dnl ---------------------------------------------------------------------------
define([[fn_find_char]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
_char_type_* _fn_name_ (const _char_type_* ptr, hio_oow_t len, _char_type_ c)
{
	const _char_type_* end;

	end = ptr + len;
	while (ptr < end)
	{
		if (*ptr == c) return (_char_type_*)ptr;
		ptr++;
	}

	return HIO_NULL;
}
]])

dnl ---------------------------------------------------------------------------
define([[fn_rfind_char]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
_char_type_* _fn_name_ (const _char_type_* ptr, hio_oow_t len, _char_type_ c)
{
	const _char_type_* cur;

	cur = ptr + len;
	while (cur > ptr)
	{
		--cur;
		if (*cur == c) return (_char_type_*)cur;
	}

	return HIO_NULL;
}
]])

dnl ---------------------------------------------------------------------------
define([[fn_find_char_in_cstr]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
_char_type_* _fn_name_ (const _char_type_* ptr, _char_type_ c)
{
	while (*ptr != '\0')
	{
		if (*ptr == c) return (_char_type_*)ptr;
		ptr++;
	}

	return HIO_NULL;
}
]])

dnl ---------------------------------------------------------------------------
dnl $3: is_space $4: prefix for option values
define([[fn_trim_chars]], [[define([[_fn_name_]], $1) define([[_char_type_]], $2)
_char_type_* _fn_name_ (const _char_type_* str, hio_oow_t* len, int flags)
{
	const _char_type_* p = str, * end = str + *len;

	if (p < end)
	{
		const _char_type_* s = HIO_NULL, * e = HIO_NULL;

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

	return (_char_type_*)str;
}
]])

dnl ---------------------------------------------------------------------------
dnl $3 is_space $4: copy_str_unlimited
define([[fn_split_cstr]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2)
int _fn_name_ (_char_type_* s, const _char_type_* delim, _char_type_ lquote, _char_type_ rquote, _char_type_ escape)
{
	_char_type_* p = s, *d;
	_char_type_* sp = HIO_NULL, * ep = HIO_NULL;
	int delim_mode;
	int cnt = 0;

	if (delim == HIO_NULL) delim_mode = 0;
	else 
	{
		delim_mode = 1;
		for (d = (_char_type_*)delim; *d != '\0'; d++)
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
				if (s != (_char_type_*)sp) $4 (s, sp);
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
				if (s != (_char_type_*)sp) $4 (s, sp);
				cnt++;
			}
		}
	}
	else if (delim_mode == 1) 
	{
		_char_type_* o;

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
		_char_type_* o;
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
				for (d = (_char_type_*)delim; *d != '\0'; d++) 
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
					for (d = (_char_type_*)delim; *d != '\0'; d++) 
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

dnl ---------------------------------------------------------------------------
define([[fn_chars_to_int]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2) define([[_int_type_]], $3)
_int_type_ _fn_name_ (const _char_type_* str, hio_oow_t len, int option, const _char_type_** endptr, int* is_sober)
{
	_int_type_ n = 0;
	const _char_type_* p, * pp;
	const _char_type_* end;
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
			_int_type_ e = 0, i;
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

dnl ---------------------------------------------------------------------------
dnl $4: is_space $5: prefix for some macros
define([[fn_chars_to_uint]], [[ define([[_fn_name_]], $1) define([[_char_type_]], $2) define([[_int_type_]], $3)
_int_type_ _fn_name_ (const _char_type_* str, hio_oow_t len, int option, const _char_type_** endptr, int* is_sober)
{
	_int_type_ n = 0;
	const _char_type_* p, * pp;
	const _char_type_* end;
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
			_int_type_ e = 0, i;
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
