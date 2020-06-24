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

#include <mio-mar.h>
#include "mio-prv.h"

#include <mariadb/mysql.h>
#include <mariadb/errmsg.h>

typedef struct sess_t sess_t;
typedef struct sess_qry_t sess_qry_t;

struct mio_svc_marc_t
{
	MIO_SVC_HEADER;

	int stopping;
	int tmout_set;

	mio_svc_marc_connect_t ci;
	mio_svc_marc_tmout_t tmout;

	MYSQL* edev;

	struct
	{
		sess_t* ptr;
		mio_oow_t capa;
	} sess;
};

struct sess_qry_t
{
	mio_bch_t*   qptr;
	mio_oow_t    qlen;
	void*        qctx;
	unsigned int sent: 1;
	unsigned int need_fetch: 1;

	mio_svc_marc_on_result_t on_result;
	sess_qry_t*  sq_next;
};

struct sess_t
{
	mio_oow_t sid;
	mio_svc_marc_t* svc;
	mio_dev_mar_t* dev;
	int connected;

	sess_qry_t* q_head;
	sess_qry_t* q_tail;
};

typedef struct dev_xtn_t dev_xtn_t;

struct dev_xtn_t
{
	sess_t* sess;
};


mio_svc_marc_t* mio_svc_marc_start (mio_t* mio, const mio_svc_marc_connect_t* ci, const mio_svc_marc_tmout_t* tmout)
{
	mio_svc_marc_t* marc = MIO_NULL;

	marc = (mio_svc_marc_t*)mio_callocmem(mio, MIO_SIZEOF(*marc));
	if (MIO_UNLIKELY(!marc)) goto oops;

	marc->edev = mysql_init(MIO_NULL);
	if (MIO_UNLIKELY(!marc->edev)) goto oops;

	marc->mio = mio;
	marc->svc_stop = mio_svc_marc_stop;
	marc->ci = *ci;
	if (tmout) 
	{
		marc->tmout = *tmout;
		marc->tmout_set = 1;
	}

	MIO_SVCL_APPEND_SVC (&mio->actsvc, (mio_svc_t*)marc);
	return marc;

oops:
	if (marc->edev) mysql_close (marc->edev);
	if (marc) mio_freemem (mio, marc);
	return MIO_NULL;
}

void mio_svc_marc_stop (mio_svc_marc_t* marc)
{
	mio_t* mio = marc->mio;
	mio_oow_t i;

	marc->stopping = 1;

	for (i = 0; i < marc->sess.capa; i++)
	{
		if (marc->sess.ptr[i].dev) mio_dev_mar_kill (marc->sess.ptr[i].dev);
	}
	mio_freemem (mio, marc->sess.ptr);

	MIO_SVCL_UNLINK_SVC (marc);

	mysql_close (marc->edev);
	mio_freemem (mio, marc);
}

/* ------------------------------------------------------------------- */

static sess_qry_t* make_session_query (mio_t* mio, mio_svc_marc_qtype_t qtype, const mio_bch_t* qptr, mio_oow_t qlen, void* qctx, mio_svc_marc_on_result_t on_result)
{
	sess_qry_t* sq;

	sq = mio_allocmem(mio, MIO_SIZEOF(*sq) + (MIO_SIZEOF(*qptr) * qlen));
	if (MIO_UNLIKELY(!sq)) return MIO_NULL;

	MIO_MEMCPY (sq + 1, qptr, (MIO_SIZEOF(*qptr) * qlen));

	sq->sent = 0;
	sq->need_fetch = (qtype == MIO_SVC_MARC_QTYPE_SELECT);
	sq->qptr = (mio_bch_t*)(sq + 1);
	sq->qlen = qlen;
	sq->qctx = qctx;
	sq->on_result = on_result;
	sq->sq_next = MIO_NULL;

	return sq;
}

static MIO_INLINE void free_session_query (mio_t* mio, sess_qry_t* sq)
{
	mio_freemem (mio, sq);
}

static MIO_INLINE void enqueue_session_query (sess_t* sess, sess_qry_t* sq)
{
	/* the initialization creates a place holder. so no need to check if q_tail is NULL */
	sess->q_tail->sq_next = sq;
	sess->q_tail = sq;
}

static MIO_INLINE void dequeue_session_query (mio_t* mio, sess_t* sess)
{
	sess_qry_t* sq;

	sq = sess->q_head;
	MIO_ASSERT (mio, sq->sq_next != MIO_NULL); /* must not be empty */
	sess->q_head = sq->sq_next;
	free_session_query (mio, sq);
}

static MIO_INLINE sess_qry_t* get_first_session_query (sess_t* sess)
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
printf ("sending... %.*s\n", (int)sq->qlen, sq->qptr);
		if (mio_dev_mar_querywithbchars(sess->dev, sq->qptr, sq->qlen) <= -1) 
		{
MIO_DEBUG1 (sess->svc->mio, "QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ SEND FAIL %js\n", mio_geterrmsg(sess->svc->mio));
			sq->sent = 0;
			mio_dev_mar_halt (sess->dev); /* this device can't carray on */
			return -1; /* halted the device for failure */
		}

		return 1; /* sent */
	}


	return 0; /* nothing to send */
}

/* ------------------------------------------------------------------- */
static mio_dev_mar_t* alloc_device (mio_svc_marc_t* marc, sess_t* sess);

static void mar_on_disconnect (mio_dev_mar_t* dev)
{
	mio_t* mio = dev->mio;
	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;

	MIO_DEBUG6 (mio, "MARC(%p) - device disconnected - sid %lu session %p session-connected %d device %p device-broken %d\n", sess->svc, (unsigned long int)sess->sid, sess, (int)sess->connected, dev, (int)dev->broken); 
	MIO_ASSERT (mio, dev == sess->dev);

	if (MIO_UNLIKELY(!sess->svc->stopping && mio->stopreq == MIO_STOPREQ_NONE))
	{
		if (sess->connected && sess->dev->broken) /* risk of infinite cycle if the underlying db suffers never-ending 'broken' issue after getting connected */
		{
			/* restart the dead device */
			mio_dev_mar_t* dev;

			sess->connected = 0;

			dev = alloc_device(sess->svc, sess);
			if (MIO_LIKELY(dev))
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
		mio_svc_marc_dev_error_t err;

		sq = get_first_session_query(sess);
		if (!sq) break;

		/* what is the best error code and message to use for this? */
		err.mar_errcode = CR_SERVER_LOST;
		err.mar_errmsg = "server lost";
		sq->on_result (sess->svc, sess->sid, MIO_SVC_MARC_RCODE_ERROR, &err, sq->qctx);
		dequeue_session_query (mio, sess);
	}

	/* it should point to a placeholder node(either the initial one or the transited one after dequeing */
	MIO_ASSERT (mio, sess->q_head == sess->q_tail);
	MIO_ASSERT (mio, sess->q_head->sq_next == MIO_NULL);
	free_session_query (mio, sess->q_head);
	sess->q_head = sess->q_tail = MIO_NULL;

	sess->dev = MIO_NULL;
}

static void mar_on_connect (mio_dev_mar_t* dev)
{
	mio_t* mio = dev->mio;
	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;

	MIO_DEBUG5 (mio, "MARC(%p) - device connected - sid %lu session %p device %p device-broken %d\n", sess->svc, (unsigned long int)sess->sid, sess, dev, dev->broken); 

	sess->connected = 1;
	send_pending_query_if_any (sess);
}

static void mar_on_query_started (mio_dev_mar_t* dev, int mar_ret, const mio_bch_t* mar_errmsg)
{
	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;
	sess_qry_t* sq = get_first_session_query(sess);

	if (mar_ret)
	{
		mio_svc_marc_dev_error_t err;
printf ("QUERY FAILED...%d -> %s\n", mar_ret, mar_errmsg);

		err.mar_errcode = mar_ret;
		err.mar_errmsg = mar_errmsg;
		sq->on_result(sess->svc, sess->sid, MIO_SVC_MARC_RCODE_ERROR, &err, sq->qctx);

		dequeue_session_query (sess->svc->mio, sess);
		send_pending_query_if_any (sess);
	}
	else
	{
printf ("QUERY STARTED\n");
		if (sq->need_fetch)
		{
			if (mio_dev_mar_fetchrows(dev) <= -1)
			{
//printf ("FETCH ROW FAILURE - %s\n", mysql_error(dev->hnd));
				mio_dev_mar_halt (dev);
			}
		}
		else
		{
			sq->on_result (sess->svc, sess->sid, MIO_SVC_MARC_RCODE_DONE, MIO_NULL, sq->qctx);
			dequeue_session_query (sess->svc->mio, sess);
			send_pending_query_if_any (sess);
		}
	}
}

static void mar_on_row_fetched (mio_dev_mar_t* dev, void* data)
{
	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;
	sess_qry_t* sq = get_first_session_query(sess);

	sq->on_result (sess->svc, sess->sid, (data? MIO_SVC_MARC_RCODE_ROW: MIO_SVC_MARC_RCODE_DONE), data, sq->qctx);

	if (!data) 
	{
		dequeue_session_query (sess->svc->mio, sess);
		send_pending_query_if_any (sess);
	}
}

static mio_dev_mar_t* alloc_device (mio_svc_marc_t* marc, sess_t* sess)
{
	mio_t* mio = (mio_t*)marc->mio;
	mio_dev_mar_t* mar;
	mio_dev_mar_make_t mi;
	dev_xtn_t* xtn;

	MIO_MEMSET (&mi, 0, MIO_SIZEOF(mi));
	if (marc->tmout_set)
	{
		mi.flags = MIO_DEV_MAR_USE_TMOUT;
		mi.tmout = marc->tmout;
	}

	mi.on_connect = mar_on_connect;
	mi.on_disconnect = mar_on_disconnect;
	mi.on_query_started = mar_on_query_started;
	mi.on_row_fetched = mar_on_row_fetched;

	mar = mio_dev_mar_make(mio, MIO_SIZEOF(*xtn), &mi);
	if (!mar) return MIO_NULL;

	xtn = (dev_xtn_t*)mio_dev_mar_getxtn(mar);
	xtn->sess = sess;

	if (mio_dev_mar_connect(mar, &marc->ci) <= -1) return MIO_NULL;

	return mar;
}

/* ------------------------------------------------------------------- */

static sess_t* get_session (mio_svc_marc_t* marc, mio_oow_t sid)
{
	mio_t* mio = marc->mio;
	sess_t* sess;

	if (sid >= marc->sess.capa)
	{
		sess_t* tmp;
		mio_oow_t newcapa, i;

		newcapa = marc->sess.capa + 16;
		if (newcapa <= sid) newcapa = sid + 1;
		newcapa = MIO_ALIGN_POW2(newcapa, 16);

		tmp = mio_reallocmem(mio, marc->sess.ptr, MIO_SIZEOF(sess_t) * newcapa);
		if (MIO_UNLIKELY(!tmp)) return MIO_NULL;

		MIO_MEMSET (&tmp[marc->sess.capa], 0, MIO_SIZEOF(sess_t) * (newcapa - marc->sess.capa));
		for (i = marc->sess.capa; i < newcapa; i++)
		{
			tmp[i].svc = marc;
			tmp[i].sid = i;
		}

		marc->sess.ptr = tmp;
		marc->sess.capa = newcapa;
	}

	sess = &marc->sess.ptr[sid];
	MIO_ASSERT (mio, sess->sid == sid);
	MIO_ASSERT (mio, sess->svc == marc);

	if (!sess->dev)
	{
		sess_qry_t* sq;

		sq = make_session_query(mio, MIO_SVC_MARC_QTYPE_ACTION, "", 0, MIO_NULL, 0); /* this is a place holder */
		if (MIO_UNLIKELY(!sq)) return MIO_NULL;

		sess->dev = alloc_device(marc, sess);
		if (MIO_UNLIKELY(!sess->dev)) 
		{
			free_session_query (mio, sq);
			return MIO_NULL;
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


int mio_svc_mar_querywithbchars (mio_svc_marc_t* marc, mio_oow_t sid, mio_svc_marc_qtype_t qtype, const mio_bch_t* qptr, mio_oow_t qlen, mio_svc_marc_on_result_t on_result, void* qctx)
{
	mio_t* mio = marc->mio;
	sess_t* sess;
	sess_qry_t* sq;

	sess = get_session(marc, sid);
	if (MIO_UNLIKELY(!sess)) return -1;

	sq = make_session_query(mio, qtype, qptr, qlen, qctx, on_result);
	if (MIO_UNLIKELY(!sq)) return -1;

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

		MIO_ASSERT (mio, sq->sent == 0);

		sq->sent = 1;
		if (mio_dev_mar_querywithbchars(sess->dev, sq->qptr, sq->qlen) <= -1) 
		{
			sq->sent = 0;
			if (!sess->dev->broken)
			{
				/* unlink the the last item added */
				old_q_tail->sq_next = MIO_NULL;
				sess->q_tail = old_q_tail;

				free_session_query (mio, sq);
				return -1;
			}

			/* the underlying socket of the device may get disconnected.
			 * in such a case, keep the enqueued query with sq->sent 0
			 * and defer actual sending and processing */
		}
	}

	return 0;
}

mio_oow_t mio_svc_mar_escapebchars (mio_svc_marc_t* marc, const mio_bch_t* qptr, mio_oow_t qlen, mio_bch_t* buf)
{
	return mysql_real_escape_string(marc->edev, buf, qptr, qlen);
}
