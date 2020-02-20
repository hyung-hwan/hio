/*
 * $Id$
 *
    Copyright (c) 2006-2019 Chung, Hyung-Hwan. All rights reserved.

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

#include "mio-skad.h"
#include "mio-nwif.h"
#include "mio-fmt.h"
#include "mio-prv.h"

#include <sys/types.h>
#include <sys/socket.h>
#if defined(HAVE_NETINET_IN_H)
#	include <netinet/in.h>
#endif
#if defined(HAVE_SYS_UN_H)
#	include <sys/un.h>
#endif
#if defined(HAVE_NETPACKET_PACKET_H)
#	include <netpacket/packet.h>
#endif
#if defined(HAVE_NET_IF_DL_H)
#	include <net/if_dl.h>
#endif

union mio_skad_alt_t
{
	struct sockaddr    sa;
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
	struct sockaddr_in in4;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	struct sockaddr_in6 in6;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll ll;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	struct sockaddr_dl dl;
#endif
#if (MIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
	struct sockaddr_un un;
#endif
};
typedef union mio_skad_alt_t mio_skad_alt_t;


static int uchars_to_ipv4 (const mio_uch_t* str, mio_oow_t len, struct in_addr* inaddr)
{
	const mio_uch_t* end;
	int dots = 0, digits = 0;
	mio_uint32_t acc = 0, addr = 0;
	mio_uch_t c;

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

static int bchars_to_ipv4 (const mio_bch_t* str, mio_oow_t len, struct in_addr* inaddr)
{
	const mio_bch_t* end;
	int dots = 0, digits = 0;
	mio_uint32_t acc = 0, addr = 0;
	mio_bch_t c;

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
static int uchars_to_ipv6 (const mio_uch_t* src, mio_oow_t len, struct in6_addr* inaddr)
{
	mio_uint8_t* tp, * endp, * colonp;
	const mio_uch_t* curtok;
	mio_uch_t ch;
	int saw_xdigit;
	unsigned int val;
	const mio_uch_t* src_end;

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
		    uchars_to_ipv4(curtok, src_end - curtok, (struct in_addr*)tp) == 0) 
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

static int bchars_to_ipv6 (const mio_bch_t* src, mio_oow_t len, struct in6_addr* inaddr)
{
	mio_uint8_t* tp, * endp, * colonp;
	const mio_bch_t* curtok;
	mio_bch_t ch;
	int saw_xdigit;
	unsigned int val;
	const mio_bch_t* src_end;

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
		    bchars_to_ipv4(curtok, src_end - curtok, (struct in_addr*)tp) == 0) 
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

/* ---------------------------------------------------------- */

int mio_ucharstoskad (mio_t* mio, const mio_uch_t* str, mio_oow_t len, mio_skad_t* _skad)
{
	mio_skad_alt_t* skad = (mio_skad_alt_t*)_skad;
	const mio_uch_t* p;
	const mio_uch_t* end;
	mio_ucs_t tmp;

	p = str;
	end = str + len;

	if (p >= end) 
	{
		mio_seterrbfmt (mio, MIO_EINVAL, "blank address");
		return -1;
	}

	MIO_MEMSET (skad, 0, MIO_SIZEOF(*skad));

#if defined(AF_UNIX)
	if (*p == '/' && len >= 2)
	{
		mio_oow_t dstlen;
		dstlen = MIO_COUNTOF(skad->un.sun_path) - 1;
		if (mio_convutobchars(mio, p, &len, skad->un.sun_path, &dstlen) <= -1) return -1;
		skad->un.sun_path[dstlen] = '\0';
		skad->un.sun_family = AF_UNIX;
		return 0;
	}
#endif

#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (*p == '[')
	{
		/* IPv6 address */
		tmp.ptr = (mio_uch_t*)++p; /* skip [ and remember the position */
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
				skad->in6.sin6_scope_id = 0;
				do
				{
					x = skad->in6.sin6_scope_id * 10 + (*p - '0');
					if (x < skad->in6.sin6_scope_id) 
					{
						mio_seterrbfmt (mio, MIO_EINVAL, "scope id too large");
						return -1; /* overflow */
					}
					skad->in6.sin6_scope_id = x;
					p++;
				}
				while (p < end && *p >= '0' && *p <= '9');
			}
			else
			{
				/* interface name as a scope id? */
				const mio_uch_t* stmp = p;
				unsigned int index;
				do p++; while (p < end && *p != ']');
				if (mio_ucharstoifindex(mio, stmp, p - stmp, &index) <= -1) return -1;
				skad->in6.sin6_scope_id = index;
			}

			if (p >= end || *p != ']') goto no_rbrack;
		}
		p++; /* skip ] */

		if (uchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.sin6_addr) <= -1) goto unrecog;
		skad->in6.sin6_family = AF_INET6;
	}
	else
	{
#endif
		/* IPv4 address */
		tmp.ptr = (mio_uch_t*)p;
		while (p < end && *p != ':') p++;
		tmp.len = p - tmp.ptr;

		if (uchars_to_ipv4(tmp.ptr, tmp.len, &skad->in4.sin_addr) <= -1)
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

			if (uchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.sin6_addr) <= -1) goto unrecog;

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
					skad->in6.sin6_scope_id = 0;
					do
					{
						x = skad->in6.sin6_scope_id * 10 + (*p - '0');
						if (x < skad->in6.sin6_scope_id) 
						{
							mio_seterrbfmt (mio, MIO_EINVAL, "scope id too large");
							return -1; /* overflow */
						}
						skad->in6.sin6_scope_id = x;
						p++;
					}
					while (p < end && *p >= '0' && *p <= '9');
				}
				else
				{
					/* interface name as a scope id? */
					const mio_uch_t* stmp = p;
					unsigned int index;
					do p++; while (p < end);
					if (mio_ucharstoifindex(mio, stmp, p - stmp, &index) <= -1) return -1;
					skad->in6.sin6_scope_id = index;
				}
			}

			if (p < end) goto unrecog; /* some gargage after the end? */

			skad->in6.sin6_family = AF_INET6;
			return 0;
		#else
			goto unrecog;
		#endif
		}

		skad->in4.sin_family = AF_INET;
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	}
#endif

	if (p < end && *p == ':') 
	{
		/* port number */
		mio_uint32_t port = 0;

		p++; /* skip : */

		tmp.ptr = (mio_uch_t*)p;
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
		if (skad->in4.sin_family == AF_INET)
			skad->in4.sin_port = mio_hton16(port);
		else
			skad->in6.sin6_port = mio_hton16(port);
	#else
		skad->in4.sin_port = mio_hton16(port);
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

/* ---------------------------------------------------------- */

int mio_bcharstoskad (mio_t* mio, const mio_bch_t* str, mio_oow_t len, mio_skad_t* _skad)
{
	mio_skad_alt_t* skad = (mio_skad_alt_t*)_skad;
	const mio_bch_t* p;
	const mio_bch_t* end;
	mio_bcs_t tmp;

	p = str;
	end = str + len;

	if (p >= end) 
	{
		mio_seterrbfmt (mio, MIO_EINVAL, "blank address");
		return -1;
	}

	MIO_MEMSET (skad, 0, MIO_SIZEOF(*skad));

#if defined(AF_UNIX)
	if (*p == '/' && len >= 2)
	{
		mio_copy_bcstr (skad->un.sun_path, MIO_COUNTOF(skad->un.sun_path), str);
		skad->un.sun_family = AF_UNIX;
		return 0;
	}
#endif

#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (*p == '[')
	{
		/* IPv6 address */
		tmp.ptr = (mio_bch_t*)++p; /* skip [ and remember the position */
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
				skad->in6.sin6_scope_id = 0;
				do
				{
					x = skad->in6.sin6_scope_id * 10 + (*p - '0');
					if (x < skad->in6.sin6_scope_id) 
					{
						mio_seterrbfmt (mio, MIO_EINVAL, "scope id too large");
						return -1; /* overflow */
					}
					skad->in6.sin6_scope_id = x;
					p++;
				}
				while (p < end && *p >= '0' && *p <= '9');
			}
			else
			{
				/* interface name as a scope id? */
				const mio_bch_t* stmp = p;
				unsigned int index;
				do p++; while (p < end && *p != ']');
				if (mio_bcharstoifindex(mio, stmp, p - stmp, &index) <= -1) return -1;
				skad->in6.sin6_scope_id = index;
			}

			if (p >= end || *p != ']') goto no_rbrack;
		}
		p++; /* skip ] */

		if (bchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.sin6_addr) <= -1) goto unrecog;
		skad->in6.sin6_family = AF_INET6;
	}
	else
	{
#endif
		/* IPv4 address */
		tmp.ptr = (mio_bch_t*)p;
		while (p < end && *p != ':') p++;
		tmp.len = p - tmp.ptr;

		if (bchars_to_ipv4(tmp.ptr, tmp.len, &skad->in4.sin_addr) <= -1)
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

			if (bchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.sin6_addr) <= -1) goto unrecog;

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
					skad->in6.sin6_scope_id = 0;
					do
					{
						x = skad->in6.sin6_scope_id * 10 + (*p - '0');
						if (x < skad->in6.sin6_scope_id) 
						{
							mio_seterrbfmt (mio, MIO_EINVAL, "scope id too large");
							return -1; /* overflow */
						}
						skad->in6.sin6_scope_id = x;
						p++;
					}
					while (p < end && *p >= '0' && *p <= '9');
				}
				else
				{
					/* interface name as a scope id? */
					const mio_bch_t* stmp = p;
					unsigned int index;
					do p++; while (p < end);
					if (mio_bcharstoifindex(mio, stmp, p - stmp, &index) <= -1) return -1;
					skad->in6.sin6_scope_id = index;
				}
			}

			if (p < end) goto unrecog; /* some gargage after the end? */

			skad->in6.sin6_family = AF_INET6;
			return 0;
		#else
			goto unrecog;
		#endif
		}

		skad->in4.sin_family = AF_INET;
#if (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	}
#endif

	if (p < end && *p == ':') 
	{
		/* port number */
		mio_uint32_t port = 0;

		p++; /* skip : */

		tmp.ptr = (mio_bch_t*)p;
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
		if (skad->in4.sin_family == AF_INET)
			skad->in4.sin_port = mio_hton16(port);
		else
			skad->in6.sin6_port = mio_hton16(port);
	#else
		skad->in4.sin_port = mio_hton16(port);
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


/* ---------------------------------------------------------- */


#define __BTOA(type_t,b,p,end) \
	do { \
		type_t* sp = p; \
		do {  \
			if (p >= end) { \
				if (p == sp) break; \
				if (p - sp > 1) p[-2] = p[-1]; \
				p[-1] = (b % 10) + '0'; \
			} \
			else *p++ = (b % 10) + '0'; \
			b /= 10; \
		} while (b > 0); \
		if (p - sp > 1) { \
			type_t t = sp[0]; \
			sp[0] = p[-1]; \
			p[-1] = t; \
		} \
	} while (0);

#define __ADDDOT(p, end) \
	do { \
		if (p >= end) break; \
		*p++ = '.'; \
	} while (0)

/* ---------------------------------------------------------- */

static mio_oow_t ip4addr_to_ucstr (const struct in_addr* ipad, mio_uch_t* buf, mio_oow_t size)
{
	mio_uint8_t b;
	mio_uch_t* p, * end;
	mio_uint32_t ip;

	if (size <= 0) return 0;

	ip = ipad->s_addr;

	p = buf;
	end = buf + size - 1;

#if defined(MIO_ENDIAN_BIG)
	b = (ip >> 24) & 0xFF; __BTOA (mio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (mio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (mio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  0) & 0xFF; __BTOA (mio_uch_t, b, p, end);
#elif defined(MIO_ENDIAN_LITTLE)
	b = (ip >>  0) & 0xFF; __BTOA (mio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (mio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (mio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 24) & 0xFF; __BTOA (mio_uch_t, b, p, end);
#else
#	error Unknown Endian
#endif

	*p = '\0';
	return p - buf;
}


static mio_oow_t ip6addr_to_ucstr (const struct in6_addr* ipad, mio_uch_t* buf, mio_oow_t size)
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */

#define IP6ADDR_NWORDS (MIO_SIZEOF(ipad->s6_addr) / MIO_SIZEOF(mio_uint16_t))

	mio_uch_t tmp[MIO_COUNTOF("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")], *tp;
	struct { int base, len; } best, cur;
	mio_uint16_t words[IP6ADDR_NWORDS];
	int i;

	if (size <= 0) return 0;

	/*
	 * Preprocess:
	 *	Copy the input (bytewise) array into a wordwise array.
	 *	Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	MIO_MEMSET (words, 0, MIO_SIZEOF(words));
	for (i = 0; i < MIO_SIZEOF(ipad->s6_addr); i++)
		words[i / 2] |= (ipad->s6_addr[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	best.len = 0;
	cur.base = -1;
	cur.len = 0;

	for (i = 0; i < IP6ADDR_NWORDS; i++) 
	{
		if (words[i] == 0) 
		{
			if (cur.base == -1)
			{
				cur.base = i;
				cur.len = 1;
			}
			else
			{
				cur.len++;
			}
		}
		else 
		{
			if (cur.base != -1) 
			{
				if (best.base == -1 || cur.len > best.len) best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1) 
	{
		if (best.base == -1 || cur.len > best.len) best = cur;
	}
	if (best.base != -1 && best.len < 2) best.base = -1;

	/*
	 * Format the result.
	 */
	tp = tmp;
	for (i = 0; i < IP6ADDR_NWORDS; i++) 
	{
		/* Are we inside the best run of 0x00's? */
		if (best.base != -1 && i >= best.base &&
		    i < (best.base + best.len)) 
		{
			if (i == best.base) *tp++ = ':';
			continue;
		}

		/* Are we following an initial run of 0x00s or any real hex? */
		if (i != 0) *tp++ = ':';

		/* Is this address an encapsulated IPv4? ipv4-compatible or ipv4-mapped */
		if (i == 6 && best.base == 0 && (best.len == 6 || (best.len == 5 && words[5] == 0xffff))) 
		{
			struct in_addr ip4ad;
			MIO_MEMCPY (&ip4ad.s_addr, ipad->s6_addr + 12, MIO_SIZEOF(ip4ad.s_addr));
			tp += ip4addr_to_ucstr(&ip4ad, tp, MIO_COUNTOF(tmp) - (tp - tmp));
			break;
		}

		tp += mio_fmt_uintmax_to_ucstr(tp, MIO_COUNTOF(tmp) - (tp - tmp), words[i], 16, 0, '\0', MIO_NULL);
	}

	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) == IP6ADDR_NWORDS) *tp++ = ':';
	*tp++ = '\0';

	return mio_copy_ucstr(buf, size, tmp);

#undef IP6ADDR_NWORDS
}


mio_oow_t mio_skadtoucstr (mio_t* mio, const mio_skad_t* _skad, mio_uch_t* buf, mio_oow_t len, int flags)
{
	const mio_skad_alt_t* skad = (const mio_skad_alt_t*)_skad;
	mio_oow_t xlen = 0;

	/* unsupported types will result in an empty string */

	switch (mio_skad_family(_skad))
	{
		case MIO_AF_INET:
			if (flags & MIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				xlen += ip4addr_to_ucstr(&skad->in4.sin_addr, buf, len);
			}

			if (flags & MIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & MIO_SKAD_TO_BCSTR_ADDR) || skad->in4.sin_port != 0)
				{
					if (flags & MIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += mio_fmt_uintmax_to_ucstr(&buf[xlen], len - xlen, mio_ntoh16(skad->in4.sin_port), 10, 0, '\0', MIO_NULL);
				}
			}
			break;

		case MIO_AF_INET6:
			if (flags & MIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & MIO_SKAD_TO_BCSTR_ADDR) || skad->in6.sin6_port != 0)
				{
					if (flags & MIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = '[';
					}
				}
			}

			if (flags & MIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				xlen += ip6addr_to_ucstr(&skad->in6.sin6_addr, &buf[xlen], len - xlen);

				if (skad->in6.sin6_scope_id != 0)
				{
					int tmp;

					if (xlen + 1 >= len) goto done;
					buf[xlen++] = '%';

					if (xlen + 1 >= len) goto done;

					tmp = mio_ifindextoucstr(mio, skad->in6.sin6_scope_id, &buf[xlen], len - xlen);
					if (tmp <= -1)
					{
						xlen += mio_fmt_uintmax_to_ucstr(&buf[xlen], len - xlen, skad->in6.sin6_scope_id, 10, 0, '\0', MIO_NULL);
					}
					else xlen += tmp;
				}
			}

			if (flags & MIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & MIO_SKAD_TO_BCSTR_ADDR) || skad->in6.sin6_port != 0) 
				{
					if (flags & MIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ']';

						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += mio_fmt_uintmax_to_ucstr(&buf[xlen], len - xlen, mio_ntoh16(skad->in6.sin6_port), 10, 0, '\0', MIO_NULL);
				}
			}

			break;

		case MIO_AF_UNIX:
			if (flags & MIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '@';

				if (xlen + 1 >= len) goto done;
				else
				{
					mio_oow_t mbslen, wcslen = len - xlen;
					mio_convbtoucstr (mio, skad->un.sun_path, &mbslen, &buf[xlen], &wcslen, 1);
					/* i don't care about conversion errors */
					xlen += wcslen;
				}
			}

			break;
	}

done:
	if (xlen < len) buf[xlen] = '\0';
	return xlen;
}

/* ---------------------------------------------------------- */

static mio_oow_t ip4addr_to_bcstr (const struct in_addr* ipad, mio_bch_t* buf, mio_oow_t size)
{
	mio_uint8_t b;
	mio_bch_t* p, * end;
	mio_uint32_t ip;

	if (size <= 0) return 0;

	ip = ipad->s_addr;

	p = buf;
	end = buf + size - 1;

#if defined(MIO_ENDIAN_BIG)
	b = (ip >> 24) & 0xFF; __BTOA (mio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (mio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (mio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  0) & 0xFF; __BTOA (mio_bch_t, b, p, end);
#elif defined(MIO_ENDIAN_LITTLE)
	b = (ip >>  0) & 0xFF; __BTOA (mio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (mio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (mio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 24) & 0xFF; __BTOA (mio_bch_t, b, p, end);
#else
#	error Unknown Endian
#endif

	*p = '\0';
	return p - buf;
}


static mio_oow_t ip6addr_to_bcstr (const struct in6_addr* ipad, mio_bch_t* buf, mio_oow_t size)
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */

#define IP6ADDR_NWORDS (MIO_SIZEOF(ipad->s6_addr) / MIO_SIZEOF(mio_uint16_t))

	mio_bch_t tmp[MIO_COUNTOF("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")], *tp;
	struct { int base, len; } best, cur;
	mio_uint16_t words[IP6ADDR_NWORDS];
	int i;

	if (size <= 0) return 0;

	/*
	 * Preprocess:
	 *	Copy the input (bytewise) array into a wordwise array.
	 *	Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	MIO_MEMSET (words, 0, MIO_SIZEOF(words));
	for (i = 0; i < MIO_SIZEOF(ipad->s6_addr); i++)
		words[i / 2] |= (ipad->s6_addr[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	best.len = 0;
	cur.base = -1;
	cur.len = 0;

	for (i = 0; i < IP6ADDR_NWORDS; i++) 
	{
		if (words[i] == 0) 
		{
			if (cur.base == -1)
			{
				cur.base = i;
				cur.len = 1;
			}
			else
			{
				cur.len++;
			}
		}
		else 
		{
			if (cur.base != -1) 
			{
				if (best.base == -1 || cur.len > best.len) best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1) 
	{
		if (best.base == -1 || cur.len > best.len) best = cur;
	}
	if (best.base != -1 && best.len < 2) best.base = -1;

	/*
	 * Format the result.
	 */
	tp = tmp;
	for (i = 0; i < IP6ADDR_NWORDS; i++) 
	{
		/* Are we inside the best run of 0x00's? */
		if (best.base != -1 && i >= best.base &&
		    i < (best.base + best.len)) 
		{
			if (i == best.base) *tp++ = ':';
			continue;
		}

		/* Are we following an initial run of 0x00s or any real hex? */
		if (i != 0) *tp++ = ':';

		/* Is this address an encapsulated IPv4? ipv4-compatible or ipv4-mapped */
		if (i == 6 && best.base == 0 && (best.len == 6 || (best.len == 5 && words[5] == 0xffff))) 
		{
			struct in_addr ip4ad;
			MIO_MEMCPY (&ip4ad.s_addr, ipad->s6_addr + 12, MIO_SIZEOF(ip4ad.s_addr));
			tp += ip4addr_to_bcstr(&ip4ad, tp, MIO_COUNTOF(tmp) - (tp - tmp));
			break;
		}

		tp += mio_fmt_uintmax_to_bcstr(tp, MIO_COUNTOF(tmp) - (tp - tmp), words[i], 16, 0, '\0', MIO_NULL);
	}

	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) == IP6ADDR_NWORDS) *tp++ = ':';
	*tp++ = '\0';

	return mio_copy_bcstr(buf, size, tmp);

#undef IP6ADDR_NWORDS
}


mio_oow_t mio_skadtobcstr (mio_t* mio, const mio_skad_t* _skad, mio_bch_t* buf, mio_oow_t len, int flags)
{
	const mio_skad_alt_t* skad = (const mio_skad_alt_t*)_skad;
	mio_oow_t xlen = 0;

	/* unsupported types will result in an empty string */

	switch (mio_skad_family(_skad))
	{
		case MIO_AF_INET:
			if (flags & MIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				xlen += ip4addr_to_bcstr(&skad->in4.sin_addr, buf, len);
			}

			if (flags & MIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & MIO_SKAD_TO_BCSTR_ADDR) || skad->in4.sin_port != 0)
				{
					if (flags & MIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += mio_fmt_uintmax_to_bcstr(&buf[xlen], len - xlen, mio_ntoh16(skad->in4.sin_port), 10, 0, '\0', MIO_NULL);
				}
			}
			break;

		case MIO_AF_INET6:
			if (flags & MIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & MIO_SKAD_TO_BCSTR_ADDR) || skad->in6.sin6_port != 0)
				{
					if (flags & MIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = '[';
					}
				}
			}

			if (flags & MIO_SKAD_TO_BCSTR_ADDR)
			{

				if (xlen + 1 >= len) goto done;
				xlen += ip6addr_to_bcstr(&skad->in6.sin6_addr, &buf[xlen], len - xlen);

				if (skad->in6.sin6_scope_id != 0)
				{
					int tmp;

					if (xlen + 1 >= len) goto done;
					buf[xlen++] = '%';

					if (xlen + 1 >= len) goto done;

					tmp = mio_ifindextobcstr(mio, skad->in6.sin6_scope_id, &buf[xlen], len - xlen);
					if (tmp <= -1)
					{
						xlen += mio_fmt_uintmax_to_bcstr(&buf[xlen], len - xlen, skad->in6.sin6_scope_id, 10, 0, '\0', MIO_NULL);
					}
					else xlen += tmp;
				}
			}

			if (flags & MIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & MIO_SKAD_TO_BCSTR_ADDR) || skad->in6.sin6_port != 0) 
				{
					if (flags & MIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ']';

						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += mio_fmt_uintmax_to_bcstr(&buf[xlen], len - xlen, mio_ntoh16(skad->in6.sin6_port), 10, 0, '\0', MIO_NULL);
				}
			}

			break;

		case MIO_AF_UNIX:
			if (flags & MIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '@';

				if (xlen + 1 >= len) goto done;
				xlen += mio_copy_bcstr(&buf[xlen], len - xlen, skad->un.sun_path);
			}

			break;
	}

done:
	if (xlen < len) buf[xlen] = '\0';
	return xlen;
}


/* ------------------------------------------------------------------------- */


int mio_skad_family (const mio_skad_t* _skad)
{
	const mio_skad_alt_t* skad = (const mio_skad_alt_t*)_skad;
	/*MIO_STATIC_ASSERT (MIO_SIZEOF(*_skad) >= MIO_SIZEOF(*skad));*/
	return skad->sa.sa_family;
}

int mio_skad_size (const mio_skad_t* _skad)
{
	const mio_skad_alt_t* skad = (const mio_skad_alt_t*)_skad;
	/*MIO_STATIC_ASSERT (MIO_SIZEOF(*_skad) >= MIO_SIZEOF(*skad));*/

	switch (skad->sa.sa_family)
	{
	#if defined(AF_INET) && (MIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET: return MIO_SIZEOF(struct sockaddr_in);
	#endif
	#if defined(AF_INET6) && (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6: return MIO_SIZEOF(struct sockaddr_in6);
	#endif
	#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
		case AF_PACKET: return MIO_SIZEOF(struct sockaddr_ll);
	#endif
	#if defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
		case AF_LINK: return MIO_SIZEOF(struct sockaddr_dl);
	#endif
	#if defined(AF_UNIX) && (MIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
		case AF_UNIX: return MIO_SIZEOF(struct sockaddr_un);
	#endif
	}

	return 0;
}

int mio_skad_port (const mio_skad_t* _skad)
{
	const mio_skad_alt_t* skad = (const mio_skad_alt_t*)_skad;

	switch (skad->sa.sa_family)
	{
	#if defined(AF_INET) && (MIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET: return mio_ntoh16(((struct sockaddr_in*)skad)->sin_port);
	#endif
	#if defined(AF_INET6) && (MIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6: return mio_ntoh16(((struct sockaddr_in6*)skad)->sin6_port);
	#endif
	}
	return 0;
}

int mio_skad_ifindex (const mio_skad_t* _skad)
{
	const mio_skad_alt_t* skad = (const mio_skad_alt_t*)_skad;

#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	if (skad->sa.sa_family == AF_PACKET) return ((struct sockaddr_ll*)skad)->sll_ifindex;

#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	if (skad->sa.sa_family == AF_LINK)  return ((struct sockaddr_dl*)skad)->sdl_index;
#endif

	return 0;
}


void mio_skad_init_for_ip4 (mio_skad_t* skad, mio_uint16_t port, mio_ip4addr_t* ip4addr)
{
	struct sockaddr_in* sin = (struct sockaddr_in*)skad;

	MIO_MEMSET (sin, 0, MIO_SIZEOF(*sin));
	sin->sin_family = AF_INET;
	sin->sin_port = htons(port);
	if (ip4addr) MIO_MEMCPY (&sin->sin_addr, ip4addr, MIO_IP4ADDR_LEN);
}

void mio_skad_init_for_ip6 (mio_skad_t* skad, mio_uint16_t port, mio_ip6addr_t* ip6addr, int scope_id)
{
	struct sockaddr_in6* sin = (struct sockaddr_in6*)skad;

	MIO_MEMSET (sin, 0, MIO_SIZEOF(*sin));
	sin->sin6_family = AF_INET;
	sin->sin6_port = htons(port);
	sin->sin6_scope_id = scope_id;
	if (ip6addr) MIO_MEMCPY (&sin->sin6_addr, ip6addr, MIO_IP6ADDR_LEN);
}

void mio_skad_init_for_eth (mio_skad_t* skad, int ifindex, mio_ethaddr_t* ethaddr)
{
#if defined(AF_PACKET) && (MIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll* sll = (struct sockaddr_ll*)skad;
	MIO_MEMSET (sll, 0, MIO_SIZEOF(*sll));
	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = ifindex;
	if (ethaddr)
	{
		sll->sll_halen = MIO_ETHADDR_LEN;
		MIO_MEMCPY (sll->sll_addr, ethaddr, MIO_ETHADDR_LEN);
	}

#elif defined(AF_LINK) && (MIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	struct sockaddr_dl* sll = (struct sockaddr_dl*)skad;
	MIO_MEMSET (sll, 0, MIO_SIZEOF(*sll));
	sll->sdl_family = AF_LINK;
	sll->sdl_index = ifindex;
	if (ethaddr)
	{
		sll->sdl_alen = MIO_ETHADDR_LEN;
		MIO_MEMCPY (sll->sdl_data, ethaddr, MIO_ETHADDR_LEN);
	}
#else
#	error UNSUPPORTED DATALINK SOCKET ADDRESS
#endif
}

void mio_clear_skad (mio_skad_t* _skad)
{
	mio_skad_alt_t* skad = (mio_skad_alt_t*)_skad;
	/*MIO_STATIC_ASSERT (MIO_SIZEOF(*_skad) >= MIO_SIZEOF(*skad));*/
	MIO_MEMSET (skad, 0, MIO_SIZEOF(*skad));
	skad->sa.sa_family = MIO_AF_UNSPEC;
}

int mio_equal_skads (const mio_skad_t* addr1, const mio_skad_t* addr2, int strict)
{
	int f1;

	if ((f1 = mio_skad_family(addr1)) != mio_skad_family(addr2) ||
	    mio_skad_size(addr1) != mio_skad_size(addr2)) return 0;

	switch (f1)
	{
		case AF_INET:
			return ((struct sockaddr_in*)addr1)->sin_addr.s_addr == ((struct sockaddr_in*)addr2)->sin_addr.s_addr &&
			       ((struct sockaddr_in*)addr1)->sin_port == ((struct sockaddr_in*)addr2)->sin_port;

	#if defined(AF_INET6)
		case AF_INET6:
			
			if (strict)
			{
				/* don't care about scope id */
				return MIO_MEMCMP(&((struct sockaddr_in6*)addr1)->sin6_addr, &((struct sockaddr_in6*)addr2)->sin6_addr, MIO_SIZEOF(((struct sockaddr_in6*)addr2)->sin6_addr)) == 0 &&
				       ((struct sockaddr_in6*)addr1)->sin6_port == ((struct sockaddr_in6*)addr2)->sin6_port &&
				       ((struct sockaddr_in6*)addr1)->sin6_scope_id == ((struct sockaddr_in6*)addr2)->sin6_scope_id;
			}
			else
			{
				return MIO_MEMCMP(&((struct sockaddr_in6*)addr1)->sin6_addr, &((struct sockaddr_in6*)addr2)->sin6_addr, MIO_SIZEOF(((struct sockaddr_in6*)addr2)->sin6_addr)) == 0 &&
				       ((struct sockaddr_in6*)addr1)->sin6_port == ((struct sockaddr_in6*)addr2)->sin6_port;
			}
	#endif

	#if defined(AF_UNIX)
		case AF_UNIX:
			return mio_comp_bcstr(((struct sockaddr_un*)addr1)->sun_path, ((struct sockaddr_un*)addr2)->sun_path) == 0;
	#endif

		default:
			return MIO_MEMCMP(addr1, addr2, mio_skad_size(addr1)) == 0;
	}
}
