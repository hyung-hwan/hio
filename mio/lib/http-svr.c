#include "mio-http.h"
#include "mio-htrd.h"
#include "mio-prv.h"


#include <unistd.h> /* TODO: move file operations to sys-file.XXX */
#include <fcntl.h>
#include <sys/stat.h>

struct mio_svc_htts_t
{
	MIO_SVC_HEADER;

	mio_dev_sck_t* lsck;
	mio_bch_t* server_name;
	mio_bch_t server_name_buf[64];
};

struct mio_svc_httc_t
{
	MIO_SVC_HEADER;
};

struct mio_htts_client_t
{
	mio_skad_t remote_addr;
	mio_skad_t local_addr;
	mio_skad_t orgdst_addr;
	mio_htrd_t* htrd;
	mio_becs_t* sbuf; /* temporary buffer for status line formatting */
};
typedef struct mio_htts_client_t mio_htts_client_t;

struct sck_xtn_t
{
	mio_svc_htts_t* htts;

	mio_htts_client_t c;
};
typedef struct sck_xtn_t sck_xtn_t;

struct htrd_xtn_t
{
	mio_dev_sck_t* sck;
};
typedef struct htrd_xtn_t htrd_xtn_t;
/* ------------------------------------------------------------------------ */

static int process_request (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, int peek)
{
	//server_xtn_t* server_xtn = GET_SERVER_XTN(htts, client->server);
	//mio_htts_task_t* task;
	sck_xtn_t* csckxtn = mio_dev_sck_getxtn(csck);
	mio_http_method_t mth;
	//mio_htts_rsrc_t rsrc;

	/* percent-decode the query path to the original buffer
	 * since i'm not going to need it in the original form
	 * any more. once it's decoded in the peek mode,
	 * the decoded query path is made available in the
	 * non-peek mode as well */
	if (peek) 
	{
		mio_htre_perdecqpath(req);

		/* TODO: proper request logging */

		MIO_DEBUG2 (htts->mio, "[PEEK] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));
	}
else
{
	MIO_DEBUG2 (htts->mio, "[NOPEEK] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));
}

#if 0
mio_printf (MIO_T("================================\n"));
mio_printf (MIO_T("[%lu] %hs REQUEST ==> [%hs] version[%d.%d %hs] method[%hs]\n"),
	(unsigned long)time(NULL),
	(peek? MIO_MT("PEEK"): MIO_MT("HANDLE")),
	mio_htre_getqpath(req),
	mio_htre_getmajorversion(req),
	mio_htre_getminorversion(req),
	mio_htre_getverstr(req),
	mio_htre_getqmethodname(req)
);
if (mio_htre_getqparam(req))
	mio_printf (MIO_T("PARAMS ==> [%hs]\n"), mio_htre_getqparam(req));

mio_htb_walk (&req->hdrtab, walk, MIO_NULL);
if (mio_htre_getcontentlen(req) > 0)
{
	mio_printf (MIO_T("CONTENT [%.*S]\n"), (int)mio_htre_getcontentlen(req), mio_htre_getcontentptr(req));
}
#endif

	mth = mio_htre_getqmethodtype(req);
	if (peek)
	{
		/* determine what to do once the header fields are all received.
		 * i don't want to delay this until the contents are received.
		 * if you don't like this behavior, you must implement your own
		 * callback function for request handling. */

#if 0
		/* TODO support X-HTTP-Method-Override */
		if (data.method == MIO_HTTP_POST)
		{
			tmp = mio_htre_getheaderval(req, MIO_MT("X-HTTP-Method-Override"));
			if (tmp)
			{
				/*while (tmp->next) tmp = tmp->next;*/ /* get the last value */
				data.method = mio_mbstohttpmethod (tmp->ptr);
			}
		}
#endif

		if (mth == MIO_HTTP_CONNECT)
		{
			/* CONNECT method must not have content set. 
			 * however, arrange to discard it if so. 
			 *
			 * NOTE: CONNECT is implemented to ignore many headers like
			 *       'Expect: 100-continue' and 'Connection: keep-alive'. */
			mio_htre_discardcontent (req);
		}
		else 
		{
//mio_svc_htts_sendstatus (htts, csck, 500, mth, mio_htre_getversion(req), (req->flags & MIO_HTRE_ATTR_KEEPALIVE), MIO_NULL);
//return 0;
			if (mth == MIO_HTTP_POST &&
			    !(req->flags & MIO_HTRE_ATTR_LENGTH) &&
			    !(req->flags & MIO_HTRE_ATTR_CHUNKED))
			{
				/* POST without Content-Length nor not chunked */
				req->flags &= ~MIO_HTRE_ATTR_KEEPALIVE;
				mio_htre_discardcontent (req);

				/* 411 Length Required - can't keep alive. Force disconnect */
				mio_svc_htts_sendstatus (htts, csck, 411, mth, mio_htre_getversion(req), 0, MIO_NULL); /* TODO: error check... */
			}
			else
			{
/* TODO: handle 100 continue??? */
				const mio_bch_t* qpath = mio_htre_getqpath(req);
				if (mio_svc_htts_sendfile(htts, csck, qpath, 200, mth, mio_htre_getversion(req), (req->flags & MIO_HTRE_ATTR_KEEPALIVE)) <= -1)
				{
					mio_htre_discardcontent (req);
					mio_dev_halt (csck);
				}

				/*
				if (mio_comp_bcstr(qpath, "/mio.c", 0) == 0)
				{
					mio_svc_htts_sendfile (htts, csck, "/home/hyung-hwan/projects/mio/lib/mio.c", 200, mth, mio_htre_getversion(req), (req->flags & MIO_HTRE_ATTR_KEEPALIVE));
				}
				else
				{
					mio_svc_htts_sendstatus (htts, csck, 500, mth, mio_htre_getversion(req), (req->flags & MIO_HTRE_ATTR_KEEPALIVE), MIO_NULL);
				}*/
			}
#if 0
			else if (server_xtn->makersrc(htts, client, req, &rsrc) <= -1)
			{
				/* failed to make a resource. just send the internal server error.
				 * the makersrc handler can return a negative number to return 
				 * '500 Internal Server Error'. If it wants to return a specific
				 * error code, it should return 0 with the MIO_HTTPD_RSRC_ERROR
				 * resource. */
				mio_htts_discardcontent (req);
				task = mio_htts_entaskerror(htts, client, MIO_NULL, 500, req);
			}
			else
			{
				task = MIO_NULL;

				if ((rsrc.flags & MIO_HTTPD_RSRC_100_CONTINUE) &&
				    (task = mio_htts_entaskcontinue(htts, client, task, req)) == MIO_NULL) 
				{
					/* inject '100 continue' first if it is needed */
					goto oops;
				}

				/* arrange the actual resource to be returned */
				task = mio_htts_entaskrsrc(htts, client, task, &rsrc, req);
				server_xtn->freersrc (htts, client, req, &rsrc);

				/* if the resource is indicating to return an error,
				 * discard the contents since i won't return them */
				if (rsrc.type == MIO_HTTPD_RSRC_ERROR) 
				{ 
					mio_htre_discardcontent (req); 
				}
			}

			if (task == MIO_NULL) goto oops;
#endif
		}
	}
	else
	{
		/* contents are all received */
		if (mth == MIO_HTTP_CONNECT)
		{
			
			MIO_DEBUG1 (htts->mio, "Switching HTRD to DUMMY for [%hs]\n", mio_htre_getqpath(req));

			/* Switch the http reader to a dummy mode so that the subsqeuent
			 * input(request) is just treated as data to the request just 
			 * completed */
			mio_htrd_dummify (csckxtn->c.htrd);

			/* connect is not handled in the peek mode. create a proxy resource here */
////////
#if 0
			if (server_xtn->makersrc(htts, client, req, &rsrc) <= -1)
			{
				MIO_DEBUG1 (htts->mio, "Cannot make resource for [%hs]\n", mio_htre_getqpath(req));

				/* failed to make a resource. just send the internal server error.
				 * the makersrc handler can return a negative number to return 
				 * '500 Internal Server Error'. If it wants to return a specific
				 * error code, it should return 0 with the MIO_HTTPD_RSRC_ERROR
				 * resource. */
				task = mio_htts_entaskerror(htts, client, MIO_NULL, 500, req);
			}
			else
			{
				/* arrange the actual resource to be returned */
				task = mio_htts_entaskrsrc (htts, client, MIO_NULL, &rsrc, req);
				server_xtn->freersrc (htts, client, req, &rsrc);
			}

			if (task == MIO_NULL) goto oops;
#endif
////////
		}
		else if (req->flags & MIO_HTRE_ATTR_PROXIED)
		{
			/* the contents should be proxied. 
			 * do nothing locally */
		}
		else
		{
			/* when the request is handled locally, 
			 * there's nothing special to do here */
		}
	}

	if (!(req->flags & MIO_HTRE_ATTR_KEEPALIVE) || mth == MIO_HTTP_CONNECT)
	{
		if (!peek)
		{
////			task = mio_htts_entaskdisconnect(htts, client, MIO_NULL);
////			if (MIO_UNLIKELY(!task)) goto oops;
		}
	}

	return 0;

oops:
	/*mio_htts_markbadclient (htts, client);*/
	return -1;
}

static int client_htrd_peek_request (mio_htrd_t* htrd, mio_htre_t* req)
{
	htrd_xtn_t* htrdxtn = (htrd_xtn_t*)mio_htrd_getxtn(htrd);
	sck_xtn_t* sckxtn = (sck_xtn_t*)mio_dev_sck_getxtn(htrdxtn->sck);
	return process_request(sckxtn->htts, htrdxtn->sck, req, 1);
//	return xtn->htts->opt.rcb.peekreq(xtn->htts, xtn->client, req);
}

static int client_htrd_poke_request (mio_htrd_t* htrd, mio_htre_t* req)
{
	htrd_xtn_t* htrdxtn = (htrd_xtn_t*)mio_htrd_getxtn(htrd);
	sck_xtn_t* sckxtn = (sck_xtn_t*)mio_dev_sck_getxtn(htrdxtn->sck);
	return process_request(sckxtn->htts, htrdxtn->sck, req, 0);
}

static mio_htrd_recbs_t client_htrd_recbs =
{
	client_htrd_peek_request,
	client_htrd_poke_request
};

static int init_client (mio_t* mio, mio_htts_client_t* c, mio_dev_sck_t* sck)
{
	htrd_xtn_t* htrdxtn;

	c->htrd = mio_htrd_open(mio, MIO_SIZEOF(*htrdxtn));
	if (MIO_UNLIKELY(!c->htrd)) goto oops;

	c->sbuf = mio_becs_open(mio, 0, 2048);
	if (MIO_UNLIKELY(!c->sbuf)) goto oops;
	htrdxtn = mio_htrd_getxtn(c->htrd);
	htrdxtn->sck = sck;

	mio_htrd_setrecbs (c->htrd, &client_htrd_recbs);

	return 0;

oops:
	if (c->sbuf) 
	{
		mio_becs_close(c->sbuf);
		c->sbuf = MIO_NULL;
	}
	if (c->htrd)
	{
		mio_htrd_close (c->htrd);
		c->htrd = MIO_NULL;
	}
	return -1;
}

static void fini_client (mio_t* mio, mio_htts_client_t* c)
{
	if (c->sbuf) 
	{
		mio_becs_close(c->sbuf);
		c->sbuf = MIO_NULL;
	}
	if (c->htrd)
	{
		mio_htrd_close (c->htrd);
		c->htrd = MIO_NULL;
	}
}

/* ------------------------------------------------------------------------ */

static int client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck);
printf ("** HTTS - client read   %p  %d -> htts:%p\n", sck, (int)len, sckxtn->htts);
	if (len <= 0)
	{
		mio_dev_sck_halt (sck);
	}
	else if (mio_htrd_feed(sckxtn->c.htrd, buf, len) <= -1)
	{
		mio_dev_sck_halt (sck);
	}

	return 0;
}

static int client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_svc_htts_rsrc_t* rsrc = (mio_svc_htts_rsrc_t*)wrctx;
	if (rsrc) 
	{
		int n;
		if ((n = rsrc->on_write(rsrc, sck)) <= 0)
		{
			mio_svc_htts_rsrc_kill (rsrc);
			/* 0: end of resource
			 * -1: error or incompelete transmission. 
			 *     arrange to close connection regardless of Connection: Keep-Alive or Connection: close */
			if (n <= -1 && mio_dev_sck_write(sck, MIO_NULL, 0, MIO_NULL, MIO_NULL) <= -1) mio_dev_sck_halt (sck);
		}
	}

	return 0; /* if this returns failure, the listener socket gets terminated. it should never return failure. */
}

static void client_on_disconnect (mio_dev_sck_t* sck)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck);
printf ("** HTTS - client disconnect  %p\n", sck);
	fini_client (sck->mio, &sckxtn->c);

	/* TODO: remove it from the list of clients */
}

/* ------------------------------------------------------------------------ */

static int listener_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	return 0;
}

static int listener_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	return 0;
}

static void listener_on_connect (mio_dev_sck_t* sck)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck); /* the contents came from the listening socket */

	if (sck->state & MIO_DEV_SCK_ACCEPTED)
	{
		/* accepted a new client */
		MIO_DEBUG3 (sck->mio, "** HTTS(%p) - accepted... %p %d \n", sckxtn->htts, sck, sck->sck);

		/* the accepted socket inherits various event callbacks. switch some of them to avoid sharing */
		sck->on_read = client_on_read;
		sck->on_write = client_on_write;
		sck->on_disconnect = client_on_disconnect;

		if (init_client(sck->mio, &sckxtn->c, sck) <= -1)
		{
			MIO_INFO2 (sck->mio, "UNABLE TO INITIALIZE NEW CLIENT %p %d\n", sck, (int)sck->sck);
			mio_dev_sck_halt (sck);
		}
	}
	else if (sck->state & MIO_DEV_SCK_CONNECTED)
	{
		/* this will never be triggered as the listing socket never call mio_dev_sck_connect() */
		MIO_DEBUG3 (sck->mio, "** HTTS(%p) - connected... %p %d \n", sckxtn->htts, sck, sck->sck);
	}

	/* MIO_DEV_SCK_CONNECTED must not be seen here as this is only for the listener socket */
}

static void listener_on_disconnect (mio_dev_sck_t* sck)
{
	sck_xtn_t* sckxtn = mio_dev_sck_getxtn(sck);

	switch (MIO_DEV_SCK_GET_PROGRESS(sck))
	{
		case MIO_DEV_SCK_CONNECTING:
			/* only for connecting sockets */
			MIO_INFO1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			/* only for connecting sockets */
			MIO_INFO1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_CONNECTED:
			/* only for connecting sockets */
			MIO_INFO1 (sck->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN %p(%d).......\n", (int)sck->sck);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO2 (sck->mio, "LISTNER SOCKET %p(%d) - SHUTTUING DOWN\n", sck, (int)sck->sck);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			MIO_INFO2 (sck->mio, "LISTENER INCOMING SSL-ACCEPT GOT DISCONNECTED %p(%d) ....\n", sck, (int)sck->sck);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			/* only for sockets accepted by the listeners. will never come here because
			 * the disconnect call for such sockets have been changed in listener_on_connect() */
			MIO_INFO2 (sck->mio, "ACCEPTED SOCKET %p(%d) GOT DISCONNECTED.......\n", sck, (int)sck->sck);
			break;

		default:
			MIO_INFO2 (sck->mio, "SOCKET %p(%d) DISCONNECTED AFTER ALL.......\n", sck, (int)sck->sck);
			break;
	}

	/* the client sockets are finalized in clinet_on_disconnect().
	 * for a listener socket, these fields must be NULL */
	MIO_ASSERT (sck->mio, sckxtn->c.htrd == MIO_NULL);
	MIO_ASSERT (sck->mio, sckxtn->c.sbuf == MIO_NULL);

	sckxtn->htts->lsck = MIO_NULL; /* let the htts service forget about this listening socket */
}


/* ------------------------------------------------------------------------ */

mio_svc_htts_t* mio_svc_htts_start (mio_t* mio, const mio_skad_t* bind_addr)
{
	mio_svc_htts_t* htts = MIO_NULL;
	union
	{
		mio_dev_sck_make_t m;
		mio_dev_sck_bind_t b;
		mio_dev_sck_listen_t l;
	} info;
	sck_xtn_t* sckxtn;

	htts = (mio_svc_htts_t*)mio_callocmem(mio, MIO_SIZEOF(*htts));
	if (MIO_UNLIKELY(!htts)) goto oops;

	htts->mio = mio;
	htts->svc_stop = mio_svc_htts_stop;

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
	htts->lsck = mio_dev_sck_make(mio, MIO_SIZEOF(*sckxtn), &info.m);
	if (!htts->lsck) goto oops;

	sckxtn = (sck_xtn_t*)mio_dev_sck_getxtn(htts->lsck);
	sckxtn->htts = htts;

	MIO_MEMSET (&info, 0, MIO_SIZEOF(info));
	info.b.localaddr = *bind_addr;
	info.b.options = MIO_DEV_SCK_BIND_REUSEADDR | MIO_DEV_SCK_BIND_REUSEPORT;
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

	MIO_DEBUG3 (mio, "STARTED SVC(HTTS) LISTENER %p LISTENER SOCKET %p(%d)\n", htts, htts->lsck, (int)htts->lsck->sck);
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

	MIO_DEBUG3 (mio, "STOPPING SVC(HTTS) %p LISTENER SOCKET %p(%d)\n", htts, htts->lsck, (int)(htts->lsck? htts->lsck->sck: -1));

	/* htts->lsck may be null if the socket has been destroyed for operational error and 
	 * forgotten in the disconnect callback thereafter */
	if (htts->lsck) mio_dev_sck_kill (htts->lsck);

	if (htts->server_name && htts->server_name != htts->server_name_buf) mio_freemem (mio, htts->server_name);

	MIO_SVCL_UNLINK_SVC (htts);
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

mio_svc_htts_rsrc_t* mio_svc_htts_rsrc_make (mio_svc_htts_t* htts, mio_svc_htts_rsrc_on_write_t on_write, mio_svc_htts_rsrc_on_kill_t on_kill, mio_oow_t xtnsize)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_rsrc_t* rsrc;

	rsrc = mio_callocmem(mio, MIO_SIZEOF(*rsrc) + xtnsize);
	if (MIO_UNLIKELY(!rsrc)) return MIO_NULL;

	rsrc->htts = htts;
	rsrc->on_write = on_write;
	rsrc->on_kill = on_kill;

	return rsrc;
}

void mio_svc_htts_rsrc_kill (mio_svc_htts_rsrc_t* rsrc)
{
	mio_t* mio = rsrc->htts->mio;

	if (rsrc->on_kill) rsrc->on_kill (rsrc);
	mio_freemem (mio, rsrc);
}

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_svc_htts_rsrc_getxtn (mio_svc_htts_rsrc_t* rsrc) { return rsrc + 1; }
#else
#define mio_svc_htts_rsrc_getxtn(rsrc) ((void*)((mio_svc_htts_rsrc_t*)rsrc + 1))
#endif

/* ----------------------------------------------------------------- */

int mio_svc_htts_sendrsrc (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_svc_htts_rsrc_t* rsrc, int status_code, mio_http_method_t method, const mio_http_version_t* version, int keepalive)
{
	sck_xtn_t* csckxtn = mio_dev_sck_getxtn(csck);
	mio_bch_t dtbuf[64];
	mio_oow_t x;

	mio_svc_htts_fmtgmtime(htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	x = mio_becs_fmt(csckxtn->c.sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\nConnection: %s\r\n",
		(int)version->major, (int)version->minor,
		(int)status_code, mio_http_status_to_bcstr(status_code), 
		htts->server_name,
		dtbuf, /* Date */
		(keepalive? "keep-alive": "close") /* Connection */
	);
	if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;

	if (rsrc->content_type)
	{
		x = mio_becs_fcat(csckxtn->c.sbuf, "Content-Type: %hs\r\n", rsrc->content_type);
		if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;
	}

	if (rsrc->flags & MIO_SVC_HTTS_RSRC_FLAG_CONTENT_LENGTH_SET)
	{
		x = mio_becs_fcat(csckxtn->c.sbuf, "Content-Length: %ju\r\n", (mio_uintmax_t)rsrc->content_length);
		if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;
	}

	x = mio_becs_fcat(csckxtn->c.sbuf, "\r\n", rsrc->content_length);
	if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;

/* TODO: handle METHOD HEAD... should abort after header trasmission... */

/* TODO: allow extra header items */

/* TODO: use timedwrite? */
	if (mio_dev_sck_write(csck, MIO_BECS_PTR(csckxtn->c.sbuf), MIO_BECS_LEN(csckxtn->c.sbuf), rsrc, MIO_NULL) <= -1)
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
	//sck_xtn_t* csckxtn = (sck_xtn_t*)mio_dev_sck_getxtn(csck);
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

	rsrc = mio_svc_htts_rsrc_make(htts, rsrc_file_on_write, rsrc_file_on_kill, MIO_SIZEOF(*rsrc_file));
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

#if 0
int mio_svc_htts_schedproxy (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req)
{
	1. attempt to connect to the proxy target...
	2. in the mean time, 
	mio_dev_watch (csck, MIO_DEV_WATCH_UPDATE, 0); /* no input, no output watching */

	3. once connected,
	mio_dev_watch (csck, MIO_DEV_WATCH_RENEW, MIO_DEV_EVENT_IN); /* enable input watching. if needed, enable output watching */

	4. start proxying


	5. if one side is stalled, don't read from another side... let the kernel slow the connection...
	   i need to know how may bytes are pending for this.
	   if pending too high, disable read watching... mio_dev_watch (csck, MIO_DEV_WATCH_RENEW, 0);
}
#endif

int mio_svc_htts_sendstatus (mio_svc_htts_t* htts, mio_dev_sck_t* csck, int status_code, mio_http_method_t method, const mio_http_version_t* version, int keepalive, void* extra)
{
/* TODO: change this to use send status */
	sck_xtn_t* csckxtn = mio_dev_sck_getxtn(csck);
	mio_bch_t text[1024]; /* TODO: make this buffer dynamic or scalable */
	mio_bch_t dtbuf[64];
	mio_oow_t x;

	const mio_bch_t* extrapre = ""; 
	const mio_bch_t* extrapst = "";
	const mio_bch_t* extraval = "";

	text[0] = '\0';

	switch (status_code)
	{
#if 0
		case 301: /* Moved Permanently */ 
		case 302: /* Found */
		case 303: /* See Other (since HTTP/1.1) */
		case 307: /* Temporary Redirect (since HTTP/1.1) */
		case 308: /* Permanent Redirect (Experimental RFC; RFC 7238) */
		{
			mio_svc_htts_rsrc_reloc_t* reloc;
			reloc = (mio_htts_rsrc_reloc_t*)extra;
			extrapre = "Location: ";
			extrapst = (reloc->flags & MIO_SVC_HTTS_RSRC_RELOC_APPENDSLASH)? "/\r\n": "\r\n";
			extraval = reloc->target;
			break;
		}
#endif

		case 304:
		case 200:
		case 201:
		case 202:
		case 203:
		case 204:
		case 205:
		case 206:
			/* nothing to do */
			break;


		default:
#if 0
			if (method != MIO_HTTP_HEAD &&
			    htts->opt.rcb.fmterr(htts, client, code, text, MIO_COUNTOF(text)) <= -1) return QSE_NULL;
#endif

			if (status_code == 401)
			{
				extrapre = "WWW-Authenticate: Basic realm=\"";
				extrapst = "\"\r\n";
				extraval = (const mio_bch_t*)extra;
			}

			break;
	}


	mio_svc_htts_fmtgmtime(htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	x = mio_becs_fmt(csckxtn->c.sbuf, "HTTP/%d.%d %d %s\r\nServer: %s\r\nDate: %s\r\nConnection: %s\r\nContent-Type: text/html\r\nContent-Length: %u\r\n%s%s%s\r\n%s",
		version->major, version->minor, status_code, mio_http_status_to_bcstr(status_code), 
		htts->server_name,
		dtbuf, /* DATE */
		(keepalive? "keep-alive": "close"), /* connection */
		mio_count_bcstr(text), /* content length */
		extrapre, extraval, extraval, text
	);
	if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;
	

/* TODO: use timedwrite? */
	if (mio_dev_sck_write(csck, MIO_BECS_PTR(csckxtn->c.sbuf), MIO_BECS_LEN(csckxtn->c.sbuf), MIO_NULL, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (csck);
		return -1;
	}
	else if (!keepalive)
	{
		/* arrange to close the writing end */
		if (mio_dev_sck_write(csck, MIO_NULL, 0, MIO_NULL, MIO_NULL) <= -1) 
		{
			mio_dev_sck_halt (csck);
			return -1;
		}
	}

	return 0;
}

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
