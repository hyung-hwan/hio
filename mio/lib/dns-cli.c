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

#include <mio-dns.h>
#include <mio-sck.h>
#include "mio-prv.h"

#include <netinet/in.h>

struct mio_svc_dns_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/
};

struct mio_svc_dnc_t
{
	MIO_SVC_HEADERS;
	/*MIO_DNS_SVC_HEADERS;*/

	mio_dev_sck_t* udp_sck;
	mio_dev_sck_t* tcp_sck;
	mio_skad_t serv_addr;

	mio_ntime_t send_tmout; 
	mio_ntime_t reply_tmout; /* default reply timeout */
	mio_oow_t reply_tmout_max_tries;

	mio_oow_t seq;
	mio_dns_msg_t* pending_req;
};

struct dnc_sck_xtn_t
{
	mio_svc_dnc_t* dnc;
	mio_dns_msg_t* reqmsg; /* used when switching to TCP from UDP  for truncation */
};
typedef struct dnc_sck_xtn_t dnc_sck_xtn_t;

/* ----------------------------------------------------------------------- */

struct dnc_dns_msg_xtn_t
{
	mio_dev_t*     dev;
	mio_tmridx_t   rtmridx;
	mio_dns_msg_t* prev;
	mio_dns_msg_t* next;
	mio_skad_t     servaddr;
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
		msgxtn->dev = (mio_dev_t*)dnc->udp_sck;
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

/* ----------------------------------------------------------------------- */


static int on_tcp_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_skad_t* srcaddr)
{
	return 0;
}

static int on_tcp_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
printf ("ON WRITE >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	return 0;
}


static void on_tcp_connect (mio_dev_sck_t* dev)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_msg_t* reqmsg = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->reqmsg;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	mio_uint16_t pktlen;

	MIO_ASSERT (mio, dev == dnc->tcp_sck);

	pktlen = mio_hton16(reqmsg->pktlen);

/* TODO: create writev... which triggered on_write_once */
	mio_dev_sck_timedwrite(dev, &pktlen, MIO_SIZEOF(pktlen), &reqmsgxtn->rtmout, reqmsg, MIO_NULL);
	if (mio_dev_sck_timedwrite(dev, mio_dns_msg_to_pkt(reqmsg), reqmsg->pktlen, &reqmsgxtn->rtmout, reqmsg, MIO_NULL) <= -1)
	{
		if (MIO_LIKELY(reqmsgxtn->on_reply))
			reqmsgxtn->on_reply (dnc, reqmsg, mio_geterrnum(mio), MIO_NULL, 0);

		release_dns_msg (dnc, reqmsg);
	}
}

static void on_tcp_disconnect (mio_dev_sck_t* dev)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_msg_t* reqmsg = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->reqmsg;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	int status;

	/* UNABLE TO CONNECT or CONNECT TIMED OUT */
	if (reqmsgxtn->rtries <= 0) 
	{
		status = mio_geterrnum(mio);
	}
	else
	{
		status = mio_geterrnum(mio);
	}
MIO_DEBUG2 (mio, "TCP UNABLED TO CONNECT .. OR DISCONNECTED ... ---> %d -> %js\n", status, mio_errnum_to_errstr(status));

	if (MIO_LIKELY(reqmsgxtn->on_reply))
		reqmsgxtn->on_reply (dnc, reqmsg, status, MIO_NULL, 0);
	

	release_dns_msg (dnc, reqmsg);
}

static int switch_reqmsg_transport_to_tcp (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg)
{
	mio_t* mio = dnc->mio;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	dnc_sck_xtn_t* sckxtn;

	mio_dev_sck_make_t mkinfo;
	mio_dev_sck_connect_t cinfo;

	if (!dnc->tcp_sck)
	{
		MIO_MEMSET (&mkinfo, 0, MIO_SIZEOF(mkinfo));
		switch (mio_skad_family(&reqmsgxtn->servaddr))
		{
			case MIO_AF_INET:
				mkinfo.type = MIO_DEV_SCK_TCP4;
				break;

			case MIO_AF_INET6:
				mkinfo.type = MIO_DEV_SCK_TCP6;
				break;

			default:
				mio_seterrnum (mio, MIO_EINTERN);
				return -1;
		}

		mkinfo.on_write = on_tcp_write;
		mkinfo.on_read = on_tcp_read;
		mkinfo.on_connect = on_tcp_connect;
		mkinfo.on_disconnect = on_tcp_disconnect;
		dnc->tcp_sck = mio_dev_sck_make(mio, MIO_SIZEOF(*sckxtn), &mkinfo);
	}

	sckxtn = (dnc_sck_xtn_t*)mio_dev_sck_getxtn(dnc->tcp_sck);
	sckxtn->dnc = dnc;
	sckxtn->reqmsg = reqmsg;

	MIO_MEMSET (&cinfo, 0, MIO_SIZEOF(cinfo));
	cinfo.remoteaddr = reqmsgxtn->servaddr;
	cinfo.connect_tmout = reqmsgxtn->rtmout; /* TOOD: create a separate connect timeout or treate rtmout as a whole transaction time and calculate the remaining time from the transaction start, and use it */

	reqmsgxtn->rtries = 0; /* reset the number of tries back to 0 which means it's never sent over tcp */
	return mio_dev_sck_connect(dnc->tcp_sck, &cinfo);
}

/* ----------------------------------------------------------------------- */

static int on_udp_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_skad_t* srcaddr)
{
	mio_t* mio = dev->mio;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;
	mio_dns_pkt_t* pkt;
	mio_dns_msg_t* reqmsg;
	mio_uint16_t id;

	if (MIO_UNLIKELY(dlen < MIO_SIZEOF(*pkt))) 
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

/* TODO: improve performance of dns response matching*/
	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		mio_dns_pkt_t* reqpkt = mio_dns_msg_to_pkt(reqmsg);
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);

		if (dev == (mio_dev_sck_t*)reqmsgxtn->dev && pkt->id == reqpkt->id && mio_equal_skads(&reqmsgxtn->servaddr, srcaddr, 0))
		{
////////////////////////
pkt->tc = 1;
////////////////////////
			if (pkt->tc) /* truncated */
			{
				/* TODO: add an option for this behavior */
				/* TODO: switch to tc ... send the same request over tcp... */
				switch_reqmsg_transport_to_tcp (dnc, reqmsg);
				return 0;
			}

MIO_DEBUG1 (mio, "received dns response...id %d\n", id);
			if (MIO_LIKELY(reqmsgxtn->on_reply))
				reqmsgxtn->on_reply (dnc, reqmsg, MIO_ENOERR, data, dlen);

			release_dns_msg (dnc, reqmsg);
			return 0;
		}
		reqmsg = reqmsgxtn->next;
	}

	/* the response id didn't match the ID of pending requests - need to wait longer? */
MIO_DEBUG1 (mio, "unknown dns response... %d\n", pkt->id); /* TODO: add source info */
	return 0;
}

static void on_udp_reply_timeout (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dns_msg_t* reqmsg = (mio_dns_msg_t*)job->ctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(reqmsg);
	mio_dev_sck_t* dev = (mio_dev_sck_t*)msgxtn->dev;
	mio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)mio_dev_sck_getxtn(dev))->dnc;

	MIO_ASSERT (mio, msgxtn->rtmridx == MIO_TMRIDX_INVALID);
	MIO_ASSERT (mio, dev == dnc->udp_sck);

MIO_DEBUG0 (mio, "*** TIMEOUT ==> unable to receive dns response in time...\n");
	if (msgxtn->rtries < msgxtn->rmaxtries)
	{
		mio_ntime_t* tmout;

		tmout = MIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: MIO_NULL;
		if (mio_dev_sck_timedwrite(dev, mio_dns_msg_to_pkt(reqmsg), reqmsg->pktlen, tmout, reqmsg, &msgxtn->servaddr) <= -1)
		{
			if (MIO_LIKELY(msgxtn->on_reply))
				msgxtn->on_reply (dnc, reqmsg, MIO_ETMOUT, MIO_NULL, 0);

			release_dns_msg (dnc, reqmsg);
			return;
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


static int on_udp_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
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
		tmrjob.handler = on_udp_reply_timeout;
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

static void on_udp_connect (mio_dev_sck_t* dev)
{
}

static void on_udp_disconnect (mio_dev_sck_t* dev)
{
}

mio_svc_dnc_t* mio_svc_dnc_start (mio_t* mio, const mio_skad_t* serv_addr, const mio_skad_t* bind_addr, const mio_ntime_t* send_tmout, const mio_ntime_t* reply_tmout, mio_oow_t reply_tmout_max_tries)
{
	mio_svc_dnc_t* dnc = MIO_NULL;
	mio_dev_sck_make_t mkinfo;
	dnc_sck_xtn_t* xtn;

	dnc = (mio_svc_dnc_t*)mio_callocmem(mio, MIO_SIZEOF(*dnc));
	if (!dnc) goto oops;

	dnc->mio = mio;
	dnc->stop = mio_svc_dnc_stop;
	dnc->serv_addr = *serv_addr;
	dnc->send_tmout = *send_tmout;
	dnc->reply_tmout = *reply_tmout;
	dnc->reply_tmout_max_tries = reply_tmout_max_tries;

	MIO_MEMSET (&mkinfo, 0, MIO_SIZEOF(mkinfo));
	switch (mio_skad_family(serv_addr))
	{
		case MIO_AF_INET:
			mkinfo.type = MIO_DEV_SCK_UDP4;
			break;

		case MIO_AF_INET6:
			mkinfo.type = MIO_DEV_SCK_UDP6;
			break;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			goto oops;
	}
	mkinfo.on_write = on_udp_write;
	mkinfo.on_read = on_udp_read;
	mkinfo.on_connect = on_udp_connect;
	mkinfo.on_disconnect = on_udp_disconnect;
	dnc->udp_sck = mio_dev_sck_make(mio, MIO_SIZEOF(*xtn), &mkinfo);
	if (!dnc->udp_sck) goto oops;

	xtn = (dnc_sck_xtn_t*)mio_dev_sck_getxtn(dnc->udp_sck);
	xtn->dnc = dnc;

	if (bind_addr) /* TODO: get mio_dev_sck_bind_t? instead of bind_addr? */
	{
		mio_dev_sck_bind_t bi;
		MIO_MEMSET (&bi, 0, MIO_SIZEOF(bi));
		bi.localaddr = *bind_addr;
		if (mio_dev_sck_bind(dnc->udp_sck, &bi) <= -1) goto oops;
	}

	MIO_SVC_REGISTER (mio, (mio_svc_t*)dnc);
	return dnc;

oops:
	if (dnc)
	{
		if (dnc->udp_sck) mio_dev_sck_kill (dnc->udp_sck);
		mio_freemem (mio, dnc);
	}
	return MIO_NULL;
}

void mio_svc_dnc_stop (mio_svc_dnc_t* dnc)
{
	mio_t* mio = dnc->mio;
	if (dnc->udp_sck) mio_dev_sck_kill (dnc->udp_sck);
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
	msgxtn->servaddr = dnc->serv_addr;

/* TODO: optionally, override dnc->serv_addr and use the target address passed as a parameter */
	tmout = MIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: MIO_NULL;
	if (mio_dev_sck_timedwrite(dnc->udp_sck, mio_dns_msg_to_pkt(msg), msg->pktlen, tmout, msg, &msgxtn->servaddr) <= -1)
	{
		release_dns_msg (dnc, msg);
		return MIO_NULL;
	}
	
	return msg;
}

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
