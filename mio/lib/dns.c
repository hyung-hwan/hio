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

#include <mio-dns.h>
#include <mio-sck.h>
#include "mio-prv.h"

typedef struct mio_svc_dns_t mio_svc_dns_t;

struct mio_svc_dns_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/
};

struct mio_svc_dnc_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/

	mio_dev_sck_t* sck;
	mio_sckaddr_t serveraddr;
	mio_oow_t seq;
	mio_dns_msg_t* pending_req;
};

struct dnc_sck_xtn_t
{
	mio_svc_dnc_t* dnc;
};
typedef struct dnc_sck_xtn_t dnc_sck_xtn_t;

/* ----------------------------------------------------------------------- */

#define DN_AT_END(ptr) (ptr[0] == '\0' || (ptr[0] == '.' && ptr[1] == '\0'))

static mio_oow_t to_dn (mio_svc_dns_t* dns, const mio_bch_t* str, mio_uint8_t* buf, mio_oow_t bufsz)
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
#if defined(MIO_HAVE_INLINE)
	static MIO_INLINE mio_dns_pkt_t* dns_msg_to_pkt (mio_dns_msg_t* msg) { return (mio_dns_pkt_t*)(msg + 1); }
#else
#	define dns_msg_to_pkt(msg) ((mio_dns_pkt_t*)((mio_dns_msg_t*)(msg) + 1))
#endif

static void release_dns_msg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	mio_t* mio = dnc->mio;

MIO_DEBUG1 (mio, "releasing dns msg %d\n", (int)mio_ntoh16(dns_msg_to_pkt(msg)->id));

	if (msg == dnc->pending_req || msg->next || msg->prev)
	{
		/* it's chained in the pending request. unchain it */
		if (msg->next) msg->next->prev = msg->prev;
		if (msg->prev) msg->prev->next = msg->next;
		else dnc->pending_req = msg->next;
	}

/* TODO:  add it to the free msg list instead of just freeing it. */
	if (msg->rtmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, msg->rtmridx);
		MIO_ASSERT (mio, msg->rtmridx == MIO_TMRIDX_INVALID);
	}

	mio_freemem (mio, msg);
}

static mio_oow_t encode_rdata_in_dns_msg (mio_svc_dnc_t* dnc, const mio_dns_brr_t* rr, mio_dns_rrtr_t* rrtr)
{
	switch (rr->qtype)
	{
		case MIO_DNS_QTYPE_A:
			break;
		case MIO_DNS_QTYPE_AAAA:
		
			break;

		/*
		case MIO_DNS_QTYPE_WKS:
			break; */

		case MIO_DNS_QTYPE_MX:
			/* preference, exchange */
			break;

		case MIO_DNS_QTYPE_CNAME: 
		/*case MIO_DNS_QTYPE_MB:
		case MIO_DNS_QTYPE_MD:
		case MIO_DNS_QTYPE_MF:
		case MIO_DNS_QTYPE_MG:
		case MIO_DNS_QTYPE_MR:*/
		case MIO_DNS_QTYPE_NS:
		case MIO_DNS_QTYPE_PTR:
			/* just a normal domain name */
			break;

	#if 0
		case MIO_DNS_QTYPE_HINFO:
			/* cpu, os */
			break;
	#endif

	#if 0
		case MIO_DNS_QTYPE_MINFO:
			/* rmailbx, emailbx */
	#endif
			break;
		
		case MIO_DNS_QTYPE_SOA:
			/* soa */
			break;

		case MIO_DNS_QTYPE_TXT:
		case MIO_DNS_QTYPE_NULL:
		default:
			/* TODO: custom transformator? */
			rrtr->dlen = mio_hton16(rr->dlen);
			if (rr->dlen > 0) MIO_MEMCPY (rrtr + 1, rr->dptr, rr->dlen);
	}

	return rr->dlen;
}

static mio_dns_msg_t* build_dns_msg (mio_svc_dnc_t* dnc, mio_dns_bdns_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, void* ctx)
{
	mio_t* mio = dnc->mio;
	mio_oow_t dnlen, msgbufsz, i;
	mio_dns_msg_t* msg;
	mio_dns_pkt_t* pkt;
	mio_uint8_t* dn;
	mio_dns_qrtr_t* qrtr;
	mio_dns_rrtr_t* rrtr;
	int rr_sect;
	mio_uint32_t edns_dlen;

	msgbufsz = MIO_SIZEOF(*msg) + MIO_SIZEOF(*pkt);

	for (i = 0; i < qr_count; i++)
	{
		/* <length>segmnet<length>segment<zero>.
		 * if the input has the ending period(e.g. mio.lib.), the dn length is namelen + 1. 
		 * if the input doesn't have the ending period(e.g. mio.lib) . the dn length is namelen + 2. */
		msgbufsz += mio_count_bcstr(qr[i].qname) + 2 + MIO_SIZEOF(*qrtr);
	}

	for (i = 0; i < rr_count; i++)
	{
		msgbufsz += mio_count_bcstr(rr[i].qname) + 2 + MIO_SIZEOF(*rrtr) + rr[i].dlen;
	}

	edns_dlen = 0;
	if (edns)
	{
		mio_dns_beopt_t* beopt;

		msgbufsz += 1 + MIO_SIZEOF(*rrtr); /* edns0 OPT RR - 1 for the root name  */

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

		msgbufsz += edns_dlen;
	}
	else 
	{
		if (bdns->rcode > 0x0F)
		{
			/* rcode is larger than 4 bits. but edns info is not provided */
			mio_seterrbfmt (mio, MIO_EINVAL, "rcode too large without edns - %d", bdns->rcode);
			return MIO_NULL;
		}
	}

	msgbufsz = MIO_ALIGN_POW2(msgbufsz, 64);

/* TODO: msg buffer reuse */
	msg = mio_callocmem(mio, msgbufsz);
	if (!msg) return MIO_NULL;

	msg->buflen = msgbufsz; /* record the buffer size in the preamble */
	msg->rtmridx = MIO_TMRIDX_INVALID;
	msg->dev = (mio_dev_t*)dnc->sck;
	msg->ctx = ctx;

	pkt = dns_msg_to_pkt(msg); /* actual packet data begins after the message structure */

	dn = (mio_uint8_t*)(pkt + 1); /* skip the dns packet header */
	for (i = 0; i < qr_count; i++)
	{
		/* dnlen includes the ending <zero> */
		dnlen = to_dn((mio_svc_dns_t*)dnc, qr[i].qname, dn, mio_count_bcstr(qr[i].qname) + 2);
		if (dnlen <= 0)
		{
			release_dns_msg (dnc, msg);
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

				dnlen = to_dn((mio_svc_dns_t*)dnc, rr[i].qname, dn, mio_count_bcstr(rr[i].qname) + 2);
				if (dnlen <= 0)
				{
					release_dns_msg (dnc, msg);
					mio_seterrbfmt (mio, MIO_EINVAL, "invalid domain name - %hs", rr[i].qname);
					return MIO_NULL;
				}

				rrtr = (mio_dns_rrtr_t*)(dn + dnlen);
				rrtr->qtype = mio_hton16(rr[i].qtype);
				rrtr->qclass = mio_hton16(rr[i].qclass);
				rrtr->ttl = mio_hton32(rr[i].ttl);

				rdata_len = encode_rdata_in_dns_msg(dnc, &rr[i], rrtr);
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
		rrtr->qtype = MIO_CONST_HTON16(MIO_DNS_QTYPE_OPT);
		rrtr->qclass = mio_hton16(edns->uplen);
		rrtr->ttl = mio_hton32(MIO_DNS_EDNS_MAKE_TTL(bdns->rcode, edns->version, edns->dnssecok));
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

	if (bdns->id < 0)
	{
		pkt->id = mio_hton16(dnc->seq);
		dnc->seq++;
	}
	else
	{
		pkt->id = mio_hton16((mio_uint16_t)bdns->id);
	}

	/*pkt->qr = (rr_count > 0);
	pkt->opcode = MIO_DNS_OPCODE_QUERY;*/
	pkt->qr = bdns->qr & 0x01;
	pkt->opcode = bdns->opcode & 0x0F;
	pkt->aa = bdns->aa & 0x01;
	pkt->tc = bdns->tc & 0x01; 
	pkt->rd = bdns->rd & 0x01; 
	pkt->ra = bdns->ra & 0x01;
	pkt->ad = bdns->ad & 0x01;
	pkt->cd = bdns->cd & 0x01;
	pkt->rcode = bdns->rcode & 0x0F;

	msg->pktlen = dn - (mio_uint8_t*)pkt;
	return msg;
}

static int parse_dns_pkt (mio_svc_dnc_t* dnc, mio_dns_pkt_t* pkt, mio_oow_t len)
{

	mio_t* mio = dnc->mio;
	mio_dns_bdns_t* bdns;
	mio_uint16_t i, rrc;
	mio_uint8_t* dn;
	mio_uint8_t* pktend = (mio_uint8_t*)pkt + len;

	MIO_ASSERT (mio, len >= MIO_SIZEOF(*pkt));
return 0;
/* TODO: */
	bdns->id = mio_ntoh16(pkt->id);

	bdns->qr = pkt->qr & 0x01;
	bdns->opcode = pkt->opcode & 0x0F;
	bdns->aa = pkt->aa & 0x01;
	bdns->tc = pkt->tc & 0x01; 
	bdns->rd = pkt->rd & 0x01; 
	bdns->ra = pkt->ra & 0x01;
	bdns->ad = pkt->ad & 0x01;
	bdns->cd = pkt->cd & 0x01;
	bdns->rcode = pkt->rcode & 0x0F;

	/*
	bdns->qdcount = mio_ntoh16(pkt->qdcount);
	bdns->ancount = mio_ntoh16(pkt->ancount);
	bdns->nscount = mio_ntoh16(pkt->nscount);
	bdns->arcount = mio_ntoh16(pkt->arcount);
	*/
	dn = (mio_uint8_t*)(pkt + 1);

	rrc = mio_ntoh16(pkt->qdcount);
	for (i = 0; i < rrc; i++)
	{
		mio_oow_t totlen, seglen;
		mio_dns_qrtr_t* qrtr;

		if (dn >= pktend)
		{
			mio_seterrbfmt (mio, MIO_EINVAL, "invalid packet");
			return -1;
		}

		totlen = 0;
		while ((seglen = *dn++) > 0)
		{
			if (seglen > 64)
			{
				/* compressed. pointer to somewhere else */
/* TODO: */
			}

			totlen += seglen;
			dn += seglen;
		}

		qrtr = (mio_dns_qrtr_t*)dn;
		dn += MIO_SIZEOF(*qrtr);
	}

	rrc = mio_ntoh16(pkt->ancount);
	for (i = 0; i < rrc; i++)
	{
		mio_oow_t totlen, seglen;
		mio_dns_rrtr_t* rrtr;

		if (dn >= pktend)
		{
			mio_seterrbfmt (mio, MIO_EINVAL, "invalid packet");
			return -1;
		}

		totlen = 0;
		while ((seglen = *dn++) > 0)
		{
			totlen += seglen;
			dn += seglen;
		}

		rrtr = (mio_dns_rrtr_t*)dn;
		dn += MIO_SIZEOF(*rrtr) + rrtr->dlen;
	}

#if 0
	for (i = 0; i < bdns->nscount; i++)
	{
	}

	for (i = 0; i < bdns->arcount; i++)
	{
	#if 0
		if (*dn == 0)
		{
			rrtr = (mio_dns_rrtr_t*)(dn + 1);
			if (rrtr->qtype == MIO_CONST_HTON16(MIO_DNS_QTYPE_OPT)
			{
				/* edns */
			}
		}
	#endif
	}
#endif

	return 0;
}

/* ----------------------------------------------------------------------- */

static int dnc_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_pkt_t* pkt;
	mio_dns_msg_t* reqmsg;
	mio_uint16_t id;

	if (dlen < MIO_SIZEOF(*pkt)) 
	{
		MIO_DEBUG0 (mio, "dns packet too small from ....\n"); /* TODO: add source packet */
		return 0; /* drop */
	}
	pkt = (mio_dns_pkt_t*)data;
	if (!pkt->qr) 
	{
		MIO_DEBUG0 (mio, "dropping dns request received ...\n"); /* TODO: add source info */
		return 0; /* drop request */
	}

	id = mio_ntoh16(pkt->id);

	/* if id doesn't match one of the pending requests sent,  drop it */

/* TODO: improve performance */
	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		mio_dns_pkt_t* reqpkt = dns_msg_to_pkt(reqmsg);
		if (dev == (mio_dev_sck_t*)reqmsg->dev && pkt->id == reqpkt->id) /* TODO: check the source address against the target address */
		{
MIO_DEBUG1 (mio, "received dns response...id %d\n", id);
			/* TODO: parse the response... perform actual work. pass the result back?? */
			/*parse_dns_pkt (dnc, pkt, dlen);*/

			if (MIO_LIKELY(reqmsg->ctx))
				((mio_svc_dnc_on_reply_t)reqmsg->ctx) (dnc, reqmsg, MIO_ENOERR, data, dlen);

			release_dns_msg (dnc, reqmsg);
			return 0;
		}
		reqmsg = reqmsg->next;
	}

	/* the response id didn't match the ID of pending requests */
	MIO_DEBUG0 (mio, "unknown dns response... \n"); /* TODO: add source info */
	return 0;
}

static void dnc_on_read_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dns_msg_t* reqmsg = (mio_dns_msg_t*)job->ctx;
	mio_dev_sck_t* dev = (mio_dev_sck_t*)reqmsg->dev;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;

	MIO_ASSERT (mio, reqmsg->rtmridx == MIO_TMRIDX_INVALID);

MIO_DEBUG0 (mio, "unable to receive dns response in time...\n");

	if (MIO_LIKELY(reqmsg->ctx))
		((mio_svc_dnc_on_reply_t)reqmsg->ctx) (dnc, reqmsg, MIO_ETMOUT, MIO_NULL, 0);

	release_dns_msg (dnc, reqmsg);
}


static int dnc_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dns_msg_t* msg = (mio_dns_msg_t*)wrctx;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;

MIO_DEBUG1 (mio, "sent dns message %d\n", (int)mio_ntoh16(dns_msg_to_pkt(msg)->id));

	MIO_ASSERT (mio, dev == (mio_dev_sck_t*)msg->dev);

	if (dns_msg_to_pkt(msg)->qr == 0)
	{
		/* question. schedule to wait for response */
		mio_tmrjob_t tmrjob;
		mio_ntime_t tmout;

		/* TODO: make this configurable. or accept dnc->config.read_timeout... */
		tmout.sec = 3;
		tmout.nsec = 0;

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = msg;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &tmout);
		tmrjob.handler = dnc_on_read_timeout;
		tmrjob.idxptr = &msg->rtmridx;
		msg->rtmridx = mio_instmrjob(mio, &tmrjob);
		if (msg->rtmridx == MIO_TMRIDX_INVALID)
		{
			if (MIO_LIKELY(msg->ctx))
				((mio_svc_dnc_on_reply_t)msg->ctx) (dnc, msg, mio_geterrnum(mio), MIO_NULL, 0);
			release_dns_msg (dnc, msg);

			MIO_DEBUG0 (mio, "unable to schedule timeout...\n");
			return 0;
		}

		/* TODO: improve performance. hashing by id? */

		/* chain it to the peing request list */
		if (dnc->pending_req)
		{
			dnc->pending_req->prev = msg;
			msg->next = dnc->pending_req;
		}
		dnc->pending_req = msg;
	}
	else
	{
		/* sent an answer - we don't need this any more */
		/* also we don't call the on_reply callback stored in msg->ctx as this is not a reply context */
		release_dns_msg (dnc, msg);
	}

	return 0;
}

static void dnc_on_connect (mio_dev_sck_t* dev)
{
}

static void dnc_on_disconnect (mio_dev_sck_t* dev)
{
}

mio_svc_dnc_t* mio_svc_dnc_start (mio_t* mio)
{
	mio_svc_dnc_t* dnc = MIO_NULL;
	mio_dev_sck_make_t mkinfo;
	dnc_sck_xtn_t* xtn;

	dnc = (mio_svc_dnc_t*)mio_callocmem(mio, MIO_SIZEOF(*dnc));
	if (!dnc) goto oops;

	dnc->mio = mio;
	dnc->stop = mio_svc_dnc_stop;

	MIO_MEMSET (&mkinfo, 0, MIO_SIZEOF(mkinfo));
	mkinfo.type = MIO_DEV_SCK_UDP4; /* or UDP6 depending on the binding address */
	mkinfo.on_write = dnc_on_write;
	mkinfo.on_read = dnc_on_read;
	mkinfo.on_connect = dnc_on_connect;
	mkinfo.on_disconnect = dnc_on_disconnect;
	dnc->sck = mio_dev_sck_make(mio, MIO_SIZEOF(*xtn), &mkinfo);
	if (!dnc->sck) goto oops;

	xtn = (dnc_sck_xtn_t*)mio_dev_sck_getxtn(dnc->sck);
	xtn->dnc = dnc;

	/* TODO: bind if requested */
	/*if (mio_dev_sck_bind(dev, ....) <= -1) goto oops;*/
{
mio_uint32_t ia = 0x01010101; /* 1.1.1.1 */ /* TODO: accept as parameter ... */
	mio_sckaddr_initforip4 (&dnc->serveraddr, 53, (mio_ip4addr_t*)&ia);
}

	MIO_SVC_REGISTER (mio, (mio_svc_t*)dnc);
	return dnc;

oops:
	if (dnc)
	{
		if (dnc->sck) mio_dev_sck_kill (dnc->sck);
		mio_freemem (mio, dnc);
	}
	return MIO_NULL;
}

void mio_svc_dnc_stop (mio_svc_dnc_t* dnc)
{
	mio_t* mio = dnc->mio;
	if (dnc->sck) mio_dev_sck_kill (dnc->sck);
	MIO_SVC_UNREGISTER (mio, dnc);
	while (dnc->pending_req) release_dns_msg (dnc, dnc->pending_req);
	mio_freemem (mio, dnc);
}


int mio_svc_dnc_sendmsg (mio_svc_dnc_t* dnc, mio_dns_bdns_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, mio_svc_dnc_on_reply_t on_reply)
{
	/* send a request or a response */
	mio_dns_msg_t* msg;

	msg = build_dns_msg(dnc, bdns, qr, qr_count, rr, rr_count, edns, on_reply);
	if (!msg) return -1;

	/* TODO: optionally, override dnc->serveraddr and use the target address passed as a parameter */
	if (mio_dev_sck_write(dnc->sck, dns_msg_to_pkt(msg), msg->pktlen, msg, &dnc->serveraddr) <= -1)
	{
		release_dns_msg (dnc, msg);
		return -1;
	}

	return 0;
}

int mio_svc_dnc_sendreq (mio_svc_dnc_t* dnc, mio_dns_bdns_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_bedns_t* edns, mio_svc_dnc_on_reply_t on_reply)
{
	/* send requests without resource record */
	mio_oow_t i;
	for (i = 0; i < qr_count; i++)
	{
		if (mio_svc_dnc_sendmsg(dnc, bdns, &qr[i], 1, MIO_NULL, 0, edns, on_reply) <= -1) return -1;
	}
	return 0;
}

static void on_dnc_resolve_reply (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg, mio_errnum_t status, const void* data, mio_oow_t dlen)
{
}

int mio_svc_dnc_resolve (mio_svc_dnc_t* dnc, const mio_bch_t* qname, mio_dns_qtype_t qtype, mio_svc_dnc_on_reply_t on_reply)
{
	static mio_dns_bdns_t qhdr =
	{
		-1,              /* id */
		0,                  /* qr */
		MIO_DNS_OPCODE_QUERY, /* opcode */
		0, /* aa */
		0, /* tc */
		1, /* rd */
		0, /* ra */
		0, /* ad */
		0, /* cd */
		MIO_DNS_RCODE_NOERROR /* rcode */
	};

	static mio_dns_bedns_t qedns =
	{
		4096, /* uplen */

		0,    /* edns version */
		0,    /* dnssec ok */

		0,    /* number of edns options */
		MIO_NULL
	};

	mio_dns_bqr_t qr;

	qr.qname = (mio_bch_t*)qname;
	qr.qtype = qtype;
	qr.qclass = MIO_DNS_QCLASS_IN;

	return mio_svc_dnc_sendreq(dnc, &qhdr, &qr, 1, &qedns, on_reply);
}

