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

typedef struct mio_dns_t mio_dns_t;

struct mio_dns_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/
};

struct mio_dnss_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/
};

struct mio_dnsc_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/

	mio_dev_sck_t* sck;
	mio_sckaddr_t serveraddr;
	mio_oow_t seq;
	mio_dns_msg_t* pending_req;
};

struct dnsc_sck_xtn_t
{
	mio_dnsc_t* dnsc;
};
typedef struct dnsc_sck_xtn_t dnsc_sck_xtn_t;

/* ----------------------------------------------------------------------- */

#define DN_AT_END(ptr) (ptr[0] == '\0' || (ptr[0] == '.' && ptr[1] == '\0'))

static mio_oow_t to_dn (mio_dns_t* dns, const mio_bch_t* str, mio_uint8_t* buf, mio_oow_t bufsz)
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


static void release_dns_msg (mio_dnsc_t* dnsc, mio_dns_msg_t* msg)
{
	mio_t* mio = dnsc->mio;

MIO_DEBUG1 (mio, "releasing dns msg %d\n", (int)mio_ntoh16(((mio_dns_pkt_t*)(msg + 1))->id));

	if (msg == dnsc->pending_req || msg->next || msg->prev)
	{
		/* it's chained in the pending request. unchain it */
		if (msg->next) msg->next->prev = msg->prev;
		if (msg->prev) msg->prev->next = msg->next;
		else dnsc->pending_req = msg->next;
	}

/* TODO:  add it to the free msg list instead of just freeing it. */
	if (msg->rtmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, msg->rtmridx);
		MIO_ASSERT (mio, msg->rtmridx == MIO_TMRIDX_INVALID);
	}

	mio_freemem (mio, msg);
}

static mio_dns_msg_t* build_dns_msg (mio_dnsc_t* dnsc, mio_dns_bdns_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns)
{
	mio_t* mio = dnsc->mio;
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
	msg->dev = (mio_dev_t*)dnsc->sck;

	pkt = (mio_dns_pkt_t*)(msg + 1); /* actual message begins after the size word */

	dn = (mio_uint8_t*)(pkt + 1);
	for (i = 0; i < qr_count; i++)
	{
		/* dnlen includes the ending <zero> */
		dnlen = to_dn((mio_dns_t*)dnsc, qr[i].qname, dn, mio_count_bcstr(qr[i].qname) + 2);
		if (dnlen <= 0)
		{
			release_dns_msg (dnsc, msg);
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
				dnlen = to_dn((mio_dns_t*)dnsc, rr[i].qname, dn, mio_count_bcstr(rr[i].qname) + 2);
				if (dnlen <= 0)
				{
					release_dns_msg (dnsc, msg);
					mio_seterrbfmt (mio, MIO_EINVAL, "invalid domain name - %hs", rr[i].qname);
					return MIO_NULL;
				}

				rrtr = (mio_dns_rrtr_t*)(dn + dnlen);
				rrtr->qtype = mio_hton16(rr[i].qtype);
				rrtr->qclass = mio_hton16(rr[i].qclass);
				rrtr->ttl = mio_hton32(rr[i].ttl);
				rrtr->dlen = mio_hton16(rr[i].dlen);
				if (rr[i].dlen > 0) MIO_MEMCPY (rrtr + 1, rr[i].dptr, rr[i].dlen);

				dn = (mio_uint8_t*)(rrtr + 1) + rr[i].dlen;

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
		pkt->id = mio_hton16(dnsc->seq);
		dnsc->seq++;
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

/* ----------------------------------------------------------------------- */

static int dnsc_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dnsc_t* dnsc = ((dnsc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnsc;
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

	/* if id doesn't matche one of the pending requests sent,  drop */

/* TODO: improve performance */
	reqmsg = dnsc->pending_req;
	while (reqmsg)
	{
		mio_dns_pkt_t* reqpkt = (mio_dns_pkt_t*)(reqmsg + 1);
		if (dev == (mio_dev_sck_t*)reqmsg->dev && pkt->id == reqpkt->id) /* TODO: check the source address against the target address */
		{
MIO_DEBUG1 (mio, "received dns response...id %d\n", id);
			/* TODO: parse the response... perform actual work. pass the result back?? */
			release_dns_msg (dnsc, reqmsg);
			return 0;
		}
		reqmsg = reqmsg->next;
	}

	/* the response id didn't match the ID of pending requests */
	MIO_DEBUG0 (mio, "unknown dns response... \n"); /* TODO: add source info */
	return 0;
}

static void dnsc_on_read_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dns_msg_t* msg = (mio_dns_msg_t*)job->ctx;
	mio_dev_sck_t* dev = (mio_dev_sck_t*)msg->dev;
	mio_dnsc_t* dnsc = ((dnsc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnsc;

	MIO_ASSERT (mio, msg->rtmridx == MIO_TMRIDX_INVALID);

MIO_DEBUG0 (mio, "unable to receive dns response in time...\n");
	release_dns_msg (dnsc, msg);
}


static int dnsc_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dns_msg_t* msg = (mio_dns_msg_t*)wrctx;
	mio_dnsc_t* dnsc = ((dnsc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnsc;
	mio_tmrjob_t tmrjob;
	mio_ntime_t tmout;

MIO_DEBUG0 (mio, "sent dns request...\n");

	MIO_ASSERT (mio, dev == (mio_dev_sck_t*)msg->dev);

	/* TODO: make this configurable. or accept dnsc->config.read_timeout... */
	tmout.sec = 3;
	tmout.nsec = 0;

	MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = msg;
	mio_gettime (mio, &tmrjob.when);
	MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &tmout);
	tmrjob.handler = dnsc_on_read_timeout;
	tmrjob.idxptr = &msg->rtmridx;
	msg->rtmridx = mio_instmrjob(mio, &tmrjob);
	if (msg->rtmridx == MIO_TMRIDX_INVALID)
	{
		MIO_DEBUG0 (mio, "unable to schedule timeout...\n");
		release_dns_msg (dnsc, msg);
		return 0;
	}

/* TODO: improve performance */
	if (dnsc->pending_req)
	{
		dnsc->pending_req->prev = msg;
		msg->next = dnsc->pending_req;
	}
	dnsc->pending_req = msg;

	return 0;
}

static void dnsc_on_connect (mio_dev_sck_t* dev)
{
}

static void dnsc_on_disconnect (mio_dev_sck_t* dev)
{
}

mio_dnsc_t* mio_dnsc_start (mio_t* mio)
{
	mio_dnsc_t* dnsc = MIO_NULL;
	mio_dev_sck_make_t mkinfo;
	dnsc_sck_xtn_t* xtn;

	dnsc = (mio_dnsc_t*)mio_callocmem(mio, MIO_SIZEOF(*dnsc));
	if (!dnsc) goto oops;

	dnsc->mio = mio;
	dnsc->stop = mio_dnsc_stop;

	MIO_MEMSET (&mkinfo, 0, MIO_SIZEOF(mkinfo));
	mkinfo.type = MIO_DEV_SCK_UDP4; /* or UDP6 depending on the binding address */
	mkinfo.on_write = dnsc_on_write;
	mkinfo.on_read = dnsc_on_read;
	mkinfo.on_connect = dnsc_on_connect;
	mkinfo.on_disconnect = dnsc_on_disconnect;
	dnsc->sck = mio_dev_sck_make(mio, MIO_SIZEOF(*xtn), &mkinfo);
	if (!dnsc->sck) goto oops;

	xtn = (dnsc_sck_xtn_t*)mio_dev_sck_getxtn(dnsc->sck);
	xtn->dnsc = dnsc;

	/* bind if requested */
	/*if (mio_dev_sck_bind(dev, ....) <= -1) goto oops;*/
{
mio_uint32_t ia = 0x01010101; /* 1.1.1.1 */
	mio_sckaddr_initforip4 (&dnsc->serveraddr, 53, (mio_ip4addr_t*)&ia);
}

	MIO_SVC_REGISTER (mio, (mio_svc_t*)dnsc);
	return dnsc;

oops:
	if (dnsc)
	{
		if (dnsc->sck) mio_dev_sck_kill (dnsc->sck);
		mio_freemem (mio, dnsc);
	}
	return MIO_NULL;
}

void mio_dnsc_stop (mio_dnsc_t* dnsc)
{
	mio_t* mio = dnsc->mio;
	if (dnsc->sck) mio_dev_sck_kill (dnsc->sck);
	MIO_SVC_UNREGISTER (mio, dnsc);
	while (dnsc->pending_req) release_dns_msg (dnsc, dnsc->pending_req);
	mio_freemem (mio, dnsc);
}


int mio_dnsc_sendmsg (mio_dnsc_t* dnsc, mio_dns_bdns_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns)
{
	/* send a request or a response */
	mio_dns_msg_t* msg;

	msg = build_dns_msg(dnsc, bdns, qr, qr_count, rr, rr_count, edns);
	if (!msg) return -1;

	/* TODO: optionally, override dnsc->serveraddr and use the target address passed as a parameter */
	if (mio_dev_sck_write(dnsc->sck, msg + 1, msg->pktlen, msg, &dnsc->serveraddr) <= -1)
	{
		release_dns_msg (dnsc, msg);
		return -1;
	}

	return 0;
}

int mio_dnsc_sendreq (mio_dnsc_t* dnsc, mio_dns_bdns_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_bedns_t* edns)
{
	/* send requests without resource record */
	mio_oow_t i;
	for (i = 0; i < qr_count; i++)
	{
		if (mio_dnsc_sendmsg(dnsc, bdns, &qr[i], 1, MIO_NULL, 0, edns) <= -1) return -1;
	}
	return 0;
}

#if 0
static void dnss_on_request (mio_dnss_t* dnss, mio_dns_req_t* req)
{
	mio_dnss_reply (dnss, rep); /* reply send timeout??? */
}

/* ----------------------------------------------------------------------- */


int main ()
{
	mio = mio_open ();

	dnss1 = mio_dnss_start(mio, "1.1.1.1:53,2.2.2.2:53", dnss_on_request);
	mio_dnss_end (dnss1);

	dnsc1 = mio_dnsc_start (mio, "8.8.8.8:53,1.1.1.1:53"); /* option - send to all, send one by one */
	mio_dnsc_resolve (dnsc1, "A:www.google.com", dnsc_on_response);
	mio_dnsc_end (dnsc1);

	mio_close (mio);
	return 0;
}

#endif
