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

#include "http-prv.h"

/* ------------------------------------------------------------------------ */
static int client_htrd_peek_request (mio_htrd_t* htrd, mio_htre_t* req)
{
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn = (mio_svc_htts_cli_htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_svc_htts_cli_t* sckxtn = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(htrdxtn->sck);
	return sckxtn->htts->proc_req(sckxtn->htts, htrdxtn->sck, req);
}

static mio_htrd_recbs_t client_htrd_recbs =
{
	client_htrd_peek_request,
	MIO_NULL,
	MIO_NULL
};

static int init_client (mio_svc_htts_cli_t* cli, mio_dev_sck_t* sck)
{
	mio_svc_htts_cli_htrd_xtn_t* htrdxtn;

	/* the htts field must be filled with the same field in the listening socket upon accept() */
	MIO_ASSERT (sck->mio, cli->htts != MIO_NULL);
	MIO_ASSERT (sck->mio, cli->sck == cli->htts->lsck); /* the field should still point to the listner socket */
	MIO_ASSERT (sck->mio, sck->mio == cli->htts->mio);

	cli->htrd = mio_htrd_open(sck->mio, MIO_SIZEOF(*htrdxtn));
	if (MIO_UNLIKELY(!cli->htrd)) goto oops;

	/* With MIO_HTRD_TRAILERS, htrd stores trailers in a separate place.
	 * Otherwise, it is merged to the headers. */
	/*mio_htrd_setopt (cli->htrd, MIO_HTRD_REQUEST | MIO_HTRD_TRAILERS);*/

	cli->sbuf = mio_becs_open(sck->mio, 0, 2048);
	if (MIO_UNLIKELY(!cli->sbuf)) goto oops;

	htrdxtn = mio_htrd_getxtn(cli->htrd);
	htrdxtn->sck = sck; /* TODO: remember cli instead? */

	mio_htrd_setrecbs (cli->htrd, &client_htrd_recbs);

	cli->sck = sck;
	MIO_SVC_HTTS_CLIL_APPEND_CLI (&cli->htts->cli, cli);
MIO_DEBUG3 (sck->mio, "HTTS(%p) - initialized client %p socket %p\n", cli->htts, cli, sck);
	return 0;

oops:
	if (cli->sbuf) 
	{
		mio_becs_close(cli->sbuf);
		cli->sbuf = MIO_NULL;
	}
	if (cli->htrd)
	{
		mio_htrd_close (cli->htrd);
		cli->htrd = MIO_NULL;
	}
	return -1;
}

static void fini_client (mio_svc_htts_cli_t* cli)
{
	MIO_DEBUG3 (cli->sck->mio, "HTTS(%p) - finalizing client %p socket %p\n", cli->htts, cli, cli->sck);

	if (cli->rsrc)
	{
		mio_svc_htts_rsrc_kill (cli->rsrc);
		cli->rsrc = MIO_NULL;
	}

	if (cli->sbuf) 
	{
		mio_becs_close (cli->sbuf);
		cli->sbuf = MIO_NULL;
	}

	if (cli->htrd)
	{
		mio_htrd_close (cli->htrd);
		cli->htrd = MIO_NULL;
	}

	MIO_SVC_HTTS_CLIL_UNLINK_CLI_CLEAN (cli);

	/* are these needed? not symmetrical if done here. 
	 * these fields are copied from the listener socket upon accept.
	 * init_client() doesn't fill in these fields. let's comment out these lines
	cli->sck = MIO_NULL;
	cli->htts = MIO_NULL; 
	*/
}

/* ------------------------------------------------------------------------ */

static int listener_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	/* unlike the function name, this callback is set on both the listener and the client.
	 * however, it must never be triggered for the listener */

	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	mio_oow_t rem;
	int x;

	MIO_ASSERT (mio, sck != cli->htts->lsck);
	MIO_ASSERT (mio, cli->rsrc == MIO_NULL); /* if a resource has been set, the resource must take over this handler */

	if (len <= -1)
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - unable to read client %p(%d)\n", cli->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0) 
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - EOF on client %p(%d)\n", cli->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if ((x = mio_htrd_feed(cli->htrd, buf, len, &rem)) <= -1) 
	{
printf ("** HTTS - client htrd feed failure socket(%p) - %d\n", sck, x);
		goto oops;
	}

	if (rem > 0)
	{
		if (cli->rsrc)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
		}
		else
		{
			/* TODO: no resource in action. so feed one more time */
		}
	}

	return 0;

oops:
printf ("HALTING CLIENT SOCKEXXT %p\n", sck);
	mio_dev_sck_halt (sck);
	return 0;
}

static int listener_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	MIO_ASSERT (sck->mio, sck != cli->htts->lsck);
	MIO_ASSERT (sck->mio, cli->rsrc == MIO_NULL); /* if a resource has been set, the resource must take over this handler */
	return 0;
}

static void listener_on_connect (mio_dev_sck_t* sck)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck); /* the contents came from the listening socket */

	if (sck->state & MIO_DEV_SCK_ACCEPTED)
	{
		/* accepted a new client */
		MIO_DEBUG3 (sck->mio, "HTTS(%p) - accepted... %p %d \n", cli->htts, sck, sck->hnd);

		if (init_client(cli, sck) <= -1)
		{
			MIO_DEBUG2 (cli->htts->mio, "HTTS(%p) - halting client(%p) for client intiaialization failure\n", cli->htts, sck);
			mio_dev_sck_halt (sck);
		}
	}
	else if (sck->state & MIO_DEV_SCK_CONNECTED)
	{
		/* this will never be triggered as the listing socket never call mio_dev_sck_connect() */
		MIO_DEBUG3 (sck->mio, "** HTTS(%p) - connected... %p %d \n", cli->htts, sck, sck->hnd);
	}

	/* MIO_DEV_SCK_CONNECTED must not be seen here as this is only for the listener socket */
}

static void listener_on_disconnect (mio_dev_sck_t* sck)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);

	switch (MIO_DEV_SCK_GET_PROGRESS(sck))
	{
		case MIO_DEV_SCK_CONNECTING:
			/* only for connecting sockets */
			MIO_DEBUG1 (mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			/* only for connecting sockets */
			MIO_DEBUG1 (mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_CONNECTED:
			/* only for connecting sockets */
			MIO_DEBUG1 (mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN %p(%d).......\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_DEBUG2 (mio, "LISTNER SOCKET %p(%d) - SHUTTUING DOWN\n", sck, (int)sck->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL: /* special case. */
			/* this progress code indicates that the ssl-level accept failed.
			 * on_disconnected() with this code is called without corresponding on_connect(). 
			 * the cli extension are is not initialized yet */
			MIO_ASSERT (mio, sck != cli->sck);
			MIO_ASSERT (mio, cli->sck == cli->htts->lsck); /* the field is a copy of the extension are of the listener socket. so it should point to the listner socket */
			MIO_DEBUG2 (mio, "LISTENER UNABLE TO SSL-ACCEPT CLIENT %p(%d) ....%p\n", sck, (int)sck->hnd);
			return;

		case MIO_DEV_SCK_ACCEPTED:
			/* only for sockets accepted by the listeners. will never come here because
			 * the disconnect call for such sockets have been changed in listener_on_connect() */
			MIO_DEBUG2 (mio, "ACCEPTED CLIENT SOCKET %p(%d) GOT DISCONNECTED.......\n", sck, (int)sck->hnd);
			break;

		default:
			MIO_DEBUG2 (mio, "SOCKET %p(%d) DISCONNECTED AFTER ALL.......\n", sck, (int)sck->hnd);
			break;
	}

	if (sck == cli->htts->lsck)
	{
		/* the listener socket has these fields set to NULL */
		MIO_ASSERT (mio, cli->htrd == MIO_NULL);
		MIO_ASSERT (mio, cli->sbuf == MIO_NULL);

		MIO_DEBUG2 (mio, "HTTS(%p) - listener socket disconnect %p\n", cli->htts, sck);
		cli->htts->lsck = MIO_NULL; /* let the htts service forget about this listening socket */
	}
	else
	{
		/* client socket */
		MIO_DEBUG2 (mio, "HTTS(%p) - client socket disconnect %p\n", cli->htts, sck);
		MIO_ASSERT (mio, cli->sck == sck);
		fini_client (cli);
	}
}


/* ------------------------------------------------------------------------ */

mio_svc_htts_t* mio_svc_htts_start (mio_t* mio, const mio_skad_t* bind_addr, mio_svc_htts_proc_req_t proc_req)
{
	mio_svc_htts_t* htts = MIO_NULL;
	union
	{
		mio_dev_sck_make_t m;
		mio_dev_sck_bind_t b;
		mio_dev_sck_listen_t l;
	} info;
	mio_svc_htts_cli_t* cli;

	htts = (mio_svc_htts_t*)mio_callocmem(mio, MIO_SIZEOF(*htts));
	if (MIO_UNLIKELY(!htts)) goto oops;

	htts->mio = mio;
	htts->svc_stop = mio_svc_htts_stop;
	htts->proc_req = proc_req;

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	switch (mio_skad_family(bind_addr))
	{
		case MIO_AF_INET:
			info.m.type = MIO_DEV_SCK_TCP4;
			break;

		case MIO_AF_INET6:
			info.m.type = MIO_DEV_SCK_TCP6;
			break;

		default:
			mio_seterrnum (mio, MIO_EINVAL);
			goto oops;
	}

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	info.m.on_write = listener_on_write;
	info.m.on_read = listener_on_read;
	info.m.on_connect = listener_on_connect;
	info.m.on_disconnect = listener_on_disconnect;
	htts->lsck = mio_dev_sck_make(mio, MIO_SIZEOF(*cli), &info.m);
	if (!htts->lsck) goto oops;

	/* the name 'cli' for the listening socket is awkard.
	 * the listing socket will use the htts and sck fields for tracking only.
	 * each accepted client socket gets the extension size for this size as well.
	 * most of other fields are used for client management */
	cli = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(htts->lsck);
	cli->htts = htts; 
	cli->sck = htts->lsck;

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	info.b.localaddr = *bind_addr;
	info.b.options = MIO_DEV_SCK_BIND_REUSEADDR | MIO_DEV_SCK_BIND_REUSEPORT | MIO_DEV_SCK_BIND_IGNERR;
	/*info.b.options |= MIO_DEV_SCK_BIND_SSL; */
	info.b.ssl_certfile = "localhost.crt";
	info.b.ssl_keyfile = "localhost.key";
	if (mio_dev_sck_bind(htts->lsck, &info.b) <= -1) goto oops;

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	info.l.options = MIO_DEV_SCK_LISTEN_LENIENT;
	info.l.backlogs = 255;
	MIO_INIT_NTIME (&info.l.accept_tmout, 5, 1);
	if (mio_dev_sck_listen(htts->lsck, &info.l) <= -1) goto oops;

	mio_fmttobcstr (htts->mio, htts->server_name_buf, MIO_COUNTOF(htts->server_name_buf), "%s-%d.%d.%d", 
		MIO_PACKAGE_NAME, (int)MIO_PACKAGE_VERSION_MAJOR, (int)MIO_PACKAGE_VERSION_MINOR, (int)MIO_PACKAGE_VERSION_PATCH);
	htts->server_name = htts->server_name_buf;

	MIO_SVCL_APPEND_SVC (&mio->actsvc, (mio_svc_t*)htts);
	MIO_SVC_HTTS_CLIL_INIT (&htts->cli);

	MIO_DEBUG3 (mio, "HTTS - STARTED SERVICE %p - LISTENER SOCKET %p(%d)\n", htts, htts->lsck, (int)htts->lsck->hnd);
	return htts;

oops:
	if (htts)
	{
		if (htts->lsck) mio_dev_sck_kill (htts->lsck);
		mio_freemem (mio, htts);
	}
	return MIO_NULL;
}

void mio_svc_htts_stop (mio_svc_htts_t* htts)
{
	mio_t* mio = htts->mio;

	MIO_DEBUG3 (mio, "HTTS - STOPPING SERVICE %p - LISTENER SOCKET %p(%d)\n", htts, htts->lsck, (int)(htts->lsck? htts->lsck->hnd: -1));

	/* htts->lsck may be null if the socket has been destroyed for operational error and 
	 * forgotten in the disconnect callback thereafter */
	if (htts->lsck) mio_dev_sck_kill (htts->lsck);

	while (!MIO_SVC_HTTS_CLIL_IS_EMPTY(&htts->cli))
	{
		mio_svc_htts_cli_t* cli = MIO_SVC_HTTS_CLIL_FIRST_CLI(&htts->cli);
		mio_dev_sck_kill (cli->sck);
	}

	MIO_SVCL_UNLINK_SVC (htts);
	if (htts->server_name && htts->server_name != htts->server_name_buf) mio_freemem (mio, htts->server_name);
	mio_freemem (mio, htts);
}

int mio_svc_htts_setservernamewithbcstr (mio_svc_htts_t* htts, const mio_bch_t* name)
{
	mio_t* mio = htts->mio;
	mio_bch_t* tmp;

	if (mio_copy_bcstr(htts->server_name_buf, MIO_COUNTOF(htts->server_name_buf), name) == mio_count_bcstr(name))
	{
		tmp = htts->server_name_buf;
	}
	else
	{
		tmp = mio_dupbcstr(mio, name, MIO_NULL);
		if (!tmp) return -1;
	}

	if (htts->server_name && htts->server_name != htts->server_name_buf) mio_freemem (mio, htts->server_name);
	htts->server_name = tmp;
	return 0;
}

/* ----------------------------------------------------------------- */

mio_svc_htts_rsrc_t* mio_svc_htts_rsrc_make (mio_svc_htts_t* htts, mio_oow_t rsrc_size, mio_svc_htts_rsrc_on_kill_t on_kill)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_rsrc_t* rsrc;

	rsrc = mio_callocmem(mio, rsrc_size);
	if (MIO_UNLIKELY(!rsrc)) return MIO_NULL;

	rsrc->htts = htts;
	rsrc->rsrc_size = rsrc_size;
	rsrc->rsrc_refcnt = 0;
	rsrc->rsrc_on_kill = on_kill;

	return rsrc;
}

void mio_svc_htts_rsrc_kill (mio_svc_htts_rsrc_t* rsrc)
{
printf ("RSRC KILL >>>>> htts=> %p\n", rsrc->htts);
	mio_t* mio = rsrc->htts->mio;
	if (rsrc->rsrc_on_kill) rsrc->rsrc_on_kill (rsrc);
	mio_freemem (mio, rsrc);
}

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_svc_htts_rsrc_getxtn (mio_svc_htts_rsrc_t* rsrc) { return rsrc + 1; }
#else
#define mio_svc_htts_rsrc_getxtn(rsrc) ((void*)((mio_svc_htts_rsrc_t*)rsrc + 1))
#endif

/* ----------------------------------------------------------------- */


/* ----------------------------------------------------------------- */

int mio_svc_htts_doproxy (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* upstream)
{
#if 0
	1. attempt to connect to the proxy target...
	2. in the mean time, 
	mio_dev_watch (csck, MIO_DEV_WATCH_UPDATE, 0); /* no input, no output watching */

	3. once connected,
	mio_dev_watch (csck, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN); /* enable input watching. if needed, enable output watching */

	4. start proxying


	5. if one side is stalled, donot read from another side... let the kernel slow the connection...
	   i need to know how may bytes are pending for this.
	   if pending too high, disable read watching... mio_dev_watch (csck, MIO_DEV_WATCH_RENEW, 0);
#endif
	return 0;
}

/* ----------------------------------------------------------------- */

void mio_svc_htts_fmtgmtime (mio_svc_htts_t* htts, const mio_ntime_t* nt, mio_bch_t* buf, mio_oow_t len)
{
	mio_ntime_t now;

	if (!nt) 
	{
		mio_sys_getrealtime(htts->mio, &now);
		nt = &now;
	}

	mio_fmt_http_time_to_bcstr(nt, buf, len);
}

mio_bch_t* mio_svc_htts_dupmergepaths (mio_svc_htts_t* htts, const mio_bch_t* base, const mio_bch_t* path)
{
	mio_bch_t* xpath;
	const mio_bch_t* ta[4];
	mio_oow_t idx = 0;

	ta[idx++] = base;
	if (path[0] != '\0')
	{
		ta[idx++] = "/";
		ta[idx++] = path;
	}
	ta[idx++] = MIO_NULL;
	xpath = mio_dupbcstrs(htts->mio, ta, MIO_NULL);
	if (MIO_UNLIKELY(!xpath)) return MIO_NULL;

	mio_canon_bcstr_path (xpath, xpath, 0);
	return xpath;
}

/* ----------------------------------------------------------------- */

#if 0
/* ----------------------------------------------------------------- */

int mio_svc_htts_sendrsrc (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_svc_htts_rsrc_t* rsrc, int status_code, mio_http_method_t method, const mio_http_version_t* version, int keepalive)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	mio_bch_t dtbuf[64];
	mio_oow_t x;

#if 0
	if ((req->flags & MIO_HTRE_ATTR_EXPECT) && 
	    mio_comp_http_version_numbers(&req->version, 1, 1) >= 0 &&
	    mio_htre_getcontentlen(req) <= 0)
	{
		if (req->flags & MIO_HTRE_ATTR_EXPECT100)
		{
			mio_dev_sck_write(csck, "HTTP/1.1 100 Continue\r\n", 23, MIO_NULL, MIO_NULL);
		}
		else
		{
		}
	}
#endif

	mio_svc_htts_fmtgmtime(htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	x = mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\nConnection: %s\r\n",
		(int)version->major, (int)version->minor,
		(int)status_code, mio_http_status_to_bcstr(status_code), 
		htts->server_name,
		dtbuf, /* Date */
		(keepalive? "keep-alive": "close") /* Connection */
	);
	if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;

	if (rsrc->content_type)
	{
		x = mio_becs_fcat(cli->sbuf, "Content-Type: %hs\r\n", rsrc->content_type);
		if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;
	}

	if (rsrc->flags & MIO_SVC_HTTS_RSRC_FLAG_CONTENT_LENGTH_SET)
	{
		x = mio_becs_fcat(cli->sbuf, "Content-Length: %ju\r\n", (mio_uintmax_t)rsrc->content_length);
		if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;
	}

	x = mio_becs_fcat(cli->sbuf, "\r\n", rsrc->content_length);
	if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;

/* TODO: handle METHOD HEAD... should abort after header trasmission... */

/* TODO: allow extra header items */

/* TODO: use timedwrite? */
	if (mio_dev_sck_write(csck, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf), rsrc, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (csck);
		return -1;
	}

	return 0;
}

/* ----------------------------------------------------------------- */


struct rsrc_file_xtn_t
{
	int fd;
	mio_foff_t range_offset;
	mio_foff_t range_start;
	mio_foff_t range_end;
	mio_bch_t rbuf[4096];
};
typedef struct rsrc_file_xtn_t rsrc_file_xtn_t;

static int rsrc_file_on_write (mio_svc_htts_rsrc_t* rsrc, mio_dev_sck_t* sck)
{
	rsrc_file_xtn_t* file = (rsrc_file_xtn_t*)mio_svc_htts_rsrc_getxtn(rsrc);
	ssize_t n;

	if (file->range_offset >= file->range_end) return 0; /* transmitted as long as content-length. end of the resource */

	if (file->range_offset < file->range_start)
	{
		if (lseek(file->fd, SEEK_SET, file->range_start) == (off_t)-1) return -1;
		file->range_offset = file->range_start;
	}

	n = read(file->fd, file->rbuf, MIO_SIZEOF(file->rbuf));
	if (n <= 0) return -1; 

	if (n > file->range_end - file->range_start)
	{
		/* TODO: adjust */
		n = file->range_end - file->range_start;
	}

	file->range_offset += n;
	if (mio_dev_sck_write(sck, file->rbuf, n, rsrc, MIO_NULL) <= -1) return -1; /* failure */

	return 1; /* keep calling me */
}

static void rsrc_file_on_kill (mio_svc_htts_rsrc_t* rsrc)
{
	rsrc_file_xtn_t* file = (rsrc_file_xtn_t*)mio_svc_htts_rsrc_getxtn(rsrc);

	if (file->fd >= 0)
	{
		close (file->fd);
		file->fd = -1;
	}
}

int mio_svc_htts_sendfile (mio_svc_htts_t* htts, mio_dev_sck_t* csck, const mio_bch_t* file_path, int status_code, mio_http_method_t method, const mio_http_version_t* version, int keepalive)
{
	//mio_svc_htts_cli_t* cli = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(csck);
	int fd;
	struct stat st;
	mio_svc_htts_rsrc_t* rsrc = MIO_NULL;
	rsrc_file_xtn_t* rsrc_file;

	fd = open(file_path, O_RDONLY, 0);
	if (fd <= -1) 
	{
/* TODO: divert to writing status code not found... */
		goto oops; /* write error status instead */
	}

	if (fstat(fd, &st) <= -1) goto oops; /* TODO: write error status instead. probably 500 internal server error???*/

	rsrc = mio_svc_htts_rsrc_make(htts, csck, rsrc_file_on_write, rsrc_file_on_kill, MIO_SIZEOF(*rsrc_file));
	if (MIO_UNLIKELY(!rsrc)) goto oops;

	rsrc->flags = MIO_SVC_HTTS_RSRC_FLAG_CONTENT_LENGTH_SET;
	rsrc->content_length = st.st_size;
	rsrc->content_type = "text/plain";

	rsrc_file = mio_svc_htts_rsrc_getxtn(rsrc);
	rsrc_file->fd = fd;
	rsrc_file->range_start = 0;
	rsrc_file->range_end = st.st_size;

	if (mio_svc_htts_sendrsrc(htts, csck, rsrc, 200, method, version, keepalive) <= -1) goto oops;
	return 0;

oops:
	if (rsrc) mio_svc_htts_rsrc_kill (rsrc);
	if (fd >= 0) close (fd);
	return -1;
}

#endif
