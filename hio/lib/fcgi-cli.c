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
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <hio-fcgi.h>
#include <hio-sck.h>
#include "hio-prv.h"


typedef struct hio_svc_fcgic_conn_t hio_svc_fcgic_conn_t;

struct hio_svc_fcgic_t
{
	HIO_SVC_HEADER;

	int stopping;
	int tmout_set;
	hio_svc_fcgic_tmout_t tmout;

	hio_svc_fcgic_conn_t* conns;

#if 0
	hio_oow_t autoi;
	hio_oow_t autoi2;
#endif
};

#define INVALID_SID HIO_TYPE_MAX(hio_oow_t)

struct hio_svc_fcgic_sess_t
{
	hio_oow_t sid;
	hio_svc_fcgic_t* fcgic;
	hio_svc_fcgic_conn_t* conn;
};

struct hio_svc_fcgic_conn_t
{
	hio_skad_t addr;
	hio_dev_sck_t* dev;
	int connected;

	struct
	{
		hio_svc_fcgic_sess_t* ptr;
		hio_oow_t capa;
		hio_oow_t free; /* the index to the first free session slot */
	} sess;

	hio_svc_fcgic_conn_t* next;
};

#if 0

struct fcgic_sck_xtn_t
{
	hio_svc_fcgic_t* fcgic;

	struct
	{
		hio_uint8_t* ptr;
		hio_oow_t  len;
		hio_oow_t  capa;
	} rbuf; /* used by tcp socket */
};
typedef struct fcgic_sck_xtn_t fcgic_sck_xtn_t;



/* ----------------------------------------------------------------------- */

struct fcgic_fcgi_msg_xtn_t
{
	hio_dev_sck_t* dev;
	hio_tmridx_t   rtmridx;
	//hio_fcgi_msg_t* prev;
	//hio_fcgi_msg_t* next;
	hio_skad_t     servaddr;
	//hio_svc_fcgic_on_done_t on_done;
	hio_ntime_t    wtmout;
	hio_ntime_t    rtmout;
	int            rmaxtries; /* maximum number of tries to receive a reply */
	int            rtries; /* number of tries made so far */
	int            pending;
};
typedef struct fcgic_fcgi_msg_xtn_t fcgic_fcgi_msg_xtn_t;

#if defined(HIO_HAVE_INLINE)
	static HIO_INLINE fcgic_fcgi_msg_xtn_t* fcgic_fcgi_msg_getxtn(hio_fcgi_msg_t* msg) { return (fcgic_fcgi_msg_xtn_t*)((hio_uint8_t*)hio_fcgi_msg_to_pkt(msg) + msg->pktalilen); }
#else
#	define fcgic_fcgi_msg_getxtn(msg) ((fcgic_fcgi_msg_xtn_t*)((hio_uint8_t*)hio_fcgi_msg_to_pkt(msg) + msg->pktalilen))
#endif

#endif


static hio_svc_fcgic_conn_t* get_connection (hio_svc_fcgic_t* fcgic, hio_skad_t* addr)
{
	hio_t* hio = fcgic->hio;
	hio_svc_fcgic_conn_t* conn = fcgic->conns;

	/* TODO: speed up? how many conns would be configured? sequential search may be ok here */
	while (conn)
	{
		if (hio_equal_skads(&conn->addr, addr, 1)) return conn;
		conn = conn->next;
	}

	conn = hio_callocmem(hio, HIO_SIZEOF(*conn));
	if (HIO_UNLIKELY(!conn)) return HIO_NULL;

	conn->sess.capa = 0;
	conn->sess.free = INVALID_SID;

	conn->next = fcgic->conns;
	fcgic->conns = conn;

	return conn;
}

static void free_connections (hio_svc_fcgic_t* fcgic)
{
	hio_t* hio = fcgic->hio;
	hio_svc_fcgic_conn_t* conn = fcgic->conns;
	hio_svc_fcgic_conn_t* next;

	while (conn)
	{
		next = conn->next;
		if (conn->dev) hio_dev_sck_kill (conn->dev);
		hio_freemem (hio, conn->sess.ptr);
		hio_freemem (hio, conn);
		conn = next;
	}
}

static hio_svc_fcgic_sess_t* new_session (hio_svc_fcgic_t* fcgic, hio_skad_t* addr)
{
	hio_t* hio = fcgic->hio;
	hio_svc_fcgic_conn_t* conn;
	hio_svc_fcgic_sess_t* sess;

	conn = get_connection(fcgic, addr);
	if (HIO_UNLIKELY(!conn)) return HIO_NULL;

	if (conn->sess.free == INVALID_SID)
	{
		hio_oow_t newcapa, i;
		hio_svc_fcgic_sess_t* newptr;

		newcapa = conn->sess.capa + 32;
		newptr = hio_reallocmem (hio, conn->sess.ptr, HIO_SIZEOF(*sess) * newcapa);
		if (HIO_UNLIKELY(!newptr)) return HIO_NULL;

		for (i = conn->sess.capa ; i < newcapa; i++)
		{
			newptr[i].sid = i + 1;
			newptr[i].fcgic = fcgic;
			newptr[i].conn = conn;
		}
		newptr[i - 1].sid = INVALID_SID;
		conn->sess.free = conn->sess.capa;

		conn->sess.capa = newcapa;
		conn->sess.ptr = newptr;
	}
	else
	{
		sess = &conn->sess.ptr[conn->sess.free];
		conn->sess.free = sess->sid;

		sess->sid = conn->sess.free;
		HIO_ASSERT (hio, sess->fcgic == fcgic);
		HIO_ASSERT (hio, sess->conn == conn);
	}

	return sess;
}

static void release_session (hio_svc_fcgic_sess_t* sess)
{
	sess->sid = sess->conn->sess.free;
	sess->conn->sess.free = sess->sid;
}

hio_svc_fcgic_t* hio_svc_fcgic_start (hio_t* hio, const hio_svc_fcgic_tmout_t* tmout)
{
	hio_svc_fcgic_t* fcgic = HIO_NULL;

	fcgic = (hio_svc_fcgic_t*)hio_callocmem(hio, HIO_SIZEOF(*fcgic));
	if (HIO_UNLIKELY(!fcgic)) goto oops;

	fcgic->hio = hio;
	fcgic->svc_stop = hio_svc_fcgic_stop;

	if (tmout) 
	{
		fcgic->tmout = *tmout;
		fcgic->tmout_set = 1;
	}

	HIO_SVCL_APPEND_SVC (&hio->actsvc, (hio_svc_t*)fcgic);
	HIO_DEBUG1 (hio, "FCGIC - STARTED SERVICE %p\n", fcgic);
	return fcgic;

oops:
	if (fcgic) hio_freemem (hio, fcgic);
	return HIO_NULL;
}

void hio_svc_fcgic_stop (hio_svc_fcgic_t* fcgic)
{
	hio_t* hio = fcgic->hio;

	HIO_DEBUG1 (hio, "FCGIC - STOPPING SERVICE %p\n", fcgic);
	fcgic->stopping = 1;

	free_connections (fcgic);

	HIO_SVCL_UNLINK_SVC (fcgic);
	hio_freemem (hio, fcgic);
}

hio_svc_fcgic_sess_t* hio_svc_fcgic_tie (hio_svc_fcgic_t* fcgic, hio_skad_t* addr)
{
	/* TODO: reference counting for safety?? */
	return new_session(fcgic, addr);
}

int hio_svc_fcgic_untie (hio_svc_fcgic_sess_t* sess)
{
	/* TODO: reference counting for safety?? */
	release_session (sess);
}

