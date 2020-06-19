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

typedef struct sess_t sess_t;
typedef struct sess_qry_t sess_qry_t;

struct mio_svc_marc_t
{
	MIO_SVC_HEADER;

	mio_svc_marc_connect_t ci;

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
	int          sent;

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



mio_svc_marc_t* mio_svc_marc_start (mio_t* mio, const mio_svc_marc_connect_t* ci)
{
	mio_svc_marc_t* marc = MIO_NULL;

	marc = (mio_svc_marc_t*)mio_callocmem(mio, MIO_SIZEOF(*marc));
	if (MIO_UNLIKELY(!marc)) goto oops;

	marc->mio = mio;
	marc->svc_stop = mio_svc_marc_stop;
	marc->ci = *ci;

	MIO_SVCL_APPEND_SVC (&mio->actsvc, (mio_svc_t*)marc);
	return marc;

oops:
	if (marc)
	{
		mio_freemem (mio, marc);
	}
	return MIO_NULL;
}

void mio_svc_marc_stop (mio_svc_marc_t* marc)
{
	mio_t* mio = marc->mio;

	MIO_SVCL_UNLINK_SVC (marc);
	mio_freemem (mio, marc);
}


/* ------------------------------------------------------------------- */

static sess_qry_t* make_session_query (mio_t* mio, const mio_bch_t* qptr, mio_oow_t qlen, void* qctx)
{
	sess_qry_t* sq;

	sq = mio_allocmem(mio, MIO_SIZEOF(*sq) + (MIO_SIZEOF(*qptr) * qlen));
	if (MIO_UNLIKELY(!sq)) return MIO_NULL;

	MIO_MEMCPY (sq + 1, qptr, (MIO_SIZEOF(*qptr) * qlen));

	sq->sent = 0;
	sq->qptr = (mio_bch_t*)(sq + 1);
	sq->qlen = qlen;
	sq->qctx = qctx;
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
			sq->sent = 0;
			return -1; /* failure */
		}

		return 1; /* sent */
	}


	return 0; /* nothing to send */
}

/* ------------------------------------------------------------------- */

static void mar_on_disconnect (mio_dev_mar_t* dev)
{
	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;

printf ("disconnected on sid %d\n", sess->sid);
	sess->connected = 0;
	sess->dev = MIO_NULL;


	//if (there is a query which has been sent  but not processed... ) <--- can this be handled by the caller?
}

static void mar_on_connect (mio_dev_mar_t* dev)
{
	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;

	sess->connected = 1;
printf ("connected on sid %d\n", sess->sid);

	if (send_pending_query_if_any (sess) <= -1)
	{
		mio_dev_mar_halt (sess->dev);
	}
}

static void mar_on_query_started (mio_dev_mar_t* dev, int mar_ret)
{
	if (mar_ret != 0)
	{
	}
	else
	{
		if (mio_dev_mar_fetchrows(dev) <= -1)
		{
			mio_dev_mar_halt (dev);
		}
	}
#if 0
	if (mar_ret != 0)
	{
printf ("QUERY NOT SENT PROPERLY..%s\n", mysql_error(dev->hnd));
	}
	else
	{
printf ("QUERY SENT..\n");
		if (mio_dev_mar_fetchrows(dev) <= -1)
		{
printf ("FETCH ROW FAILURE - %s\n", mysql_error(dev->hnd));
			mio_dev_mar_halt (dev);
		}
	}
#endif
}

static void mar_on_row_fetched (mio_dev_mar_t* dev, void* data)
{
#if 0
	MYSQL_ROW row = (MYSQL_ROW)data;
	static int x = 0;
	if (!row) 
	{
		printf ("NO MORE ROW..\n");
		if (x == 0 && mio_dev_mar_querywithbchars(dev, "SELECT * FROM pdns.records", 26) <= -1) mio_dev_mar_halt (dev);
		x++;
	}
	else
	{
		if (x == 0)
			printf ("%s %s\n", row[0], row[1]);
		else if (x == 1)
			printf ("%s %s %s %s %s\n", row[0], row[1], row[2], row[3], row[4]);
		//printf ("GOT ROW\n");
	}
#endif

	dev_xtn_t* xtn = (dev_xtn_t*)mio_dev_mar_getxtn(dev);
	sess_t* sess = xtn->sess;

	if (!data)
	{
		/* no more rows */
		
		//marc->on_row_fetched (marc, void* data, sess->sid, sess->qctx);
printf ("there is no more row...\n");
	}
	else
	{
printf ("there is row...\n");
	}

}

static mio_dev_mar_t* alloc_device (mio_svc_marc_t* marc, sess_t* sess)
{
	mio_t* mio = (mio_t*)marc->mio;
	mio_dev_mar_t* mar;
	mio_dev_mar_make_t mi;
	mio_dev_mar_connect_t ci;
	dev_xtn_t* xtn;

	MIO_MEMSET (&mi, 0, MIO_SIZEOF(mi));
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
		mio_oow_t newcapa;

		newcapa = marc->sess.capa + 16;
		if (newcapa <= sid) newcapa = sid + 1;
		newcapa = MIO_ALIGN_POW2(newcapa, 16);

		tmp = mio_reallocmem(mio, marc->sess.ptr, MIO_SIZEOF(sess_t) * newcapa);
		if (MIO_UNLIKELY(!tmp)) return MIO_NULL;

		MIO_MEMSET (&tmp[marc->sess.capa], 0, MIO_SIZEOF(sess_t) * (newcapa - marc->sess.capa));

		marc->sess.ptr = tmp;
		marc->sess.capa = newcapa;

		sess = &marc->sess.ptr[sid];
		sess->svc = marc;
		sess->sid = sid;
	}
	else
	{
		sess = &marc->sess.ptr[sid];
		MIO_ASSERT (mio, sess->sid == sid);
		MIO_ASSERT (mio, sess->svc == marc);
	}

	if (!sess->dev)
	{
		sess_qry_t* sq;

		sq = make_session_query(mio, "", 0, MIO_NULL); /* this is a place holder */
		if (MIO_UNLIKELY(!sq)) return MIO_NULL;

		sess->dev = alloc_device(marc, sess);
		if (MIO_UNLIKELY(!sess->dev)) 
		{
			free_session_query (mio, sq);
			return MIO_NULL;
		}

		/* queue initialization with a place holder */
		sess->q_head = sess->q_tail = sq;
	}

	return sess;
}


int mio_svc_mar_querywithbchars (mio_svc_marc_t* marc, mio_oow_t sid, const mio_bch_t* qptr, mio_oow_t qlen, void* qctx)
{
	mio_t* mio = marc->mio;
	sess_t* sess;
	sess_qry_t* sq;

	sess = get_session(marc, sid);
	if (MIO_UNLIKELY(!sess)) return -1;

	sq = make_session_query(mio, qptr, qlen, qctx);
	if (MIO_UNLIKELY(!sq)) return -1;

	if (get_first_session_query(sess))
	{
printf ("XXXXXXXXXx\n");
		/* there are other ongoing queries */
		enqueue_session_query (sess, sq);
	}
	else
	{
		/* this is the first query */
		sess_qry_t* old_q_tail = sess->q_tail;
		enqueue_session_query (sess, sq);

printf ("YYYYYYYYYYYYYY\n");
		if(sess->dev->connected && !sq->sent)
		{
			sq->sent = 1;
			if (mio_dev_mar_querywithbchars(sess->dev, qptr, qlen) <= -1) 
			{
				sq->sent = 0;

				/* ugly to unlink the the last item added */
				old_q_tail->sq_next = MIO_NULL;
				sess->q_tail = old_q_tail;

				free_session_query (mio, sq);
				return -1;
			}
		}
	}

	return 0;
}
