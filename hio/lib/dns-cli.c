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

#include <hio-dns.h>
#include <hio-sck.h>
#include "hio-prv.h"

#include <netinet/in.h>

struct hio_svc_dns_t
{
	HIO_SVC_HEADER;
	/*HIO_DNS_SVC_HEADER;*/
};

struct hio_svc_dnc_t
{
	HIO_SVC_HEADER;
	/*HIO_DNS_SVC_HEADER;*/

	hio_dev_sck_t* udp_sck;
	hio_dev_sck_t* tcp_sck;
	hio_skad_t serv_addr;

	hio_ntime_t send_tmout; 
	hio_ntime_t reply_tmout; /* default reply timeout */

	/* For a question sent out, it may wait for a corresponding answer.
	 * if max_tries is greater than 0, sending and waiting is limited
	 * to this value over udp and to 1 over tcp. if max_tries is 0,
	 * it sends out the question but never waits for a response.
	 * For a non-question message sent out, it never waits for a response
	 * regardless of max_tries. */ 
	hio_oow_t max_tries; 

	hio_dns_cookie_t cookie;

	hio_oow_t seq;
	hio_dns_msg_t* pending_req;
};

struct dnc_sck_xtn_t
{
	hio_svc_dnc_t* dnc;

	struct
	{
		hio_uint8_t* ptr;
		hio_oow_t  len;
		hio_oow_t  capa;
	} rbuf; /* used by tcp socket */
};
typedef struct dnc_sck_xtn_t dnc_sck_xtn_t;

/* ----------------------------------------------------------------------- */

struct dnc_dns_msg_xtn_t
{
	hio_dev_sck_t* dev;
	hio_tmridx_t   rtmridx;
	hio_dns_msg_t* prev;
	hio_dns_msg_t* next;
	hio_skad_t     servaddr;
	hio_svc_dnc_on_done_t on_done;
	hio_ntime_t    wtmout;
	hio_ntime_t    rtmout;
	int            rmaxtries; /* maximum number of tries to receive a reply */
	int            rtries; /* number of tries made so far */
	int            pending;
};
typedef struct dnc_dns_msg_xtn_t dnc_dns_msg_xtn_t;

#if defined(HIO_HAVE_INLINE)
	static HIO_INLINE dnc_dns_msg_xtn_t* dnc_dns_msg_getxtn(hio_dns_msg_t* msg) { return (dnc_dns_msg_xtn_t*)((hio_uint8_t*)hio_dns_msg_to_pkt(msg) + msg->pktalilen); }
#else
#	define dnc_dns_msg_getxtn(msg) ((dnc_dns_msg_xtn_t*)((hio_uint8_t*)hio_dns_msg_to_pkt(msg) + msg->pktalilen))
#endif

static HIO_INLINE void chain_pending_dns_reqmsg (hio_svc_dnc_t* dnc, hio_dns_msg_t* msg)
{
	if (dnc->pending_req)
	{
		dnc_dns_msg_getxtn(dnc->pending_req)->prev = msg;
		dnc_dns_msg_getxtn(msg)->next = dnc->pending_req;
	}
	dnc->pending_req = msg;
	dnc_dns_msg_getxtn(msg)->pending = 1;
}

static HIO_INLINE void unchain_pending_dns_reqmsg (hio_svc_dnc_t* dnc, hio_dns_msg_t* msg)
{
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	if (msgxtn->next) dnc_dns_msg_getxtn(msgxtn->next)->prev = msgxtn->prev;
	if (msgxtn->prev) dnc_dns_msg_getxtn(msgxtn->prev)->next = msgxtn->next;
	else dnc->pending_req = msgxtn->next;
	dnc_dns_msg_getxtn(msg)->pending = 0;
}

static hio_dns_msg_t* make_dns_msg (hio_svc_dnc_t* dnc, hio_dns_bhdr_t* bdns, hio_dns_bqr_t* qr, hio_oow_t qr_count, hio_dns_brr_t* rr, hio_oow_t rr_count, hio_dns_bedns_t* edns, hio_svc_dnc_on_done_t on_done, hio_oow_t xtnsize)
{
	hio_dns_msg_t* msg;
	dnc_dns_msg_xtn_t* msgxtn;

	msg = hio_dns_make_msg(dnc->hio, bdns, qr, qr_count, rr, rr_count, edns, HIO_SIZEOF(*msgxtn) + xtnsize);
	if (HIO_UNLIKELY(!msg)) return HIO_NULL;

	if (bdns->id < 0)
	{
		hio_dns_pkt_t* pkt = hio_dns_msg_to_pkt(msg);
		pkt->id = hio_hton16(dnc->seq);
		dnc->seq++;
	}

	msgxtn = dnc_dns_msg_getxtn(msg);
	msgxtn->dev = dnc->udp_sck;
	msgxtn->rtmridx = HIO_TMRIDX_INVALID;
	msgxtn->on_done = on_done;
	msgxtn->wtmout = dnc->send_tmout;
	msgxtn->rtmout = dnc->reply_tmout;
	msgxtn->rmaxtries = dnc->max_tries; 
	msgxtn->rtries = 0;
	msgxtn->servaddr = dnc->serv_addr;

	return msg;
}

static void release_dns_msg (hio_svc_dnc_t* dnc, hio_dns_msg_t* msg)
{
	hio_t* hio = dnc->hio;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);

HIO_DEBUG1 (hio, "DNC - releasing dns message - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));

	if (msg == dnc->pending_req || msgxtn->next || msgxtn->prev)
	{
		/* it's chained in the pending request. unchain it */
		unchain_pending_dns_reqmsg (dnc, msg);
	}

	if (msgxtn->rtmridx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (hio, msgxtn->rtmridx);
		HIO_ASSERT (hio, msgxtn->rtmridx == HIO_TMRIDX_INVALID);
	}

/* TODO: add it to the free msg list instead of just freeing it. */
	hio_dns_free_msg (dnc->hio, msg);
}
/* ----------------------------------------------------------------------- */

static int handle_tcp_packet (hio_dev_sck_t* dev, hio_dns_pkt_t* pkt, hio_uint16_t pktlen)
{
	hio_t* hio = dev->hio;
	dnc_sck_xtn_t* sckxtn = hio_dev_sck_getxtn(dev);
	hio_svc_dnc_t* dnc = sckxtn->dnc;
	hio_uint16_t id;
	hio_dns_msg_t* reqmsg;

	if (!pkt->qr) 
	{
		HIO_DEBUG0 (hio, "DNC - dropping dns request received over tcp ...\n"); /* TODO: add source info */
		return 0; /* drop request. nothing to do */
	}

	id = hio_ntoh16(pkt->id);

HIO_DEBUG1 (hio, "DNC - got dns response over tcp - msgid:%d\n", id);

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		hio_dns_pkt_t* reqpkt = hio_dns_msg_to_pkt(reqmsg);
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);

		if (dev == (hio_dev_sck_t*)reqmsgxtn->dev && pkt->id == reqpkt->id)
		{
			if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, HIO_ENOERR, pkt, pktlen);
			release_dns_msg (dnc, reqmsg);
			return 0;
		}

		reqmsg = reqmsgxtn->next;
	}

HIO_DEBUG1 (hio, "DNC - unknown dns response over tcp... msgid:%d\n", id); /* TODO: add source info */
	return 0;
}

static HIO_INLINE int copy_data_to_sck_rbuf (hio_dev_sck_t* dev, const void* data, hio_uint16_t dlen)
{
	dnc_sck_xtn_t* sckxtn = hio_dev_sck_getxtn(dev);

	if (sckxtn->rbuf.capa - sckxtn->rbuf.len < dlen)
	{
		hio_uint16_t newcapa;
		hio_uint8_t* tmp;

		newcapa = sckxtn->rbuf.len + dlen;
		newcapa = HIO_ALIGN_POW2(newcapa, 512);
		tmp = hio_reallocmem(dev->hio, sckxtn->rbuf.ptr, newcapa);
		if (!tmp) return -1;

		sckxtn->rbuf.capa = newcapa;
		sckxtn->rbuf.ptr = tmp;
	}

	HIO_MEMCPY (&sckxtn->rbuf.ptr[sckxtn->rbuf.len], data, dlen);
	sckxtn->rbuf.len += dlen;
	return 0;
}

static int on_tcp_read (hio_dev_sck_t* dev, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_t* hio = dev->hio;
	dnc_sck_xtn_t* sckxtn = hio_dev_sck_getxtn(dev);
	hio_uint16_t pktlen;
	hio_uint8_t* dptr;
	hio_iolen_t rem;

	if (HIO_UNLIKELY(dlen <= -1))
	{
		HIO_DEBUG1 (hio, "DNC - dns tcp read error ....%js\n", hio_geterrmsg(hio)); /* TODO: add source packet */
		goto oops;
	}
	else if (HIO_UNLIKELY(dlen == 0))
	{
		HIO_DEBUG0 (hio, "DNC - dns tcp read error ...premature tcp socket end\n"); /* TODO: add source packet */
		goto oops;
	}

	dptr = (hio_uint8_t*)data;
	rem = dlen;
	do
	{
		if (HIO_UNLIKELY(sckxtn->rbuf.len == 1))
		{
			pktlen = ((hio_uint16_t)sckxtn->rbuf.ptr[0] << 8) | *(hio_uint8_t*)dptr;
			if (HIO_UNLIKELY((rem - 1) < pktlen)) goto incomplete_data;
			dptr += 1; rem -= 1; sckxtn->rbuf.len = 0;
			handle_tcp_packet (dev, (hio_dns_pkt_t*)dptr, pktlen);
			dptr += pktlen; rem -= pktlen;
		}
		else if (HIO_UNLIKELY(sckxtn->rbuf.len > 1))
		{
			hio_uint16_t cplen;

			pktlen = ((hio_uint16_t)sckxtn->rbuf.ptr[0] << 8) | sckxtn->rbuf.ptr[1];
			if (HIO_UNLIKELY(sckxtn->rbuf.len - 2 + rem < pktlen)) goto incomplete_data;

			cplen = pktlen - (sckxtn->rbuf.len - 2);
			if (copy_data_to_sck_rbuf(dev, dptr, cplen) <= -1) goto oops;

			dptr += cplen; rem -= cplen; sckxtn->rbuf.len = 0;
			handle_tcp_packet (dev, (hio_dns_pkt_t*)&sckxtn->rbuf.ptr[2], pktlen);
		}
		else
		{
			if (HIO_LIKELY(rem >= 2)) 
			{
				pktlen = ((hio_uint16_t)*(hio_uint8_t*)dptr << 8) | *((hio_uint8_t*)dptr + 1);
				dptr += 2; rem -= 2;
				if (HIO_UNLIKELY(rem < pktlen)) goto incomplete_data;
				handle_tcp_packet (dev, (hio_dns_pkt_t*)dptr, pktlen);
				dptr += pktlen; rem -= pktlen;
			}
			else
			{
			incomplete_data:
				if (copy_data_to_sck_rbuf(dev, dptr, rem) <= -1) goto oops;
				rem = 0;
			}
		}
	}
	while (rem > 0);

	return 0;

oops:
	hio_dev_sck_halt (dev);
	return 0;
}

static void on_tcp_reply_timeout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dns_msg_t* reqmsg = (hio_dns_msg_t*)job->ctx;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	hio_dev_sck_t* dev = reqmsgxtn->dev;
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;

	HIO_ASSERT (hio, reqmsgxtn->rtmridx == HIO_TMRIDX_INVALID);
	HIO_ASSERT (hio, dev == dnc->tcp_sck);

HIO_DEBUG1 (hio, "DNC - unable to receive dns response in time over TCP - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(reqmsg)->id));

	if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, HIO_ETMOUT, HIO_NULL, 0);
	release_dns_msg (dnc, reqmsg);
}

static int on_tcp_write (hio_dev_sck_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dns_msg_t* msg = (hio_dns_msg_t*)wrctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_errnum_t status;

	if (wrlen <= -1)
	{
		/* send failure */
		status = hio_geterrnum(hio);
		goto finalize;
	}
	else if (hio_dns_msg_to_pkt(msg)->qr == 0 && msgxtn->rmaxtries > 0)
	{
		/* question. schedule to wait for response */
		hio_tmrjob_t tmrjob;

		HIO_DEBUG1 (hio, "DNC - sent dns question over tcp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));

		HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
		tmrjob.ctx = msg;
		hio_gettime (hio, &tmrjob.when);
		HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &msgxtn->rtmout);
		tmrjob.handler = on_tcp_reply_timeout;
		tmrjob.idxptr = &msgxtn->rtmridx;
		msgxtn->rtmridx = hio_instmrjob(hio, &tmrjob);
		if (msgxtn->rtmridx == HIO_TMRIDX_INVALID)
		{
			/* call the callback to indicate this operation failure in the middle of transaction */
			status = hio_geterrnum(hio);
			HIO_DEBUG1 (hio, "DNC - unable to schedule tcp timeout - msgid: %d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));
			goto finalize;
		}

		HIO_ASSERT (hio, msgxtn->pending != 0);
	}
	else
	{
		/* no error. successfuly sent a message. no reply is expected */
		HIO_DEBUG1 (hio, "DNC - sent dns message over tcp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));
		status = HIO_ENOERR;
		goto finalize;
	}

	return 0;

finalize:
	if (HIO_LIKELY(msgxtn->on_done)) msgxtn->on_done (dnc, msg, status, HIO_NULL, 0);
	release_dns_msg (dnc, msg);
	return 0;
}

static int write_dns_msg_over_tcp (hio_dev_sck_t* dev, hio_dns_msg_t* msg)
{
	hio_t* hio = dev->hio;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	hio_uint16_t pktlen;
	hio_iovec_t iov[2];

	HIO_DEBUG1 (hio, "DNC - sending dns message over tcp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));

	pktlen = hio_hton16(msg->pktlen);

	HIO_ASSERT (hio, msgxtn->rtries == 0);
	msgxtn->rtries = 1;

	/* TODO: Is it better to create 2 byte space when sending UDP and use it here instead of iov? */
	iov[0].iov_ptr = &pktlen;
	iov[0].iov_len = HIO_SIZEOF(pktlen);
	iov[1].iov_ptr = hio_dns_msg_to_pkt(msg);
	iov[1].iov_len = msg->pktlen;
	return hio_dev_sck_timedwritev(dev, iov, HIO_COUNTOF(iov), &msgxtn->rtmout, msg, HIO_NULL);
}

static void on_tcp_connect (hio_dev_sck_t* dev)
{
	hio_t* hio = dev->hio;
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_dns_msg_t* reqmsg;

	HIO_ASSERT (hio, dev == dnc->tcp_sck);

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
		hio_dns_msg_t* nextreqmsg = reqmsgxtn->next;

		if (reqmsgxtn->dev == dev && reqmsgxtn->rtries == 0) 
		{
			if (write_dns_msg_over_tcp(dev, reqmsg) <= -1)
			{
				if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, hio_geterrnum(hio), HIO_NULL, 0);
				release_dns_msg (dnc, reqmsg);
			}
		}
		reqmsg = nextreqmsg;
	}
}

static void on_tcp_disconnect (hio_dev_sck_t* dev)
{
	hio_t* hio = dev->hio;
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_dns_msg_t* reqmsg;
	int status;

	/* UNABLE TO CONNECT or CONNECT TIMED OUT */
	status = hio_geterrnum(hio);

	if (status == HIO_ENOERR)
	{
		HIO_DEBUG0 (hio, "DNC - TCP DISCONNECTED\n");
	}
	else
	{
		HIO_DEBUG2 (hio, "DNC - TCP UNABLE TO CONNECT  %d -> %js\n", status, hio_errnum_to_errstr(status));
	}

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
		hio_dns_msg_t* nextreqmsg = reqmsgxtn->next;

		if (reqmsgxtn->dev == dev)
		{
			if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, HIO_ENORSP, HIO_NULL, 0);
			release_dns_msg (dnc, reqmsg);
		}

		reqmsg = nextreqmsg;
	}

	/* let's forget about the tcp socket */
	dnc->tcp_sck = HIO_NULL; 
}

static int switch_reqmsg_transport_to_tcp (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg)
{
	hio_t* hio = dnc->hio;
	dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
	dnc_sck_xtn_t* sckxtn;

	hio_dev_sck_make_t mkinfo;
	hio_dev_sck_connect_t cinfo;

/* TODO: more reliable way to check if connection is ok.
 * even if tcp_sck is not null, the connection could have been torn down... */
	if (!dnc->tcp_sck)
	{
		HIO_MEMSET (&mkinfo, 0, HIO_SIZEOF(mkinfo));
		switch (hio_skad_family(&reqmsgxtn->servaddr))
		{
			case HIO_AF_INET:
				mkinfo.type = HIO_DEV_SCK_TCP4;
				break;

			case HIO_AF_INET6:
				mkinfo.type = HIO_DEV_SCK_TCP6;
				break;

			default:
				hio_seterrnum (hio, HIO_EINTERN);
				return -1;
		}

		mkinfo.on_write = on_tcp_write;
		mkinfo.on_read = on_tcp_read;
		mkinfo.on_connect = on_tcp_connect;
		mkinfo.on_disconnect = on_tcp_disconnect;
		dnc->tcp_sck = hio_dev_sck_make(hio, HIO_SIZEOF(*sckxtn), &mkinfo);
		if (!dnc->tcp_sck) return -1;

		sckxtn = (dnc_sck_xtn_t*)hio_dev_sck_getxtn(dnc->tcp_sck);
		sckxtn->dnc = dnc;

		HIO_MEMSET (&cinfo, 0, HIO_SIZEOF(cinfo));
		cinfo.remoteaddr = reqmsgxtn->servaddr;
		cinfo.connect_tmout = reqmsgxtn->rtmout; /* TOOD: create a separate connect timeout or treate rtmout as a whole transaction time and calculate the remaining time from the transaction start, and use it */

		if (hio_dev_sck_connect(dnc->tcp_sck, &cinfo) <= -1) 
		{
			hio_dev_sck_kill (dnc->tcp_sck);
			dnc->tcp_sck = HIO_NULL;
			return -1; /* the connect request hasn't been honored. */
		}
	}
	
	/* switch the belonging device to the tcp socket since the connect request has been acknowledged. */
	HIO_ASSERT (hio, reqmsgxtn->rtmridx == HIO_TMRIDX_INVALID); /* ensure no timer job scheduled at this moment */
	reqmsgxtn->dev = dnc->tcp_sck;
	reqmsgxtn->rtries = 0;
	if (!reqmsgxtn->pending && hio_dns_msg_to_pkt(reqmsg)->qr == 0) chain_pending_dns_reqmsg (dnc, reqmsg);

HIO_DEBUG6 (hio, "DNC - switched transport to tcp - msgid:%d %p %p %p %p %p\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(reqmsg)->id), reqmsg, reqmsgxtn, reqmsgxtn->dev, dnc->udp_sck, dnc->tcp_sck);

	if (HIO_DEV_SCK_GET_PROGRESS(dnc->tcp_sck) & HIO_DEV_SCK_CONNECTED)
	{
		if (write_dns_msg_over_tcp(reqmsgxtn->dev, reqmsg) <= -1)
		{
			/* the caller must not use reqmsg from now because it's freed here */
			if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, hio_geterrnum(hio), HIO_NULL, 0);
			release_dns_msg (dnc, reqmsg);
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int on_udp_read (hio_dev_sck_t* dev, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_t* hio = dev->hio;
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_dns_pkt_t* pkt;
	hio_dns_msg_t* reqmsg;
	hio_uint16_t id;

	if (HIO_UNLIKELY(dlen <= -1))
	{
		HIO_DEBUG1 (hio, "DNC - dns read error ....%js\n", hio_geterrmsg(hio)); /* TODO: add source packet */
		return 0;
	}

	if (HIO_UNLIKELY(dlen < HIO_SIZEOF(*pkt))) 
	{
		HIO_DEBUG0 (hio, "DNC - dns packet too small from ....\n"); /* TODO: add source packet */
		return 0; /* drop */
	}
	pkt = (hio_dns_pkt_t*)data;
	if (!pkt->qr) 
	{
		HIO_DEBUG0 (hio, "DNC - dropping dns request received ...\n"); /* TODO: add source info */
		return 0; /* drop request */
	}

	id = hio_ntoh16(pkt->id);

	/* if id doesn't match one of the pending requests sent,  drop it */

/* TODO: improve performance of dns response matching*/
	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		hio_dns_pkt_t* reqpkt = hio_dns_msg_to_pkt(reqmsg);
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);

		if (reqmsgxtn->dev == dev && pkt->id == reqpkt->id && hio_equal_skads(&reqmsgxtn->servaddr, srcaddr, 0))
		{
			if (reqmsgxtn->rtmridx != HIO_TMRIDX_INVALID)
			{
				/* unschedule a timer job if any */
				hio_deltmrjob (hio, reqmsgxtn->rtmridx);
				HIO_ASSERT (hio, reqmsgxtn->rtmridx == HIO_TMRIDX_INVALID);
			}

////////////////////////
// for simple testing without actual truncated dns response
//pkt->tc = 1;
////////////////////////
			if (HIO_UNLIKELY(pkt->tc))
			{
				/* TODO: add an option for this behavior */
				if (switch_reqmsg_transport_to_tcp(dnc, reqmsg) >= 0) return 0;
				/* TODO: add an option to call an error callback with TRUNCATION error code instead of fallback to received UDP truncated message */
			}

HIO_DEBUG1 (hio, "DNC - received dns response over udp for msgid:%d\n", id);
			if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, HIO_ENOERR, data, dlen);
			release_dns_msg (dnc, reqmsg);
			return 0;
		}

		reqmsg = reqmsgxtn->next;
	}

	/* the response id didn't match the ID of pending requests - need to wait longer? */
HIO_DEBUG1 (hio, "DNC - unknown dns response over udp... msgid:%d\n", id); /* TODO: add source info */
	return 0;
}

static void on_udp_reply_timeout (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dns_msg_t* reqmsg = (hio_dns_msg_t*)job->ctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(reqmsg);
	hio_dev_sck_t* dev = msgxtn->dev;
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_errnum_t status = HIO_ETMOUT;

	HIO_ASSERT (hio, msgxtn->rtmridx == HIO_TMRIDX_INVALID);
	HIO_ASSERT (hio, dev == dnc->udp_sck);

HIO_DEBUG1 (hio, "DNC - unable to receive dns response in time over udp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(reqmsg)->id));
	if (msgxtn->rtries < msgxtn->rmaxtries)
	{
		hio_ntime_t* tmout;

		tmout = HIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: HIO_NULL;
HIO_DEBUG1 (hio, "DNC - sending dns question again over udp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(reqmsg)->id));
		if (hio_dev_sck_timedwrite(dev, hio_dns_msg_to_pkt(reqmsg), reqmsg->pktlen, tmout, reqmsg, &msgxtn->servaddr) >= 0) return; /* resent */

		/* retry failed */
		status = hio_geterrnum(hio);
	}

	if (HIO_LIKELY(msgxtn->on_done)) msgxtn->on_done (dnc, reqmsg, status, HIO_NULL, 0);
	release_dns_msg (dnc, reqmsg);
}

static int on_udp_write (hio_dev_sck_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	hio_t* hio = dev->hio;
	hio_dns_msg_t* msg = (hio_dns_msg_t*)wrctx;
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_errnum_t status;

	HIO_ASSERT (hio, dev == (hio_dev_sck_t*)msgxtn->dev);

	if (wrlen <= -1)
	{
		/* write has timed out or an error has occurred */
		status = hio_geterrnum(hio);
		goto finalize;
	}
	else if (hio_dns_msg_to_pkt(msg)->qr == 0 && msgxtn->rmaxtries > 0)
	{
		/* question. schedule to wait for response */
		hio_tmrjob_t tmrjob;

		HIO_DEBUG1 (hio, "DNC - sent dns question over udp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));
		HIO_MEMSET (&tmrjob, 0, HIO_SIZEOF(tmrjob));
		tmrjob.ctx = msg;
		hio_gettime (hio, &tmrjob.when);
		HIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &msgxtn->rtmout);
		tmrjob.handler = on_udp_reply_timeout;
		tmrjob.idxptr = &msgxtn->rtmridx;
		msgxtn->rtmridx = hio_instmrjob(hio, &tmrjob);
		if (msgxtn->rtmridx == HIO_TMRIDX_INVALID)
		{
			/* call the callback to indicate this operation failure in the middle of transaction */
			status = hio_geterrnum(hio);
			HIO_DEBUG1 (hio, "DNC - unable to schedule udp timeout - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));
			goto finalize;
		}

		if (msgxtn->rtries == 0)
		{
			/* this is the first wait */
			/* TODO: improve performance. hashing by id? */
			/* chain it to the peing request list */
			chain_pending_dns_reqmsg (dnc, msg);
		}
		msgxtn->rtries++;
	}
	else
	{
		HIO_DEBUG1 (hio, "DNC - sent dns message over udp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));
		/* sent an answer. however this may be a question if msgxtn->rmaxtries is 0. */
		status = HIO_ENOERR;
		goto finalize;
	}

	return 0;

finalize:
	if (HIO_LIKELY(msgxtn->on_done)) msgxtn->on_done (dnc, msg, status, HIO_NULL, 0);
	release_dns_msg (dnc, msg);
	return 0;
}

static void on_udp_connect (hio_dev_sck_t* dev)
{
}

static void on_udp_disconnect (hio_dev_sck_t* dev)
{
	/*hio_t* hio = dev->hio;*/
	hio_svc_dnc_t* dnc = ((dnc_sck_xtn_t*)hio_dev_sck_getxtn(dev))->dnc;
	hio_dns_msg_t* reqmsg;

	reqmsg = dnc->pending_req;
	while (reqmsg)
	{
		dnc_dns_msg_xtn_t* reqmsgxtn = dnc_dns_msg_getxtn(reqmsg);
		hio_dns_msg_t* nextreqmsg = reqmsgxtn->next;

		if (reqmsgxtn->dev == dev)
		{
			if (HIO_LIKELY(reqmsgxtn->on_done)) reqmsgxtn->on_done (dnc, reqmsg, HIO_ENORSP, HIO_NULL, 0);
			release_dns_msg (dnc, reqmsg);
		}

		reqmsg = nextreqmsg;
	}
}

hio_svc_dnc_t* hio_svc_dnc_start (hio_t* hio, const hio_skad_t* serv_addr, const hio_skad_t* bind_addr, const hio_ntime_t* send_tmout, const hio_ntime_t* reply_tmout, hio_oow_t max_tries)
{
	hio_svc_dnc_t* dnc = HIO_NULL;
	hio_dev_sck_make_t mkinfo;
	dnc_sck_xtn_t* sckxtn;
	hio_ntime_t now;

	dnc = (hio_svc_dnc_t*)hio_callocmem(hio, HIO_SIZEOF(*dnc));
	if (HIO_UNLIKELY(!dnc)) goto oops;

	dnc->hio = hio;
	dnc->svc_stop = hio_svc_dnc_stop;
	dnc->serv_addr = *serv_addr;
	dnc->send_tmout = *send_tmout;
	dnc->reply_tmout = *reply_tmout;
	dnc->max_tries = max_tries;

	HIO_MEMSET (&mkinfo, 0, HIO_SIZEOF(mkinfo));
	switch (hio_skad_family(serv_addr))
	{
		case HIO_AF_INET:
			mkinfo.type = HIO_DEV_SCK_UDP4;
			break;

		case HIO_AF_INET6:
			mkinfo.type = HIO_DEV_SCK_UDP6;
			break;

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			goto oops;
	}
	mkinfo.on_write = on_udp_write;
	mkinfo.on_read = on_udp_read;
	mkinfo.on_connect = on_udp_connect;
	mkinfo.on_disconnect = on_udp_disconnect;
	dnc->udp_sck = hio_dev_sck_make(hio, HIO_SIZEOF(*sckxtn), &mkinfo);
	if (!dnc->udp_sck) goto oops;

	sckxtn = (dnc_sck_xtn_t*)hio_dev_sck_getxtn(dnc->udp_sck);
	sckxtn->dnc = dnc;

	if (bind_addr) /* TODO: get hio_dev_sck_bind_t? instead of bind_addr? */
	{
		hio_dev_sck_bind_t bi;
		HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
		bi.localaddr = *bind_addr;
		if (hio_dev_sck_bind(dnc->udp_sck, &bi) <= -1) goto oops;
	}


	/* initialize the dns cookie key */
	hio_gettime (hio, &now);
	HIO_MEMCPY (&dnc->cookie.key[0], &now.sec, (HIO_SIZEOF(now.sec) < 8? HIO_SIZEOF(now.sec): 8));
	HIO_MEMCPY (&dnc->cookie.key[8], &now.nsec, (HIO_SIZEOF(now.nsec) < 8? HIO_SIZEOF(now.nsec): 8));

	HIO_SVCL_APPEND_SVC (&hio->actsvc, (hio_svc_t*)dnc);
	HIO_DEBUG1 (hio, "DNC - STARTED SERVICE %p\n", dnc);
	return dnc;

oops:
	if (dnc)
	{
		if (dnc->udp_sck) hio_dev_sck_kill (dnc->udp_sck);
		hio_freemem (hio, dnc);
	}
	return HIO_NULL;
}

void hio_svc_dnc_stop (hio_svc_dnc_t* dnc)
{
	hio_t* hio = dnc->hio;

	HIO_DEBUG1 (hio, "DNC - STOPPING SERVICE %p\n", dnc);
	if (dnc->udp_sck) hio_dev_sck_kill (dnc->udp_sck);
	if (dnc->tcp_sck) hio_dev_sck_kill (dnc->tcp_sck);
	while (dnc->pending_req) release_dns_msg (dnc, dnc->pending_req);
	HIO_SVCL_UNLINK_SVC (dnc);
	hio_freemem (hio, dnc);
}


static HIO_INLINE int send_dns_msg (hio_svc_dnc_t* dnc, hio_dns_msg_t* msg, int send_flags)
{
	dnc_dns_msg_xtn_t* msgxtn = dnc_dns_msg_getxtn(msg);
	hio_ntime_t* tmout;

	if ((send_flags & HIO_SVC_DNC_SEND_FLAG_PREFER_TCP) && switch_reqmsg_transport_to_tcp(dnc, msg) >= 0) return 0;

	HIO_DEBUG1 (dnc->hio, "DNC - sending dns message over udp - msgid:%d\n", (int)hio_ntoh16(hio_dns_msg_to_pkt(msg)->id));

	tmout = HIO_IS_POS_NTIME(&msgxtn->wtmout)? &msgxtn->wtmout: HIO_NULL;
/* TODO: optionally, override dnc->serv_addr and use the target address passed as a parameter */
	return hio_dev_sck_timedwrite(dnc->udp_sck, hio_dns_msg_to_pkt(msg), msg->pktlen, tmout, msg, &msgxtn->servaddr);
}

hio_dns_msg_t* hio_svc_dnc_sendmsg (hio_svc_dnc_t* dnc, hio_dns_bhdr_t* bdns, hio_dns_bqr_t* qr, hio_oow_t qr_count, hio_dns_brr_t* rr, hio_oow_t rr_count, hio_dns_bedns_t* edns, int send_flags, hio_svc_dnc_on_done_t on_done, hio_oow_t xtnsize)
{
	/* send a request or a response */
	hio_dns_msg_t* msg;

	msg = make_dns_msg(dnc, bdns, qr, qr_count, rr, rr_count, edns, on_done, xtnsize);
	if (!msg) return HIO_NULL;

	if (send_dns_msg(dnc, msg, send_flags) <= -1)
	{
		release_dns_msg (dnc, msg);
		return HIO_NULL;
	}

	return msg;
}

hio_dns_msg_t* hio_svc_dnc_sendreq (hio_svc_dnc_t* dnc, hio_dns_bhdr_t* bdns, hio_dns_bqr_t* qr, hio_dns_bedns_t* edns, int send_flags, hio_svc_dnc_on_done_t on_done, hio_oow_t xtnsize)
{
	/* send a request without resource records */
	if (bdns->rcode != HIO_DNS_RCODE_NOERROR)
	{
		hio_seterrnum (dnc->hio, HIO_EINVAL);
		return HIO_NULL;
	}

	return hio_svc_dnc_sendmsg(dnc, bdns, qr, 1, HIO_NULL, 0, edns, send_flags, on_done, xtnsize);
}

/* ----------------------------------------------------------------------- */


struct dnc_dns_msg_resolve_xtn_t
{
	hio_dns_rrt_t qtype;
	int flags;
	hio_uint8_t client_cookie[HIO_DNS_COOKIE_CLIENT_LEN];
	hio_svc_dnc_on_resolve_t on_resolve;
};
typedef struct dnc_dns_msg_resolve_xtn_t dnc_dns_msg_resolve_xtn_t;

#if defined(HIO_HAVE_INLINE)
	static HIO_INLINE dnc_dns_msg_resolve_xtn_t* dnc_dns_msg_resolve_getxtn(hio_dns_msg_t* msg) { return ((dnc_dns_msg_resolve_xtn_t*)((hio_uint8_t*)dnc_dns_msg_getxtn(msg) + HIO_SIZEOF(dnc_dns_msg_xtn_t))); }
#else
#	define dnc_dns_msg_resolve_getxtn(msg) ((dnc_dns_msg_resolve_xtn_t*)((hio_uint8_t*)dnc_dns_msg_getxtn(msg) + HIO_SIZEOF(dnc_dns_msg_xtn_t)))
#endif

static void on_dnc_resolve (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_errnum_t status, const void* data, hio_oow_t dlen)
{
	hio_t* hio = hio_svc_dnc_gethio(dnc);
	hio_dns_pkt_info_t* pi = HIO_NULL;
	dnc_dns_msg_resolve_xtn_t* resolxtn = dnc_dns_msg_resolve_getxtn(reqmsg);

	if (data)
	{
		hio_uint32_t i;

		HIO_ASSERT (hio, status == HIO_ENOERR);

		pi = hio_dns_make_pkt_info(hio, data, dlen);
		if (!pi)
		{
			status = hio_geterrnum(hio);
			goto no_data;
		}

		if (resolxtn->flags & HIO_SVC_DNC_RESOLVE_FLAG_COOKIE)
		{
			if (pi->edns.cookie.server_len > 0)
			{
				/* remember the received server cookie to use it with other new requests */
				HIO_MEMCPY (dnc->cookie.data.server, pi->edns.cookie.data.server, pi->edns.cookie.server_len);
				dnc->cookie.server_len = pi->edns.cookie.server_len;
			}
		}

		if (!(resolxtn->flags & HIO_SVC_DNC_RESOLVE_FLAG_BRIEF))
		{
			/* the full reply packet is requested. */
			if (resolxtn->on_resolve) resolxtn->on_resolve (dnc, reqmsg, status, pi, 0);
			goto done;
		}

		if (pi->hdr.rcode != HIO_DNS_RCODE_NOERROR) 
		{
			status = HIO_EINVAL;
			goto no_data;
		}

		if (pi->ancount < 0) goto no_data;

		/* in the brief mode, we inspect the answer section only */
		if (resolxtn->qtype == HIO_DNS_RRT_Q_ANY)
		{
			/* return A or AAAA for ANY in the brief mode */
			for (i = 0; i < pi->ancount; i++)
			{
				if (pi->rr.an[i].rrtype == HIO_DNS_RRT_A || pi->rr.an[i].rrtype == HIO_DNS_RRT_AAAA)
				{
				match_found:
					if (resolxtn->on_resolve) resolxtn->on_resolve (dnc, reqmsg, status, &pi->rr.an[i], HIO_SIZEOF(pi->rr.an[i]));
					goto done;
				}
			}
		}

		for (i = 0; i < pi->ancount; i++)
		{
			/* it is a bit time taking to retreive the query type from the packet
			 * bundled in reqmsg as it requires parsing of the packet. let me use
			 * the query type i stored in the extension space. */
			switch (resolxtn->qtype)
			{
				case HIO_DNS_RRT_Q_ANY: 
				case HIO_DNS_RRT_Q_AFXR: /* AFXR doesn't make sense in the brief mode. just treat it like ANY */
					/* no A or AAAA found. so give the first entry in the answer */
					goto match_found;

				case HIO_DNS_RRT_Q_MAILA:
					/* if you want to get the full RRs, don't use the brief mode. */
					if (pi->rr.an[i].rrtype == HIO_DNS_RRT_MD || pi->rr.an[i].rrtype == HIO_DNS_RRT_MF) goto match_found;
					break;

				case HIO_DNS_RRT_Q_MAILB:
					/* if you want to get the full RRs, don't use the brief mode. */
					if (pi->rr.an[i].rrtype == HIO_DNS_RRT_MB || pi->rr.an[i].rrtype == HIO_DNS_RRT_MG ||
					    pi->rr.an[i].rrtype == HIO_DNS_RRT_MR || pi->rr.an[i].rrtype == HIO_DNS_RRT_MINFO) goto match_found;
					break;

				default:
					if (pi->rr.an[i].rrtype == resolxtn->qtype) goto match_found;
					break;
			}
		}
		goto no_data;
	}
	else
	{
	no_data:
		if (resolxtn->on_resolve) resolxtn->on_resolve (dnc, reqmsg, status, HIO_NULL, 0);
	}

done:
	if (pi) hio_dns_free_pkt_info(hio_svc_dnc_gethio(dnc), pi);
}

hio_dns_msg_t* hio_svc_dnc_resolve (hio_svc_dnc_t* dnc, const hio_bch_t* qname, hio_dns_rrt_t qtype, int resolve_flags, hio_svc_dnc_on_resolve_t on_resolve, hio_oow_t xtnsize)
{
	static hio_dns_bhdr_t qhdr =
	{
		-1, /* id */
		0,  /* qr */
		HIO_DNS_OPCODE_QUERY, /* opcode */
		0, /* aa */
		0, /* tc */
		1, /* rd */
		0, /* ra */
		0, /* ad */
		0, /* cd */
		HIO_DNS_RCODE_NOERROR /* rcode */
	};

	hio_dns_bedns_t qedns =
	{
		4096, /* uplen */

		0,    /* edns version */
		0,    /* dnssec ok */

		0,    /* number of edns options */
		HIO_NULL
	};

	hio_dns_beopt_t beopt_cookie;

	hio_dns_bqr_t qr;
	hio_dns_msg_t* reqmsg;
	dnc_dns_msg_resolve_xtn_t* resolxtn;

	qr.qname = (hio_bch_t*)qname;
	qr.qtype = qtype;
	qr.qclass = HIO_DNS_RRC_IN;

	if (resolve_flags & HIO_SVC_DNC_RESOLVE_FLAG_COOKIE)
	{
		beopt_cookie.code = HIO_DNS_EOPT_COOKIE;
		beopt_cookie.dptr = &dnc->cookie.data;

		beopt_cookie.dlen = HIO_DNS_COOKIE_CLIENT_LEN; 
		if (dnc->cookie.server_len > 0) beopt_cookie.dlen += dnc->cookie.server_len;

		/* compute the client cookie */
		HIO_STATIC_ASSERT (HIO_SIZEOF(dnc->cookie.data.client) == HIO_DNS_COOKIE_CLIENT_LEN);
		hio_sip_hash_24 (dnc->cookie.key, &dnc->serv_addr, HIO_SIZEOF(dnc->serv_addr), dnc->cookie.data.client);

		qedns.beonum = 1;
		qedns.beoptr = &beopt_cookie;
	}

	if (resolve_flags & HIO_SVC_DNC_RESOLVE_FLAG_DNSSEC)
	{
		qedns.dnssecok = 1;
	}

	reqmsg = make_dns_msg(dnc, &qhdr, &qr, 1, HIO_NULL, 0, &qedns, on_dnc_resolve, HIO_SIZEOF(*resolxtn) + xtnsize);
	if (reqmsg)
	{
		int send_flags;

#if 0
		if ((resolve_flags & HIO_SVC_DNC_RESOLVE_FLAG_COOKIE) && dnc->cookie.server_len == 0)
		{
			/* Exclude the server cookie from the packet when the server cookie is not available.
			 *
			 * ASSUMPTIONS:
			 *  the eopt entries are at the back of the packet.
			 *  only 1 eopt entry(HIO_DNS_EOPT_COOKIE) has been added. 
			 * 
			 * manipulate the data length of the EDNS0 RR and COOKIE option 
			 * as if the server cookie data has not been added.
			 */
			hio_dns_rrtr_t* edns_rrtr;
			hio_dns_eopt_t* eopt;

			edns_rrtr = (hio_dns_rrtr_t*)((hio_uint8_t*)hio_dns_msg_to_pkt(reqmsg) + reqmsg->ednsrrtroff);
			reqmsg->pktlen -= HIO_DNS_COOKIE_SERVER_MAX_LEN;

			HIO_ASSERT (dnc->hio, edns_rrtr->rrtype == HIO_CONST_HTON16(HIO_DNS_RRT_OPT));
			HIO_ASSERT (dnc->hio, edns_rrtr->dlen == HIO_CONST_HTON16(HIO_SIZEOF(hio_dns_eopt_t) + HIO_DNS_COOKIE_MAX_LEN));
			edns_rrtr->dlen = HIO_CONST_HTON16(HIO_SIZEOF(hio_dns_eopt_t) + HIO_DNS_COOKIE_CLIENT_LEN);

			eopt = (hio_dns_eopt_t*)(edns_rrtr + 1);
			HIO_ASSERT (dnc->hio, eopt->dlen == HIO_CONST_HTON16(HIO_DNS_COOKIE_MAX_LEN));
			eopt->dlen = HIO_CONST_HTON16(HIO_DNS_COOKIE_CLIENT_LEN);
		}
#endif

		resolxtn = dnc_dns_msg_resolve_getxtn(reqmsg);
		resolxtn->on_resolve = on_resolve;
		resolxtn->qtype = qtype;
		resolxtn->flags = resolve_flags;
		/* store in the extension area the client cookie set in the packet */
		HIO_MEMCPY (resolxtn->client_cookie, dnc->cookie.data.client, HIO_DNS_COOKIE_CLIENT_LEN);

		send_flags = (resolve_flags & HIO_SVC_DNC_SEND_FLAG_ALL);
		if (HIO_UNLIKELY(qtype == HIO_DNS_RRT_Q_AFXR)) send_flags |= HIO_SVC_DNC_SEND_FLAG_PREFER_TCP;
		if (send_dns_msg(dnc, reqmsg, send_flags) <= -1)
		{
			release_dns_msg (dnc, reqmsg);
			return HIO_NULL;
		}
	}

	return reqmsg;
}

int hio_svc_dnc_checkclientcookie (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_dns_pkt_info_t* respi)
{
	hio_uint8_t xb[HIO_DNS_COOKIE_CLIENT_LEN];
	hio_uint8_t* x;

	x = hio_dns_find_client_cookie_in_msg(reqmsg, &xb);
	if (x)
	{
		/* there is a client cookie in the request. */
		if (respi->edns.cookie.client_len > 0)
		{
			HIO_ASSERT (dnc->hio, respi->edns.cookie.client_len == HIO_DNS_COOKIE_CLIENT_LEN);
			return HIO_MEMCMP(x, respi->edns.cookie.data.client, HIO_DNS_COOKIE_CLIENT_LEN) == 0; /* 1 if ok, 0 if not */
		}
		else
		{
			/* no client cookie in the response - the server doesn't support cookie? */
			return -1;
		}
	}

	return 2; /* ok because the request doesn't include the client cookie */
}

/* TODO: upon startup, read /etc/hosts. setup inotify or find a way to detect file changes..
 *       in resolve, add an option to use entries from /etc/hosts */

/* TODO: afxr client ... */

/* TODO: trace function to do its own recursive resolution?... 
hio_dns_msg_t* hio_svc_dnc_trace (hio_svc_dnc_t* dnc, const hio_bch_t* qname, hio_dns_rrt_t qtype, int resolve_flags, hio_svc_dnc_on_resolve_t on_resolve, hio_oow_t xtnsize)
{
}
*/
