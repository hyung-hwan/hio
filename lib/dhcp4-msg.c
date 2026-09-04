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

#include <hio-dhcp.h>
#include "hio-prv.h"

#include <hio-pac1.h>
struct magic_cookie_t
{
	hio_uint32_t value;
};
typedef struct magic_cookie_t magic_cookie_t;
#include <hio-upac.h>


int hio_dhcp4_init_pktbuf (hio_dhcp4_pktbuf_t* pkt, void* buf, hio_oow_t capa)
{
	if (capa < HIO_SIZEOF(*pkt->hdr)) return -1;
	pkt->hdr = (hio_dhcp4_pkt_hdr_t*)buf;
	pkt->len = HIO_SIZEOF(*pkt->hdr);
	pkt->capa = capa;
	HIO_MEMSET(pkt->hdr, 0, HIO_SIZEOF(*pkt->hdr));
	return 0;
}

int hio_dhcp4_add_option (hio_dhcp4_pktbuf_t* pkt, int code, void* optr, hio_uint8_t olen)
{
	hio_dhcp4_opt_hdr_t* opthdr;
	magic_cookie_t* cookie;
	int optlen;

	/* an option code is one octet on the wire, and 'opthdr->code = code' below
	 * truncates without complaint - so code 0x101 would silently write option
	 * 1, reporting success for an option the caller did not ask for. PADDING
	 * (0) and END (255) are both legitimate here, so the whole octet range is
	 * allowed. */
	if (code < 0 || code > 255) return -1;

/* TODO: support to override sname and file */
	if (pkt->len < HIO_SIZEOF(*pkt->hdr) || pkt->capa < pkt->len)
	{
		/* the pktbuf_t structure got messy */
		return -1;
	}

	if (pkt->len == HIO_SIZEOF(*pkt->hdr))
	{
		/* the first option is being added */
		if (pkt->capa - pkt->len < HIO_SIZEOF(*cookie)) return -1;
		cookie = (magic_cookie_t*)((hio_uint8_t*)pkt->hdr + pkt->len);
		cookie->value = HIO_CONST_HTON32(HIO_DHCP4_MAGIC_COOKIE);
		pkt->len += HIO_SIZEOF(*cookie);
	}
	else if (pkt->len < HIO_SIZEOF(*pkt->hdr) + HIO_SIZEOF(*cookie))
	{
		/* no space for cookie */
		return -1;
	}
	else
	{
		cookie = (magic_cookie_t*)(pkt->hdr + 1);
		if (cookie->value != HIO_CONST_HTON32(HIO_DHCP4_MAGIC_COOKIE)) return -1;
	}

/* do i need to disallow adding a new option if END is found? */

	if (code == HIO_DHCP4_OPT_PADDING || code == HIO_DHCP4_OPT_END)
	{
		optlen = 1; /* no length field in the header and no option palyload */
		if (pkt->capa - pkt->len < optlen) return -1;
		opthdr = (hio_dhcp4_opt_hdr_t*)((hio_uint8_t*)pkt->hdr + pkt->len);
	}
	else
	{
		optlen = HIO_SIZEOF(*opthdr) + olen;

		if (pkt->capa - pkt->len < optlen) return -1;
		opthdr = (hio_dhcp4_opt_hdr_t*)((hio_uint8_t*)pkt->hdr + pkt->len);

		opthdr->len = olen;
		if (olen > 0) HIO_MEMCPY(opthdr + 1, optr, olen);
	}

	opthdr->code = code;
	pkt->len += optlen;

	return 0;
}

int hio_dhcp4_delete_option (hio_dhcp4_pktbuf_t* pkt, int code)
{
	hio_dhcp4_opt_hdr_t* ohdr;
	hio_oow_t olen;
	hio_uint8_t* ovend;

	ohdr = hio_dhcp4_find_option((hio_dhcp4_pktinf_t*)pkt, code);
	if (!ohdr) return -1;

	olen = (code == HIO_DHCP4_OPT_PADDING || code == HIO_DHCP4_OPT_END)? 1: (ohdr->len) + HIO_SIZEOF(*ohdr);

	if (((hio_uint8_t*)ohdr >= (hio_uint8_t*)pkt->hdr->file &&
	     (hio_uint8_t*)ohdr < (ovend = (hio_uint8_t*)pkt->hdr->file + HIO_SIZEOF(pkt->hdr->file))) ||
	    ((hio_uint8_t*)ohdr >= (hio_uint8_t*)pkt->hdr->sname &&
	     (hio_uint8_t*)ohdr < (ovend = (hio_uint8_t*)pkt->hdr->sname + HIO_SIZEOF(pkt->hdr->sname))))
	{
		/* the option resides in the overload area */
		HIO_MEMMOVE(ohdr, (hio_uint8_t*)ohdr + olen, ovend - ((hio_uint8_t*)ohdr + olen));
		HIO_MEMSET(ovend - olen, 0, olen);
		/* packet length remains unchanged */
	}
	else
	{
		HIO_MEMMOVE(ohdr, (hio_uint8_t*)ohdr + olen, ((hio_uint8_t*)pkt->hdr + pkt->len) - ((hio_uint8_t*)ohdr + olen));
		pkt->len -= olen;
	}
	return 0;
}

void hio_dhcp4_compact_options (hio_dhcp4_pktbuf_t* pkt)
{
	/* TODO: move some optiosn to sname or file fields if they are not in use. */
}

/* ------------------------------------------------------------------------- */
/* typed option access                                                       */
/* ------------------------------------------------------------------------- */

int hio_dhcp4_add_option_uint8 (hio_dhcp4_pktbuf_t* pkt, int code, hio_uint8_t value)
{
	return hio_dhcp4_add_option(pkt, code, &value, 1);
}

int hio_dhcp4_add_option_uint16 (hio_dhcp4_pktbuf_t* pkt, int code, hio_uint16_t value)
{
	hio_uint16_t v = hio_hton16(value);
	return hio_dhcp4_add_option(pkt, code, &v, 2);
}

int hio_dhcp4_add_option_uint32 (hio_dhcp4_pktbuf_t* pkt, int code, hio_uint32_t value)
{
	hio_uint32_t v = hio_hton32(value);
	return hio_dhcp4_add_option(pkt, code, &v, 4);
}

int hio_dhcp4_get_option_data (const hio_dhcp4_pktinf_t* pkt, int code, const hio_uint8_t** ptr, hio_uint8_t* len)
{
	hio_dhcp4_opt_hdr_t* opt;

	opt = hio_dhcp4_find_option(pkt, code);
	if (!opt) return -1;

	if (ptr) *ptr = (const hio_uint8_t*)(opt + 1);
	if (len) *len = opt->len;
	return 0;
}

/* one reader for all three widths. the copy is not laziness: an option payload
 * begins wherever the options before it happened to end, so it carries no
 * alignment, and reading a u32 out of it through a cast is undefined - and a
 * fault outright on an architecture that cannot do unaligned loads. */
static int get_option_fixed (const hio_dhcp4_pktinf_t* pkt, int code, void* dst, hio_uint8_t width)
{
	hio_dhcp4_opt_hdr_t* opt;

	opt = hio_dhcp4_find_option(pkt, code);
	if (!opt) return -1;

	/* a length that disagrees with the option's definition is a malformed
	 * packet. there is no sensible value to recover from it. */
	if (opt->len != width) return -1;

	HIO_MEMCPY(dst, opt + 1, width);
	return 0;
}

int hio_dhcp4_get_option_uint8 (const hio_dhcp4_pktinf_t* pkt, int code, hio_uint8_t* value)
{
	return get_option_fixed(pkt, code, value, 1);
}

int hio_dhcp4_get_option_uint16 (const hio_dhcp4_pktinf_t* pkt, int code, hio_uint16_t* value)
{
	hio_uint16_t v;
	if (get_option_fixed(pkt, code, &v, 2) <= -1) return -1;
	*value = hio_ntoh16(v);
	return 0;
}

int hio_dhcp4_get_option_uint32 (const hio_dhcp4_pktinf_t* pkt, int code, hio_uint32_t* value)
{
	hio_uint32_t v;
	if (get_option_fixed(pkt, code, &v, 4) <= -1) return -1;
	*value = hio_ntoh32(v);
	return 0;
}

/* ------------------------------------------------------------------------- */
/* message-level helpers shared by both ends                                 */
/* ------------------------------------------------------------------------- */

int hio_dhcp4_get_msg_type (const hio_dhcp4_pktinf_t* pkt, hio_uint8_t* mtype)
{
	hio_uint8_t t;

	if (hio_dhcp4_get_option_uint8(pkt, HIO_DHCP4_OPT_MESSAGE_TYPE, &t) <= -1) return -1;

	/* 0 is not a message type, and the highest this implementation knows of is
	 * the bulk lease query family. anything outside that is not something to
	 * dispatch on. */
	if (t < HIO_DHCP4_MSG_DISCOVER || t > HIO_DHCP4_MSG_LEASE_QUERY_DONE) return -1;

	*mtype = t;
	return 0;
}

int hio_dhcp4_get_client_id (const hio_dhcp4_pktinf_t* pkt, const hio_uint8_t** ptr, hio_uint8_t* len)
{
	const hio_uint8_t* p;
	hio_uint8_t l;

	/* RFC 2131 section 9.14: if the client sent one, it is the identifier -
	 * and it is opaque, so it is not inspected here. */
	if (hio_dhcp4_get_option_data(pkt, HIO_DHCP4_OPT_CLIENT_ID, &p, &l) == 0 && l > 0)
	{
		*ptr = p;
		*len = l;
		return 0;
	}

	/* otherwise the hardware address. hlen is checked because it indexes a
	 * fixed 16-octet field and a packet may claim more than that. */
	if (pkt->hdr->hlen == 0 || pkt->hdr->hlen > HIO_SIZEOF(pkt->hdr->chaddr)) return -1;

	*ptr = pkt->hdr->chaddr;
	*len = pkt->hdr->hlen;
	return 0;
}

int hio_dhcp4_check_pkt (const hio_dhcp4_pktinf_t* pkt)
{
	const magic_cookie_t* cookie;

	if (!pkt->hdr || pkt->len < HIO_SIZEOF(*pkt->hdr)) return -1;

	/* the hardware address length indexes a fixed field, so a packet claiming
	 * more than fits is refused before anything reads through it */
	if (pkt->hdr->hlen > HIO_SIZEOF(pkt->hdr->chaddr)) return -1;

	/* without the cookie there are no options, which means no message type,
	 * which means this is bootp rather than dhcp */
	if (pkt->len < HIO_SIZEOF(*pkt->hdr) + HIO_SIZEOF(*cookie)) return -1;
	cookie = (const magic_cookie_t*)(pkt->hdr + 1);
	if (cookie->value != HIO_CONST_HTON32(HIO_DHCP4_MAGIC_COOKIE)) return -1;

	return 0;
}

int hio_dhcp4_init_reply_pktbuf (hio_dhcp4_pktbuf_t* pkt, void* buf, hio_oow_t capa, const hio_dhcp4_pktinf_t* req)
{
	if (hio_dhcp4_init_pktbuf(pkt, buf, capa) <= -1) return -1;

	pkt->hdr->op = HIO_DHCP4_OP_BOOTREPLY;

	/* the transaction id is what lets the client match this to what it asked;
	 * the hardware type and address are what let it recognise itself when the
	 * reply arrives by broadcast. */
	pkt->hdr->xid = req->hdr->xid;
	pkt->hdr->htype = req->hdr->htype;
	pkt->hdr->hlen = req->hdr->hlen;
	if (req->hdr->hlen > 0 && req->hdr->hlen <= HIO_SIZEOF(pkt->hdr->chaddr))
		HIO_MEMCPY(pkt->hdr->chaddr, req->hdr->chaddr, req->hdr->hlen);

	/* the broadcast flag is the client saying it cannot yet receive a unicast,
	 * so it has to survive into the reply or the reply may be undeliverable */
	pkt->hdr->flags = req->hdr->flags;

	/* and giaddr is how the reply finds its way back through the relay that
	 * forwarded the request */
	pkt->hdr->giaddr = req->hdr->giaddr;

	return 0;
}

/* ------------------------------------------------------------------------- */

static hio_uint8_t* get_option_start (const hio_dhcp4_pkt_hdr_t* pkt, hio_oow_t len, hio_oow_t* olen)
{
	magic_cookie_t* cookie;
	hio_oow_t optlen;

	/* check if a packet is large enough to hold the known header */
	if (len < HIO_SIZEOF(hio_dhcp4_pkt_hdr_t)) return HIO_NULL;

	/* get the length of option fields */
	optlen = len - HIO_SIZEOF(hio_dhcp4_pkt_hdr_t);

	/* check if a packet is large enough to have a magic cookie */
	if (optlen < HIO_SIZEOF(*cookie)) return HIO_NULL;

	/* get the pointer to the beginning of options */
	cookie = (magic_cookie_t*)(pkt + 1);

	/* check if the packet contains the right magic cookie */
	if (cookie->value != HIO_CONST_HTON32(HIO_DHCP4_MAGIC_COOKIE)) return HIO_NULL;

	*olen = optlen - HIO_SIZEOF(*cookie);
	return (hio_uint8_t*)(cookie + 1);
}

int hio_dhcp4_walk_options (const hio_dhcp4_pktinf_t* pkt, hio_dhcp4_opt_walker_t walker)
{
	const hio_uint8_t* optptr[3];
	hio_oow_t optlen[3];
	int i;

	optptr[0] = get_option_start(pkt->hdr, pkt->len, &optlen[0]);
	if (optptr[0] == HIO_NULL) return -1;

	optptr[1] = (const hio_uint8_t*)pkt->hdr->file;
	optptr[2] = (const hio_uint8_t*)pkt->hdr->sname;
	optlen[1] = 0;
	optlen[2] = 0;

	for (i = 0; i < 3; i++)
	{
		const hio_uint8_t* opt = optptr[i];
		const hio_uint8_t* end = opt + optlen[i];

		while (opt < end)
		{
			hio_dhcp4_opt_hdr_t* opthdr;

			/* PADDING and END are one octet each and carry no length field,
			 * so the code is examined having required only the single octet
			 * the loop condition guarantees. demanding a two-octet header
			 * first would reject a packet whose last octet is a lone END -
			 * which is how every conforming packet ends, and what this
			 * library's own reply builder emits.
			 *
			 * hio_dhcp4_find_option() below walks the same structure the same
			 * way; the two are meant to agree. */
			if (*opt == HIO_DHCP4_OPT_PADDING)
			{
				/* one octet, not the size of a header it does not have -
				 * advancing by two would step over the octet after it and
				 * leave the cursor inside the following option, where
				 * everything read afterwards is misaligned */
				opt++;
				continue;
			}
			if (*opt == HIO_DHCP4_OPT_END) break;

			/* every other option has a length octet after the code */
			if (opt + HIO_SIZEOF(*opthdr) > end) return -1;
			opthdr = (hio_dhcp4_opt_hdr_t*)opt;
			opt += HIO_SIZEOF(*opthdr);

			/*
			  5    6    7    8    9    10   11
			+----+----+----+----+----+----+----+
			| 01 | 04 | ff | ff | ff | 00 |    |
			+----+----+----+----+----+----+----+
			  ^    ^    ^                   ^
			code  len   |                 end here
                       (opt here)
              */

			/* opt now at the value part past the header.
			 * more intuitively, wrong if opt + opthdr->len - 1 >= end */
			if (opt + opthdr->len > end) return -1; /* the length field is wrong */

			if (opthdr->code == HIO_DHCP4_OPT_OVERLOAD)
			{
				if (opthdr->len != 1) return -1;
				if (*opt & HIO_DHCP4_OPT_OVERLOAD_FILE) optlen[1] = HIO_SIZEOF(pkt->hdr->file);
				if (*opt & HIO_DHCP4_OPT_OVERLOAD_SNAME) optlen[2] = HIO_SIZEOF(pkt->hdr->sname);
			}
			else
			{
				int n;
				if ((n = walker(opthdr)) <= -1) return -1;
				if (n == 0) break; /* stop */
			}

			opt += opthdr->len;
		}
	}

	return 0;
}

static hio_dhcp4_opt_hdr_t* dhcp4_find_option (const hio_dhcp4_pktinf_t* pkt, int code, int relay_subcode, hio_uint8_t** suboptptr, hio_uint8_t* suboptlen)
{
	const hio_uint8_t* optptr[3];
	hio_oow_t optlen[3];
	int i;

	optptr[0] = get_option_start(pkt->hdr, pkt->len, &optlen[0]);
	if (!optptr[0]) return HIO_NULL;

	optptr[1] = (const hio_uint8_t*)pkt->hdr->file;
	optptr[2] = (const hio_uint8_t*)pkt->hdr->sname;
	optlen[1] = 0;
	optlen[2] = 0;

	for (i = 0; i < 3; i++)
	{
		const hio_uint8_t* opt = optptr[i];
		const hio_uint8_t* end = opt + optlen[i];

		while (opt < end)
		{
			/* option code */
			hio_dhcp4_opt_hdr_t* opthdr;

			/* at least 1 byte is available. the check is because of PADDING or END */
			if (*opt == HIO_DHCP4_OPT_PADDING)
			{
				opt++;
				continue;
			}
			if (*opt == HIO_DHCP4_OPT_END)
			{
				if (code == HIO_DHCP4_OPT_END)
				{
					/* the caller must handle END specially becuase it is only 1 byte long
				 	 * for no length part in the header */
					return (hio_dhcp4_opt_hdr_t*)opt;
				}
				break;
			}

			if (opt + HIO_SIZEOF(*opthdr) > end) break;

			opthdr = (hio_dhcp4_opt_hdr_t*)opt;
			opt += HIO_SIZEOF(*opthdr);

			/* opt now at the value part past the header.
			 * more intuitively, wrong if opt + opthdr->len - 1 >= end */
			if (opt + opthdr->len > end) break; /* the length field is wrong */

			if (opthdr->code == code)
			{
				if (code == HIO_DHCP4_OPT_RELAY && relay_subcode > 0)
				{
					/* the caller wants a suboption inside this option, so a
					 * relay option that does not carry it is not the one
					 * being looked for - there may be another further on. */
					hio_uint8_t* optr;
					hio_uint8_t olen;

					optr = hio_dhcp4_get_relay_suboption_value((hio_uint8_t*)(opthdr + 1), opthdr->len, relay_subcode, &olen);
					if (!optr) goto try_next;

					if (suboptptr) *suboptptr = optr; /* the value, not the suboption header */
					if (suboptlen) *suboptlen = olen;
				}

				return opthdr;
			}

		try_next:

			/*
			 * If option overload is used, the SName and/or File fields are read and
			 * interpreted in the same way as the Options field, after all options in
			 * the Option field are parsed. If the message actually does need to carry
			 * a server name or boot file, these are included as separate options
			 * (number 66 and number 67, respectively), which are variable-length and
			 * can therefore be made exactly the length needed.
			 */
			if (opthdr->code == HIO_DHCP4_OPT_OVERLOAD)
			{
				if (opthdr->len != 1) break;
				if (*opt & HIO_DHCP4_OPT_OVERLOAD_FILE) optlen[1] = HIO_SIZEOF(pkt->hdr->file);
				if (*opt & HIO_DHCP4_OPT_OVERLOAD_SNAME) optlen[2] = HIO_SIZEOF(pkt->hdr->sname);
			}

			opt += opthdr->len;
		}
	}

	return HIO_NULL;
}

hio_dhcp4_opt_hdr_t* hio_dhcp4_find_option (const hio_dhcp4_pktinf_t* pkt, int code)
{
	/* the header of the option, not its value */
	return dhcp4_find_option(pkt, code, 0, HIO_NULL, HIO_NULL);
}

hio_uint8_t* hio_dhcp4_get_option_value (const hio_dhcp4_pktinf_t* pkt, int code, hio_uint8_t* olen)
{
	hio_dhcp4_opt_hdr_t* ohdr;

	ohdr = dhcp4_find_option(pkt, code, 0, HIO_NULL, HIO_NULL);
	if (!ohdr) return HIO_NULL;
	if (olen) *olen = ohdr->len;
	return (hio_uint8_t*)(ohdr + 1);
}

hio_uint8_t* hio_dhcp4_find_relay_suboption_value (const hio_dhcp4_pktinf_t* pkt, int relay_subcode, hio_uint8_t* olen)
{
	hio_dhcp4_opt_hdr_t* ohdr;
	hio_uint8_t* optr;

	/* dhcp4_find_option() uses relay_subcode > 0 to mean actual relay subcode requested.
	 * the subcode is stored in an octet and it can't hold the code greater than 255  */
	if (relay_subcode < 1 || relay_subcode > 255) return HIO_NULL;

	ohdr = dhcp4_find_option(pkt, HIO_DHCP4_OPT_RELAY, relay_subcode, &optr, olen);
	return ohdr? optr: HIO_NULL;
}

/* is this option sitting in the overloaded sname or file field rather than in
 * the options area? those are fixed-size, so an option in one of them can be
 * shrunk but never grown, and the packet length does not change either way. */
static int option_in_overload_area (hio_dhcp4_pktbuf_t* pkt, const hio_dhcp4_opt_hdr_t* ohdr, hio_uint8_t** fend)
{
	hio_uint8_t* o = (hio_uint8_t*)ohdr;
	hio_uint8_t* fb = (hio_uint8_t*)pkt->hdr->file;
	hio_uint8_t* sb = (hio_uint8_t*)pkt->hdr->sname;

	if (o >= fb && o < fb + HIO_SIZEOF(pkt->hdr->file)) { *fend = fb + HIO_SIZEOF(pkt->hdr->file); return 1; }
	if (o >= sb && o < sb + HIO_SIZEOF(pkt->hdr->sname)) { *fend = sb + HIO_SIZEOF(pkt->hdr->sname); return 1; }
	return 0;
}

int hio_dhcp4_delete_relay_suboption (hio_dhcp4_pktbuf_t* pkt, int relay_subcode)
{
	hio_dhcp4_opt_hdr_t* ohdr;
	hio_uint8_t* optr;
	hio_uint8_t olen;
	hio_uint8_t* fend = HIO_NULL;
	hio_uint8_t newolen;

	/* dhcp4_find_option() uses relay_subcode > 0 to mean actual relay subcode requested.
	 * the subcode is stored in an octet and it can't hold the code greater than 255  */
	if (relay_subcode < 1 || relay_subcode > 255) return -1;

	ohdr = dhcp4_find_option((hio_dhcp4_pktinf_t*)pkt, HIO_DHCP4_OPT_RELAY, relay_subcode, &optr, &olen);
	if (!ohdr || ohdr->len <= 0) return -1; /* not there */

	/* olen + 2 <= ohdr->len holds by construction, so this cannot go negative:
	 * dhcp4_find_option() only reports a suboption whose value fits inside
	 * ohdr->len, and that value starts past its own two-octet header. */
	newolen = ohdr->len - (olen + 2);

	if (option_in_overload_area(pkt, ohdr, &fend))
	{
		if (newolen == 0) /* on an unsigned type this is what '<= 0' means */
		{
			/* that was its only suboption, so the option goes too */
			hio_uint8_t ovlen = ohdr->len + HIO_SIZEOF(*ohdr);
			HIO_MEMMOVE(ohdr, (hio_uint8_t*)ohdr + ovlen, fend - ((hio_uint8_t*)ohdr + ovlen));
			HIO_MEMSET(fend - ovlen, 0, ovlen);
		}
		else
		{
			HIO_MEMMOVE(optr - 2, optr + olen, fend - (optr + olen));
			HIO_MEMSET(fend - (olen + 2), 0, olen + 2);
			ohdr->len = newolen;
		}
		/* the field is fixed-size, so the packet length is unchanged */
	}
	else
	{
		hio_uint8_t* pend = (hio_uint8_t*)pkt->hdr + pkt->len;

		if (newolen == 0)
		{
			hio_uint8_t ovlen = ohdr->len + HIO_SIZEOF(*ohdr);
			HIO_MEMMOVE(ohdr, (hio_uint8_t*)ohdr + ovlen, pend - ((hio_uint8_t*)ohdr + ovlen));
			pkt->len -= ovlen;
		}
		else
		{
			HIO_MEMMOVE(optr - 2, optr + olen, pend - (optr + olen));
			pkt->len -= olen + 2;
			ohdr->len = newolen;
		}
	}

	return 0;
}

int hio_dhcp4_add_relay_suboption (hio_dhcp4_pktbuf_t* pkt, int relay_subcode, const hio_uint8_t* dptr, hio_uint8_t dlen)
{
	hio_dhcp4_opt_hdr_t* ohdr;

	/* a suboption costs its two header octets on top of its value, and an
	 * option length field holds 255. without this check the addition below
	 * wraps in the hio_uint8_t it is passed as, and a large suboption becomes
	 * a zero-length option instead of an error. */
	if (dlen > 253) return -1;

	/* dhcp4_find_option() uses relay_subcode > 0 to mean actual relay subcode requested.
	 * the subcode is stored in an octet and it can't hold the code greater than 255  */
	if (relay_subcode < 1 || relay_subcode > 255) return -1;

	ohdr = hio_dhcp4_find_option((hio_dhcp4_pktinf_t*)pkt, HIO_DHCP4_OPT_RELAY);
	if (ohdr)
	{
		hio_uint8_t* fend = HIO_NULL;
		hio_uint8_t* pend, * oend;

		/* the overloaded fields are fixed-size and already full of whatever
		 * else is in them, so there is nowhere to grow into */
		if (option_in_overload_area(pkt, ohdr, &fend)) return -1;

		/* and the option's own length field must be able to express the
		 * result - this too would wrap silently */
		if ((int)ohdr->len + (int)dlen + 2 > 255) return -1;

		if (pkt->capa - pkt->len < (hio_oow_t)dlen + 2) return -1;

		pend = (hio_uint8_t*)pkt->hdr + pkt->len;
		oend = (hio_uint8_t*)ohdr + ohdr->len + HIO_SIZEOF(*ohdr);

		/* open a gap at the end of the option and write the suboption in */
		HIO_MEMMOVE(oend + dlen + 2, oend, pend - oend);
		oend[0] = (hio_uint8_t)relay_subcode;
		oend[1] = dlen;
		if (dlen > 0) HIO_MEMCPY(&oend[2], dptr, dlen);
		ohdr->len += dlen + 2;
		pkt->len += dlen + 2;
		return 0;
	}
	else
	{
		/* no relay option yet, so the suboption becomes the whole of a new one */
		hio_uint8_t buf[255];

		buf[0] = (hio_uint8_t)relay_subcode;
		buf[1] = dlen;
		if (dlen > 0) HIO_MEMCPY(&buf[2], dptr, dlen);
		return hio_dhcp4_add_option(pkt, HIO_DHCP4_OPT_RELAY, buf, (hio_uint8_t)(dlen + 2));
	}
}

hio_uint8_t* hio_dhcp4_get_relay_suboption_value (const hio_uint8_t* ptr, hio_uint8_t len, int relay_subcode, hio_uint8_t* olen)
{
	/* each suboption is a one-octet code, a one-octet length, then the value:
	 *
	 *     | code | len | value... | code | len | value... |
	 */
	const hio_uint8_t* end = ptr + len;

	while (ptr < end)
	{
		hio_uint8_t oc, ol;

		oc = *ptr++;

		if (ptr >= end) break; /* a code with no length octet after it */
		ol = *ptr++;

		if (oc == relay_subcode)
		{
			/* the value has to lie inside the enclosing option before its
			 * address is handed out: the caller reads 'ol' octets through the
			 * returned pointer, and a suboption length running past 'end'
			 * would take that read past the packet itself when the relay
			 * option is the last one. */
			if (ptr + ol > end) break;
			if (olen) *olen = ol;
			return (hio_uint8_t*)ptr;
		}

		ptr += ol;
	}

	return HIO_NULL;
}
