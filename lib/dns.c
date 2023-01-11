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

#include <hio-dns.h>
#include "hio-prv.h"

/* ----------------------------------------------------------------------- */

#define DN_AT_END(ptr) (ptr[0] == '\0' || (ptr[0] == '.' && ptr[1] == '\0'))

static hio_oow_t to_dn (const hio_bch_t* str, hio_uint8_t* buf)
{
	hio_uint8_t* bp = buf;
	/*HIO_ASSERT (HIO_SIZEOF(hio_uint8_t) == HIO_SIZEOF(hio_bch_t));*/

	if (str && !DN_AT_END(str))
	{
		hio_uint8_t* lp;
		hio_oow_t len;
		const hio_bch_t* seg;
		const hio_bch_t* cur = str - 1;

		do
		{
			lp = bp++;

			seg = ++cur;
			while (*cur != '\0' && *cur != '.')
			{
				*bp++ = *cur;
				cur++;
			}
			len = cur - seg;
			if (len <= 0 || len > 63) return 0;

			*lp = (hio_uint8_t)len;
		}
		while (!DN_AT_END(cur));
	}

	*bp++ = 0;

	/* the length includes the terminating zero. */
	return bp - buf;
}

static hio_oow_t to_dn_capa (const hio_bch_t* str)
{
	hio_oow_t capa = 0;

	/*HIO_ASSERT (HIO_SIZEOF(hio_uint8_t) == HIO_SIZEOF(hio_bch_t));*/

	if (str && !DN_AT_END(str))
	{
		hio_oow_t len;
		const hio_bch_t* seg;
		const hio_bch_t* cur = str - 1;

		do
		{
			capa++;

			seg = ++cur;
			while (*cur != '\0' && *cur != '.')
			{
				capa++;
				cur++;
			}
			len = cur - seg;
			if (len <= 0 || len > 63) return 0;
		}
		while (!DN_AT_END(cur));
	}

	capa++;

	/* the length includes the terminating zero. */
	return capa;
}

static hio_oow_t dn_length (hio_uint8_t* ptr, hio_oow_t len)
{
	hio_uint8_t* curptr;
	hio_oow_t curlen, seglen;

	curptr = ptr;
	curlen = len;

	do
	{
		if (curlen <= 0) return 0;

		seglen = *curptr++;
		curlen = curlen - 1;
		if (seglen == 0) break;
		else if (seglen > curlen || seglen > 63) return 0;

		curptr += seglen;
		curlen -= seglen;
	}
	while (1);

	return curptr - ptr;
}

/* ----------------------------------------------------------------------- */

static int parse_domain_name (hio_t* hio, hio_dns_pkt_info_t* pi)
{
	hio_oow_t seglen;
	hio_uint8_t* xptr;

	if (HIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
	xptr = HIO_NULL;

	if ((seglen = *pi->_ptr++) == 0)
	{
		if (pi->_rrdptr) pi->_rrdptr[0] = '\0';
		pi->_rrdlen++; /* for a terminating null */
		return 0;
	}

	do
	{
		if (HIO_LIKELY(seglen < 64))
		{
			/* normal. 00XXXXXXXX */
		normal:
			if (pi->_rrdptr)
			{
				HIO_MEMCPY (pi->_rrdptr, pi->_ptr, seglen);
				pi->_rrdptr += seglen + 1; /* +1 for '.' */
				pi->_rrdptr[-1] = '.';
			}

			pi->_rrdlen += seglen + 1; /* +1 for '.' */
			pi->_ptr += seglen;
			if (HIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
		}
		else if (seglen >= 192)
		{
			/* compressed. 11XXXXXXXX XXXXXXXX */
			hio_oow_t offset;

			if (HIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
			offset = ((seglen & 0x3F) << 8) | *pi->_ptr++;

			/*if (HIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops; <- this condition can be true if the function is called for the domain name at the back of the last RR */

			seglen = pi->_start[offset];
			if (seglen >= 64) goto oops; /* the pointed position must not contain another pointer */

			if (!xptr) xptr = pi->_ptr; /* some later parts can also be a pointer again. so xptr, once set, must not be set again */
			pi->_ptr = &pi->_start[offset + 1];
			if (HIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;

			goto normal;
		}
		else if (seglen >= 128)
		{
			/* 128 to 191. 10XXXXXXXX */
			goto oops;
		}
		else
		{
			/* 64 to 127. 01XXXXXXXX */
			goto oops;
		}
	}
	while ((seglen = *pi->_ptr++) > 0);

	if (pi->_rrdptr) pi->_rrdptr[-1] = '\0';

	if (xptr) pi->_ptr = xptr;
	return 0;

oops:
	hio_seterrnum (hio, HIO_EINVAL);
	return -1;
}

static int parse_question_rr (hio_t* hio, hio_oow_t pos, hio_dns_pkt_info_t* pi)
{
	hio_dns_qrtr_t* qrtr;
	hio_uint8_t* xrrdptr;

	xrrdptr = pi->_rrdptr;
	if (parse_domain_name(hio, pi) <= -1) return -1;

	qrtr = (hio_dns_qrtr_t*)pi->_ptr;
	if (HIO_UNLIKELY(pi->_ptr > pi->_end || pi->_end - pi->_ptr < HIO_SIZEOF(*qrtr))) goto oops;

	pi->_ptr += HIO_SIZEOF(*qrtr);
	/*pi->_rrdlen += HIO_SIZEOF(*qrtr);*/

	if (pi->_rrdptr)
	{
		hio_dns_bqr_t* bqr;
		bqr = pi->rr.qd;
		bqr[pos].qname = (hio_bch_t*)xrrdptr;
		bqr[pos].qtype = hio_ntoh16(qrtr->qtype);
		bqr[pos].qclass = hio_ntoh16(qrtr->qclass);
	}

	return 0;

oops:
	hio_seterrnum (hio, HIO_EINVAL);
	return -1;
}

static int parse_answer_rr (hio_t* hio, hio_dns_rr_part_t rr_part, hio_oow_t pos, hio_dns_pkt_info_t* pi)
{
	hio_dns_rrtr_t* rrtr;
	hio_uint16_t qtype, dlen;
	hio_uint8_t* xrrdptr, *xrrdptr2;

	xrrdptr = pi->_rrdptr;
	if (parse_domain_name(hio, pi) <= -1) return -1;

	rrtr = (hio_dns_rrtr_t*)pi->_ptr;
	if (HIO_UNLIKELY(pi->_ptr > pi->_end ||  pi->_end - pi->_ptr < HIO_SIZEOF(*rrtr))) goto oops;

	pi->_ptr += HIO_SIZEOF(*rrtr);
	dlen = hio_ntoh16(rrtr->dlen);
	if (HIO_UNLIKELY(pi->_ptr > pi->_end ||  pi->_end - pi->_ptr < dlen)) goto oops;

	qtype = hio_ntoh16(rrtr->rrtype);

	xrrdptr2 = pi->_rrdptr;

	switch (qtype)
	{
		case HIO_DNS_RRT_OPT:
		{
			hio_uint16_t eopt_tot_len, eopt_len;
			hio_dns_eopt_t* eopt;

			/* RFC 6891
			The extended RCODE and flags, which OPT stores in the RR Time to Live
			(TTL) field, are structured as follows:

			   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
			0: |         EXTENDED-RCODE        |            VERSION            |
			   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
			2: | DO|                           Z                               |
			   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

			EXTENDED-RCODE
			 Forms the upper 8 bits of extended 12-bit RCODE (together with the
			 4 bits defined in [RFC1035].  Note that EXTENDED-RCODE value 0
			 indicates that an unextended RCODE is in use (values 0 through
			 15).
			*/

			/* TODO: do i need to check if rrname is <ROOT>? */
			/* TODO: do i need to check if rr_part  is HIO_DNS_RR_PART_ADDITIONAL? the OPT pseudo-RR may exist in the ADDITIONAL section only */
			/* TODO: do i need to check if there is more than 1 OPT RRs */
			pi->edns.exist++; /* you may treat this as the number of OPT RRs */
			pi->edns.uplen = hio_ntoh16(rrtr->rrclass);
			pi->hdr.rcode |= (rrtr->ttl >> 24);
			pi->edns.version = (rrtr->ttl >> 16) & 0xFF;
			pi->edns.dnssecok = ((rrtr->ttl & 0x8000) >> 15);
			/*if ((rrtr->ttl & 0x7FFF) != 0) goto oops;*/ /* Z not 0 - ignore this for now */

			eopt = (hio_dns_eopt_t*)(rrtr + 1);
			eopt_tot_len = dlen;
			while (eopt_tot_len > 0)
			{
				if (eopt_tot_len < HIO_SIZEOF(hio_dns_eopt_t)) goto oops;

				eopt_len = hio_ntoh16(eopt->dlen);
				if (eopt_tot_len - HIO_SIZEOF(hio_dns_eopt_t) < eopt_len) goto oops; /* wrong eopt length */

				if (eopt->code == HIO_CONST_HTON16(HIO_DNS_EOPT_COOKIE))
				{
					if (eopt_len == HIO_DNS_COOKIE_CLIENT_LEN)
					{
						/* client cookie only */
						HIO_MEMCPY (pi->edns.cookie.data.client, eopt + 1, eopt_len);
						pi->edns.cookie.client_len = eopt_len;
						pi->edns.cookie.server_len = 0;
					}
					else if (eopt_len >= (HIO_DNS_COOKIE_CLIENT_LEN + HIO_DNS_COOKIE_SERVER_MIN_LEN) &&
					         eopt_len <= (HIO_DNS_COOKIE_CLIENT_LEN + HIO_DNS_COOKIE_SERVER_MAX_LEN))
					{
						/* both client and server cookie */
						HIO_MEMCPY (&pi->edns.cookie.data, eopt + 1, eopt_len);
						pi->edns.cookie.client_len = HIO_DNS_COOKIE_CLIENT_LEN;
						pi->edns.cookie.server_len = eopt_len - HIO_DNS_COOKIE_CLIENT_LEN;
					}
					else
					{
						/* wrong cookie length */
						goto oops;
					}
				}

				eopt_tot_len -= HIO_SIZEOF(hio_dns_eopt_t) + eopt_len;
				eopt = (hio_dns_eopt_t*)((hio_uint8_t*)eopt + HIO_SIZEOF(hio_dns_eopt_t) + eopt_len);
			}

			goto verbatim; /* keep the entire option data including cookies */
		}

		case HIO_DNS_RRT_A:
			if (HIO_UNLIKELY(dlen != HIO_SIZEOF(hio_ip4ad_t))) goto oops;
			goto verbatim;

		case HIO_DNS_RRT_AAAA:
			if (HIO_UNLIKELY(dlen != HIO_SIZEOF(hio_ip6ad_t))) goto oops;
			goto verbatim;

		/*case HIO_DNS_RRT_MB:
		case HIO_DNS_RRT_MD:
		case HIO_DNS_RRT_MF:
		case HIO_DNS_RRT_MG:
		case HIO_DNS_RRT_MR:*/
		case HIO_DNS_RRT_CNAME:
		case HIO_DNS_RRT_NS:
		case HIO_DNS_RRT_PTR:
		{
		#if !defined(HIO_BUILD_RELEASE)
			hio_uint8_t* xptr = pi->_ptr;
		#endif
			if (parse_domain_name(hio, pi) <= -1) goto oops;
			HIO_ASSERT (hio, pi->_ptr == xptr + dlen);
			break;
		}

		case HIO_DNS_RRT_MX:
		{
		#if !defined(HIO_BUILD_RELEASE)
			hio_uint8_t* xptr = pi->_ptr;
		#endif
			hio_dns_brrd_mx_t* mx;

			pi->_rrdlen += HIO_SIZEOF(*mx);
			if (HIO_UNLIKELY(pi->_end - pi->_ptr < 2)) goto oops;

			if (pi->_rrdptr)
			{
				mx = (hio_dns_brrd_mx_t*)pi->_rrdptr;
				pi->_rrdptr += HIO_SIZEOF(*mx);

				HIO_MEMCPY (&mx->preference, pi->_ptr, 2); pi->_ptr += 2;

				mx->preference = hio_ntoh16(mx->preference);
				mx->exchange = (hio_bch_t*)pi->_rrdptr;
				if (parse_domain_name(hio, pi) <= -1) goto oops;
			}
			else
			{
				pi->_ptr += 2;
				if (parse_domain_name(hio, pi) <= -1) goto oops;
			}

			HIO_ASSERT (hio, pi->_ptr == xptr + dlen);
			break;
		}

		case HIO_DNS_RRT_SOA:
		{
		#if !defined(HIO_BUILD_RELEASE)
			hio_uint8_t* xptr = pi->_ptr;
		#endif
			hio_dns_brrd_soa_t* soa;

			pi->_rrdlen += HIO_SIZEOF(*soa);
			if (pi->_rrdptr)
			{
				soa = (hio_dns_brrd_soa_t*)pi->_rrdptr;
				pi->_rrdptr += HIO_SIZEOF(*soa);

				soa->mname = (hio_bch_t*)pi->_rrdptr;
				if (parse_domain_name(hio, pi) <= -1) goto oops;

				soa->rname = (hio_bch_t*)pi->_rrdptr;
				if (parse_domain_name(hio, pi) <= -1) goto oops;

				if (HIO_UNLIKELY(pi->_end - pi->_ptr < 20)) goto oops;
				HIO_MEMCPY (&soa->serial, pi->_ptr, 20);
				soa->serial = hio_ntoh32(soa->serial);
				soa->refresh = hio_ntoh32(soa->refresh);
				soa->retry = hio_ntoh32(soa->retry);
				soa->expire = hio_ntoh32(soa->expire);
				soa->minimum = hio_ntoh32(soa->minimum);
			}
			else
			{
				if (parse_domain_name(hio, pi) <= -1) goto oops;
				if (parse_domain_name(hio, pi) <= -1) goto oops;
				if (HIO_UNLIKELY(pi->_end - pi->_ptr < 20)) goto oops;
			}
			pi->_ptr += 20;

			HIO_ASSERT (hio, pi->_ptr == xptr + dlen);
			break;
		}

		default:
		verbatim:
			pi->_ptr += dlen;
			pi->_rrdlen += dlen;
			if (pi->_rrdptr)
			{
				HIO_MEMCPY (pi->_rrdptr, rrtr + 1, dlen); /* copy actual data */
				pi->_rrdptr += dlen;
			}
	}

	if (pi->_rrdptr)
	{
		/* store information about the actual record */
		hio_dns_brr_t* brr;

		switch (rr_part)
		{
			case HIO_DNS_RR_PART_ANSWER: brr = pi->rr.an; break;
			case HIO_DNS_RR_PART_AUTHORITY: brr = pi->rr.ns; break;
			case HIO_DNS_RR_PART_ADDITIONAL: brr = pi->rr.ar; break;
			default: goto oops;
		}

		brr[pos].part = rr_part;
		brr[pos].rrname = (hio_bch_t*)xrrdptr;
		brr[pos].rrtype = hio_ntoh16(rrtr->rrtype);
		brr[pos].rrclass = hio_ntoh16(rrtr->rrclass);
		brr[pos].ttl = hio_ntoh32(rrtr->ttl);
		brr[pos].dptr = xrrdptr2;
		/* this length may be different from the length in the header as transformation is performed on some RR data.
		 * for a domain name, it's inclusive of the termining null. */
		brr[pos].dlen = pi->_rrdptr - xrrdptr2;
	}

	return 0;

oops:
	hio_seterrnum (hio, HIO_EINVAL);
	return -1;
}

hio_dns_pkt_info_t* hio_dns_make_pkt_info (hio_t* hio, const hio_dns_pkt_t* pkt, hio_oow_t len)
{
	hio_uint16_t i;
	hio_dns_pkt_info_t pib, * pii;

	HIO_ASSERT (hio, len >= HIO_SIZEOF(*pkt));

	HIO_MEMSET (&pib, 0, HIO_SIZEOF(pib));

	/* pib is used as the initial workspace and also indicates that it's the first run.
	 * at the second run, pii is set to a dynamically allocated memory block large enough
	 * to hold actual data.  */
	pii = &pib;

redo:
	pii->_start = (hio_uint8_t*)pkt;
	pii->_end = (hio_uint8_t*)pkt + len;
	pii->_ptr = (hio_uint8_t*)(pkt + 1);

	pii->hdr.id = hio_ntoh16(pkt->id);
	pii->hdr.qr = pkt->qr & 0x01;
	pii->hdr.opcode = pkt->opcode & 0x0F;
	pii->hdr.aa = pkt->aa & 0x01;
	pii->hdr.tc = pkt->tc & 0x01;
	pii->hdr.rd = pkt->rd & 0x01;
	pii->hdr.ra = pkt->ra & 0x01;
	pii->hdr.ad = pkt->ad & 0x01;
	pii->hdr.cd = pkt->cd & 0x01;
	pii->hdr.rcode = pkt->rcode & 0x0F;
	pii->qdcount = hio_ntoh16(pkt->qdcount);
	pii->ancount = hio_ntoh16(pkt->ancount);
	pii->nscount = hio_ntoh16(pkt->nscount);
	pii->arcount = hio_ntoh16(pkt->arcount);

	for (i = 0; i < pii->qdcount; i++)
	{
		if (parse_question_rr(hio, i, pii) <= -1) goto oops;
	}

	for (i = 0; i < pii->ancount; i++)
	{
		if (parse_answer_rr(hio, HIO_DNS_RR_PART_ANSWER, i, pii) <= -1) goto oops;
	}

	for (i = 0; i < pii->nscount; i++)
	{
		if (parse_answer_rr(hio, HIO_DNS_RR_PART_AUTHORITY, i, pii) <= -1) goto oops;
	}

	for (i = 0; i < pii->arcount; i++)
	{
		if (parse_answer_rr(hio, HIO_DNS_RR_PART_ADDITIONAL, i, pii) <= -1) goto oops;
	}

	if (pii == &pib)
	{
	/* TODO: better buffer management... */
		pii = (hio_dns_pkt_info_t*)hio_callocmem(hio, HIO_SIZEOF(*pii) + (HIO_SIZEOF(hio_dns_bqr_t) * pib.qdcount) + (HIO_SIZEOF(hio_dns_brr_t) * (pib.ancount + pib.nscount + pib.arcount)) + pib._rrdlen);
		if (!pii) goto oops;

		pii->rr.qd = (hio_dns_bqr_t*)(&pii[1]);
		pii->rr.an = (hio_dns_brr_t*)&pii->rr.qd[pib.qdcount];
		pii->rr.ns = (hio_dns_brr_t*)&pii->rr.an[pib.ancount];
		pii->rr.ar = (hio_dns_brr_t*)&pii->rr.ns[pib.nscount];
		pii->_rrdptr = (hio_uint8_t*)&pii->rr.ar[pib.arcount];

		/* _rrdptr points to the beginning of memory where additional data will
		 * be held for some RRs. _rrdlen is the length of total additional data.
		 * the additional data refers to the data that is pointed to by the
		 * breakdown RRs(hio_dns_bqr_t/hio_dns_brr_t) but is not stored in them. */

		goto redo;
	}

	return pii;

oops:
	if (pii && pii != &pib) hio_freemem (hio, pii);
	return HIO_NULL;
}

void hio_dns_free_pkt_info (hio_t* hio, hio_dns_pkt_info_t* pi)
{
/* TODO: better management */
	hio_freemem (hio, pi);
}


/* ----------------------------------------------------------------------- */

static int encode_rrdata_in_dns_msg (hio_t* hio, const hio_dns_brr_t* rr, hio_uint16_t* dxlen, void* dptr)
{
	hio_oow_t xlen; /* actual data length after encoding */

	switch (rr->rrtype)
	{
		case HIO_DNS_RRT_A:
			if (HIO_UNLIKELY(rr->dlen != HIO_SIZEOF(hio_ip4ad_t))) goto inval;
			goto verbatim;

		case HIO_DNS_RRT_AAAA:
			if (HIO_UNLIKELY(rr->dlen != HIO_SIZEOF(hio_ip6ad_t))) goto inval;
			goto verbatim;

		/*case HIO_DNS_RRT_MB:
		case HIO_DNS_RRT_MD:
		case HIO_DNS_RRT_MF:
		case HIO_DNS_RRT_MG:
		case HIO_DNS_RRT_MR:*/
		case HIO_DNS_RRT_CNAME:
		case HIO_DNS_RRT_NS:
		case HIO_DNS_RRT_PTR:
			/* just a normal domain name */
			if (dptr)
				xlen = to_dn(rr->dptr, dptr);
			else
				xlen = to_dn_capa(rr->dptr);
			if (HIO_UNLIKELY(xlen <= 0)) goto inval;
			break;

	#if 0
		case HIO_DNS_RRT_HINFO:
			/* cpu, os */
			break;
	#endif

	#if 0
		case HIO_DNS_RRT_MINFO:
			/* rmailbx, emailbx */
	#endif
			xlen = rr->dlen;
			break;

		case HIO_DNS_RRT_MX:
		{
			hio_dns_brrd_mx_t* mx;
			hio_oow_t tmp;

			if (HIO_UNLIKELY(rr->dlen != HIO_SIZEOF(hio_dns_brrd_mx_t))) goto inval;
			mx = (hio_dns_brrd_mx_t*)rr->dptr;
			xlen = 0;
			if (dptr)
			{
				hio_uint16_t ti;

				ti = hio_hton16(mx->preference);
				HIO_MEMCPY((hio_uint8_t*)dptr + xlen, &ti, HIO_SIZEOF(ti)); xlen += HIO_SIZEOF(ti);

				tmp = to_dn(mx->exchange, (hio_uint8_t*)dptr + xlen);
				if (HIO_UNLIKELY(tmp <= 0)) goto inval;
				xlen += tmp;
			}
			else
			{
				xlen += 2;

				tmp = to_dn_capa(mx->exchange);
				if (HIO_UNLIKELY(tmp <= 0)) goto inval;
				xlen += tmp;
			}
			break;
		}

		case HIO_DNS_RRT_SOA:
		{
			/* soa */
			hio_dns_brrd_soa_t* soa;
			hio_oow_t tmp;

			if (HIO_UNLIKELY(rr->dlen != HIO_SIZEOF(hio_dns_brrd_soa_t))) goto inval;

			soa = (hio_dns_brrd_soa_t*)rr->dptr;
			xlen = 0;
			if (dptr)
			{
				hio_uint32_t ti;

				tmp = to_dn(soa->mname, (hio_uint8_t*)dptr + xlen);
				if (HIO_UNLIKELY(tmp <= 0)) goto inval;
				xlen += tmp;

				tmp = to_dn(soa->rname, (hio_uint8_t*)dptr + xlen);
				if (HIO_UNLIKELY(tmp <= 0)) goto inval;
				xlen += tmp;

				ti = hio_hton32(soa->serial);
				HIO_MEMCPY((hio_uint8_t*)dptr + xlen, &ti, HIO_SIZEOF(ti)); xlen += HIO_SIZEOF(ti);
				ti = hio_hton32(soa->refresh);
				HIO_MEMCPY((hio_uint8_t*)dptr + xlen, &ti, HIO_SIZEOF(ti)); xlen += HIO_SIZEOF(ti);
				ti = hio_hton32(soa->retry);
				HIO_MEMCPY((hio_uint8_t*)dptr + xlen, &ti, HIO_SIZEOF(ti)); xlen += HIO_SIZEOF(ti);
				ti = hio_hton32(soa->expire);
				HIO_MEMCPY((hio_uint8_t*)dptr + xlen, &ti, HIO_SIZEOF(ti)); xlen += HIO_SIZEOF(ti);
				ti = hio_hton32(soa->minimum);
				HIO_MEMCPY((hio_uint8_t*)dptr + xlen, &ti, HIO_SIZEOF(ti)); xlen += HIO_SIZEOF(ti);
			}
			else
			{
				tmp = to_dn_capa(soa->mname);
				if (HIO_UNLIKELY(tmp <= 0)) goto inval;
				xlen += tmp;

				tmp = to_dn_capa(soa->rname);
				if (HIO_UNLIKELY(tmp <= 0)) goto inval;
				xlen += tmp;

				xlen += 20;
			}
			break;
		}

		case HIO_DNS_RRT_TXT:
		case HIO_DNS_RRT_NULL:
		default:
		verbatim:
			/* TODO: custom transformator? */
			if (dptr) HIO_MEMCPY (dptr, rr->dptr, rr->dlen);
			xlen = rr->dlen;
			break;
	}

	if (HIO_UNLIKELY(xlen > HIO_TYPE_MAX(hio_uint16_t))) goto inval;
	*dxlen = (hio_uint16_t)xlen;
	return 0;


inval:
	hio_seterrnum (hio, HIO_EINVAL);
	return -1;
}

hio_dns_msg_t* hio_dns_make_msg (hio_t* hio, hio_dns_bhdr_t* bhdr, hio_dns_bqr_t* qr, hio_oow_t qr_count, hio_dns_brr_t* rr, hio_oow_t rr_count, hio_dns_bedns_t* edns, hio_oow_t xtnsize)
{
	hio_oow_t dnlen, msgbufsz, pktlen, i;
	hio_dns_msg_t* msg;
	hio_dns_pkt_t* pkt;
	hio_uint8_t* dn;
	hio_dns_qrtr_t* qrtr;
	hio_dns_rrtr_t* rrtr;
	int rr_sect;
	hio_uint32_t edns_dlen;

	pktlen = HIO_SIZEOF(*pkt);

	for (i = 0; i < qr_count; i++)
	{
		dnlen = to_dn_capa(qr[i].qname);
		if (HIO_UNLIKELY(dnlen <= 0))
		{
			hio_seterrnum (hio, HIO_EINVAL);
			return HIO_NULL;
		}
		pktlen += dnlen + HIO_SIZEOF(*qrtr);
	}

	for (i = 0; i < rr_count; i++)
	{
		hio_uint16_t rrdata_len;
		dnlen = to_dn_capa(rr[i].rrname);
		if (HIO_UNLIKELY(dnlen <= 0))
		{
			hio_seterrnum (hio, HIO_EINVAL);
			return HIO_NULL;
		}
		if (HIO_UNLIKELY(encode_rrdata_in_dns_msg(hio, &rr[i], &rrdata_len, HIO_NULL) <= -1)) return HIO_NULL;
		pktlen += dnlen + HIO_SIZEOF(*rrtr) + rrdata_len;
	}

	edns_dlen = 0;
	if (edns)
	{
		hio_dns_beopt_t* beopt;

		pktlen += 1 + HIO_SIZEOF(*rrtr); /* edns0 OPT RR - 1 for the root name  */

		beopt = edns->beoptr;
		for (i = 0; i < edns->beonum; i++)
		{
			edns_dlen += HIO_SIZEOF(hio_dns_eopt_t) + beopt->dlen;
			if (HIO_UNLIKELY(edns_dlen > HIO_TYPE_MAX(hio_uint16_t)))
			{
				hio_seterrbfmt (hio, HIO_EINVAL, "edns options too large");
				return HIO_NULL;
			}
			beopt++;
		}

		pktlen += edns_dlen;
	}
	else
	{
		if (HIO_UNLIKELY(bhdr->rcode > 0x0F))
		{
			/* rcode is larger than 4 bits. but edns info is not provided */
			hio_seterrbfmt (hio, HIO_EINVAL, "rcode too large without edns - %d", bhdr->rcode);
			return HIO_NULL;
		}
	}

	msgbufsz = HIO_SIZEOF(*msg) + HIO_ALIGN_POW2(pktlen, HIO_SIZEOF_VOID_P) + xtnsize;

/* TODO: msg buffer reuse */
	msg = (hio_dns_msg_t*)hio_callocmem(hio, msgbufsz);
	if (HIO_UNLIKELY(!msg)) return HIO_NULL;

	msg->msglen = msgbufsz; /* record the instance size */
	msg->pktalilen = HIO_ALIGN_POW2(pktlen, HIO_SIZEOF_VOID_P);

	pkt = hio_dns_msg_to_pkt(msg); /* actual packet data begins after the message structure */

	dn = (hio_uint8_t*)(pkt + 1); /* skip the dns packet header */
	for (i = 0; i < qr_count; i++)
	{
		/* dnlen includes the ending <zero> */
		dnlen = to_dn(qr[i].qname, dn);
		HIO_ASSERT (hio, dnlen > 0);

		qrtr = (hio_dns_qrtr_t*)(dn + dnlen);
		qrtr->qtype = hio_hton16(qr[i].qtype);
		qrtr->qclass = hio_hton16(qr[i].qclass);

		dn = (hio_uint8_t*)(qrtr + 1);
	}

	for (rr_sect = HIO_DNS_RR_PART_ANSWER; rr_sect <= HIO_DNS_RR_PART_ADDITIONAL;)
	{
		hio_oow_t match_count = 0;
		for (i = 0; i < rr_count; i++)
		{
			if (rr[i].part == rr_sect)
			{
				hio_uint16_t rrdata_len;

				dnlen = to_dn(rr[i].rrname, dn);
				HIO_ASSERT (hio, dnlen > 0);

				rrtr = (hio_dns_rrtr_t*)(dn + dnlen);
				rrtr->rrtype = hio_hton16(rr[i].rrtype);
				rrtr->rrclass = hio_hton16(rr[i].rrclass);
				rrtr->ttl = hio_hton32(rr[i].ttl);

				encode_rrdata_in_dns_msg(hio, &rr[i], &rrdata_len, rrtr + 1); /* this must succeed */
				rrtr->dlen = hio_hton16(rrdata_len);
				dn = (hio_uint8_t*)(rrtr + 1) + rrdata_len;

				match_count++;
			}
		}

		rr_sect = rr_sect + 1;
		((hio_dns_pkt_alt_t*)pkt)->rrcount[rr_sect] = hio_hton16(match_count);
	}

	if (edns)
	{
		hio_dns_eopt_t* eopt;
		hio_dns_beopt_t* beopt;

		/* add EDNS0 OPT RR */
		*dn = 0; /* root domain. as if to_dn("") is called */
		rrtr = (hio_dns_rrtr_t*)(dn + 1);
		rrtr->rrtype = HIO_CONST_HTON16(HIO_DNS_RRT_OPT);
		rrtr->rrclass = hio_hton16(edns->uplen);
		rrtr->ttl = hio_hton32(HIO_DNS_EDNS_MAKE_TTL(bhdr->rcode, edns->version, edns->dnssecok));
		rrtr->dlen = hio_hton16((hio_uint16_t)edns_dlen);
		dn = (hio_uint8_t*)(rrtr + 1);

		beopt = edns->beoptr;
		eopt = (hio_dns_eopt_t*)dn;
		msg->ednsrrtroff = (hio_uint8_t*)rrtr - (hio_uint8_t*)pkt;

		for (i = 0; i < edns->beonum; i++)
		{
			eopt->code = hio_hton16(beopt->code);
			eopt->dlen = hio_hton16(beopt->dlen);
			HIO_MEMCPY (++eopt, beopt->dptr, beopt->dlen);
			eopt = (hio_dns_eopt_t*)((hio_uint8_t*)eopt + beopt->dlen);
			beopt++;
		}

		pkt->arcount = hio_hton16((hio_ntoh16(pkt->arcount) + 1));
		dn += edns_dlen;
	}

	pkt->qdcount = hio_hton16(qr_count);
	pkt->id = hio_hton16((hio_uint16_t)bhdr->id);

	/*pkt->qr = (rr_count > 0);
	pkt->opcode = HIO_DNS_OPCODE_QUERY;*/
	pkt->qr = bhdr->qr & 0x01;
	pkt->opcode = bhdr->opcode & 0x0F;
	pkt->aa = bhdr->aa & 0x01;
	pkt->tc = bhdr->tc & 0x01;
	pkt->rd = bhdr->rd & 0x01;
	pkt->ra = bhdr->ra & 0x01;
	pkt->ad = bhdr->ad & 0x01;
	pkt->cd = bhdr->cd & 0x01;
	pkt->rcode = bhdr->rcode & 0x0F;

	msg->pktlen = dn - (hio_uint8_t*)pkt;
	HIO_ASSERT (hio, msg->pktlen == pktlen);
	HIO_ASSERT (hio, msg->pktalilen == HIO_ALIGN_POW2(pktlen, HIO_SIZEOF_VOID_P));

	return msg;
}

void hio_dns_free_msg (hio_t* hio, hio_dns_msg_t* msg)
{
/* TODO: better management */
	hio_freemem (hio, msg);
}

hio_uint8_t* hio_dns_find_client_cookie_in_msg (hio_dns_msg_t* reqmsg, hio_uint8_t (*cookie)[HIO_DNS_COOKIE_CLIENT_LEN])
{
	hio_dns_rrtr_t* edns_rrtr;
	hio_dns_eopt_t* eopt;
	hio_uint16_t rem, dlen;

	/* this function doesn't check malformed packet assuming
	 * reqmsg points to the packet message created with hio_dns_make_msg().
	 * such a packet message must be well-formed */
	if (reqmsg->ednsrrtroff <= 0) return HIO_NULL; /* doesn't exist */

	edns_rrtr = (hio_dns_rrtr_t*)((hio_uint8_t*)hio_dns_msg_to_pkt(reqmsg) + reqmsg->ednsrrtroff);
	rem = hio_ntoh16(edns_rrtr->dlen);

	eopt = (hio_dns_eopt_t*)(edns_rrtr + 1);
	while (rem >= HIO_SIZEOF(hio_dns_eopt_t))
	{
		dlen = hio_ntoh16(eopt->dlen);
		if (eopt->code == HIO_CONST_HTON16(HIO_DNS_EOPT_COOKIE))
		{
			if (cookie) HIO_MEMCPY (cookie, eopt + 1, HIO_DNS_COOKIE_CLIENT_LEN);
			return (hio_uint8_t*)(eopt + 1);
		}

		rem -= dlen;
		eopt = (hio_dns_eopt_t*)((hio_uint8_t*)(eopt + 1) + dlen);
	}

	return HIO_NULL;
}


hio_bch_t* hio_dns_rcode_to_bcstr (hio_dns_rcode_t rcode)
{
	hio_bch_t* _errmsg[] =
	{
		"NOERR",
		"FORMERR",
		"SERVFAIL",
		"NXDOMAIN",
		"NOTIMPL",
		"REFUSED",
		"YXDOMAIN",
		"YXRRSET",
		"NXRRSET",
		"NOAUTH",
		"NOTZONE", /* 10 */

		"UNKNOWNERR",
		"UNKNOWNERR",
		"UNKNOWNERR",
		"UNKNOWNERR",
		"UNKNOWNERR",
		"UNKNOWNERR",

		"BADVERS", /* 16 */
		"BADSIG",
		"BADTIME",
		"BADMODE",
		"BADNAME",
		"BADALG",
		"BADTRUNC",
		"BADCOOKIE"
	};

	return rcode < HIO_COUNTOF(_errmsg)? _errmsg[rcode]: "UNKNOWNERR";
}
