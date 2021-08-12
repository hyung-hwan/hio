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

#include <hio-mar.h>
#include "hio-prv.h"

#if 0
#include <mariadb/mysql.h>
#include <mariadb/errmsg.h>
#else
#include <mysql.h>
#include <errmsg.h>
#endif

typedef struct sess_t sess_t;
typedef struct sess_qry_t sess_qry_t;

struct hio_svc_marc_t
{
	HIO_SVC_HEADER;

	int stopping;
	int tmout_set;

	const hio_bch_t* default_group;

	hio_svc_marc_connect_t ci;
	hio_svc_marc_tmout_t tmout;

	MYSQL* edev;

	struct
	{
		sess_t* ptr;
		hio_oow_t capa;
	} sess;

	hio_oow_t autoi;
	hio_oow_t autoi2;
};

struct sess_qry_t
{
	hio_bch_t*   qptr;
	hio_oow_t    qlen;
	void*        qctx;
	unsigned int sent: 1;
	unsigned int need_fetch: 1;

	hio_svc_marc_on_result_t on_result;
	sess_qry_t*  sq_next;
};

struct sess_t
{
	hio_oow_t sid;
	hio_svc_marc_t* svc;
	hio_dev_mar_t* dev;
	int connected;

	sess_qry_t* q_head;
	sess_qry_t* q_tail;
};

typedef struct dev_xtn_t dev_xtn_t;

struct dev_xtn_t
{
	hio_oow_t sid;
	hio_svc_marc_t* svc;
};

#define INVALID_SID HIO_TYPE_MAX(hio_oow_t)

hio_svc_marc_t* hio_svc_marc_start (hio_t* hio, const hio_svc_marc_connect_t* ci, const hio_svc_marc_tmout_t* tmout, const hio_bch_t* default_group)
{
	hio_svc_marc_t* marc = HIO_NULL;

	marc = (hio_svc_marc_t*)hio_callocmem(hio, HIO_SIZEOF(*marc));
	if (HIO_UNLIKELY(!marc)) goto oops;

	marc->edev = mysql_init(HIO_NULL);
	if (HIO_UNLIKELY(!marc->edev)) goto oops;

	marc->hio = hio;
	marc->svc_stop = hio_svc_marc_stop;
	marc->ci = *ci;
	if (tmout) 
	{
		marc->tmout = *tmout;
		marc->tmout_set = 1;
	}
	marc->default_group = default_group;

	HIO_SVCL_APPEND_SVC (&hio->actsvc, (hio_svc_t*)marc);
	return marc;

oops:
	if (marc->edev) mysql_close (marc->edev);
	if (marc) hio_freemem (hio, marc);
	return HIO_NULL;
}

void hio_svc_marc_stop (hio_svc_marc_t* marc)
{
	hio_t* hio = marc->hio;
	hio_oow_t i;

	marc->stopping = 1;

	for (i = 0; i < marc->sess.capa; i++)
	{
		if (marc->sess.ptr[i].dev) 
		{
			hio_dev_mar_kill (marc->sess.ptr[i].dev);
		}
	}
	hio_freemem (hio, marc->sess.ptr);

	HIO_SVCL_UNLINK_SVC (marc);

	mysql_close (marc->edev);

	hio_freemem (hio, marc);
}

/* ------------------------------------------------------------------- */

static sess_qry_t* make_session_query (hio_t* hio, hio_svc_marc_qtype_t qtype, const hio_bch_t* qptr, hio_oow_t qlen, void* qctx, hio_svc_marc_on_result_t on_result)
{
	sess_qry_t* sq;

	sq = hio_allocmem(hio, HIO_SIZEOF(*sq) + (HIO_SIZEOF(*qptr) * qlen));
	if (HIO_UNLIKELY(!sq)) return HIO_NULL;

	HIO_MEMCPY (sq + 1, qptr, (HIO_SIZEOF(*qptr) * qlen));

	sq->sent = 0;
	sq->need_fetch = (qtype == HIO_SVC_MARC_QTYPE_SELECT);
	sq->qptr = (hio_bch_t*)(sq + 1);
	sq->qlen = qlen;
	sq->qctx = qctx;
	sq->on_result = on_result;
	sq->sq_next = HIO_NULL;

	return sq;
}

static HIO_INLINE void free_session_query (hio_t* hio, sess_qry_t* sq)
{
	hio_freemem (hio, sq);
}

static HIO_INLINE void enqueue_session_query (sess_t* sess, sess_qry_t* sq)
{
	/* the initialization creates a place holder. so no need to check if q_tail is NULL */
	sess->q_tail->sq_next = sq;
	sess->q_tail = sq;
}

static HIO_INLINE void dequeue_session_query (hio_t* hio, sess_t* sess)
{
	sess_qry_t* sq;

	sq = sess->q_head;
	HIO_ASSERT (hio, sq->sq_next != HIO_NULL); /* must not be empty */
	sess->q_head = sq->sq_next;
	free_session_query (hio, sq);
}

static HIO_INLINE sess_qry_t* get_first_session_query (sess_t* sess)
{
	return sess->q_head->sq_next;
}

/* ------------------------------------------------------------------- */

static int send_pending_query_if_any (sess_t* sess)
{
	sess_qry_t* sq;

	sq = get_first_session_query(sess);
	if (sq)
	{
		sq->sent = 1;
/*printf ("sending... %.*s\n", (int)sq->qlen, sq->qptr);*/
		if (hio_dev_mar_querywithbchars(sess->dev, sq->qptr, sq->qlen) <= -1) 
		{
			HIO_DEBUG2 (sess->svc->hio, "MARC(%p) - SEND FAIL %js\n", sess->dev, hio_geterrmsg(sess->svc->hio));
			sq->sent = 0;
			hio_dev_mar_halt (sess->dev); /* this device can't carray on */
			return -1; /* halted the device for failure */
		}

		return 1; /* sent */
	}


	return 0; /* nothing to send */
}

/* ------------------------------------------------------------------- */
static hio_dev_mar_t* alloc_device (hio_svc_marc_t* marc, hio_oow_t sid);

static void mar_on_disconnect (hio_dev_mar_t* dev)
{
	hio_t* hio = dev->hio;
	dev_xtn_t* xtn = (dev_xtn_t*)hio_dev_mar_getxtn(dev);
	sess_t* sess;

	if (xtn->sid == INVALID_SID) return; /* this session data is not set if there's failure in alloc_device() */

	sess = &xtn->svc->sess.ptr[xtn->sid];
	HIO_DEBUG6 (hio, "MARC(%p) - device disconnected - sid %lu session %p session-connected %d device %p device-broken %d\n", sess->svc, (unsigned long int)sess->sid, sess, (int)sess->connected, dev, (int)dev->broken); 
	HIO_ASSERT (hio, dev == sess->dev);

	if (HIO_UNLIKELY(!sess->svc->stopping && hio->stopreq == HIO_STOPREQ_NONE))
	{
		if (sess->connected && sess->dev->broken) /* risk of infinite cycle if the underlying db suffers never-ending 'broken' issue after getting connected */
		{
			/* restart the dead device */
			hio_dev_mar_t* dev;

			sess->connected = 0;

			dev = alloc_device(sess->svc, sess->sid);
			if (HIO_LIKELY(dev))
			{
				sess->dev = dev;
				/* the pending query will be sent in on_connect() */
				return;
			}

			/* if device allocation fails, just carry on */
		}
	}

	sess->connected = 0;

	while (1)
	{
		sess_qry_t* sq;
		hio_svc_marc_dev_error_t err;

		sq = get_first_session_query(sess);
		if (!sq) break;

		/* what is the best error code and message to use for this? */
		err.mar_errcode = CR_SERVER_LOST;
		err.mar_errmsg = "server lost";
		sq->on_result (sess->svc, sess->sid, HIO_SVC_MARC_RCODE_ERROR, &err, sq->qctx);
		dequeue_session_query (hio, sess);
	}

	/* it should point to a placeholder node(either the initial one or the transited one after dequeing */
	HIO_ASSERT (hio, sess->q_head == sess->q_tail);
	HIO_ASSERT (hio, sess->q_head->sq_next == HIO_NULL);
	free_session_query (hio, sess->q_head);
	sess->q_head = sess->q_tail = HIO_NULL;

	sess->dev = HIO_NULL;
}

static void mar_on_connect (hio_dev_mar_t* dev)
{
	hio_t* hio = dev->hio;
	dev_xtn_t* xtn = (dev_xtn_t*)hio_dev_mar_getxtn(dev);
	sess_t* sess;

	HIO_ASSERT (hio, xtn->sid != INVALID_SID);
	sess = &xtn->svc->sess.ptr[xtn->sid];
	HIO_DEBUG5 (hio, "MARC(%p) - device connected - sid %lu session %p device %p device-broken %d\n", sess->svc, (unsigned long int)sess->sid, sess, dev, dev->broken); 

	sess->connected = 1;
	send_pending_query_if_any (sess);
}

static void mar_on_query_started (hio_dev_mar_t* dev, int mar_ret, const hio_bch_t* mar_errmsg)
{
	hio_t* hio = dev->hio;
	dev_xtn_t* xtn = (dev_xtn_t*)hio_dev_mar_getxtn(dev);
	sess_t* sess;
	sess_qry_t* sq;

	HIO_ASSERT (hio, xtn->sid != INVALID_SID);
	sess = &xtn->svc->sess.ptr[xtn->sid];
	sq = get_first_session_query(sess);

	if (mar_ret)
	{
		hio_svc_marc_dev_error_t err;
/*printf ("QUERY FAILED...%d -> %s\n", mar_ret, mar_errmsg);*/

		err.mar_errcode = mar_ret;
		err.mar_errmsg = mar_errmsg;
		sq->on_result(sess->svc, sess->sid, HIO_SVC_MARC_RCODE_ERROR, &err, sq->qctx);

		dequeue_session_query (sess->svc->hio, sess);
		send_pending_query_if_any (sess);
	}
	else
	{
/*printf ("QUERY STARTED\n");*/
		if (sq->need_fetch)
		{
			if (hio_dev_mar_fetchrows(dev) <= -1)
			{
/*printf ("FETCH ROW FAILURE - %s\n", mysql_error(dev->hnd));*/
				hio_dev_mar_halt (dev);
			}
		}
		else
		{
			sq->on_result (sess->svc, sess->sid, HIO_SVC_MARC_RCODE_DONE, HIO_NULL, sq->qctx);
			dequeue_session_query (sess->svc->hio, sess);
			send_pending_query_if_any (sess);
		}
	}
}

static void mar_on_row_fetched (hio_dev_mar_t* dev, void* data)
{
	hio_t* hio = dev->hio;
	dev_xtn_t* xtn = (dev_xtn_t*)hio_dev_mar_getxtn(dev);
	sess_t* sess;
	sess_qry_t* sq;

	HIO_ASSERT (hio, xtn->sid != INVALID_SID);
	sess = &xtn->svc->sess.ptr[xtn->sid];
	sq = get_first_session_query(sess);

	sq->on_result (sess->svc, sess->sid, (data? HIO_SVC_MARC_RCODE_ROW: HIO_SVC_MARC_RCODE_DONE), data, sq->qctx);

	if (!data) 
	{
		dequeue_session_query (sess->svc->hio, sess);
		send_pending_query_if_any (sess);
	}
}

static hio_dev_mar_t* alloc_device (hio_svc_marc_t* marc, hio_oow_t sid)
{
	hio_t* hio = (hio_t*)marc->hio;
	hio_dev_mar_t* mar;
	hio_dev_mar_make_t mi;
	dev_xtn_t* xtn;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	if (marc->tmout_set)
	{
		mi.flags = HIO_DEV_MAR_USE_TMOUT;
		mi.tmout = marc->tmout;
	}
	mi.default_group = marc->default_group;

	mi.on_connect = mar_on_connect;
	mi.on_disconnect = mar_on_disconnect;
	mi.on_query_started = mar_on_query_started;
	mi.on_row_fetched = mar_on_row_fetched;

	mar = hio_dev_mar_make(hio, HIO_SIZEOF(*xtn), &mi);
	if (HIO_UNLIKELY(!mar)) return HIO_NULL;

	xtn = (dev_xtn_t*)hio_dev_mar_getxtn(mar);
	xtn->svc = marc;
	xtn->sid = sid;

	if (hio_dev_mar_connect(mar, &marc->ci) <= -1) 
	{
		/* connection failed immediately */
		xtn->sid = INVALID_SID;
		hio_dev_mar_halt (mar);
		return HIO_NULL;
	}

	return mar;
}

/* ------------------------------------------------------------------- */

static sess_t* get_session (hio_svc_marc_t* marc, hio_oow_t flagged_sid)
{
	hio_t* hio = marc->hio;
	hio_oow_t sid;
	sess_t* sess;

	sid = flagged_sid & ~HIO_SVC_MARC_SID_FLAG_ALL;

	if ((flagged_sid & HIO_SVC_MARC_SID_FLAG_AUTO_BOUNDED) && marc->sess.capa > 0)
	{
		hio_oow_t i, ubound, mbound;
		hio_oow_t unused = INVALID_SID;

		/* automatic sid assignment. sid holds the largest session id that can be assigned */
		ubound = marc->sess.capa;
		if (sid < ubound) ubound = sid + 1;

		mbound = marc->autoi;
		if (mbound > ubound) mbound = ubound;
		for (i = mbound; i < ubound; i++)
		{
			sess = &marc->sess.ptr[i];
			if (sess->dev)
			{
				if (!get_first_session_query(sess)) 
				{
					marc->autoi = i;
					sid = marc->autoi;
					goto got_sid;
				}
			}
			else unused = i;
		}
		for (i = 0; i < mbound; i++)
		{
			sess = &marc->sess.ptr[i];
			if (sess->dev)
			{
				if (!get_first_session_query(sess)) 
				{
					marc->autoi = i;
					sid = marc->autoi;
					goto got_sid;
				}
			}
			else unused = i;
		}

		if (unused == INVALID_SID)
		{
			if (sid >= ubound)
			{
				marc->autoi = sid;
			}
			else
			{
				/* TODO: more optimizations - take the one with the least enqueued queries */
				marc->autoi2 = (marc->autoi2 + 1) % ubound;
				marc->autoi = marc->autoi;
				sid = marc->autoi2;
			}
		}
		else
		{
			marc->autoi = unused;
			sid = marc->autoi;
		}
	}

got_sid:
	if (sid >= marc->sess.capa)
	{
		sess_t* tmp;
		hio_oow_t newcapa, i;

		newcapa = marc->sess.capa + 64; /* TODO: make this configurable? */
		if (newcapa <= sid) newcapa = sid + 1;
		newcapa = HIO_ALIGN_POW2(newcapa, 16);

		tmp = (sess_t*)hio_reallocmem(hio, marc->sess.ptr, HIO_SIZEOF(sess_t) * newcapa);
		if (HIO_UNLIKELY(!tmp)) return HIO_NULL;

		HIO_MEMSET (&tmp[marc->sess.capa], 0, HIO_SIZEOF(sess_t) * (newcapa - marc->sess.capa));
		for (i = marc->sess.capa; i < newcapa; i++)
		{
			tmp[i].svc = marc;
			tmp[i].sid = i;
		}

		marc->sess.ptr = tmp;
		marc->sess.capa = newcapa;
	}

	sess = &marc->sess.ptr[sid];
	HIO_ASSERT (hio, sess->svc == marc);
	HIO_ASSERT (hio, sess->sid == sid);

	if (!sess->dev)
	{
		sess_qry_t* sq;

		sq = make_session_query(hio, HIO_SVC_MARC_QTYPE_ACTION, "", 0, HIO_NULL, 0); /* this is a place holder */
		if (HIO_UNLIKELY(!sq)) return HIO_NULL;

		sess->dev = alloc_device(marc, sess->sid);
		if (HIO_UNLIKELY(!sess->dev)) 
		{
			free_session_query (hio, sq);
			return HIO_NULL;
		}

		/* queue initialization with a place holder. the queue maintains a placeholder node. 
		 * the first actual data node enqueued is inserted at the back and becomes the second
		 * node in terms of the entire queue. 
		 *     
		 *     PH -> DN1 -> DN2 -> ... -> DNX
		 *     ^                          ^
		 *     q_head                     q_tail
		 *
		 * get_first_session_query() returns the data of DN1, not the data held in PH.
		 *
		 * the first dequeing operations kills PH.
 		 * 
		 *     DN1 -> DN2 -> ... -> DNX
		 *     ^                    ^
		 *     q_head               q_tail
		 *
		 * get_first_session_query() at this point returns the data of DN2, not the data held in DN1.
		 * dequeing kills DN1, however.
		 */

		sess->q_head = sess->q_tail = sq;
	}

	return sess;
}


int hio_svc_marc_querywithbchars (hio_svc_marc_t* marc, hio_oow_t flagged_sid, hio_svc_marc_qtype_t qtype, const hio_bch_t* qptr, hio_oow_t qlen, hio_svc_marc_on_result_t on_result, void* qctx)
{
	hio_t* hio = marc->hio;
	sess_t* sess;
	sess_qry_t* sq;

	sess = get_session(marc, flagged_sid);
	if (HIO_UNLIKELY(!sess)) return -1;

	sq = make_session_query(hio, qtype, qptr, qlen, qctx, on_result);
	if (HIO_UNLIKELY(!sq)) return -1;

	if (get_first_session_query(sess) || !sess->connected)
	{
		/* there are other ongoing queries */
		enqueue_session_query (sess, sq);
	}
	else
	{
		/* this is the first query or the device is not connected yet */
		sess_qry_t* old_q_tail = sess->q_tail;

		enqueue_session_query (sess, sq);

		HIO_ASSERT (hio, sq->sent == 0);

		sq->sent = 1;
		if (hio_dev_mar_querywithbchars(sess->dev, sq->qptr, sq->qlen) <= -1) 
		{
			sq->sent = 0;
			if (!sess->dev->broken)
			{
				/* unlink the the last item added */
				old_q_tail->sq_next = HIO_NULL;
				sess->q_tail = old_q_tail;

				free_session_query (hio, sq);
				return -1;
			}

			/* the underlying socket of the device may get disconnected.
			 * in such a case, keep the enqueued query with sq->sent 0
			 * and defer actual sending and processing */
		}
	}

	return 0;
}

hio_oow_t hio_svc_marc_escapebchars (hio_svc_marc_t* marc, const hio_bch_t* qptr, hio_oow_t qlen, hio_bch_t* buf)
{
	return mysql_real_escape_string(marc->edev, buf, qptr, qlen);
}
