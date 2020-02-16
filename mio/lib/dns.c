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

#include <netinet/in.h>
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

	mio_ntime_t send_tmout; 
	mio_ntime_t reply_tmout; /* default reply timeout */
	mio_oow_t reply_tmout_max_tries;

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

struct dnc_dns_msg_xtn_t
{
	mio_dev_t*     dev;
	mio_tmridx_t   rtmridx;
	mio_dns_msg_t* prev;
	mio_dns_msg_t* next;
	mio_svc_dnc_on_reply_t on_reply;
	mio_ntime_t    wtmout;
	mio_ntime_t    rtmout;
	int            rmaxtries; /* maximum number of tries to receive a reply */
	int            rtries; /* number of tries made so far */
};
typedef struct dnc_dns_msg_xtn_t dnc_dns_msg_xtn_t;

#if defined(MIO_HAVE_INLINE)
	static MIO_INLINE dnc_dns_msg_xtn_t* dnc_dns_msg_getxtn(mio_dns_msg_t* msg) { return (dnc_dns_msg_xtn_t*)((mio_uint8_t*)mio_dns_msg_to_pkt(msg) + msg->pktalilen); }
#else
#	define dnc_dns_msg_getxtn(msg) ((dnc_dns_msg_xtn_t*)((mio_uint8_t*)mio_dns_msg_to_pkt(msg) + msg->pktalilen))
#endif

static mio_dns_msg_t* build_dns_msg (mio_svc_dnc_t* dnc, mio_dns_bhdr_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, mio_oow_t xtnsize)
{
	mio_dns_msg_t* msg;
	dnc_dns_msg_xtn_t* msgxtn;

	msg = mio_dns_make_msg(dnc->mio, bdns, qr, qr_count, rr, rr_count, edns, MIO_SIZEOF(*msgxtn) + xtnsize);
	if (msg)
	{
		
		if (bdns->id < 0)
		{
			mio_dns_pkt_t* pkt = mio_dns_msg_to_pkt(msg);
			pkt->id = mio_hton16(dnc->seq);
			dnc->seq++;
		}

		msgxtn = dnc_dns_msg_getxtn(msg);
		msgxtn->dev = (mio_dev_t*)dnc->sck;
		msgxtn->rtmridx = MIO_TMRIDX_INVALID;
	}

	return msg;
}

static MIO_INLINE void chain_pending_dns_reqmsg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	if (dnc->pending_req)
	{
		dnc_dns_msg_getxtn(dnc->pending_req)->prev = msg;
		dnc_dns_msg_getxtn(msg)->next = dnc->pending_req;
	}
	dnc->pending_req = msg;
}

static MIO_INLINE void unchain_pending_dns_reqmsg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	if (msgxtn->next) dnc_dns_msg_getxtn(msgxtn->next)->prev = msgxtn->prev;
	if (msgxtn->prev) dnc_dns_msg_getxtn(msgxtn->prev)->next = msgxtn->next;
	else dnc->pending_req = msgxtn->next;
}

static void release_dns_msg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	mio_t* mio = dnc->mio;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);

MIO_DEBUG1 (mio, "releasing dns msg %d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));

	if (msg == dnc->pending_req || msgxtn->next || msgxtn->prev)
	{
		/* it's chained in the pending request. unchain it */
		unchain_pending_dns_reqmsg (dnc, msg);
	}

/* TODO:  add it to the free msg list instead of just freeing it. */
	if (msgxtn->rtmridx != MIO_TMRIDX_INVALID)
	{
		mio_deltmrjob (mio, msgxtn->rtmridx);
		MIO_ASSERT (mio, msgxtn->rtmridx == MIO_TMRIDX_INVALID);
	}

	mio_dns_free_msg (dnc->mio, msg);
}


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
		mio_dns_pkt_t* reqpkt = mio_dns_msg_to_pkt(reqmsg);
		dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(reqmsg);
		if (dev == (mio_dev_sck_t*)msgxtn->dev && pkt->id == reqpkt->id) /* TODO: check the source address against the target address */
		{
MIO_DEBUG1 (mio, "received dns response...id %d\n", id);
			if (MIO_LIKELY(msgxtn->on_reply))
				msgxtn->on_reply (dnc, reqmsg, MIO_ENOERR, data, dlen);

			release_dns_msg (dnc, reqmsg);
			return 0;
		}
		reqmsg = msgxtn->next;
	}

	/* the response id didn't match the ID of pending requests - need to wait longer? */
MIO_DEBUG1 (mio, "unknown dns response... %d\n", pkt->id); /* TODO: add source info */
	return 0;
}

static void dnc_on_reply_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dns_msg_t* reqmsg = (mio_dns_msg_t*)job->ctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(reqmsg);
	mio_dev_sck_t* dev = (mio_dev_sck_t*)msgxtn->dev;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;

	MIO_ASSERT (mio, msgxtn->rtmridx == MIO_TMRIDX_INVALID);

MIO_DEBUG0 (mio, "unable to receive dns response in time...\n");
	if (msgxtn->rtries < msgxtn->rmaxtries)
	{
		mio_ntime_t* tmout;

		tmout = MIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: MIO_NULL;
		if (mio_dev_sck_timedwrite(dnc->sck, mio_dns_msg_to_pkt(reqmsg), reqmsg->pktlen, tmout, reqmsg, &dnc->serveraddr) <= -1)
		{
			if (MIO_LIKELY(msgxtn->on_reply))
				msgxtn->on_reply (dnc, reqmsg, MIO_ETMOUT, MIO_NULL, 0);

			release_dns_msg (dnc, reqmsg);
			return MIO_NULL;
		}

printf (">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> RESENT REQUEST >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> \n");
	}
	else
	{
		if (MIO_LIKELY(msgxtn->on_reply))
			msgxtn->on_reply (dnc, reqmsg, MIO_ETMOUT, MIO_NULL, 0);
		release_dns_msg (dnc, reqmsg);
	}
}


static int dnc_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_sckaddr_t* dstaddr)
{
	mio_t* mio = dev->mio;
	mio_dns_msg_t* msg = (mio_dns_msg_t*)wrctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;

MIO_DEBUG1 (mio, "sent dns message %d\n", (int)mio_ntoh16(mio_dns_msg_to_pkt(msg)->id));

	MIO_ASSERT (mio, dev == (mio_dev_sck_t*)msgxtn->dev);

	if (wrlen <= -1)
	{
		/* write has timed out or an error has occurred */
		if (MIO_LIKELY(msgxtn->on_reply))
			msgxtn->on_reply (dnc, msg, mio_geterrnum(mio), MIO_NULL, 0);
		release_dns_msg (dnc, msg);
	}
	else if (mio_dns_msg_to_pkt(msg)->qr == 0 && msgxtn->rmaxtries > 0)
	{
		/* question. schedule to wait for response */
		mio_tmrjob_t tmrjob;

		MIO_MEMSET (&tmrjob, 0, MIO_SIZEOF(tmrjob));
		tmrjob.ctx = msg;
		mio_gettime (mio, &tmrjob.when);
		MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &msgxtn->rtmout);
		tmrjob.handler = dnc_on_reply_timeout;
		tmrjob.idxptr = &msgxtn->rtmridx;
		msgxtn->rtmridx = mio_instmrjob(mio, &tmrjob);
		if (msgxtn->rtmridx == MIO_TMRIDX_INVALID)
		{
			/* call the callback to indicate this operation failure in the middle of transaction */
			if (MIO_LIKELY(msgxtn->on_reply))
				msgxtn->on_reply (dnc, msg, mio_geterrnum(mio), MIO_NULL, 0);
			release_dns_msg (dnc, msg);

			MIO_DEBUG0 (mio, "unable to schedule timeout...\n");
		}
		else
		{
			if (msgxtn->rtries == 0)
			{
				/* this is the first wait */

				/* TODO: improve performance. hashing by id? */
				/* chain it to the peing request list */
				chain_pending_dns_reqmsg (dnc, msg);
			}

			msgxtn->rtries++;
		}
	}
	else
	{
		/* sent an answer - we don't need this any more */
		/* we don't call the on_reply callback stored in msg->ctx as this is not a reply context */
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

mio_svc_dnc_t* mio_svc_dnc_start (mio_t* mio, const mio_ntime_t* send_tmout, const mio_ntime_t* reply_tmout, mio_oow_t reply_tmout_max_tries)
{
	mio_svc_dnc_t* dnc = MIO_NULL;
	mio_dev_sck_make_t mkinfo;
	dnc_sck_xtn_t* xtn;

	dnc = (mio_svc_dnc_t*)mio_callocmem(mio, MIO_SIZEOF(*dnc));
	if (!dnc) goto oops;

	dnc->mio = mio;
	dnc->stop = mio_svc_dnc_stop;
	dnc->send_tmout = *send_tmout;
	dnc->reply_tmout = *reply_tmout;
	dnc->reply_tmout_max_tries = reply_tmout_max_tries;

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


mio_dns_msg_t* mio_svc_dnc_sendmsg (mio_svc_dnc_t* dnc, mio_dns_bhdr_t* bdns, mio_dns_bqr_t* qr, mio_oow_t qr_count, mio_dns_brr_t* rr, mio_oow_t rr_count, mio_dns_bedns_t* edns, mio_svc_dnc_on_reply_t on_reply, mio_oow_t xtnsize)
{
	/* send a request or a response */
	mio_dns_msg_t* msg;
	dnc_dns_msg_xtn_t* msgxtn;
	mio_ntime_t* tmout;

	msg = build_dns_msg(dnc, bdns, qr, qr_count, rr, rr_count, edns, MIO_SIZEOF(*msgxtn) + xtnsize);
	if (!msg) return MIO_NULL;

	msgxtn = dnc_dns_msg_getxtn(msg);
	msgxtn->on_reply = on_reply;
	msgxtn->wtmout = dnc->send_tmout;
	msgxtn->rtmout = dnc->reply_tmout;
	msgxtn->rmaxtries = dnc->reply_tmout_max_tries; 
	msgxtn->rtries = 0;

/* TODO: optionally, override dnc->serveraddr and use the target address passed as a parameter */
	tmout = MIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: MIO_NULL;
	if (mio_dev_sck_timedwrite(dnc->sck, mio_dns_msg_to_pkt(msg), msg->pktlen, tmout, msg, &dnc->serveraddr) <= -1)
	{
		release_dns_msg (dnc, msg);
		return MIO_NULL;
	}
	
	return msg;
}

#if 0
mio_dns_msg_t* mio_svc_dnc_resendmsg (mio_svc_dnc_t* dnc, mio_dns_msg_t* msg)
{
	if (mio_dev_sck_write(dnc->sck, mio_dns_msg_to_pkt(msg), msg->pktlen, msg, &dnc->serveraddr) <= -1)
	{
		release_dns_msg (dnc, msg);
		return MIO_NULL;
	}

	msg->no_release = 1; ????
	return msg;
}
#endif


mio_dns_msg_t* mio_svc_dnc_sendreq (mio_svc_dnc_t* dnc, mio_dns_bhdr_t* bdns, mio_dns_bqr_t* qr, mio_dns_bedns_t* edns, mio_svc_dnc_on_reply_t on_reply, mio_oow_t xtnsize)
{
	/* send a request without resource records */
	if (bdns->rcode != MIO_DNS_RCODE_NOERROR)
	{
		mio_seterrnum (dnc->mio, MIO_EINVAL);
		return MIO_NULL;
	}

	return mio_svc_dnc_sendmsg(dnc, bdns, qr, 1, MIO_NULL, 0, edns, on_reply, xtnsize);
}

/* ----------------------------------------------------------------------- */


struct dnc_dns_msg_resolve_xtn_t
{
	mio_dns_rrt_t qtype;
	int flags;
	mio_svc_dnc_on_resolve_t on_resolve;
};
typedef struct dnc_dns_msg_resolve_xtn_t dnc_dns_msg_resolve_xtn_t;

#if defined(MIO_HAVE_INLINE)
	static MIO_INLINE dnc_dns_msg_resolve_xtn_t* dnc_dns_msg_resolve_getxtn(mio_dns_msg_t* msg) { return ((dnc_dns_msg_resolve_xtn_t*)((mio_uint8_t*)dnc_dns_msg_getxtn(msg) + MIO_SIZEOF(dnc_dns_msg_xtn_t))); }
#else
#	define dnc_dns_msg_resolve_getxtn(msg) ((dnc_dns_msg_resolve_xtn_t*)((mio_uint8_t*)dnc_dns_msg_getxtn(msg) + MIO_SIZEOF(dnc_dns_msg_xtn_t)))
#endif

static void on_dnc_resolve (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg, mio_errnum_t status, const void* data, mio_oow_t dlen)
{
	mio_t* mio = mio_svc_dnc_getmio(dnc);
	mio_dns_pkt_info_t* pi = MIO_NULL;
	dnc_dns_msg_resolve_xtn_t* reqmsgxtn = dnc_dns_msg_resolve_getxtn(reqmsg);

	if (!(reqmsgxtn->flags & MIO_SVC_DNC_RESOLVE_FLAG_BRIEF))
	{
		/* the full reply packet is requested. no transformation is required */
		if (reqmsgxtn->on_resolve) reqmsgxtn->on_resolve(dnc, reqmsg, status, data, dlen);
		return;
	}

	if (data)
	{
		mio_uint32_t i;

		MIO_ASSERT (mio, status == MIO_ENOERR);

		pi = mio_dns_make_packet_info(mio, data, dlen);
		if (!pi)
		{
			status = mio_geterrnum(mio);
			goto no_valid_reply;
		}

		if (pi->hdr.rcode != MIO_DNS_RCODE_NOERROR) 
		{
			status = MIO_EINVAL;
			goto no_valid_reply;
		}

		if (pi->ancount < 0) goto no_valid_reply;

		/* in the brief mode, we inspect the answer section only */
		if (reqmsgxtn->qtype == MIO_DNS_RRT_Q_ANY)
		{
			/* return A or AAAA for ANY in the brief mode */
			for (i = 0; i < pi->ancount; i++)
			{
				if (pi->rr.an[i].rrtype == MIO_DNS_RRT_A || pi->rr.an[i].rrtype == MIO_DNS_RRT_AAAA)
				{
				match_found:
					if (reqmsgxtn->on_resolve) reqmsgxtn->on_resolve (dnc, reqmsg, status, &pi->rr.an[i], MIO_SIZEOF(pi->rr.an[i]));
					goto done;
				}
			}
		}

		for (i = 0; i < pi->ancount; i++)
		{
			/* it is a bit time taking to retreive the query type from the packet
			 * bundled in reqmsg as it requires parsing of the packet. let me use
			 * the query type i stored in the extension space. */
			switch (reqmsgxtn->qtype)
			{
				case MIO_DNS_RRT_Q_ANY: 
				case MIO_DNS_RRT_Q_AFXR: /* AFXR doesn't make sense in the brief mode. just treat it like ANY */
					/* no A or AAAA found. so give the first entry in the answer */
					goto match_found;

				case MIO_DNS_RRT_Q_MAILA:
					/* if you want to get the full RRs, don't use the brief mode. */
					if (pi->rr.an[i].rrtype == MIO_DNS_RRT_MD || pi->rr.an[i].rrtype == MIO_DNS_RRT_MF) goto match_found;
					break;

				case MIO_DNS_RRT_Q_MAILB:
					/* if you want to get the full RRs, don't use the brief mode. */
					if (pi->rr.an[i].rrtype == MIO_DNS_RRT_MB || pi->rr.an[i].rrtype == MIO_DNS_RRT_MG ||
					    pi->rr.an[i].rrtype == MIO_DNS_RRT_MR || pi->rr.an[i].rrtype == MIO_DNS_RRT_MINFO) goto match_found;
					break;

				default:
					if (pi->rr.an[i].rrtype == reqmsgxtn->qtype) goto match_found;
					break;
			}
		}
		goto no_valid_reply;
	}
	else
	{
	no_valid_reply:
		if (reqmsgxtn->on_resolve) reqmsgxtn->on_resolve (dnc, reqmsg, status, MIO_NULL, 0);
	}

done:
	if (pi) mio_dns_free_packet_info(mio_svc_dnc_getmio(dnc), pi);
}

mio_dns_msg_t* mio_svc_dnc_resolve (mio_svc_dnc_t* dnc, const mio_bch_t* qname, mio_dns_rrt_t qtype, int flags, mio_svc_dnc_on_resolve_t on_resolve, mio_oow_t xtnsize)
{
	static mio_dns_bhdr_t qhdr =
	{
		-1, /* id */
		0,  /* qr */
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
	mio_dns_msg_t* reqmsg;
	dnc_dns_msg_resolve_xtn_t* reqmsgxtn;

	qr.qname = (mio_bch_t*)qname;
	qr.qtype = qtype;
	qr.qclass = MIO_DNS_RRC_IN;

	reqmsg = mio_svc_dnc_sendmsg(dnc, &qhdr, &qr, 1, MIO_NULL, 0, &qedns, on_dnc_resolve, MIO_SIZEOF(*reqmsgxtn) + xtnsize);
	if (reqmsg)
	{
		reqmsgxtn = dnc_dns_msg_resolve_getxtn(reqmsg);
		reqmsgxtn->on_resolve = on_resolve;
		reqmsgxtn->qtype = qtype;
		reqmsgxtn->flags = flags;
	}

	return reqmsg;
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
	mio_uint8_t* xrrdptr;

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
		brr[pos].dptr = pi->_rrdptr;
		brr[pos].dlen = dlen;
	}

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
			break;
		}

		case MIO_DNS_RRT_A:
			if (MIO_UNLIKELY(dlen != 4)) goto oops;
			break;

		case MIO_DNS_RRT_AAAA:
			if (MIO_UNLIKELY(dlen != 16)) goto oops;
			break;

		case MIO_DNS_RRT_CNAME:
		{
		#if !defined(MIO_BUILD_RELEASE)
			mio_uint8_t* xptr = pi->_ptr;
		#endif
			if (parse_domain_name(mio, pi) <= -1) return -1;
			MIO_ASSERT (mio, pi->_ptr == xptr + dlen);
			dlen = 0; /* to skip additional update on pi->_ptr and data copy before return */
			break;
		}
	}

	if (dlen > 0)
	{
		pi->_ptr += dlen;
		pi->_rrdlen += dlen;
		if (pi->_rrdptr) 
		{
			MIO_MEMCPY (pi->_rrdptr, rrtr + 1, dlen); /* copy actual data */
			pi->_rrdptr += dlen;
		}
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

static mio_oow_t encode_rdata_in_dns_msg (mio_t* mio, const mio_dns_brr_t* rr, mio_dns_rrtr_t* rrtr)
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

				rdata_len = encode_rdata_in_dns_msg(mio, &rr[i], rrtr);
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
