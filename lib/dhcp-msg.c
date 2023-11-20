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
	HIO_MEMSET (pkt->hdr, 0, HIO_SIZEOF(*pkt->hdr));
	return 0;
}

int hio_dhcp4_add_option (hio_dhcp4_pktbuf_t* pkt, int code, void* optr, hio_uint8_t olen)
{
	hio_dhcp4_opt_hdr_t* opthdr;
	magic_cookie_t* cookie; 
	int optlen;

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
		if (olen > 0) HIO_MEMCPY (opthdr + 1, optr, olen);
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

	if ((ohdr >= pkt->hdr->file && ohdr < (ovend = (hio_uint8_t*)pkt->hdr->file + HIO_SIZEOF(pkt->hdr->file))) ||
	    (ohdr >= pkt->hdr->sname && ohdr < (ovend = (hio_uint8_t*)pkt->hdr->sname + HIO_SIZEOF(pkt->hdr->sname))))
	{
		/* the option resides in the overload area */
		HIO_MEMMOVE (ohdr, (hio_uint8_t*)ohdr + olen, ovend - ((hio_uint8_t*)ohdr + olen));
		HIO_MEMSET (ovend - olen, 0, olen);
		/* packet length remains unchanged */
	}
	else
	{
		HIO_MEMMOVE (ohdr, (hio_uint8_t*)ohdr + olen, ((hio_uint8_t*)pkt->hdr + pkt->len) - ((hio_uint8_t*)ohdr + olen));
		pkt->len -= olen;
	}
	return 0;
}

void hio_dhcp4_compact_options (hio_dhcp4_pktbuf_t* pkt)
{
	/* TODO: move some optiosn to sname or file fields if they are not in use. */
}

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
			/* option code */
			hio_dhcp4_opt_hdr_t* opthdr;

			if (opt + HIO_SIZEOF(*opthdr) >= end) return -1;
			opthdr = (hio_dhcp4_opt_hdr_t*)opt;
			opt += HIO_SIZEOF(*opthdr);

			/* no len field exists for PADDING and END */
			if (opthdr->code == HIO_DHCP4_OPT_PADDING) continue; 
			if (opthdr->code == HIO_DHCP4_OPT_END) break;

			if (opt + opthdr->len >= end) return -1; /* the length field is wrong */

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

hio_dhcp4_opt_hdr_t* hio_dhcp4_find_option (const hio_dhcp4_pktinf_t* pkt, int code)
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

			/* option length */

			if (opthdr->code == code)
			{
				if (opt + opthdr->len > end) break;
				return opthdr;
			}

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

hio_uint8_t* hio_dhcp4_get_relay_suboption (const hio_uint8_t* ptr, hio_uint8_t len, int code, hio_uint8_t* olen)
{
	const hio_uint8_t* end = ptr + len;

	while (ptr < end)
	{
		hio_uint8_t oc, ol;

		oc = *ptr++;

		if (ptr >= end) break;
		ol = *ptr++;

		if (oc == code)
		{
			*olen = ol;
			return (hio_uint8_t*)ptr;
		}

		ptr += ol;
	}

	return HIO_NULL;
}


/* -------------------------------------------------------------------------- */

hio_dhcp6_opt_hdr_t* hio_dhcp6_find_option (const hio_dhcp6_pktinf_t* pkt, int code)
{
	hio_dhcp6_opt_hdr_t* opt;
	hio_oow_t rem, opt_len;

	if (pkt->len < HIO_SIZEOF(hio_dhcp6_pkt_hdr_t)) return HIO_NULL;

	if (pkt->hdr->msgtype == HIO_DHCP6_MSG_RELAYFORW || pkt->hdr->msgtype == HIO_DHCP6_MSG_RELAYREPL)
	{
		if (pkt->len < HIO_SIZEOF(hio_dhcp6_relay_hdr_t)) return HIO_NULL;

		rem = pkt->len - HIO_SIZEOF(hio_dhcp6_relay_hdr_t);
		opt = (hio_dhcp6_opt_hdr_t*)(((hio_dhcp6_relay_hdr_t*)pkt->hdr) + 1);
	}
	else
	{
		rem = pkt->len - HIO_SIZEOF(hio_dhcp6_pkt_hdr_t);
		opt = (hio_dhcp6_opt_hdr_t*)(pkt->hdr + 1);
	}

	while (rem >= HIO_SIZEOF(hio_dhcp6_opt_hdr_t))
	{
		if (hio_ntoh16(opt->code) == code) 
		{
			if (rem - HIO_SIZEOF(hio_dhcp6_opt_hdr_t) < hio_ntoh16(opt->len)) return HIO_NULL; /* probably the packet is ill-formed */
			return opt;
		}

		opt_len = HIO_SIZEOF(hio_dhcp6_opt_hdr_t) + hio_ntoh16(opt->len);
		if (rem < opt_len) break;
		rem -= opt_len;
		opt = (hio_dhcp6_opt_hdr_t*)((hio_uint8_t*)(opt + 1) + hio_ntoh16(opt->len));
		
	}

	return HIO_NULL;
}
