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

#include "mio-dns.h"
#include "mio-prv.h"

/* ----------------------------------------------------------------------- */

#define DN_AT_END(ptr) (ptr[0] == '\0' || (ptr[0] == '.' && ptr[1] == '\0'))

static mio_oow_t to_dn (const mio_bch_t* str, mio_uint8_t* buf, mio_oow_t bufsz)
{
	mio_uint8_t* bp = buf, * be = buf + bufsz;

	/*MIO_ASSERT (MIO_SIZEOF(mio_uint8_t) == MIO_SIZEOF(mio_bch_t));*/

	if (str && !DN_AT_END(str))
	{
		mio_uint8_t* lp;
		mio_oow_t len;
		const mio_bch_t* seg;
		const mio_bch_t* cur = str - 1;

		do
		{
			if (bp < be) lp = bp++;
			else lp = MIO_NULL;

			seg = ++cur;
			while (*cur != '\0' && *cur != '.')
			{
				if (bp < be) *bp++ = *cur;
				cur++;
			}
			len = cur - seg;
			if (len <= 0 || len > 63) return 0;

			if (lp) *lp = (mio_uint8_t)len;
		}
		while (!DN_AT_END(cur));
	}

	if (bp < be) *bp++ = 0;

	/* the length includes the terminating zero. */
	return bp - buf;
}

static mio_oow_t dn_length (mio_uint8_t* ptr, mio_oow_t len)
{
	mio_uint8_t* curptr;
	mio_oow_t curlen, seglen;

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

static int parse_domain_name (mio_t* mio, mio_dns_pkt_info_t* pi)
{
	mio_oow_t seglen;
	mio_uint8_t* xptr;

	if (MIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
	xptr = MIO_NULL;

	if ((seglen = *pi->_ptr++) == 0)
	{
		if (pi->_rrdptr) pi->_rrdptr[0] = '\0';
		pi->_rrdlen++; /* for a terminating null */
		return 0;
	}

	do
	{
		if (MIO_LIKELY(seglen < 64))
		{
			/* normal. 00XXXXXXXX */
		normal:
			if (pi->_rrdptr)
			{
				MIO_MEMCPY (pi->_rrdptr, pi->_ptr, seglen);
				pi->_rrdptr += seglen + 1; /* +1 for '.' */
				pi->_rrdptr[-1] = '.';
			}

			pi->_rrdlen += seglen + 1; /* +1 for '.' */
			pi->_ptr += seglen;
			if (MIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
		}
		else if (seglen >= 192)
		{
			/* compressed. 11XXXXXXXX XXXXXXXX */
			mio_oow_t offset;

			if (MIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
			offset = ((seglen & 0x3F) << 8) | *pi->_ptr++;

			if (MIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;
			seglen = pi->_start[offset];
			if (seglen >= 64) goto oops; /* the pointed position must not contain another pointer */

			if (!xptr) xptr = pi->_ptr; /* some later parts can also be a poitner again. so xptr, once set, must not be set again */
			pi->_ptr = &pi->_start[offset + 1];
			if (MIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;

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
	mio_seterrnum (mio, MIO_EINVAL);
	return -1;
}

static int parse_question_rr (mio_t* mio, mio_oow_t pos, mio_dns_pkt_info_t* pi)
{
	mio_dns_qrtr_t* qrtr;
	mio_uint8_t* xrrdptr;

	xrrdptr = pi->_rrdptr;
	if (parse_domain_name(mio, pi) <= -1) return -1;

	qrtr = (mio_dns_qrtr_t*)pi->_ptr;
	pi->_ptr += MIO_SIZEOF(*qrtr);
	pi->_rrdlen += MIO_SIZEOF(*qrtr);
	if (MIO_UNLIKELY(pi->_ptr >= pi->_end)) goto oops;

	if (pi->_rrdptr)
	{
		mio_dns_bqr_t* bqr;
		bqr = pi->rr.qd;
		bqr[pos].qname = (mio_bch_t*)xrrdptr;
		bqr[pos].qtype = mio_ntoh16(qrtr->qtype);
		bqr[pos].qclass = mio_ntoh16(qrtr->qclass);
	}

	return 0;

oops:
	mio_seterrnum (mio, MIO_EINVAL);
	return -1;
}

static int parse_answer_rr (mio_t* mio, mio_dns_rr_part_t rr_part, mio_oow_t pos, mio_dns_pkt_info_t* pi)
{
	mio_dns_rrtr_t* rrtr;
	mio_uint16_t qtype, dlen;
	mio_oow_t remsize;
	mio_uint8_t* xrrdptr, *xrrdptr2;

	xrrdptr = pi->_rrdptr;
	if (parse_domain_name(mio, pi) <= -1) return -1;

	rrtr = (mio_dns_rrtr_t*)pi->_ptr;
	if (MIO_UNLIKELY(pi->_end - pi->_ptr < MIO_SIZEOF(*rrtr))) goto oops;
	pi->_ptr += MIO_SIZEOF(*rrtr);
	dlen = mio_ntoh16(rrtr->dlen);

	if (MIO_UNLIKELY(pi->_end - pi->_ptr < dlen)) goto oops;

	qtype = mio_ntoh16(rrtr->rrtype);
	remsize = pi->_end - pi->_ptr;
	if (MIO_UNLIKELY(remsize < dlen)) goto oops;

	xrrdptr2 = pi->_rrdptr;

	switch (qtype)
	{
		case MIO_DNS_RRT_OPT:
		{
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
			pi->edns.exist = 1;
			pi->edns.uplen = mio_ntoh16(rrtr->rrclass);
			pi->hdr.rcode |= (rrtr->ttl >> 24);
			pi->edns.version = (rrtr->ttl >> 16) & 0xFF;
			pi->edns.dnssecok = ((rrtr->ttl & 0x8000) >> 15);
			/*if ((rrtr->ttl & 0x7FFF) != 0) goto oops;*/ /* Z not 0 - ignore this for now */
			goto verbatim;
		}

		case MIO_DNS_RRT_A:
			if (MIO_UNLIKELY(dlen != 4)) goto oops;
			goto verbatim;

		case MIO_DNS_RRT_AAAA:
			if (MIO_UNLIKELY(dlen != 16)) goto oops;
			goto verbatim;

		case MIO_DNS_RRT_CNAME:
		case MIO_DNS_RRT_NS:
		case MIO_DNS_RRT_PTR:
		{
		#if !defined(MIO_BUILD_RELEASE)
			mio_uint8_t* xptr = pi->_ptr;
		#endif
			if (parse_domain_name(mio, pi) <= -1) goto oops;
			MIO_ASSERT (mio, pi->_ptr == xptr + dlen);
			break;
		}

		case MIO_DNS_RRT_SOA:
		{
		#if !defined(MIO_BUILD_RELEASE)
			mio_uint8_t* xptr = pi->_ptr;
		#endif
			mio_dns_brrd_soa_t* soa;

			pi->_rrdlen += MIO_SIZEOF(*soa);
			if (pi->_rrdptr)
			{
				soa = (mio_dns_brrd_soa_t*)pi->_rrdptr;
				pi->_rrdptr += MIO_SIZEOF(*soa);

				soa->mname = (mio_bch_t*)pi->_rrdptr;
				if (parse_domain_name(mio, pi) <= -1) goto oops;

				soa->rname = (mio_bch_t*)pi->_rrdptr;
				if (parse_domain_name(mio, pi) <= -1) goto oops;

				if (MIO_UNLIKELY(pi->_end - pi->_ptr) < 20) goto oops;
				MIO_MEMCPY (&soa->serial, pi->_ptr, 20);
				soa->serial = mio_ntoh32(soa->serial);
				soa->refresh = mio_ntoh32(soa->refresh);
				soa->retry = mio_ntoh32(soa->retry);
				soa->expire = mio_ntoh32(soa->expire);
				soa->minimum = mio_ntoh32(soa->minimum);
			}
			else
			{
				if (parse_domain_name(mio, pi) <= -1) goto oops;
				if (parse_domain_name(mio, pi) <= -1) goto oops;
				if (MIO_UNLIKELY(pi->_end - pi->_ptr) < 20) goto oops;
			}
			pi->_ptr += 20;

			MIO_ASSERT (mio, pi->_ptr == xptr + dlen);
			break;
		}

		default:
		verbatim:
			pi->_ptr += dlen;
			pi->_rrdlen += dlen;
			if (pi->_rrdptr) 
			{
				MIO_MEMCPY (pi->_rrdptr, rrtr + 1, dlen); /* copy actual data */
				pi->_rrdptr += dlen;
			}
	}

	if (pi->_rrdptr)
	{
		/* store information about the actual record */
		mio_dns_brr_t* brr;

		switch (rr_part)
		{
			case MIO_DNS_RR_PART_ANSWER: brr = pi->rr.an; break;
			case MIO_DNS_RR_PART_AUTHORITY: brr = pi->rr.ns; break;
			case MIO_DNS_RR_PART_ADDITIONAL: brr = pi->rr.ar; break;
		}

		brr[pos].part = rr_part;
		brr[pos].rrname = (mio_bch_t*)xrrdptr;
		brr[pos].rrtype = mio_ntoh16(rrtr->rrtype);
		brr[pos].rrclass = mio_ntoh16(rrtr->rrclass);
		brr[pos].ttl = mio_ntoh32(rrtr->ttl);
		brr[pos].dptr = xrrdptr2;
		/* this length may be different from the length in the header as transformation is performed on some RR data.
		 * for a domain name, it's inclusive of the termining null. */
		brr[pos].dlen = pi->_rrdptr - xrrdptr2; 
	}

	return 0;

oops:
	mio_seterrnum (mio, MIO_EINVAL);
	return -1;
}

mio_dns_pkt_info_t* mio_dns_make_packet_info (mio_t* mio, const mio_dns_pkt_t* pkt, mio_oow_t len)
{
	mio_uint16_t i;
	mio_dns_pkt_info_t pib, * pii;

	MIO_ASSERT (mio, len >= MIO_SIZEOF(*pkt));

	MIO_MEMSET (&pib, 0, MIO_SIZEOF(pib));
	pii = &pib;

redo:
	pii->_start = (mio_uint8_t*)pkt;
	pii->_end = (mio_uint8_t*)pkt + len;
	pii->_ptr = (mio_uint8_t*)(pkt + 1);

	pii->hdr.id = mio_ntoh16(pkt->id);
	pii->hdr.qr = pkt->qr & 0x01;
	pii->hdr.opcode = pkt->opcode & 0x0F;
	pii->hdr.aa = pkt->aa & 0x01;
	pii->hdr.tc = pkt->tc & 0x01; 
	pii->hdr.rd = pkt->rd & 0x01; 
	pii->hdr.ra = pkt->ra & 0x01;
	pii->hdr.ad = pkt->ad & 0x01;
	pii->hdr.cd = pkt->cd & 0x01;
	pii->hdr.rcode = pkt->rcode & 0x0F;
	pii->qdcount = mio_ntoh16(pkt->qdcount);
	pii->ancount = mio_ntoh16(pkt->ancount);
	pii->nscount = mio_ntoh16(pkt->nscount);
	pii->arcount = mio_ntoh16(pkt->arcount);

	for (i = 0; i < pii->qdcount; i++)
	{
		if (parse_question_rr(mio, i, pii) <= -1) goto oops;
	}

	for (i = 0; i < pii->ancount; i++)
	{
		if (parse_answer_rr(mio, MIO_DNS_RR_PART_ANSWER, i, pii) <= -1) goto oops;
	}

	for (i = 0; i < pii->nscount; i++)
	{
		if (parse_answer_rr(mio, MIO_DNS_RR_PART_AUTHORITY, i, pii) <= -1) goto oops;
	}

	for (i = 0; i < pii->arcount; i++)
	{
		if (parse_answer_rr(mio, MIO_DNS_RR_PART_ADDITIONAL, i, pii) <= -1) goto oops;
	}

	if (pii == &pib)
	{
	/* TODO: buffer management... */
		pii = (mio_dns_pkt_info_t*)mio_callocmem(mio, MIO_SIZEOF(*pii) + (MIO_SIZEOF(mio_dns_bqr_t) * pib.qdcount) + (MIO_SIZEOF(mio_dns_brr_t) * (pib.ancount + pib.nscount + pib.arcount)) + pib._rrdlen);
		if (!pii) goto oops;

		pii->rr.qd = (mio_dns_bqr_t*)(&pii[1]);
		pii->rr.an = (mio_dns_brr_t*)&pii->rr.qd[pib.qdcount];
		pii->rr.ns = (mio_dns_brr_t*)&pii->rr.an[pib.ancount];
		pii->rr.ar = (mio_dns_brr_t*)&pii->rr.ns[pib.nscount];
		pii->_rrdptr = (mio_uint8_t*)&pii->rr.ar[pib.arcount];
		goto redo;
	}

	return pii;

oops:
	if (pii && pii != &pib) mio_freemem (mio, pii);
	return MIO_NULL;
}

void mio_dns_free_packet_info (mio_t* mio, mio_dns_pkt_info_t* pi)
{
/* TODO: better management */
	mio_freemem (mio, pi);
}


/* ----------------------------------------------------------------------- */

static mio_oow_t encode_rrdata_in_dns_msg (mio_t* mio, const mio_dns_brr_t* rr, mio_dns_rrtr_t* rrtr)
{
	switch (rr->rrtype)
	{
		case MIO_DNS_RRT_A:
			break;
		case MIO_DNS_RRT_AAAA:
			break;

		/*
		case MIO_DNS_RRT_WKS:
			break; */

		case MIO_DNS_RRT_MX:
			/* preference, exchange */
			break;

		case MIO_DNS_RRT_CNAME: 
		/*case MIO_DNS_RRT_MB:
		case MIO_DNS_RRT_MD:
		case MIO_DNS_RRT_MF:
		case MIO_DNS_RRT_MG:
		case MIO_DNS_RRT_MR:*/
		case MIO_DNS_RRT_NS:
		case MIO_DNS_RRT_PTR:
			/* just a normal domain name */
			/* TODO: take a null-terminated string and encode it using to_dn() */
			break;

	#if 0
		case MIO_DNS_RRT_HINFO:
			/* cpu, os */
			break;
	#endif

	#if 0
		case MIO_DNS_RRT_MINFO:
			/* rmailbx, emailbx */
	#endif
			break;
		
		case MIO_DNS_RRT_SOA:
			/* soa */
			break;

		case MIO_DNS_RRT_TXT:
		case MIO_DNS_RRT_NULL:
		default:
			/* TODO: custom transformator? */
			rrtr->dlen = mio_hton16(rr->dlen);
			if (rr->dlen > 0) MIO_MEMCPY (rrtr + 1, rr->dptr, rr->dlen);
	}

	return rr->dlen;
}

mio_dns_msg_t* mio_dns_make_msg (mio_t* mio, mio_dns_bhdr_t* bhdr, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, mio_oow_t xtnsize)
{
	mio_oow_t dnlen, msgbufsz, pktlen, i;
	mio_dns_msg_t* msg;
	mio_dns_pkt_t* pkt;
	mio_uint8_t* dn;
	mio_dns_qrtr_t* qrtr;
	mio_dns_rrtr_t* rrtr;
	int rr_sect;
	mio_uint32_t edns_dlen;

	pktlen = MIO_SIZEOF(*pkt);

	for (i = 0; i < qr_count; i++)
	{
		/* <length>segmnet<length>segment<zero>.
		 * if the input has the ending period(e.g. mio.lib.), the dn length is namelen + 1. 
		 * if the input doesn't have the ending period(e.g. mio.lib) . the dn length is namelen + 2. */
		pktlen += mio_count_bcstr(qr[i].qname) + 2 + MIO_SIZEOF(*qrtr);
	}

	for (i = 0; i < rr_count; i++)
	{
		pktlen += mio_count_bcstr(rr[i].rrname) + 2 + MIO_SIZEOF(*rrtr) + rr[i].dlen;
	}

	edns_dlen = 0;
	if (edns)
	{
		mio_dns_beopt_t* beopt;

		pktlen += 1 + MIO_SIZEOF(*rrtr); /* edns0 OPT RR - 1 for the root name  */

		beopt = edns->beoptr;
		for (i = 0; i < edns->beonum; i++)
		{
			edns_dlen += MIO_SIZEOF(mio_dns_eopt_t) + beopt->dlen;
			if (edns_dlen > MIO_TYPE_MAX(mio_uint16_t))
			{
				mio_seterrbfmt (mio, MIO_EINVAL, "edns options too large");
				return MIO_NULL;
			}
			beopt++;
		}

		pktlen += edns_dlen;
	}
	else 
	{
		if (bhdr->rcode > 0x0F)
		{
			/* rcode is larger than 4 bits. but edns info is not provided */
			mio_seterrbfmt (mio, MIO_EINVAL, "rcode too large without edns - %d", bhdr->rcode);
			return MIO_NULL;
		}
	}

	msgbufsz = MIO_SIZEOF(*msg) + MIO_ALIGN_POW2(pktlen, MIO_SIZEOF_VOID_P) + xtnsize;

/* TODO: msg buffer reuse */
	msg = mio_callocmem(mio, msgbufsz);
	if (!msg) return MIO_NULL;

	msg->msglen = msgbufsz; /* record the instance size */
	msg->pktalilen = MIO_ALIGN_POW2(pktlen, MIO_SIZEOF_VOID_P);

	pkt = mio_dns_msg_to_pkt(msg); /* actual packet data begins after the message structure */

	dn = (mio_uint8_t*)(pkt + 1); /* skip the dns packet header */
	for (i = 0; i < qr_count; i++)
	{
		/* dnlen includes the ending <zero> */
		dnlen = to_dn(qr[i].qname, dn, mio_count_bcstr(qr[i].qname) + 2);
		if (dnlen <= 0)
		{
			mio_dns_free_msg (mio, msg);
			mio_seterrbfmt (mio, MIO_EINVAL, "invalid domain name - %hs", qr[i].qname);
			return MIO_NULL;
		}
		qrtr = (mio_dns_qrtr_t*)(dn + dnlen);
		qrtr->qtype = mio_hton16(qr[i].qtype);
		qrtr->qclass = mio_hton16(qr[i].qclass);

		dn = (mio_uint8_t*)(qrtr + 1);
	}

	for (rr_sect = MIO_DNS_RR_PART_ANSWER; rr_sect <= MIO_DNS_RR_PART_ADDITIONAL;)
	{
		mio_oow_t match_count = 0;
		for (i = 0; i < rr_count; i++)
		{
			if (rr[i].part == rr_sect)
			{
				mio_oow_t rdata_len;

				dnlen = to_dn(rr[i].rrname, dn, mio_count_bcstr(rr[i].rrname) + 2);
				if (dnlen <= 0)
				{
					mio_dns_free_msg (mio, msg);
					mio_seterrbfmt (mio, MIO_EINVAL, "invalid domain name - %hs", rr[i].rrname);
					return MIO_NULL;
				}

				rrtr = (mio_dns_rrtr_t*)(dn + dnlen);
				rrtr->rrtype = mio_hton16(rr[i].rrtype);
				rrtr->rrclass = mio_hton16(rr[i].rrclass);
				rrtr->ttl = mio_hton32(rr[i].ttl);

				rdata_len = encode_rrdata_in_dns_msg(mio, &rr[i], rrtr);
				dn = (mio_uint8_t*)(rrtr + 1) + rdata_len;

				match_count++;
			}
		}

		rr_sect = rr_sect + 1;
		((mio_dns_pkt_alt_t*)pkt)->rrcount[rr_sect] = mio_hton16(match_count);
	}

	if (edns)
	{
		mio_dns_eopt_t* eopt;
		mio_dns_beopt_t* beopt;

		/* add EDNS0 OPT RR */
		*dn = 0; /* root domain. as if to_dn("") is called */
		rrtr = (mio_dns_rrtr_t*)(dn + 1);
		rrtr->rrtype = MIO_CONST_HTON16(MIO_DNS_RRT_OPT);
		rrtr->rrclass = mio_hton16(edns->uplen);
		rrtr->ttl = mio_hton32(MIO_DNS_EDNS_MAKE_TTL(bhdr->rcode, edns->version, edns->dnssecok));
		rrtr->dlen = mio_hton16((mio_uint16_t)edns_dlen);
		dn = (mio_uint8_t*)(rrtr + 1);

		beopt = edns->beoptr;
		eopt = (mio_dns_eopt_t*)dn;

		for (i = 0; i < edns->beonum; i++)
		{
			eopt->code = mio_hton16(beopt->code);
			eopt->dlen = mio_hton16(beopt->dlen);
			MIO_MEMCPY (++eopt, beopt->dptr, beopt->dlen);
			eopt = (mio_dns_eopt_t*)((mio_uint8_t*)eopt + beopt->dlen);
			beopt++;
		}

		pkt->arcount = mio_hton16((mio_ntoh16(pkt->arcount) + 1));
		dn += edns_dlen;
	}

	pkt->qdcount = mio_hton16(qr_count);
	pkt->id = mio_hton16((mio_uint16_t)bhdr->id);

	/*pkt->qr = (rr_count > 0);
	pkt->opcode = MIO_DNS_OPCODE_QUERY;*/
	pkt->qr = bhdr->qr & 0x01;
	pkt->opcode = bhdr->opcode & 0x0F;
	pkt->aa = bhdr->aa & 0x01;
	pkt->tc = bhdr->tc & 0x01; 
	pkt->rd = bhdr->rd & 0x01; 
	pkt->ra = bhdr->ra & 0x01;
	pkt->ad = bhdr->ad & 0x01;
	pkt->cd = bhdr->cd & 0x01;
	pkt->rcode = bhdr->rcode & 0x0F;

	msg->pktlen = dn - (mio_uint8_t*)pkt;
	MIO_ASSERT (mio, msg->pktlen == pktlen);
	MIO_ASSERT (mio, msg->pktalilen == MIO_ALIGN_POW2(pktlen, MIO_SIZEOF_VOID_P));

	return msg;
}

void mio_dns_free_msg (mio_t* mio, mio_dns_msg_t* msg)
{
/* TODO: better management */
	mio_freemem (mio, msg);
}
