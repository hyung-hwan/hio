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

#include <hio-skad.h>
#include <hio-nwif.h>
#include <hio-fmt.h>
#include <hio-chr.h>
#include "hio-prv.h"

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

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
/* dirty hack to secure more space at the end of the actual socket address.
 * the extra fiels must be transparent to unaware parties.
 * if you add/delete extra fields or change the size of existing fields,
 * you must update the corresponding checks in configure.ac.
 *
 * extra fields:
 *   chan - used as a stream number for SCTP PACKETSEQ sockets. 
 *          use hio_skad_chan() and hio_skad_setchan() for safe access.
 */
struct sockaddr_in_x 
{
	struct sockaddr_in a;
	hio_uint16_t chan;
};
#endif

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
struct sockaddr_in6_x
{
	struct sockaddr_in6 a;
	hio_uint16_t chan;
};
#endif

union hio_skad_alt_t
{
	struct sockaddr    sa;
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
	struct sockaddr_in_x in4;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	struct sockaddr_in6_x in6;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll ll;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	struct sockaddr_dl dl;
#endif
#if (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
	struct sockaddr_un un;
#endif
};
typedef union hio_skad_alt_t hio_skad_alt_t;


static int uchars_to_ipv4 (const hio_uch_t* str, hio_oow_t len, struct in_addr* inaddr)
{
	const hio_uch_t* end;
	int dots = 0, digits = 0;
	hio_uint32_t acc = 0, addr = 0;
	hio_uch_t c;

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

	inaddr->s_addr = hio_hton32(addr);
	return 0;

}

static int bchars_to_ipv4 (const hio_bch_t* str, hio_oow_t len, struct in_addr* inaddr)
{
	const hio_bch_t* end;
	int dots = 0, digits = 0;
	hio_uint32_t acc = 0, addr = 0;
	hio_bch_t c;

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

	inaddr->s_addr = hio_hton32(addr);
	return 0;

}

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
static int uchars_to_ipv6 (const hio_uch_t* src, hio_oow_t len, struct in6_addr* inaddr)
{
	hio_uint8_t* tp, * endp, * colonp;
	const hio_uch_t* curtok;
	hio_uch_t ch;
	int saw_xdigit;
	unsigned int val;
	const hio_uch_t* src_end;

	src_end = src + len;

	HIO_MEMSET (inaddr, 0, HIO_SIZEOF(*inaddr));
	tp = &inaddr->s6_addr[0];
	endp = &inaddr->s6_addr[HIO_COUNTOF(inaddr->s6_addr)];
	colonp = HIO_NULL;

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

		v1 = HIO_XDIGIT_TO_NUM(ch);
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

			*tp++ = (hio_uint8_t)(val >> 8) & 0xff;
			*tp++ = (hio_uint8_t)val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}

		if (ch == '.' && ((tp + HIO_SIZEOF(struct in_addr)) <= endp) &&
		    uchars_to_ipv4(curtok, src_end - curtok, (struct in_addr*)tp) == 0) 
		{
			tp += HIO_SIZEOF(struct in_addr*);
			saw_xdigit = 0;
			break; 
		}

		return -1;
	}

	if (saw_xdigit) 
	{
		if (tp + HIO_SIZEOF(hio_uint16_t) > endp) return -1;
		*tp++ = (hio_uint8_t)(val >> 8) & 0xff;
		*tp++ = (hio_uint8_t)val & 0xff;
	}
	if (colonp != HIO_NULL) 
	{
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		hio_oow_t n = tp - colonp;
		hio_oow_t i;
 
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

static int bchars_to_ipv6 (const hio_bch_t* src, hio_oow_t len, struct in6_addr* inaddr)
{
	hio_uint8_t* tp, * endp, * colonp;
	const hio_bch_t* curtok;
	hio_bch_t ch;
	int saw_xdigit;
	unsigned int val;
	const hio_bch_t* src_end;

	src_end = src + len;

	HIO_MEMSET (inaddr, 0, HIO_SIZEOF(*inaddr));
	tp = &inaddr->s6_addr[0];
	endp = &inaddr->s6_addr[HIO_COUNTOF(inaddr->s6_addr)];
	colonp = HIO_NULL;

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

		v1 = HIO_XDIGIT_TO_NUM(ch);
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

			*tp++ = (hio_uint8_t)(val >> 8) & 0xff;
			*tp++ = (hio_uint8_t)val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}

		if (ch == '.' && ((tp + HIO_SIZEOF(struct in_addr)) <= endp) &&
		    bchars_to_ipv4(curtok, src_end - curtok, (struct in_addr*)tp) == 0) 
		{
			tp += HIO_SIZEOF(struct in_addr*);
			saw_xdigit = 0;
			break; 
		}

		return -1;
	}

	if (saw_xdigit) 
	{
		if (tp + HIO_SIZEOF(hio_uint16_t) > endp) return -1;
		*tp++ = (hio_uint8_t)(val >> 8) & 0xff;
		*tp++ = (hio_uint8_t)val & 0xff;
	}
	if (colonp != HIO_NULL) 
	{
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		hio_oow_t n = tp - colonp;
		hio_oow_t i;
 
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

int hio_ucharstoskad (hio_t* hio, const hio_uch_t* str, hio_oow_t len, hio_skad_t* _skad)
{
	hio_skad_alt_t* skad = (hio_skad_alt_t*)_skad;
	const hio_uch_t* p;
	const hio_uch_t* end;
	hio_ucs_t tmp;

	p = str;
	end = str + len;

	if (p >= end) 
	{
		hio_seterrbfmt (hio, HIO_EINVAL, "blank address");
		return -1;
	}

	/* use HIO_SIZEOF(*_skad) instead of HIO_SIZEOF(*skad) in case they are different */
	HIO_MEMSET (skad, 0, HIO_SIZEOF(*_skad)); 

	if (p[0] == '<' && p[1] == 'q' && p[2] == 'x' && p[3] == '>' && p[4] == '\0')
	{
		/* this is HIO specific. the rest isn't important */
		skad->sa.sa_family = HIO_AF_QX;
		return 0;
	}

	if (*p == '@')
	{
#if defined(AF_UNIX) && (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
		/* @aaa,  @/tmp/aaa ... */
		hio_oow_t srclen, dstlen;
		dstlen = HIO_COUNTOF(skad->un.sun_path) - 1;
		srclen = len - 1;
		if (hio_convutobchars(hio, p + 1, &srclen, skad->un.sun_path, &dstlen) <= -1) return -1;
		skad->un.sun_path[dstlen] = '\0';
		skad->un.sun_family = AF_UNIX;
		return 0;
#else
		hio_seterrbfmt (hio, HIO_ENOIMPL, "unix address not supported");
		return -1;	
#endif
	}


#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (*p == '[')
	{
		/* IPv6 address */
		tmp.ptr = (hio_uch_t*)++p; /* skip [ and remember the position */
		while (p < end && *p != '%' && *p != ']') p++;

		if (p >= end) goto no_rbrack;

		tmp.len = p - tmp.ptr;
		if (*p == '%')
		{
			/* handle scope id */
			hio_uint32_t x;

			p++; /* skip % */

			if (p >= end)
			{
				/* premature end */
				hio_seterrbfmt (hio, HIO_EINVAL, "scope id blank");
				return -1;
			}

			if (*p >= '0' && *p <= '9') 
			{
				/* numeric scope id */
				skad->in6.a.sin6_scope_id = 0;
				do
				{
					x = skad->in6.a.sin6_scope_id * 10 + (*p - '0');
					if (x < skad->in6.a.sin6_scope_id) 
					{
						hio_seterrbfmt (hio, HIO_EINVAL, "scope id too large");
						return -1; /* overflow */
					}
					skad->in6.a.sin6_scope_id = x;
					p++;
				}
				while (p < end && *p >= '0' && *p <= '9');
			}
			else
			{
				/* interface name as a scope id? */
				const hio_uch_t* stmp = p;
				unsigned int index;
				do p++; while (p < end && *p != ']');
				if (hio_ucharstoifindex(hio, stmp, p - stmp, &index) <= -1) return -1;
				skad->in6.a.sin6_scope_id = index;
			}

			if (p >= end || *p != ']') goto no_rbrack;
		}
		p++; /* skip ] */

		if (uchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.a.sin6_addr) <= -1) goto unrecog;
		skad->in6.a.sin6_family = AF_INET6;
	}
	else
	{
#endif
		/* IPv4 address */
		tmp.ptr = (hio_uch_t*)p;
		while (p < end && *p != ':') p++;
		tmp.len = p - tmp.ptr;

		if (uchars_to_ipv4(tmp.ptr, tmp.len, &skad->in4.a.sin_addr) <= -1)
		{
		#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
			/* check if it is an IPv6 address not enclosed in []. 
			 * the port number can't be specified in this format. */
			if (p >= end || *p != ':') 
			{
				/* without :, it can't be an ipv6 address */
				goto unrecog;
			}

			while (p < end && *p != '%') p++;
			tmp.len = p - tmp.ptr;

			if (uchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.a.sin6_addr) <= -1) goto unrecog;

			if (p < end && *p == '%')
			{
				/* handle scope id */
				hio_uint32_t x;

				p++; /* skip % */

				if (p >= end)
				{
					/* premature end */
					hio_seterrbfmt (hio, HIO_EINVAL, "scope id blank");
					return -1;
				}

				if (*p >= '0' && *p <= '9') 
				{
					/* numeric scope id */
					skad->in6.a.sin6_scope_id = 0;
					do
					{
						x = skad->in6.a.sin6_scope_id * 10 + (*p - '0');
						if (x < skad->in6.a.sin6_scope_id) 
						{
							hio_seterrbfmt (hio, HIO_EINVAL, "scope id too large");
							return -1; /* overflow */
						}
						skad->in6.a.sin6_scope_id = x;
						p++;
					}
					while (p < end && *p >= '0' && *p <= '9');
				}
				else
				{
					/* interface name as a scope id? */
					const hio_uch_t* stmp = p;
					unsigned int index;
					do p++; while (p < end);
					if (hio_ucharstoifindex(hio, stmp, p - stmp, &index) <= -1) return -1;
					skad->in6.a.sin6_scope_id = index;
				}
			}

			if (p < end) goto unrecog; /* some gargage after the end? */

			skad->in6.a.sin6_family = AF_INET6;
			return 0;
		#else
			goto unrecog;
		#endif
		}

		skad->in4.a.sin_family = AF_INET;
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	}
#endif

	if (p < end && *p == ':') 
	{
		/* port number */
		hio_uint32_t port = 0;

		p++; /* skip : */

		tmp.ptr = (hio_uch_t*)p;
		while (p < end && *p >= '0' && *p <= '9')
		{
			port = port * 10 + (*p - '0');
			p++;
		}

		tmp.len = p - tmp.ptr;
		if (tmp.len <= 0 || tmp.len >= 6 || 
		    port > HIO_TYPE_MAX(hio_uint16_t)) 
		{
			hio_seterrbfmt (hio, HIO_EINVAL, "port number blank or too large");
			return -1;
		}

	#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		if (skad->in4.a.sin_family == AF_INET)
			skad->in4.a.sin_port = hio_hton16(port);
		else
			skad->in6.a.sin6_port = hio_hton16(port);
	#else
		skad->in4.a.sin_port = hio_hton16(port);
	#endif
	}

	return 0;

unrecog:
	hio_seterrbfmt (hio, HIO_EINVAL, "unrecognized address");
	return -1;
	
no_rbrack:
	hio_seterrbfmt (hio, HIO_EINVAL, "missing right bracket");
	return -1;
}

/* ---------------------------------------------------------- */

int hio_bcharstoskad (hio_t* hio, const hio_bch_t* str, hio_oow_t len, hio_skad_t* _skad)
{
	hio_skad_alt_t* skad = (hio_skad_alt_t*)_skad;
	const hio_bch_t* p;
	const hio_bch_t* end;
	hio_bcs_t tmp;

	p = str;
	end = str + len;

	if (p >= end) 
	{
		hio_seterrbfmt (hio, HIO_EINVAL, "blank address");
		return -1;
	}

	/* use HIO_SIZEOF(*_skad) instead of HIO_SIZEOF(*skad) in case they are different */
	HIO_MEMSET (skad, 0, HIO_SIZEOF(*_skad));

	if (p[0] == '<' && p[1] == 'q' && p[2] == 'x' && p[3] == '>' && p[4] == '\0')
	{
		/* this is HIO specific. the rest isn't important */
		skad->sa.sa_family = HIO_AF_QX;
		return 0;
	}

	if (*p == '@')
	{
#if defined(AF_UNIX) && (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
		/* @aaa,  @/tmp/aaa ... */
		hio_copy_bchars_to_bcstr (skad->un.sun_path, HIO_COUNTOF(skad->un.sun_path), str + 1, len - 1);
		skad->un.sun_family = HIO_AF_UNIX;
		return 0;
#else
		hio_seterrbfmt (hio, HIO_ENOIMPL, "unix address not supported");
		return -1;	
#endif
	}

#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (*p == '[')
	{
		/* IPv6 address */
		tmp.ptr = (hio_bch_t*)++p; /* skip [ and remember the position */
		while (p < end && *p != '%' && *p != ']') p++;

		if (p >= end) goto no_rbrack;

		tmp.len = p - tmp.ptr;
		if (*p == '%')
		{
			/* handle scope id */
			hio_uint32_t x;

			p++; /* skip % */

			if (p >= end)
			{
				/* premature end */
				hio_seterrbfmt (hio, HIO_EINVAL, "scope id blank");
				return -1;
			}

			if (*p >= '0' && *p <= '9') 
			{
				/* numeric scope id */
				skad->in6.a.sin6_scope_id = 0;
				do
				{
					x = skad->in6.a.sin6_scope_id * 10 + (*p - '0');
					if (x < skad->in6.a.sin6_scope_id) 
					{
						hio_seterrbfmt (hio, HIO_EINVAL, "scope id too large");
						return -1; /* overflow */
					}
					skad->in6.a.sin6_scope_id = x;
					p++;
				}
				while (p < end && *p >= '0' && *p <= '9');
			}
			else
			{
				/* interface name as a scope id? */
				const hio_bch_t* stmp = p;
				unsigned int index;
				do p++; while (p < end && *p != ']');
				if (hio_bcharstoifindex(hio, stmp, p - stmp, &index) <= -1) return -1;
				skad->in6.a.sin6_scope_id = index;
			}

			if (p >= end || *p != ']') goto no_rbrack;
		}
		p++; /* skip ] */

		if (bchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.a.sin6_addr) <= -1) goto unrecog;
		skad->in6.a.sin6_family = AF_INET6;
	}
	else
	{
#endif
		/* IPv4 address */
		tmp.ptr = (hio_bch_t*)p;
		while (p < end && *p != ':') p++;
		tmp.len = p - tmp.ptr;

		if (bchars_to_ipv4(tmp.ptr, tmp.len, &skad->in4.a.sin_addr) <= -1)
		{
		#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
			/* check if it is an IPv6 address not enclosed in []. 
			 * the port number can't be specified in this format. */
			if (p >= end || *p != ':') 
			{
				/* without :, it can't be an ipv6 address */
				goto unrecog;
			}


			while (p < end && *p != '%') p++;
			tmp.len = p - tmp.ptr;

			if (bchars_to_ipv6(tmp.ptr, tmp.len, &skad->in6.a.sin6_addr) <= -1) goto unrecog;

			if (p < end && *p == '%')
			{
				/* handle scope id */
				hio_uint32_t x;

				p++; /* skip % */

				if (p >= end)
				{
					/* premature end */
					hio_seterrbfmt (hio, HIO_EINVAL, "scope id blank");
					return -1;
				}

				if (*p >= '0' && *p <= '9') 
				{
					/* numeric scope id */
					skad->in6.a.sin6_scope_id = 0;
					do
					{
						x = skad->in6.a.sin6_scope_id * 10 + (*p - '0');
						if (x < skad->in6.a.sin6_scope_id) 
						{
							hio_seterrbfmt (hio, HIO_EINVAL, "scope id too large");
							return -1; /* overflow */
						}
						skad->in6.a.sin6_scope_id = x;
						p++;
					}
					while (p < end && *p >= '0' && *p <= '9');
				}
				else
				{
					/* interface name as a scope id? */
					const hio_bch_t* stmp = p;
					unsigned int index;
					do p++; while (p < end);
					if (hio_bcharstoifindex(hio, stmp, p - stmp, &index) <= -1) return -1;
					skad->in6.a.sin6_scope_id = index;
				}
			}

			if (p < end) goto unrecog; /* some gargage after the end? */

			skad->in6.a.sin6_family = AF_INET6;
			return 0;
		#else
			goto unrecog;
		#endif
		}

		skad->in4.a.sin_family = AF_INET;
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	}
#endif

	if (p < end && *p == ':') 
	{
		/* port number */
		hio_uint32_t port = 0;

		p++; /* skip : */

		tmp.ptr = (hio_bch_t*)p;
		while (p < end && *p >= '0' && *p <= '9')
		{
			port = port * 10 + (*p - '0');
			p++;
		}

		tmp.len = p - tmp.ptr;
		if (tmp.len <= 0 || tmp.len >= 6 || 
		    port > HIO_TYPE_MAX(hio_uint16_t)) 
		{
			hio_seterrbfmt (hio, HIO_EINVAL, "port number blank or too large");
			return -1;
		}

	#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		if (skad->in4.a.sin_family == AF_INET)
			skad->in4.a.sin_port = hio_hton16(port);
		else
			skad->in6.a.sin6_port = hio_hton16(port);
	#else
		skad->in4.a.sin_port = hio_hton16(port);
	#endif
	}

	return 0;

unrecog:
	hio_seterrbfmt (hio, HIO_EINVAL, "unrecognized address");
	return -1;

no_rbrack:
	hio_seterrbfmt (hio, HIO_EINVAL, "missing right bracket");
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

static hio_oow_t ip4ad_to_ucstr (const struct in_addr* ipad, hio_uch_t* buf, hio_oow_t size)
{
	hio_uint8_t b;
	hio_uch_t* p, * end;
	hio_uint32_t ip;

	if (size <= 0) return 0;

	ip = ipad->s_addr;

	p = buf;
	end = buf + size - 1;

#if defined(HIO_ENDIAN_BIG)
	b = (ip >> 24) & 0xFF; __BTOA (hio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (hio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (hio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  0) & 0xFF; __BTOA (hio_uch_t, b, p, end);
#elif defined(HIO_ENDIAN_LITTLE)
	b = (ip >>  0) & 0xFF; __BTOA (hio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (hio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (hio_uch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 24) & 0xFF; __BTOA (hio_uch_t, b, p, end);
#else
#	error Unknown Endian
#endif

	*p = '\0';
	return p - buf;
}

static hio_oow_t ip6ad_to_ucstr (const struct in6_addr* ipad, hio_uch_t* buf, hio_oow_t size)
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */

#define IP6AD_NWORDS (HIO_SIZEOF(ipad->s6_addr) / HIO_SIZEOF(hio_uint16_t))

	hio_uch_t tmp[HIO_COUNTOF("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")], *tp;
	struct { int base, len; } best, cur;
	hio_uint16_t words[IP6AD_NWORDS];
	int i;

	if (size <= 0) return 0;

	/*
	 * Preprocess:
	 *	Copy the input (bytewise) array into a wordwise array.
	 *	Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	HIO_MEMSET (words, 0, HIO_SIZEOF(words));
	for (i = 0; i < HIO_SIZEOF(ipad->s6_addr); i++)
		words[i / 2] |= (ipad->s6_addr[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	best.len = 0;
	cur.base = -1;
	cur.len = 0;

	for (i = 0; i < IP6AD_NWORDS; i++) 
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
	for (i = 0; i < IP6AD_NWORDS; i++) 
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
			HIO_MEMCPY (&ip4ad.s_addr, ipad->s6_addr + 12, HIO_SIZEOF(ip4ad.s_addr));
			tp += ip4ad_to_ucstr(&ip4ad, tp, HIO_COUNTOF(tmp) - (tp - tmp));
			break;
		}

		tp += hio_fmt_uintmax_to_ucstr(tp, HIO_COUNTOF(tmp) - (tp - tmp), words[i], 16, 0, '\0', HIO_NULL);
	}

	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) == IP6AD_NWORDS) *tp++ = ':';
	*tp++ = '\0';

	return hio_copy_ucstr(buf, size, tmp);

#undef IP6AD_NWORDS
}


hio_oow_t hio_skadtoucstr (hio_t* hio, const hio_skad_t* _skad, hio_uch_t* buf, hio_oow_t len, int flags)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;
	hio_oow_t xlen = 0;

	/* unsupported types will result in an empty string */

	switch (hio_skad_family(_skad))
	{
		case HIO_AF_INET:
			if (flags & HIO_SKAD_TO_UCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				xlen += ip4ad_to_ucstr(&skad->in4.a.sin_addr, buf, len);
			}

			if (flags & HIO_SKAD_TO_UCSTR_PORT)
			{
				if (!(flags & HIO_SKAD_TO_UCSTR_ADDR) || skad->in4.a.sin_port != 0)
				{
					if (flags & HIO_SKAD_TO_UCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += hio_fmt_uintmax_to_ucstr(&buf[xlen], len - xlen, hio_ntoh16(skad->in4.a.sin_port), 10, 0, '\0', HIO_NULL);
				}
			}
			break;

		case HIO_AF_INET6:
			if (flags & HIO_SKAD_TO_UCSTR_PORT)
			{
				if (!(flags & HIO_SKAD_TO_UCSTR_ADDR) || skad->in6.a.sin6_port != 0)
				{
					if (flags & HIO_SKAD_TO_UCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = '[';
					}
				}
			}

			if (flags & HIO_SKAD_TO_UCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				xlen += ip6ad_to_ucstr(&skad->in6.a.sin6_addr, &buf[xlen], len - xlen);

				if (skad->in6.a.sin6_scope_id != 0)
				{
					int tmp;

					if (xlen + 1 >= len) goto done;
					buf[xlen++] = '%';

					if (xlen + 1 >= len) goto done;

					tmp = hio_ifindextoucstr(hio, skad->in6.a.sin6_scope_id, &buf[xlen], len - xlen);
					if (tmp <= -1)
					{
						xlen += hio_fmt_uintmax_to_ucstr(&buf[xlen], len - xlen, skad->in6.a.sin6_scope_id, 10, 0, '\0', HIO_NULL);
					}
					else xlen += tmp;
				}
			}

			if (flags & HIO_SKAD_TO_UCSTR_PORT)
			{
				if (!(flags & HIO_SKAD_TO_UCSTR_ADDR) || skad->in6.a.sin6_port != 0) 
				{
					if (flags & HIO_SKAD_TO_UCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ']';

						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += hio_fmt_uintmax_to_ucstr(&buf[xlen], len - xlen, hio_ntoh16(skad->in6.a.sin6_port), 10, 0, '\0', HIO_NULL);
				}
			}

			break;

		case HIO_AF_UNIX:
			if (flags & HIO_SKAD_TO_UCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '@';

				if (xlen + 1 >= len) goto done;
				else
				{
					hio_oow_t mbslen, wcslen = len - xlen;
					hio_convbtoucstr (hio, skad->un.sun_path, &mbslen, &buf[xlen], &wcslen, 1);
					/* i don't care about conversion errors */
					xlen += wcslen;
				}
			}

			break;

		case HIO_AF_QX:
			if (flags & HIO_SKAD_TO_UCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '<';
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = 'q';
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = 'x';
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '>';
			}

			break;
	}

done:
	if (xlen < len) buf[xlen] = '\0';
	return xlen;
}

/* ---------------------------------------------------------- */

static hio_oow_t ip4ad_to_bcstr (const struct in_addr* ipad, hio_bch_t* buf, hio_oow_t size)
{
	hio_uint8_t b;
	hio_bch_t* p, * end;
	hio_uint32_t ip;

	if (size <= 0) return 0;

	ip = ipad->s_addr;

	p = buf;
	end = buf + size - 1;

#if defined(HIO_ENDIAN_BIG)
	b = (ip >> 24) & 0xFF; __BTOA (hio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (hio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (hio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  0) & 0xFF; __BTOA (hio_bch_t, b, p, end);
#elif defined(HIO_ENDIAN_LITTLE)
	b = (ip >>  0) & 0xFF; __BTOA (hio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >>  8) & 0xFF; __BTOA (hio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 16) & 0xFF; __BTOA (hio_bch_t, b, p, end); __ADDDOT (p, end);
	b = (ip >> 24) & 0xFF; __BTOA (hio_bch_t, b, p, end);
#else
#	error Unknown Endian
#endif

	*p = '\0';
	return p - buf;
}


static hio_oow_t ip6ad_to_bcstr (const struct in6_addr* ipad, hio_bch_t* buf, hio_oow_t size)
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */

#define IP6AD_NWORDS (HIO_SIZEOF(ipad->s6_addr) / HIO_SIZEOF(hio_uint16_t))

	hio_bch_t tmp[HIO_COUNTOF("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")], *tp;
	struct { int base, len; } best, cur;
	hio_uint16_t words[IP6AD_NWORDS];
	int i;

	if (size <= 0) return 0;

	/*
	 * Preprocess:
	 *	Copy the input (bytewise) array into a wordwise array.
	 *	Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	HIO_MEMSET (words, 0, HIO_SIZEOF(words));
	for (i = 0; i < HIO_SIZEOF(ipad->s6_addr); i++)
		words[i / 2] |= (ipad->s6_addr[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	best.len = 0;
	cur.base = -1;
	cur.len = 0;

	for (i = 0; i < IP6AD_NWORDS; i++) 
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
	for (i = 0; i < IP6AD_NWORDS; i++) 
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
			HIO_MEMCPY (&ip4ad.s_addr, ipad->s6_addr + 12, HIO_SIZEOF(ip4ad.s_addr));
			tp += ip4ad_to_bcstr(&ip4ad, tp, HIO_COUNTOF(tmp) - (tp - tmp));
			break;
		}

		tp += hio_fmt_uintmax_to_bcstr(tp, HIO_COUNTOF(tmp) - (tp - tmp), words[i], 16, 0, '\0', HIO_NULL);
	}

	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) == IP6AD_NWORDS) *tp++ = ':';
	*tp++ = '\0';

	return hio_copy_bcstr(buf, size, tmp);

#undef IP6AD_NWORDS
}


hio_oow_t hio_skadtobcstr (hio_t* hio, const hio_skad_t* _skad, hio_bch_t* buf, hio_oow_t len, int flags)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;
	hio_oow_t xlen = 0;

	/* unsupported types will result in an empty string */

	switch (hio_skad_family(_skad))
	{
		case HIO_AF_INET:
			if (flags & HIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				xlen += ip4ad_to_bcstr(&skad->in4.a.sin_addr, buf, len);
			}

			if (flags & HIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & HIO_SKAD_TO_BCSTR_ADDR) || skad->in4.a.sin_port != 0)
				{
					if (flags & HIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += hio_fmt_uintmax_to_bcstr(&buf[xlen], len - xlen, hio_ntoh16(skad->in4.a.sin_port), 10, 0, '\0', HIO_NULL);
				}
			}
			break;

		case HIO_AF_INET6:
			if (flags & HIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & HIO_SKAD_TO_BCSTR_ADDR) || skad->in6.a.sin6_port != 0)
				{
					if (flags & HIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = '[';
					}
				}
			}

			if (flags & HIO_SKAD_TO_BCSTR_ADDR)
			{

				if (xlen + 1 >= len) goto done;
				xlen += ip6ad_to_bcstr(&skad->in6.a.sin6_addr, &buf[xlen], len - xlen);

				if (skad->in6.a.sin6_scope_id != 0)
				{
					int tmp;

					if (xlen + 1 >= len) goto done;
					buf[xlen++] = '%';

					if (xlen + 1 >= len) goto done;

					tmp = hio_ifindextobcstr(hio, skad->in6.a.sin6_scope_id, &buf[xlen], len - xlen);
					if (tmp <= -1)
					{
						xlen += hio_fmt_uintmax_to_bcstr(&buf[xlen], len - xlen, skad->in6.a.sin6_scope_id, 10, 0, '\0', HIO_NULL);
					}
					else xlen += tmp;
				}
			}

			if (flags & HIO_SKAD_TO_BCSTR_PORT)
			{
				if (!(flags & HIO_SKAD_TO_BCSTR_ADDR) || skad->in6.a.sin6_port != 0) 
				{
					if (flags & HIO_SKAD_TO_BCSTR_ADDR)
					{
						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ']';

						if (xlen + 1 >= len) goto done;
						buf[xlen++] = ':';
					}

					if (xlen + 1 >= len) goto done;
					xlen += hio_fmt_uintmax_to_bcstr(&buf[xlen], len - xlen, hio_ntoh16(skad->in6.a.sin6_port), 10, 0, '\0', HIO_NULL);
				}
			}

			break;

		case HIO_AF_UNIX:
			if (flags & HIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '@';

				if (xlen + 1 >= len) goto done;
				xlen += hio_copy_bcstr(&buf[xlen], len - xlen, skad->un.sun_path);
			}

			break;

		case HIO_AF_QX:
			if (flags & HIO_SKAD_TO_BCSTR_ADDR)
			{
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '<';
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = 'q';
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = 'x';
				if (xlen + 1 >= len) goto done;
				buf[xlen++] = '>';
			}

			break;
	}

done:
	if (xlen < len) buf[xlen] = '\0';
	return xlen;
}


/* ------------------------------------------------------------------------- */


int hio_skad_family (const hio_skad_t* _skad)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;
	/*HIO_STATIC_ASSERT (HIO_SIZEOF(*_skad) >= HIO_SIZEOF(*skad));*/
	return skad->sa.sa_family;
}

int hio_skad_size (const hio_skad_t* _skad)
{
	/* this excludes the size of the 'chan' field.
	 * the field is not part of the core socket address */

	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;
	/*HIO_STATIC_ASSERT (HIO_SIZEOF(*_skad) >= HIO_SIZEOF(*skad));*/

	switch (skad->sa.sa_family)
	{
	#if defined(AF_INET) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET: return HIO_SIZEOF(struct sockaddr_in);
	#endif
	#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6: return HIO_SIZEOF(struct sockaddr_in6);
	#endif
	#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
		case AF_PACKET: return HIO_SIZEOF(struct sockaddr_ll);
	#endif
	#if defined(AF_LINK) && (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
		case AF_LINK: return HIO_SIZEOF(struct sockaddr_dl);
	#endif
	#if defined(AF_UNIX) && (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
		case AF_UNIX: return HIO_SIZEOF(struct sockaddr_un);
	#endif
	}

	return 0;
}

int hio_skad_port (const hio_skad_t* _skad)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;

	switch (skad->sa.sa_family)
	{
	#if defined(AF_INET) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET: return hio_ntoh16(skad->in4.a.sin_port);
	#endif
	#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6: return hio_ntoh16(skad->in6.a.sin6_port);
	#endif
	}
	return 0;
}

int hio_skad_ifindex (const hio_skad_t* _skad)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;

#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	if (skad->sa.sa_family == AF_PACKET) return skad->ll.sll_ifindex;

#elif defined(AF_LINK) && (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	if (skad->sa.sa_family == AF_LINK)  return skad->dl.sdl_index;
#endif

	return 0;
}

int hio_skad_scope_id (const hio_skad_t* _skad)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;

#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (skad->sa.sa_family == AF_INET6)  return skad->in6.a.sin6_scope_id;
#endif

	return 0;
}

void hio_skad_set_scope_id (hio_skad_t* _skad, int scope_id)
{
	hio_skad_alt_t* skad = (hio_skad_alt_t*)_skad;

#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	if (skad->sa.sa_family == AF_INET6) skad->in6.a.sin6_scope_id = scope_id;
#endif
}

hio_uint16_t hio_skad_chan (const hio_skad_t* _skad)
{
	const hio_skad_alt_t* skad = (const hio_skad_alt_t*)_skad;

	switch (skad->sa.sa_family)
	{
	#if defined(AF_INET) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET: return skad->in4.chan;
	#endif
	#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6: return skad->in6.chan;
	#endif
	}
	return 0;
}

void hio_skad_set_chan (hio_skad_t* _skad, hio_uint16_t chan)
{
	hio_skad_alt_t* skad = (hio_skad_alt_t*)_skad;

	switch (skad->sa.sa_family)
	{
	#if defined(AF_INET) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET: skad->in4.chan = chan; break;
	#endif
	#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6: skad->in6.chan = chan; break;
	#endif
	}
}

void hio_skad_init_for_ip4 (hio_skad_t* skad, hio_uint16_t port, hio_ip4ad_t* ip4ad)
{
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
	struct sockaddr_in* sin = (struct sockaddr_in*)skad;
	HIO_MEMSET (sin, 0, HIO_SIZEOF(*sin));
	sin->sin_family = AF_INET;
	sin->sin_port = hio_hton16(port);
	if (ip4ad) HIO_MEMCPY (&sin->sin_addr, ip4ad->v, HIO_IP4AD_LEN);
#endif
}

void hio_skad_init_for_ip6 (hio_skad_t* skad, hio_uint16_t port, hio_ip6ad_t* ip6ad, int scope_id)
{
#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
	struct sockaddr_in6* sin = (struct sockaddr_in6*)skad;
	HIO_MEMSET (sin, 0, HIO_SIZEOF(*sin));
	sin->sin6_family = AF_INET6;
	sin->sin6_port = hio_hton16(port);
	sin->sin6_scope_id = scope_id;
	if (ip6ad) HIO_MEMCPY (&sin->sin6_addr, ip6ad->v, HIO_IP6AD_LEN);
#endif
}

void hio_skad_init_for_ip_with_bytes (hio_skad_t* skad, hio_uint16_t port, const hio_uint8_t* bytes, hio_oow_t len)
{
	switch (len)
	{
	#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case HIO_IP4AD_LEN:
		{
			struct sockaddr_in* sin = (struct sockaddr_in*)skad;
			HIO_MEMSET (sin, 0, HIO_SIZEOF(*sin));
			sin->sin_family = AF_INET;
			sin->sin_port = hio_hton16(port);
			HIO_MEMCPY (&sin->sin_addr, bytes, len);
			break;
		}
	#endif
	#if (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case HIO_IP6AD_LEN:
		{
			struct sockaddr_in6* sin = (struct sockaddr_in6*)skad;
			HIO_MEMSET (sin, 0, HIO_SIZEOF(*sin));
			sin->sin6_family = AF_INET6;
			sin->sin6_port = hio_hton16(port);
			HIO_MEMCPY (&sin->sin6_addr, bytes, len);
			break;
		}
	#endif
		default:
			break;
	}
}


void hio_skad_init_for_eth (hio_skad_t* skad, int ifindex, hio_ethad_t* ethad)
{
#if defined(AF_PACKET) && (HIO_SIZEOF_STRUCT_SOCKADDR_LL > 0)
	struct sockaddr_ll* sll = (struct sockaddr_ll*)skad;
	HIO_MEMSET (sll, 0, HIO_SIZEOF(*sll));
	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = ifindex;
	if (ethad)
	{
		sll->sll_halen = HIO_ETHAD_LEN;
		HIO_MEMCPY (sll->sll_addr, ethad, HIO_ETHAD_LEN);
	}

#elif defined(AF_LINK) && (HIO_SIZEOF_STRUCT_SOCKADDR_DL > 0)
	struct sockaddr_dl* sll = (struct sockaddr_dl*)skad;
	HIO_MEMSET (sll, 0, HIO_SIZEOF(*sll));
	sll->sdl_family = AF_LINK;
	sll->sdl_index = ifindex;
	if (ethad)
	{
		sll->sdl_alen = HIO_ETHAD_LEN;
		HIO_MEMCPY (sll->sdl_data, ethad, HIO_ETHAD_LEN);
	}
#else
#	error UNSUPPORTED DATALINK SOCKET ADDRESS
#endif
}

void hio_skad_init_for_qx (hio_skad_t* _skad)
{
	hio_skad_alt_t* skad = (hio_skad_alt_t*)_skad;
	HIO_MEMSET (skad, 0, HIO_SIZEOF(*_skad));
	skad->sa.sa_family = HIO_AF_QX;
}

void hio_clear_skad (hio_skad_t* _skad)
{
	hio_skad_alt_t* skad = (hio_skad_alt_t*)_skad;
	/*HIO_STATIC_ASSERT (HIO_SIZEOF(*_skad) >= HIO_SIZEOF(*skad));*/
	/* use HIO_SIZEOF(*_skad) instead of HIO_SIZEOF(*skad) in case they are different */
	HIO_MEMSET (skad, 0, HIO_SIZEOF(*_skad));
	skad->sa.sa_family = HIO_AF_UNSPEC;
}

int hio_equal_skads (const hio_skad_t* addr1, const hio_skad_t* addr2, int strict)
{
	int f1;

	if ((f1 = hio_skad_family(addr1)) != hio_skad_family(addr2) ||
	    hio_skad_size(addr1) != hio_skad_size(addr2)) return 0;

	switch (f1)
	{
	#if defined(AF_INET) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN > 0)
		case AF_INET:
			return ((struct sockaddr_in*)addr1)->sin_addr.s_addr == ((struct sockaddr_in*)addr2)->sin_addr.s_addr &&
			       ((struct sockaddr_in*)addr1)->sin_port == ((struct sockaddr_in*)addr2)->sin_port;
	#endif

	#if defined(AF_INET6) && (HIO_SIZEOF_STRUCT_SOCKADDR_IN6 > 0)
		case AF_INET6:
			
			if (strict)
			{
				/* don't care about scope id */
				return HIO_MEMCMP(&((struct sockaddr_in6*)addr1)->sin6_addr, &((struct sockaddr_in6*)addr2)->sin6_addr, HIO_SIZEOF(((struct sockaddr_in6*)addr2)->sin6_addr)) == 0 &&
				       ((struct sockaddr_in6*)addr1)->sin6_port == ((struct sockaddr_in6*)addr2)->sin6_port &&
				       ((struct sockaddr_in6*)addr1)->sin6_scope_id == ((struct sockaddr_in6*)addr2)->sin6_scope_id;
			}
			else
			{
				return HIO_MEMCMP(&((struct sockaddr_in6*)addr1)->sin6_addr, &((struct sockaddr_in6*)addr2)->sin6_addr, HIO_SIZEOF(((struct sockaddr_in6*)addr2)->sin6_addr)) == 0 &&
				       ((struct sockaddr_in6*)addr1)->sin6_port == ((struct sockaddr_in6*)addr2)->sin6_port;
			}
	#endif

	#if defined(AF_UNIX) && (HIO_SIZEOF_STRUCT_SOCKADDR_UN > 0)
		case AF_UNIX:
			return hio_comp_bcstr(((struct sockaddr_un*)addr1)->sun_path, ((struct sockaddr_un*)addr2)->sun_path, 0) == 0;
	#endif

		default:
			return HIO_MEMCMP(addr1, addr2, hio_skad_size(addr1)) == 0;
	}
}

hio_oow_t hio_ipad_bytes_to_ucstr (const hio_uint8_t* iptr, hio_oow_t ilen, hio_uch_t* buf, hio_oow_t blen)
{
	switch (ilen)
	{
		case HIO_IP4AD_LEN:
		{
			struct in_addr ip4ad;
			HIO_MEMCPY (&ip4ad.s_addr, iptr, ilen);
			return ip4ad_to_ucstr(&ip4ad, buf, blen);
		}

		case HIO_IP6AD_LEN:
		{
			struct in6_addr ip6ad;
			HIO_MEMCPY (&ip6ad.s6_addr, iptr, ilen);
			return ip6ad_to_ucstr(&ip6ad, buf, blen);
		}

		default:
			if (blen > 0) buf[blen] = '\0';
			return 0;
	}
}

hio_oow_t hio_ipad_bytes_to_bcstr (const hio_uint8_t* iptr, hio_oow_t ilen, hio_bch_t* buf, hio_oow_t blen)
{
	switch (ilen)
	{
		case HIO_IP4AD_LEN:
		{
			struct in_addr ip4ad;
			HIO_MEMCPY (&ip4ad.s_addr, iptr, ilen);
			return ip4ad_to_bcstr(&ip4ad, buf, blen);
		}

		case HIO_IP6AD_LEN:
		{
			struct in6_addr ip6ad;
			HIO_MEMCPY (&ip6ad.s6_addr, iptr, ilen);
			return ip6ad_to_bcstr(&ip6ad, buf, blen);
		}

		default:
			if (blen > 0) buf[blen] = '\0';
			return 0;
	}
}

int hio_uchars_to_ipad_bytes (const hio_uch_t* str, hio_oow_t slen, hio_uint8_t* buf, hio_oow_t blen)
{
	if (blen >= HIO_IP6AD_LEN)
	{
		struct in6_addr i6;
		if (uchars_to_ipv6(str, slen, &i6) <= -1) goto ipv4;
		HIO_MEMCPY (buf, i6.s6_addr, 16);
		return HIO_IP6AD_LEN;
	}
	else if (blen >= HIO_IP4AD_LEN)
	{
		struct in_addr i4;
	ipv4:
		if (uchars_to_ipv4(str, slen, &i4) <= -1) return -1;
		HIO_MEMCPY (buf, &i4.s_addr, 4);
		return HIO_IP4AD_LEN;
	}

	return -1;
}

int hio_bchars_to_ipad_bytes (const hio_bch_t* str, hio_oow_t slen, hio_uint8_t* buf, hio_oow_t blen)
{
	if (blen >= HIO_IP6AD_LEN)
	{
		struct in6_addr i6;
		if (bchars_to_ipv6(str, slen, &i6) <= -1) goto ipv4;
		HIO_MEMCPY (buf, i6.s6_addr, 16);
		return HIO_IP6AD_LEN;
	}
	else if (blen >= HIO_IP4AD_LEN)
	{
		struct in_addr i4;
	ipv4:
		if (bchars_to_ipv4(str, slen, &i4) <= -1) return -1;
		HIO_MEMCPY (buf, &i4.s_addr, 4);
		return HIO_IP4AD_LEN;
	}

	return -1;
}

int hio_ipad_bytes_is_v4_mapped (const hio_uint8_t* iptr, hio_oow_t ilen)
{
	if (ilen != HIO_IP6AD_LEN) return 0;

	return iptr[0] == 0x00 && iptr[1] == 0x00 &&
	       iptr[2] == 0x00 && iptr[3] == 0x00 &&
	       iptr[4] == 0x00 && iptr[5] == 0x00 &&
	       iptr[6] == 0x00 && iptr[7] == 0x00 &&
	       iptr[8] == 0x00 && iptr[9] == 0x00 &&
	       iptr[10] == 0xFF && iptr[11] == 0xFF;
}

int hio_ipad_bytes_is_loop_back (const hio_uint8_t* iptr, hio_oow_t ilen)
{
	switch (ilen)
	{
		case HIO_IP4AD_LEN:
		{
			// 127.0.0.0/8
			return iptr[0] == 0x7F;
		}

		case HIO_IP6AD_LEN:
		{
			hio_uint32_t* x = (hio_uint32_t*)iptr;
			return (x[0] == 0 && x[1] == 0 && x[2] == 0 && x[3] == HIO_CONST_HTON32(1)) || /* TODO: is this alignment safe?  */
			       (hio_ipad_bytes_is_v4_mapped(iptr, ilen) && (x[3] & HIO_CONST_HTON32(0xFF000000u)) == HIO_CONST_HTON32(0x7F000000u)); 
		}

		default:
			return 0;
	}
}

int hio_ipad_bytes_is_link_local (const hio_uint8_t* iptr, hio_oow_t ilen)
{
	switch (ilen)
	{
		case HIO_IP4AD_LEN:
		{
			// 169.254.0.0/16
			return iptr[0] == 0xA9 && iptr[1] == 0xFE;
		}

		case HIO_IP6AD_LEN:
		{
			/* FE80::/10 */
			return iptr[0] == 0xFE && (iptr[1] & 0xC0) == 0x80;
		}

		default:
			return 0;
	}
}
