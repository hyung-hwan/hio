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

struct mio_dnss_t
{
	mio_t* mio;
};

struct mio_dnsc_t
{
	mio_t* mio;
	mio_dev_sck_t* sck;
	mio_sckaddr_t serveraddr;
	mio_oow_t seq;
};

struct mio_dns_req_t 
{
	mio_dns_msg_t* msg;
	/* source address */

	/*mio_dns_req_t* prev;
	mio_dns_req_t* next;*/
};
typedef struct mio_dns_req_t mio_dns_req_t;


static int dns_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_sckaddr_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_dns_msg_t* msg;
	mio_uint16_t id;

	if (dlen < MIO_SIZEOF(*msg)) 
	{
		MIO_DEBUG0 (mio, "dns packet too small from ....\n"); /* TODO: add source packet */
		return 0; /* drop */
	}
	msg = (mio_dns_msg_t*)data;
	if (!msg->qr) return 0; /* drop request */

	id = mio_ntoh16(msg->id);
	/* if id doesn't matche one of the pending requests sent,  drop */

MIO_DEBUG1 (mio, "received dns response...id %d\n", id);
	return 0;
}

static int dns_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;

MIO_DEBUG0 (mio, "sent dns request...\n");
	return 0;
}

static void dns_on_connect (mio_dev_sck_t* dev)
{
}

static void dns_on_disconnect (mio_dev_sck_t* dev)
{
}


mio_dnsc_t* mio_dnsc_start (mio_t* mio)
{
	mio_dnsc_t* dnsc = MIO_NULL;
	mio_dev_sck_make_t minfo;

	dnsc = (mio_dnsc_t*)mio_callocmem(mio, MIO_SIZEOF(*dnsc));
	if (!dnsc) goto oops;

	dnsc->mio = mio;

	MIO_MEMSET (&minfo, 0, MIO_SIZEOF(minfo));
	minfo.type = MIO_DEV_SCK_UDP4; /* or UDP6 depending on the binding address */
	minfo.on_write = dns_on_write;
	minfo.on_read = dns_on_read;
	minfo.on_connect = dns_on_connect;
	minfo.on_disconnect = dns_on_disconnect;
	dnsc->sck = mio_dev_sck_make(mio, 0, &minfo);
	if (!dnsc->sck) goto oops;

	/* bind if requested */
	/*if (mio_dev_sck_bind(dev, ....) <= -1) goto oops;*/
{
mio_uint32_t ia = 0x01010101; /* 1.1.1.1 */
	mio_sckaddr_initforip4 (&dnsc->serveraddr, 53, (mio_ip4addr_t*)&ia);
}

	return dnsc;

oops:
	if (dnsc)
	{
		if (dnsc->sck) mio_dev_sck_kill (dnsc->sck);
	}
	return MIO_NULL;
}

void mio_dnsc_stop (mio_dnsc_t* dnsc)
{
	mio_t* mio = dnsc->mio;
	mio_freemem (mio, dnsc);
}

static void release_req_msg (mio_dnsc_t* dnsc, mio_dns_msg_t* msg)
{
	mio_t* mio = dnsc->mio;
	void* msgbuf;

/* TODO:  add it to the free msg list instead of just freeing it. */
	msgbuf = ((mio_uint8_t*)msg - MIO_SIZEOF(mio_oow_t));
	mio_freemem (mio, msgbuf);
}

static mio_dns_msg_t* build_req_msg (mio_dnsc_t* dnsc, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_oow_t* xmsglen)
{
	mio_t* mio = dnsc->mio;
	mio_oow_t dnlen, msgbufsz, i;
	void* msgbuf;
	mio_dns_msg_t* msg;
	mio_uint8_t* dn;
	mio_dns_qrtr_t* qrtr;
	mio_dns_rrtr_t* rrtr;
	int rr_sect;

	msgbufsz = MIO_SIZEOF(msgbufsz) + MIO_SIZEOF(*msg);

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

	msgbufsz += 1 + MIO_SIZEOF(*rrtr); /* edns0 OPT RR */

	msgbufsz = MIO_ALIGN_POW2(msgbufsz, 64);

/* TODO: msg buffer reuse */
	msgbuf = mio_callocmem(mio, msgbufsz);
	if (!msgbuf) return MIO_NULL;

	*(mio_oow_t*)msgbuf = msgbufsz; /* record the buffer size in the first word */
	msg = (mio_dns_msg_t*)(msgbuf + MIO_SIZEOF(mio_oow_t)); /* actual message begins the after the size word */

	dn = (mio_uint8_t*)(msg + 1);
	for (i = 0; i < qr_count; i++)
	{
		/* dnlen includes the ending <zero> */
		dnlen = to_dn(qr[i].qname, dn, mio_count_bcstr(qr[i].qname) + 2);
		if (dnlen <= 0)
		{
			release_req_msg (dnsc, msg);
			mio_seterrbfmt (mio, MIO_EINVAL, "invalid domain name - %hs", qr[i].qname);
			return MIO_NULL;

		}
		qrtr = (mio_dns_qrtr_t*)(dn + dnlen);
		qrtr->qtype = mio_hton16(qr[i].qtype);
		qrtr->qclass = mio_hton16(qr[i].qclass);

		dn = (mio_uint8_t*)(qrtr + 1);
	}

	for (rr_sect = MIO_DNS_RRR_PART_ANSWER; rr_sect <= MIO_DNS_RRR_PART_ADDITIONAL;)
	{
		mio_oow_t match_count = 0;
		for (i = 0; i < rr_count; i++)
		{
			if (rr[i].part == rr_sect)
			{
				dnlen = to_dn(rr[i].qname, dn, mio_count_bcstr(rr[i].qname) + 2);
				if (dnlen <= 0)
				{
					release_req_msg (dnsc, msg);
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
		((mio_dns_msg_alt_t*)msg)->rrcount[rr_sect] = mio_hton16(match_count);
	}

	/* add EDNS0 OPT RR */
	*dn = 0; /* root domain. as if to_dn("") is called */
	rrtr = (mio_dns_rrtr_t*)(dn + 1);
	rrtr->qtype = MIO_CONST_HTON16(MIO_DNS_QTYPE_OPT);
	rrtr->qclass = MIO_CONST_HTON16(4096); /* udp payload size - TODO: if answer, copy from request, if not, get it from mio settings???? */
	rrtr->ttl = MIO_CONST_HTON32(0); /* extended rcode, version, flags */
	rrtr->dlen = MIO_CONST_HTON16(0);
	msg->arcount = mio_hton16((mio_ntoh16(msg->arcount) + 1));
	dn = (mio_uint8_t*)(rrtr + 1) + 0;

	msg->qr = (rr_count > 0);
	msg->opcode = MIO_DNS_OPCODE_QUERY;
	msg->qdcount = mio_hton16(qr_count);

	*xmsglen = dn - (mio_uint8_t*)msg;
	return msg; /* return the pointer to the beginning of the actual message */
}


static MIO_INLINE int send_req_with_single_rr (mio_dnsc_t* dnsc, mio_dns_bqr_t* qr)
{
	mio_dns_msg_t* msg;
	mio_oow_t msglen;

	msg = build_req_msg(dnsc, qr, 1, MIO_NULL, 0, &msglen);
	if (!msg) return -1;

	msg->rd = 1;
	msg->id = mio_hton16(dnsc->seq);
	dnsc->seq++;

/* TODO: if response, set msg->ra, set msg->id  with id in request */

	mio_dev_sck_write(dnsc->sck, msg, msglen, MIO_NULL, &dnsc->serveraddr); /* TODO: optionally, override dnsc->serveraddr */

	release_req_msg (dnsc, msg);
	return 0;
}

int mio_dnsc_sendreq (mio_dnsc_t* dnsc, mio_dns_bqr_t* qr, mio_oow_t count)
{
#if 1
	mio_oow_t i;

	for (i = 0; i < count; i++)
	{
		if (send_req_with_single_rr(dnsc, &qr[i]) <= -1) return -1;
	}
	return 0;
#else
	mio_dns_msg_t* msg;
	mio_oow_t msglen;

	msg = build_req_msg(dnsc, qr, count, &msglen);
	if (!msg) return -1;

	msg->rd = 1;
	msg->id = mio_hton16(dnsc->seq);
	dnsc->seq++;
/* TODO: if response, set msg->ra, set msg->id  with id in request */

	mio_dev_sck_write(dnsc->sck, msg, msglen, MIO_NULL, &dnsc->serveraddr); /* TODO: optionally, override dnsc->serveraddr */

	release_req_msg (dnsc, msg);
	return 0;
#endif
}

int mio_dnsc_sendrep (mio_dnsc_t* dnsc, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count)
{
	mio_dns_msg_t* msg;
	mio_oow_t msglen;

	msg = build_req_msg(dnsc, qr, qr_count, rr, rr_count, &msglen);
	if (!msg) return -1;

	msg->rd = 1;
	msg->ra = 1;
	/* TODO: msg->id = copy from request */

	mio_dev_sck_write(dnsc->sck, msg, msglen, MIO_NULL, &dnsc->serveraddr); /* TODO: optionally, override dnsc->serveraddr */

	release_req_msg (dnsc, msg);
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
