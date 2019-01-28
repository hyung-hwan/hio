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

/* this file is included by sck-addr.c */

static int str_to_ipv4 (const char_t* str, mio_oow_t len, struct in_addr* inaddr)
{
	const char_t* end;
	int dots = 0, digits = 0;
	mio_uint32_t acc = 0, addr = 0;
	char_t c;

	end = str + len;

	do
	{
		if (str >= end)
		{
			if (dots < 3 || digits == 0) return -1;
			addr = (addr << 8) | acc;
			break;
		}

		c = *str++;

		if (c >= '0' && c <= '9') 
		{
			if (digits > 0 && acc == 0) return -1;
			acc = acc * 10 + (c - '0');
			if (acc > 255) return -1;
			digits++;
		}
		else if (c == '.') 
		{
			if (dots >= 3 || digits == 0) return -1;
			addr = (addr << 8) | acc;
			dots++; acc = 0; digits = 0;
		}
		else return -1;
	}
	while (1);

	inaddr->s_addr = mio_hton32(addr);
	return 0;

}

#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
static int str_to_ipv6 (const char_t* src, mio_oow_t len, struct in6_addr* inaddr)
{
	mio_uint8_t* tp, * endp, * colonp;
	const char_t* curtok;
	char_t ch;
	int saw_xdigit;
	unsigned int val;
	const char_t* src_end;

	src_end = src + len;

	MIO_MEMSET (inaddr, 0, MIO_SIZEOF(*inaddr));
	tp = &inaddr->s6_addr[0];
	endp = &inaddr->s6_addr[MIO_COUNTOF(inaddr->s6_addr)];
	colonp = MIO_NULL;

	/* Leading :: requires some special handling. */
	if (src < src_end && *src == ':')
	{
		src++;
		if (src >= src_end || *src != ':') return -1;
	}

	curtok = src;
	saw_xdigit = 0;
	val = 0;

	while (src < src_end)
	{
		int v1;

		ch = *src++;

		if (ch >= '0' && ch <= '9')
			v1 = ch - '0';
		else if (ch >= 'A' && ch <= 'F')
			v1 = ch - 'A' + 10;
		else if (ch >= 'a' && ch <= 'f')
			v1 = ch - 'a' + 10;
		else v1 = -1;
		if (v1 >= 0)
		{
			val <<= 4;
			val |= v1;
			if (val > 0xffff) return -1;
			saw_xdigit = 1;
			continue;
		}

		if (ch == ':') 
		{
			curtok = src;
			if (!saw_xdigit) 
			{
				if (colonp) return -1;
				colonp = tp;
				continue;
			}
			else if (src >= src_end)
			{
				/* a colon can't be the last character */
				return -1;
			}

			*tp++ = (mio_uint8_t)(val >> 8) & 0xff;
			*tp++ = (mio_uint8_t)val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}

		if (ch == '.' && ((tp + MIO_SIZEOF(struct in_addr)) <= endp) &&
		    str_to_ipv4(curtok, src_end - curtok, (struct in_addr*)tp) == 0) 
		{
			tp += MIO_SIZEOF(struct in_addr*);
			saw_xdigit = 0;
			break; 
		}

		return -1;
	}

	if (saw_xdigit) 
	{
		if (tp + MIO_SIZEOF(mio_uint16_t) > endp) return -1;
		*tp++ = (mio_uint8_t)(val >> 8) & 0xff;
		*tp++ = (mio_uint8_t)val & 0xff;
	}
	if (colonp != MIO_NULL) 
	{
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		mio_oow_t n = tp - colonp;
		mio_oow_t i;
 
		for (i = 1; i <= n; i++) 
		{
			endp[-i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}

	if (tp != endp) return -1;

	return 0;
}
#endif


static int str_to_sockaddr (mio_t* mio, const char_t* str, mio_oow_t len, sockaddr_t* nwad)
{
	const char_t* p;
	const char_t* end;
	cstr_t tmp;

	p = str;
	end = str + len;

	if (p >= end) 
	{
		mio_seterrbfmt (mio, MIO_EINVAL, "blank address");
		return -1;
	}

	MIO_MEMSET (nwad, 0, MIO_SIZEOF(*nwad));

#if defined(AF_UNIX)
	if (*p == '/' && len >= 2)
	{
	#if defined(CHAR_T_IS_BCH)
		mio_copy_bcstr (nwad->un.sun_path, MIO_COUNTOF(nwad->un.sun_path), str);
	#else
		mio_oow_t dstlen;

		dstlen = MIO_COUNTOF(nwad->un.sun_path) - 1;
		if (mio_convutobchars(mio, p, &len, nwad->un.sun_path, &dstlen) <= -1) 
		{
			mio_seterrbfmt (mio, MIO_EINVAL, "unable to convert encoding");
			return -1;
		}
		nwad->un.sun_path[dstlen] = '\0';
	#endif
		nwad->un.sun_family = AF_UNIX;
		return 0;
	}
#endif

#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (*p == '[')
	{
		/* IPv6 address */
		tmp.ptr = (char_t*)++p; /* skip [ and remember the position */
		while (p < end && *p != '%' && *p != ']') p++;

		if (p >= end) goto no_rbrack;

		tmp.len = p - tmp.ptr;
		if (*p == '%')
		{
			/* handle scope id */
			mio_uint32_t x;

			p++; /* skip % */

			if (p >= end)
			{
				/* premature end */
				mio_seterrbfmt (mio, MIO_EINVAL, "scope id blank");
				return -1;
			}

			if (*p >= '0' && *p <= '9') 
			{
				/* numeric scope id */
				nwad->in6.sin6_scope_id = 0;
				do
				{
					x = nwad->in6.sin6_scope_id * 10 + (*p - '0');
					if (x < nwad->in6.sin6_scope_id) 
					{
						mio_seterrbfmt (mio, MIO_EINVAL, "scope id too large");
						return -1; /* overflow */
					}
					nwad->in6.sin6_scope_id = x;
					p++;
				}
				while (p < end && *p >= '0' && *p <= '9');
			}
			else
			{
#if 0
TODO:
				/* interface name as a scope id? */
				const char_t* stmp = p;
				unsigned int index;
				do p++; while (p < end && *p != ']');
				if (mio_nwifwcsntoindex (stmp, p - stmp, &index) <= -1) return -1;
				tmpad.u.in6.scope = index;
#endif
			}

			if (p >= end || *p != ']') goto no_rbrack;
		}
		p++; /* skip ] */

		if (str_to_ipv6(tmp.ptr, tmp.len, &nwad->in6.sin6_addr) <= -1) goto unrecog;
		nwad->in6.sin6_family = AF_INET6;
	}
	else
	{
#endif
		/* IPv4 address */
		tmp.ptr = (char_t*)p;
		while (p < end && *p != ':') p++;
		tmp.len = p - tmp.ptr;

		if (str_to_ipv4(tmp.ptr, tmp.len, &nwad->in4.sin_addr) <= -1)
		{
		#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
			/* check if it is an IPv6 address not enclosed in []. 
			 * the port number can't be specified in this format. */
			if (p >= end || *p != ':') 
			{
				/* without :, it can't be an ipv6 address */
				goto unrecog;
			}


			while (p < end && *p != '%') p++;
			tmp.len = p - tmp.ptr;

			if (str_to_ipv6(tmp.ptr, tmp.len, &nwad->in6.sin6_addr) <= -1) goto unrecog;

			if (p < end && *p == '%')
			{
				/* handle scope id */
				mio_uint32_t x;

				p++; /* skip % */

				if (p >= end)
				{
					/* premature end */
					mio_seterrbfmt (mio, MIO_EINVAL, "scope id blank");
					return -1;
				}

				if (*p >= '0' && *p <= '9') 
				{
					/* numeric scope id */
					nwad->in6.sin6_scope_id = 0;
					do
					{
						x = nwad->in6.sin6_scope_id * 10 + (*p - '0');
						if (x < nwad->in6.sin6_scope_id) 
						{
							mio_seterrbfmt (mio, MIO_EINVAL, "scope id too large");
							return -1; /* overflow */
						}
						nwad->in6.sin6_scope_id = x;
						p++;
					}
					while (p < end && *p >= '0' && *p <= '9');
				}
				else
				{
#if 0
TODO
					/* interface name as a scope id? */
					const char_t* stmp = p;
					unsigned int index;
					do p++; while (p < end);
					if (mio_nwifwcsntoindex(stmp, p - stmp, &index) <= -1) return -1;
					nwad->in6.sin6_scope_id = index;
#endif
				}
			}

			if (p < end) goto unrecog; /* some gargage after the end? */

			nwad->in6.sin6_family = AF_INET6;
			return 0;
		#else
			goto unrecog;
		#endif
		}

		nwad->in4.sin_family = AF_INET;
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	}
#endif

	if (p < end && *p == ':') 
	{
		/* port number */
		mio_uint32_t port = 0;

		p++; /* skip : */

		tmp.ptr = (char_t*)p;
		while (p < end && *p >= '0' && *p <= '9')
		{
			port = port * 10 + (*p - '0');
			p++;
		}

		tmp.len = p - tmp.ptr;
		if (tmp.len <= 0 || tmp.len >= 6 || 
		    port > MIO_TYPE_MAX(mio_uint16_t)) 
		{
			mio_seterrbfmt (mio, MIO_EINVAL, "port number blank or too large");
			return -1;
		}

	#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		if (nwad->in4.sin_family == AF_INET)
			nwad->in4.sin_port = mio_hton16(port);
		else
			nwad->in6.sin6_port = mio_hton16(port);
	#else
		nwad->in4.sin_port = mio_hton16(port);
	#endif
	}

	return 0;
	
	
unrecog:
	mio_seterrbfmt (mio, MIO_EINVAL, "unrecognized address");
	return -1;
	
no_rbrack:
	mio_seterrbfmt (mio, MIO_EINVAL, "missing right bracket");
	return -1;
}
