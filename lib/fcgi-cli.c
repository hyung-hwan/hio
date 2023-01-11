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
};

#define CONN_SESS_CAPA_MAX (65536)
#define CONN_SESS_INC  (32)
#define INVALID_SID HIO_TYPE_MAX(hio_oow_t)

struct hio_svc_fcgic_sess_t
{
	int active;
	hio_oow_t sid;
	hio_svc_fcgic_conn_t* conn;
	hio_svc_fcgic_on_read_t on_read;
};

struct hio_svc_fcgic_conn_t
{
	hio_svc_fcgic_t* fcgic;
	hio_skad_t addr;
	hio_dev_sck_t* dev;
	int connected;

	struct
	{
		hio_svc_fcgic_sess_t* ptr;
		hio_oow_t capa;
		hio_oow_t free; /* the index to the first free session slot */
	} sess;

	struct
	{
		enum
		{
			R_AWAITING_HEADER,
			R_AWAITING_BODY
		} state;

		hio_uint8_t   type;
		hio_uint16_t  id;
		hio_uint16_t  content_len;
		hio_uint8_t   padding_len;

		hio_uint8_t buf[1024]; /* TODO: make it smaller... */
		hio_oow_t len;
	} r; /* space to parse incoming reply header */

	hio_svc_fcgic_conn_t* next;
};

struct fcgic_sck_xtn_t
{
	hio_svc_fcgic_conn_t* conn;
};
typedef struct fcgic_sck_xtn_t fcgic_sck_xtn_t;


#if 0
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

static int make_connection_socket (hio_svc_fcgic_conn_t* conn);
static void release_session (hio_svc_fcgic_sess_t* sess);

static void sck_on_disconnect (hio_dev_sck_t* sck)
{
	fcgic_sck_xtn_t* sck_xtn = hio_dev_sck_getxtn(sck);
	hio_svc_fcgic_conn_t* conn = sck_xtn->conn;

printf ("DISCONNECT SOCKET .................. sck->%p conn->%p\n", sck, conn);

	if (conn)
	{
/* TODO: arrange to create it again if the server is not closing... */
/* if (.... ) */
#if 0
		if (sck->hio->stopreq == HIO_STOPREQ_NONE)
		{
			/* this may create a busy loop if the connection attempt fails repeatedly */
			make_connection_socket(conn); /* don't care about failure for now */
		}
#else
		hio_oow_t i;
		for (i = 0; i < conn->sess.capa; i++)
		{
			hio_svc_fcgic_sess_t* sess;
			sess = &conn->sess.ptr[i + 1];
			if (sess->active)
			{
				/* TODO: release the session???*/
				release_session (sess); /* TODO: is this correct?? */
				/* or do we fire a callback??? */
			}
		}
		conn->dev = HIO_NULL;
#endif
	}
}

static void sck_on_connect (hio_dev_sck_t* sck)
{
	fcgic_sck_xtn_t* sck_xtn = hio_dev_sck_getxtn(sck);
	hio_svc_fcgic_conn_t* conn = sck_xtn->conn;

printf ("CONNECTED >>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	/* reinitialize the input parsing information */
	HIO_MEMSET (&conn->r, 0, HIO_SIZEOF(conn->r));
	conn->r.state = R_AWAITING_HEADER;
}

static int sck_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static int sck_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	fcgic_sck_xtn_t* sck_xtn = hio_dev_sck_getxtn(sck);
	hio_svc_fcgic_conn_t* conn = sck_xtn->conn;
	hio_t* hio = conn->fcgic->hio;

	if (dlen <= -1)
	{
		/* error or timeout */
/* fire all related fcgi sessions?? -> handled on disconnect */
	}
	else if (dlen == 0)
	{
		/* EOF */
		hio_dev_sck_halt (sck);
/* fire all related fcgi sessions?? -> handled on disconnect?? */
	}
	else
	{
		do
		{
			if (conn->r.state == R_AWAITING_HEADER)
			{
				hio_fcgi_record_header_t* h;
				hio_iolen_t reqlen, cplen;
				HIO_ASSERT (hio, conn->r.len < HIO_SIZEOF(*h));

				reqlen = HIO_SIZEOF(*h) - conn->r.len;
				cplen = (dlen > reqlen)? reqlen: dlen;
				HIO_MEMCPY (&conn->r.buf[conn->r.len], data, cplen);
				conn->r.len += cplen;

				data += cplen;
				dlen -= cplen;

				if (conn->r.len < HIO_SIZEOF(*h))
				{
					/* not enough data to complete a header*/
					HIO_ASSERT (hio, dlen == 0);
					break;
				}

				h = (hio_fcgi_record_header_t*)conn->r.buf;
				conn->r.type = h->type;
				conn->r.id = hio_ntoh16(h->id);
				conn->r.content_len = hio_ntoh16(h->content_len);
				conn->r.padding_len = hio_ntoh16(h->padding_len);
				conn->r.len = 0;
				conn->r.state == R_AWAITING_BODY;
			}
			else /* R_AWAITING_BODY */
			{
				switch (conn->r.type)
				{
					case HIO_FCGI_END_REQUEST:

					case HIO_FCGI_STDOUT:
					case HIO_FCGI_STDERR:

					default:
						/* discard the record */
						goto done;
				}


				if (conn->r.id >= 1 && conn->r.id <= conn->sess.capa)
				{
					hio_svc_fcgic_sess_t* sess;
					sess = &conn->sess.ptr[conn->r.id - 1];
					if (sess->active)
					{
						sess->on_read (sess, data, dlen);
						/* TODO: return code check??? */
					}
				}
				else
				{
					/* invalid sid */
					/* TODO: logging or something*/
				}
			}
		} while (dlen > 0);
	}

done:
	return 0;
}

static int make_connection_socket (hio_svc_fcgic_conn_t* conn)
{
	hio_t* hio = conn->fcgic->hio;
	hio_dev_sck_t* sck;
	hio_dev_sck_make_t mi;
	hio_dev_sck_connect_t ci;
	fcgic_sck_xtn_t* sck_xtn;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	switch (hio_skad_get_family(&conn->addr))
	{
		case HIO_AF_INET:
			mi.type = HIO_DEV_SCK_TCP4;
			break;

		case HIO_AF_INET6:
			mi.type = HIO_DEV_SCK_TCP6;
			break;

	#if defined(HIO_AF_UNIX)
		case HIO_AF_UNIX:
			mi.type = HIO_DEV_SCK_UNIX;
			break;
	#endif

		default:
			hio_seterrnum (hio, HIO_EINVAL);
			return -1;
	}

	mi.options = HIO_DEV_SCK_MAKE_LENIENT;
	mi.on_write = sck_on_write;
	mi.on_read = sck_on_read;
	mi.on_connect = sck_on_connect;
	mi.on_disconnect = sck_on_disconnect;

	sck = hio_dev_sck_make(hio, HIO_SIZEOF(*sck_xtn), &mi);
	if (HIO_UNLIKELY(!sck)) return -1;

	sck_xtn = hio_dev_sck_getxtn(sck);
	sck_xtn->conn = conn;

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.remoteaddr = conn->addr;

	if (hio_dev_sck_connect(sck, &ci) <= -1)
	{
		/* immediate failure */
		sck_xtn->conn = HIO_NULL; /* disassociate the socket from the fcgi connection object */
		hio_dev_sck_halt (sck);
		return -1;
	}

printf ("MAKING CONNECTION %p %p\n", conn->dev, sck);
	HIO_ASSERT (hio, conn->dev == HIO_NULL);

	conn->dev = sck;
	return 0;
}

static hio_svc_fcgic_conn_t* get_connection (hio_svc_fcgic_t* fcgic, const hio_skad_t* fcgis_addr)
{
	hio_t* hio = fcgic->hio;
	hio_svc_fcgic_conn_t* conn = fcgic->conns;

	/* TODO: speed up? how many conns would be configured? sequential search may be ok here */
	while (conn)
	{
		if (hio_equal_skads(&conn->addr, fcgis_addr, 1))
		{
			if (conn->sess.free != INVALID_SID ||
			    conn->sess.capa <= (CONN_SESS_CAPA_MAX - CONN_SESS_INC))
			{
				/* the connection has room for more sessions */
				if (!conn->dev) make_connection_socket(conn); /* conn->dev will still be null if connection fails*/
				return conn;
			}
		}
		conn = conn->next;
	}

	conn = hio_callocmem(hio, HIO_SIZEOF(*conn));
	if (HIO_UNLIKELY(!conn)) return HIO_NULL;

	conn->fcgic = fcgic;
	conn->addr = *fcgis_addr;
	conn->sess.capa = 0;
	conn->sess.free = INVALID_SID;

	if (make_connection_socket(conn) <= -1)
	{
		hio_freemem (hio, conn);
		return HIO_NULL;
	}

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
		if (conn->dev)
		{
			struct fcgic_sck_xtn_t* sck_xtn;
			sck_xtn = hio_dev_sck_getxtn(conn->dev);
			sck_xtn->conn = HIO_NULL;
			hio_dev_sck_halt (conn->dev);
		}
		hio_freemem (hio, conn->sess.ptr);
		hio_freemem (hio, conn);
		conn = next;
	}
}

static hio_svc_fcgic_sess_t* new_session (hio_svc_fcgic_t* fcgic, const hio_skad_t* fcgis_addr, hio_svc_fcgic_on_read_t on_read)
{
	hio_t* hio = fcgic->hio;
	hio_svc_fcgic_conn_t* conn;
	hio_svc_fcgic_sess_t* sess;

	conn = get_connection(fcgic, fcgis_addr);
	if (HIO_UNLIKELY(!conn)) return HIO_NULL;

	if (conn->sess.free == INVALID_SID)
	{
		hio_oow_t newcapa, i;
		hio_svc_fcgic_sess_t* newptr;

		newcapa = conn->sess.capa + CONN_SESS_INC;
		newptr = hio_reallocmem(hio, conn->sess.ptr, HIO_SIZEOF(*sess) * newcapa);
		if (HIO_UNLIKELY(!newptr)) return HIO_NULL;

		for (i = conn->sess.capa ; i < newcapa; i++)
		{
			/* management records use 0 for requestId.
			 * but application records have a nonzero requestId. */
			newptr[i].sid = i + 1;
			newptr[i].conn = conn;
		}
		newptr[i - 1].sid = INVALID_SID;
		conn->sess.free = conn->sess.capa;

		conn->sess.capa = newcapa;
		conn->sess.ptr = newptr;
	}

	sess = &conn->sess.ptr[conn->sess.free];
	conn->sess.free = sess->sid;

	sess->sid = conn->sess.free;
	sess->on_read = on_read;
	sess->active = 1;
	HIO_ASSERT (hio, sess->conn == conn);
	HIO_ASSERT (hio, sess->conn->fcgic == fcgic);

	return sess;
}

static void release_session (hio_svc_fcgic_sess_t* sess)
{
	sess->active = 0;
	sess->sid = sess->conn->sess.free;
	sess->conn->sess.free = sess->sid;
}

hio_svc_fcgic_t* hio_svc_fcgic_start (hio_t* hio, const hio_svc_fcgic_tmout_t* tmout)
{
	hio_svc_fcgic_t* fcgic = HIO_NULL;

	fcgic = (hio_svc_fcgic_t*)hio_callocmem(hio, HIO_SIZEOF(*fcgic));
	if (HIO_UNLIKELY(!fcgic)) goto oops;

	fcgic->hio = hio;
	fcgic->svc_stop = (hio_svc_stop_t)hio_svc_fcgic_stop;

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

	HIO_DEBUG1 (hio, "FCGIC - STOPPED SERVICE %p\n", fcgic);
}

hio_svc_fcgic_sess_t* hio_svc_fcgic_tie (hio_svc_fcgic_t* fcgic, const hio_skad_t* addr, hio_svc_fcgic_on_read_t on_read)
{
	/* TODO: reference counting for safety?? */
	return new_session(fcgic, addr, on_read);
}

void hio_svc_fcgic_untie (hio_svc_fcgic_sess_t* sess)
{
	/* TODO: reference counting for safety?? */
	release_session (sess);
}

int hio_svc_fcgic_beginrequest (hio_svc_fcgic_sess_t* sess)
{
	hio_iovec_t iov[2];
	hio_fcgi_record_header_t h;
	hio_fcgi_begin_request_body_t b;

	if (!sess->conn->dev)
	{
		/* TODO: set error **/
		return -1;
	}

	HIO_MEMSET (&h, 0, HIO_SIZEOF(h));
	h.version = HIO_FCGI_VERSION;
	h.type = HIO_FCGI_BEGIN_REQUEST;
	h.id = h.id = hio_hton16(sess->sid);
	h.content_len = hio_hton16(HIO_SIZEOF(b));
	h.padding_len = 0;

	HIO_MEMSET (&b, 0, HIO_SIZEOF(b));
	b.role = HIO_CONST_HTON16(HIO_FCGI_ROLE_RESPONDER);
	b.flags = HIO_FCGI_KEEP_CONN;

	iov[0].iov_ptr = &h;
	iov[0].iov_len = HIO_SIZEOF(h);
	iov[1].iov_ptr = &b;
	iov[1].iov_len = HIO_SIZEOF(b);

/* TODO: check if sess->conn->dev is still valid */
	return hio_dev_sck_writev(sess->conn->dev, iov, 2, HIO_NULL, HIO_NULL);
}

int hio_svc_fcgic_writeparam (hio_svc_fcgic_sess_t* sess, const void* key, hio_iolen_t ksz, const void* val, hio_iolen_t vsz)
{
	hio_iovec_t iov[4];
	hio_fcgi_record_header_t h;
	hio_uint8_t sz[8];
	hio_oow_t szc = 0;

	if (!sess->conn->dev)
	{
		/* TODO: set error **/
		return -1;
	}

/* TODO: buffer key value pairs. flush on the end of param of buffer full.
* can merge multipl key values pairs in one FCGI_PARAMS packets....
*/
	HIO_MEMSET (&h, 0, HIO_SIZEOF(h));
	h.version = HIO_FCGI_VERSION;
	h.type = HIO_FCGI_PARAMS;
	h.id = hio_hton16(sess->sid);
	h.content_len = 0;

	/* TODO: check ksz and vsz can't exceed max 32bit value. */
	/* limit sizes to the max of the signed 32-bit interger
	*  the high-order bit is used as encoding marker (1-byte or 4-byte encoding).
	*  so a size can't hit the unsigned max. */
	ksz &= HIO_TYPE_MAX(hio_int32_t);
	vsz &= HIO_TYPE_MAX(hio_int32_t);
	if (ksz > 0)
	{
		if (ksz > 0xFF)
		{
			sz[szc++] = (ksz >> 24) | 0x80;
			sz[szc++] = (ksz >> 16) & 0xFF;
			sz[szc++] = (ksz >> 8) & 0xFF;
			sz[szc++] = ksz & 0xFF;
		}
		else
		{
			sz[szc++] = ksz;
		}

		if (vsz > 0xFF)
		{
			sz[szc++] = (vsz >> 24) | 0x80;
			sz[szc++] = (vsz >> 16) & 0xFF;
			sz[szc++] = (vsz >> 8) & 0xFF;
			sz[szc++] = vsz & 0xFF;
		}
		else
		{
			sz[szc++] = vsz;
		}

		h.content_len = szc + ksz + vsz;
		/* TODO: check content_len overflows... */
	}

	h.content_len = hio_hton16(h.content_len);
	h.padding_len = 0;

/* TODO: some buffering of parameters??? if the key/value isn't long enough, it may trigger many system calls*/
	iov[0].iov_ptr = &h;
	iov[0].iov_len = HIO_SIZEOF(h);
	if (ksz > 0)
	{
		iov[1].iov_ptr = sz;
		iov[1].iov_len = szc;
		iov[2].iov_ptr = key;
		iov[2].iov_len = ksz;
		iov[3].iov_ptr = val;
		iov[3].iov_len = vsz;
	}

	return hio_dev_sck_writev(sess->conn->dev, iov, (ksz > 0? 4: 1), HIO_NULL, HIO_NULL);
}

int hio_svc_fcgic_writestdin (hio_svc_fcgic_sess_t* sess, const void* data, hio_iolen_t size)
{
	hio_iovec_t iov[2];
	hio_fcgi_record_header_t h;

	if (!sess->conn->dev)
	{
		/* TODO: set error **/
		return -1;
	}

	HIO_MEMSET (&h, 0, HIO_SIZEOF(h));
	h.version = HIO_FCGI_VERSION;
	h.type = HIO_FCGI_STDIN;
	h.id = hio_hton16(sess->sid);
	h.content_len = hio_hton16(size);

	iov[0].iov_ptr = &h;
	iov[0].iov_len = HIO_SIZEOF(h);
	if (size > 0)
	{
		iov[1].iov_ptr = (void*)data;
		iov[1].iov_len = size;
	}

/* TODO: check if sess->conn->dev is still valid */
	return hio_dev_sck_writev(sess->conn->dev, iov, (size > 0? 2: 1), HIO_NULL, HIO_NULL);
}
