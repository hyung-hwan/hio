#include "mio-http.h"
#include "mio-htrd.h"
#include "mio-pro.h" /* for cgi */
#include "mio-fmt.h"
#include "mio-prv.h"


#include <unistd.h> /* TODO: move file operations to sys-file.XXX */
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>


typedef struct mio_svc_htts_cli_t mio_svc_htts_cli_t;
struct mio_svc_htts_cli_t
{
	/* a listener socket sets htts and sck fields only */
	/* a client sockets uses all the fields in this struct */
	mio_svc_htts_t* htts;
	mio_dev_sck_t* sck;

	mio_skad_t remote_addr;
	mio_skad_t local_addr;
	mio_skad_t orgdst_addr;
	mio_htrd_t* htrd;
	mio_becs_t* sbuf; /* temporary buffer for status line formatting */
	mio_becs_t rbuf; /* data that has been read but unconsumed */

	mio_svc_htts_rsrc_t rsrc; /* list head for resource list */

	mio_svc_htts_cli_t* cli_prev;
	mio_svc_htts_cli_t* cli_next;
};

struct mio_svc_htts_t
{
	MIO_SVC_HEADER;

	mio_dev_sck_t* lsck;
	mio_svc_htts_cli_t cli; /* list head for client list */

	mio_bch_t* server_name;
	mio_bch_t server_name_buf[64];
};

struct mio_svc_httc_t
{
	MIO_SVC_HEADER;
};

/* ------------------------------------------------------------------------- */

#define CLIL_APPEND_CLI(lh,cli) do { \
	(cli)->cli_next = (lh); \
	(cli)->cli_prev = (lh)->cli_prev; \
	(cli)->cli_prev->cli_next = (cli); \
	(lh)->cli_prev = (cli); \
} while(0)

#define CLIL_UNLINK_CLI(cli) do { \
	(cli)->cli_prev->cli_next = (cli)->cli_next; \
	(cli)->cli_next->cli_prev = (cli)->cli_prev; \
} while (0)

#define CLIL_UNLINK_CLI_CLEAN(cli) do { \
	(cli)->cli_prev->cli_next = (cli)->cli_next; \
	(cli)->cli_next->cli_prev = (cli)->cli_prev; \
	(cli)->cli_prev = (cli); \
	(cli)->cli_next = (cli); \
} while (0)

#define CLIL_INIT(lh) ((lh)->cli_next = (lh)->cli_prev = lh)
#define CLIL_FIRST_CLI(lh) ((lh)->cli_next)
#define CLIL_LAST_CLI(lh) ((lh)->cli_prev)
#define CLIL_IS_EMPTY(lh) (CLIL_FIRST_CLI(lh) == (lh))
#define CLIL_IS_NIL_CLI(lh,cli) ((cli) == (lh))


/* ------------------------------------------------------------------------- */

#define RSRCL_APPEND_RSRC(lh,rsrc) do { \
	(rsrc)->rsrc_next = (lh); \
	(rsrc)->rsrc_prev = (lh)->rsrc_prev; \
	(rsrc)->rsrc_prev->rsrc_next = (rsrc); \
	(lh)->rsrc_prev = (rsrc); \
} while(0)

#define RSRCL_UNLINK_RSRC(rsrc) do { \
	(rsrc)->rsrc_prev->rsrc_next = (rsrc)->rsrc_next; \
	(rsrc)->rsrc_next->rsrc_prev = (rsrc)->rsrc_prev; \
} while (0)

#define RSRCL_INIT(lh) ((lh)->rsrc_next = (lh)->rsrc_prev = lh)
#define RSRCL_FIRST_RSRC(lh) ((lh)->rsrc_next)
#define RSRCL_LAST_RSRC(lh) ((lh)->rsrc_prev)
#define RSRCL_IS_EMPTY(lh) (RSRCL_FIRST_RSRC(lh) == (lh))
#define RSRCL_IS_NIL_RSRC(lh,rsrc) ((rsrc) == (lh))

/* ------------------------------------------------------------------------- */

struct htrd_xtn_t
{
	mio_dev_sck_t* sck;
};
typedef struct htrd_xtn_t htrd_xtn_t;
/* ------------------------------------------------------------------------ */

static int test_func_handler (int rfd, int wfd)
{
	int i;

	/* you can read the post data from rfd;
	 * you can write result to wfd */
	write (wfd, "Content-Type: text/plain\r\n\r\n", 28);
	for (i = 0 ; i < 10; i++)
	{
		write (wfd, "hello\n", 6);
		sleep (1);
	}
	return -1;
}

static int process_request (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, int peek)
{
	//server_xtn_t* server_xtn = GET_SERVER_XTN(htts, client->server);
	//mio_htts_task_t* task;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
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
			if (mth == MIO_HTTP_POST && !(req->flags & (MIO_HTRE_ATTR_LENGTH | MIO_HTRE_ATTR_CHUNKED)))
			{
				/* POST without Content-Length nor not chunked */
				mio_htre_discardcontent (req); 
				/* 411 Length Required - can't keep alive. Force disconnect */
				req->flags &= ~MIO_HTRE_ATTR_KEEPALIVE; /* to cause sendstatus() to close */
				if (mio_svc_htts_sendstatus(htts, csck, req, 411, MIO_NULL) <= -1) mio_dev_sck_halt (csck); /*TODO: redo this sendstatus */
			}
			else
			{
				const mio_bch_t* qpath = mio_htre_getqpath(req);
				if (mio_svc_htts_docgi(htts, csck, req, "") <= -1)
				{
					mio_dev_sck_halt (csck);
				}
#if 0
/*
				if (mio_comp_bcstr(qpath, "/testfunc", 0) == 0)
				{
					if (mio_svc_htts_sendcgi(htts, csck, test_func_handler, req) <= -1)
					{
						mio_htre_discardcontent (req);
						mio_dev_sck_halt (csck);
					}
				}
				else */if (mio_svc_htts_sendfile(htts, csck, qpath, 200, mth, mio_htre_getversion(req), (req->flags & MIO_HTRE_ATTR_KEEPALIVE)) <= -1)
				{
					mio_htre_discardcontent (req);
					mio_dev_sck_halt (csck);
				}
#endif
			}
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
			mio_htrd_dummify (cli->htrd);
			/* connect is not handled in the peek mode. create a proxy resource here */
			/* TODO: arrange to forward all raw bytes */
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
	mio_svc_htts_cli_t* sckxtn = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(htrdxtn->sck);
	return process_request(sckxtn->htts, htrdxtn->sck, req, 1);
}

static int client_htrd_poke_request (mio_htrd_t* htrd, mio_htre_t* req)
{
	htrd_xtn_t* htrdxtn = (htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_svc_htts_cli_t* sckxtn = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(htrdxtn->sck);
	return process_request(sckxtn->htts, htrdxtn->sck, req, 0);
}

static mio_htrd_recbs_t client_htrd_recbs =
{
	client_htrd_peek_request,
	client_htrd_poke_request
};

static int init_client (mio_svc_htts_cli_t* cli, mio_dev_sck_t* sck)
{
	htrd_xtn_t* htrdxtn;

	/* the htts field must be filled with the same field in the listening socket upon accept() */
	MIO_ASSERT (sck->mio, cli->htts != MIO_NULL);
	MIO_ASSERT (sck->mio, cli->sck == cli->htts->lsck); /* the field should still point to the listner socket */
	MIO_ASSERT (sck->mio, sck->mio == cli->htts->mio);

	cli->htrd = mio_htrd_open(sck->mio, MIO_SIZEOF(*htrdxtn));
	if (MIO_UNLIKELY(!cli->htrd)) goto oops;

	cli->sbuf = mio_becs_open(sck->mio, 0, 2048);
	if (MIO_UNLIKELY(!cli->sbuf)) goto oops;

	/* the following actions from here down must not fail. otherwise, 
	 * some more statements will have to be added under the oops: label */
	mio_becs_init(&cli->rbuf, sck->mio, 0); /* it never fails if the capacity is zero */

	htrdxtn = mio_htrd_getxtn(cli->htrd);
	htrdxtn->sck = sck; /* TODO: remember cli instead? */

	mio_htrd_setrecbs (cli->htrd, &client_htrd_recbs);
	RSRCL_INIT (&cli->rsrc);

	cli->sck = sck;
	CLIL_APPEND_CLI (&cli->htts->cli, cli);
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

	while (!RSRCL_IS_EMPTY(&cli->rsrc)) 
		mio_svc_htts_rsrc_kill (RSRCL_FIRST_RSRC(&cli->rsrc));

	mio_becs_fini (&cli->rbuf);
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

	CLIL_UNLINK_CLI_CLEAN (cli);

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
	MIO_ASSERT (mio, RSRCL_IS_EMPTY(&cli->rsrc));

	if (len <= -1)
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - unable to read client socket %p(%d)\n", cli->htts, sck, (int)sck->hnd);
		goto oops;
	}

	if (len == 0)
	{
		mio_dev_sck_halt (sck);
		goto oops;
	}

feed_to_htrd:
	if ((x = mio_htrd_feed(cli->htrd, buf, len, &rem)) <= -1) 
	{
/* in some cases, we may have to send  some http response depending on the failure type */
/* BADRE -> bad request? */
printf ("** HTTS - client htrd feed failure socket(%p) - %d\n", sck, x);
		/* TODO: either send bad request or server failure ... */
		mio_dev_sck_halt (sck);
	}
	else if (rem > 0)
	{
		/* the peek and poke handlers are called by mio_htrd_feed(). 
		 * mio_htrd_feed() returns after the handlers are finished. */

		if (RSRCL_IS_EMPTY(&cli->rsrc))
		{
			/* no resource has been scheduled for the request. or the schedule job has been completed*/
			len = rem;
			buf = (mio_uint8_t*)buf + (len - rem);
			goto feed_to_htrd;
		}

/* TODO: don't use mio_becs_ncpy. this is not a string. no null termination is need */
		MIO_ASSERT (mio, MIO_BECS_LEN(&cli->rbuf) == 0);
		if (mio_becs_ncpy(&cli->rbuf, (mio_bch_t*)buf + (len - rem), rem) == (mio_oow_t)-1) goto oops;
	}

	return 0;

oops:
	mio_dev_sck_halt (sck);
}

static int listener_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	mio_svc_htts_rsrc_t* rsrc = (mio_svc_htts_rsrc_t*)wrctx;

	MIO_ASSERT (sck->mio, sck != cli->htts->lsck);

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
			MIO_DEBUG2 (sck->mio, "UNABLE TO INITIALIZE NEW CLIENT %p %d\n", sck, (int)sck->hnd);
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
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);

	switch (MIO_DEV_SCK_GET_PROGRESS(sck))
	{
		case MIO_DEV_SCK_CONNECTING:
			/* only for connecting sockets */
			MIO_DEBUG1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			/* only for connecting sockets */
			MIO_DEBUG1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_CONNECTED:
			/* only for connecting sockets */
			MIO_DEBUG1 (sck->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN %p(%d).......\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_DEBUG2 (sck->mio, "LISTNER SOCKET %p(%d) - SHUTTUING DOWN\n", sck, (int)sck->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL: /* special case. */
			/* this progress code indicates that the ssl-level accept failed.
			 * on_disconnected() with this code is called without corresponding on_connect(). 
			 * the cli extension are is not initialized yet */
			MIO_ASSERT (sck->mio, sck != cli->sck);
			MIO_ASSERT (sck->mio, cli->sck == cli->htts->lsck); /* the field is a copy of the extension are of the listener socket. so it should point to the listner socket */
			MIO_DEBUG2 (sck->mio, "LISTENER UNABLE TO SSL-ACCEPT CLIENT %p(%d) ....%p\n", sck, (int)sck->hnd);
			return;

		case MIO_DEV_SCK_ACCEPTED:
			/* only for sockets accepted by the listeners. will never come here because
			 * the disconnect call for such sockets have been changed in listener_on_connect() */
			MIO_DEBUG2 (sck->mio, "ACCEPTED CLIENT SOCKET %p(%d) GOT DISCONNECTED.......\n", sck, (int)sck->hnd);
			break;

		default:
			MIO_DEBUG2 (sck->mio, "SOCKET %p(%d) DISCONNECTED AFTER ALL.......\n", sck, (int)sck->hnd);
			break;
	}

	if (sck == cli->htts->lsck)
	{
		/* the listener socket has these fields set to NULL */
		MIO_ASSERT (sck->mio, cli->htrd == MIO_NULL);
		MIO_ASSERT (sck->mio, cli->sbuf == MIO_NULL);

		MIO_DEBUG2 (sck->mio, "HTTS(%p) - listener socket disconnect %p\n", cli->htts, sck);
		cli->htts->lsck = MIO_NULL; /* let the htts service forget about this listening socket */
	}
	else
	{
		/* client socket */
		MIO_DEBUG2 (sck->mio, "HTTS(%p) - client socket disconnect %p\n", cli->htts, sck);
		MIO_ASSERT (sck->mio, cli->sck == sck);
		fini_client (cli);
	}
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
	mio_svc_htts_cli_t* cli;

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
	info.b.options = MIO_DEV_SCK_BIND_REUSEADDR | MIO_DEV_SCK_BIND_REUSEPORT;
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
	CLIL_INIT (&htts->cli);

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

	while (!CLIL_IS_EMPTY(&htts->cli))
	{
		mio_svc_htts_cli_t* cli = CLIL_FIRST_CLI(&htts->cli);
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

mio_svc_htts_rsrc_t* mio_svc_htts_rsrc_make (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_svc_htts_rsrc_on_write_t on_write, mio_svc_htts_rsrc_on_kill_t on_kill, mio_oow_t xtnsize)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_cli_t* cli = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(csck);
	mio_svc_htts_rsrc_t* rsrc;
	

	rsrc = mio_callocmem(mio, MIO_SIZEOF(*rsrc) + xtnsize);
	if (MIO_UNLIKELY(!rsrc)) return MIO_NULL;

	rsrc->htts = htts;
	rsrc->on_write = on_write;
	rsrc->on_kill = on_kill;

	RSRCL_APPEND_RSRC (&cli->rsrc, rsrc);
	return rsrc;
}

void mio_svc_htts_rsrc_kill (mio_svc_htts_rsrc_t* rsrc)
{
printf ("RSRC KILL >>>>> htts=> %p\n", rsrc->htts);
	mio_t* mio = rsrc->htts->mio;

	if (rsrc->on_kill) rsrc->on_kill (rsrc);

	RSRCL_UNLINK_RSRC (rsrc);
	mio_freemem (mio, rsrc);
}

#if defined(MIO_HAVE_INLINE)
static MIO_INLINE void* mio_svc_htts_rsrc_getxtn (mio_svc_htts_rsrc_t* rsrc) { return rsrc + 1; }
#else
#define mio_svc_htts_rsrc_getxtn(rsrc) ((void*)((mio_svc_htts_rsrc_t*)rsrc + 1))
#endif

/* ----------------------------------------------------------------- */

enum cgi_state_res_mode_t
{
	CGI_STATE_RES_MODE_CHUNKED,
	CGI_STATE_RES_MODE_CLOSE,
	CGI_STATE_RES_MODE_LENGTH
};
typedef enum cgi_state_res_mode_t cgi_state_res_mode_t;

#define CGI_STATE_PENDING_IO_THRESHOLD 5

struct cgi_state_t
{
	/**** CHANGE THESE FIELDS AFTER RSRC CLEANUP */
	mio_svc_htts_t* htts;
	mio_svc_htts_rsrc_t* rsrc_prev;
	mio_svc_htts_rsrc_t* rsrc_next;
	mio_svc_htts_rsrc_on_kill_t on_kill;
	/**** CHANGE THESE FIELDS AFTER RSRC CLEANUP */

	mio_oow_t num_pending_writes_to_client;
	mio_oow_t num_pending_writes_to_peer;
	mio_dev_pro_t* peer;
	mio_htrd_t* peer_htrd;
	mio_svc_htts_cli_t* cli;
	mio_http_version_t req_version; /* client request */
	mio_oow_t req_content_length; /* client request content length */

	cgi_state_res_mode_t res_mode_to_cli;
	unsigned int peer_eof: 1;

	mio_dev_sck_on_read_t cli_org_on_read;
	mio_dev_sck_on_write_t cli_org_on_write;
};
typedef struct cgi_state_t cgi_state_t;

struct cgi_peer_xtn_t
{
	cgi_state_t* state;
};
typedef struct cgi_peer_xtn_t cgi_peer_xtn_t;

static void cgi_state_halt (cgi_state_t* cgi_state)
{
	MIO_ASSERT (cgi_state->cli->htts->mio, cgi_state->cli != MIO_NULL);
	MIO_ASSERT (cgi_state->cli->htts->mio, cgi_state->cli->sck != MIO_NULL);

	MIO_DEBUG4 (cgi_state->cli->htts->mio, "HTTS(%p) - halting cgi state %p(client=%p,peer=%p)\n", cgi_state->cli->htts, cgi_state, cgi_state->cli->sck, cgi_state->peer);

	mio_dev_sck_halt (cgi_state->cli->sck);
	/* check for peer as it may not have been started */
	if (cgi_state->peer) mio_dev_pro_halt (cgi_state->peer);

/* TODO: when to destroy cgi_state? */

}

static void cgi_state_on_kill (cgi_state_t* cgi_state)
{
printf ("**** CGI_STATE_ON_KILL \n");
	if (cgi_state->peer)
	{
		cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(cgi_state->peer);
		cgi_peer->state = MIO_NULL;

		mio_dev_pro_kill (cgi_state->peer);
		cgi_state->peer = MIO_NULL;
	}

	if (cgi_state->peer_htrd)
	{
		cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
		cgi_peer->state = MIO_NULL;

		mio_htrd_close (cgi_state->peer_htrd);
		cgi_state->peer_htrd = MIO_NULL;
	}

	if (cgi_state->cli_org_on_read)
	{
		cgi_state->cli->sck->on_read = cgi_state->cli_org_on_read;
		cgi_state->cli_org_on_read = MIO_NULL;
	}

	if (cgi_state->cli_org_on_write)
	{
		cgi_state->cli->sck->on_write = cgi_state->cli_org_on_write;
		cgi_state->cli_org_on_write = MIO_NULL;
	}

	cgi_state->cli->htrd->recbs.push_content = MIO_NULL;

	if (MIO_BECS_LEN(&cgi_state->cli->rbuf) > 0)
	{
		mio_svc_htts_cli_t* cli = cgi_state->cli;
		/* TODO: trigger cli->on_read again (). */

		/* mio_arrange_fake_on_read (mio, cli->sck, data, len, MIO_NULL);??? can i do this instead of calling on-read() here? */
		cli->sck->on_read (cli->sck, MIO_BECS_PTR(&cli->rbuf), MIO_BECS_LEN(&cli->rbuf), MIO_NULL); /* TOOD: better error handling?? */

		mio_becs_clear (&cli->rbuf);
	}
}

static int cgi_send_simple_status_to_client (cgi_state_t* cgi_state, int status_code)
{
	mio_svc_htts_cli_t* cli = cgi_state->cli;
	mio_bch_t dtbuf[64];

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
		cgi_state->req_version.major, cgi_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (mio_oow_t)-1) return -1;

	return mio_dev_sck_write(cli->sck, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf), MIO_NULL, MIO_NULL);
}

static void cgi_peer_on_close (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	if (!cgi_state) return; /* cgi state already gone */

printf (">> cgi_state %p\n", cgi_state);
printf (">> cgi_state->peer %p\n", cgi_state->peer);
printf (">> cgi_state->cli %p\n", cgi_state->cli);
printf (">> cgi_state->cli->htts %p\n", cgi_state->cli->htts);

	if (sid == MIO_DEV_PRO_MASTER)
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - peer %p(pid=%d) closing master\n", cgi_state->cli->htts, pro, (int)pro->child_pid);
		cgi_peer->state->peer = MIO_NULL; /* clear this peer from the state */
	}
	else
	{
		MIO_ASSERT (mio, cgi_state->peer == pro);

		MIO_DEBUG4 (mio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi_state->cli->htts, pro, (int)pro->child_pid, sid);
		if (sid == MIO_DEV_PRO_OUT)
		{
			cgi_state->peer_eof = 1;
		}
	}
}

static int cgi_peer_on_read (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (mio, sid == MIO_DEV_PRO_OUT); /* since MIO_DEV_PRO_ERRTONUL is used, there should be no input from MIO_DEV_PRO_ERR */

	if (dlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - read error from peer %p(pid=%u)\n", cgi_state->cli->htts, pro, (unsigned int)pro->child_pid);
		goto oops;
	}

	if (cgi_state->peer_eof) return 0; /* will ignore */

	if (dlen == 0)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from peer %p(pid=%u)\n", cgi_state->cli->htts, pro, (unsigned int)pro->child_pid);
		cgi_state->peer_eof = 1;

		/* don't care about cgi_state->num_pending_writes_to_client since this is the last read */
		if (cgi_state->res_mode_to_cli == CGI_STATE_RES_MODE_CHUNKED && 
		    mio_dev_sck_write(cgi_state->cli->sck, "0\r\n\r\n", 5, MIO_NULL, MIO_NULL) <= -1) goto oops;
	}
	else
	{
		mio_oow_t rem;
printf ( "QQQQQQQQQQQQQQQQQQ>>>>>>>>>>>>>>>>>> feeding to peer htrd.... dlen => %d\n", (int)dlen);
		if (mio_htrd_feed(cgi_state->peer_htrd, data, dlen, &rem) <= -1) 
		{
printf ( "FEEDING FAILURE TO PEER HTDDRD.. dlen => %d\n", (int)dlen);
			cgi_send_simple_status_to_client (cgi_state, 500); /* don't care about error */
			goto oops;
		}
		if (rem > 0) 
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
			printf ("AAAAAAAAAAAAAAAAAa EEEEEXcessive DATA..................\n");
			cgi_state->peer_eof = 1; /* EOF or use another bit-field? */
		}
	}

	return 0;

oops:
	cgi_state_halt (cgi_state);
	return 0;
}

static int cgi_peer_capture_header (mio_htre_t* req, const mio_bch_t* key, const mio_htre_hdrval_t* val, void* ctx)
{
	mio_svc_htts_cli_t* cli = (mio_svc_htts_cli_t*)ctx;

	/* capture a header except Status, Connection, Transfer-Encoding, and Server */
	if (mio_comp_bcstr(key, "Status", 1) != 0 &&
	    mio_comp_bcstr(key, "Connection", 1) != 0 &&
	    mio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    mio_comp_bcstr(key, "Server", 1) != 0 &&
	    mio_comp_bcstr(key, "Date", 1) != 0)
	{
		do
		{
			if (mio_becs_cat(cli->sbuf, key) == (mio_oow_t)-1 ||
			    mio_becs_cat(cli->sbuf, ": ") == (mio_oow_t)-1 ||
			    mio_becs_cat(cli->sbuf, val->ptr) == (mio_oow_t)-1 ||
			    mio_becs_cat(cli->sbuf, "\r\n") == (mio_oow_t)-1)
			{
				return -1;
			}

			val = val->next;
		}
		while (val);
	}

	return 0;
}
static int cgi_peer_htrd_peek (mio_htrd_t* htrd, mio_htre_t* req)
{
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;
	mio_svc_htts_cli_t* cli = cgi_state->cli;
	mio_bch_t dtbuf[64];
	int status_code;

	if (req->attr.content_length)
	{
// TOOD: remove content_length if content_length is negative or not numeric.
		cgi_state->res_mode_to_cli = CGI_STATE_RES_MODE_LENGTH;
	}

	if (req->attr.status)
	{
	}
	else
	{
	}

	status_code = 200;

printf ("CGI PEER HTRD PEEK...\n");
	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %hs\r\n",
		cgi_state->req_version.major, cgi_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (mio_oow_t)-1) return -1;

	if (mio_htre_walkheaders(req, cgi_peer_capture_header, cli) <= -1) return -1;

	switch (cgi_state->res_mode_to_cli)
	{
		case CGI_STATE_RES_MODE_CHUNKED:
			if (mio_becs_cat(cli->sbuf, "Transfer-Encoding: chunked\r\n") == (mio_oow_t)-1) return -1;
			/*if (mio_becs_cat(cli->sbuf, "Connection: keep-alive\r\n") == (mio_oow_t)-1) return -1;*/
			break;

		case CGI_STATE_RES_MODE_CLOSE:
			if (mio_becs_cat(cli->sbuf, "Connection: close\r\n") == (mio_oow_t)-1) return -1;
			break;

		case CGI_STATE_RES_MODE_LENGTH:
			/* TODO: Keep-Alive if explicit in http/1.0 .
			 *       Keep-Alive not needed if http/1.1 or later */
			break;
	}

	if (mio_becs_cat(cli->sbuf, "\r\n") == (mio_oow_t)-1) return -1;
	return mio_dev_sck_write(cli->sck, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf), MIO_NULL, MIO_NULL);
}

static int cgi_peer_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (cgi_state->cli->htts->mio, htrd == cgi_state->peer_htrd);

	switch (cgi_state->res_mode_to_cli)
	{
		case CGI_STATE_RES_MODE_CHUNKED:
		{
			mio_iovec_t iov[3];
			mio_bch_t lbuf[16];
			mio_oow_t llen;

			llen = mio_fmt_uintmax_to_bcstr(lbuf, MIO_COUNTOF(lbuf) - 2, dlen, 16 | MIO_FMT_UINTMAX_UPPERCASE, 0, '\0', MIO_NULL);
			lbuf[llen++] = '\r';
			lbuf[llen++] = '\n';

			iov[0].iov_ptr = lbuf;
			iov[0].iov_len = llen;
			iov[1].iov_ptr = data;
			iov[1].iov_len = dlen;
			iov[2].iov_ptr = "\r\n";
			iov[2].iov_len = 2;

			cgi_state->num_pending_writes_to_client++;
			if (mio_dev_sck_writev(cgi_state->cli->sck, iov, MIO_COUNTOF(iov), MIO_NULL, MIO_NULL) <= -1)
			{
				cgi_state->num_pending_writes_to_client--;
				goto oops;
			}
			break;
		}

		case CGI_STATE_RES_MODE_CLOSE:
		case CGI_STATE_RES_MODE_LENGTH:
			cgi_state->num_pending_writes_to_client++;
			if (mio_dev_sck_write(cgi_state->cli->sck, data, dlen, MIO_NULL, MIO_NULL) <= -1)
			{
				cgi_state->num_pending_writes_to_client--;
				goto oops;
			}
	}

	if (cgi_state->num_pending_writes_to_client > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) goto oops;
	}

	return 0;

oops:
	return -1;
}

static mio_htrd_recbs_t cgi_peer_htrd_recbs =
{
	cgi_peer_htrd_peek,
	MIO_NULL,
	cgi_peer_htrd_push_content
};

static int cgi_client_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	htrd_xtn_t* htrdxtn = mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = RSRCL_FIRST_RSRC(&cli->rsrc);

	MIO_ASSERT (sck->mio, cli->sck == sck);

	cgi_state->num_pending_writes_to_peer++;
	if (mio_dev_pro_write(cgi_state->peer, data, dlen, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_peer--;
		goto oops;
	}

#if 0
	if (cgi_state->num_pending_writes_to_peer > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_sck_read(cli->sck, 0) <= -1) goto oops; /* disable input watching */
	}
#endif

	return 0;

oops:
	return -1;
}

static int cgi_peer_on_write (mio_dev_pro_t* pro, mio_iolen_t wrlen, void* wrctx)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (mio, cgi_state->peer == pro);

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - unable to write to peer %p(pid=%u)\n", cgi_state->cli->htts, pro, (int)pro->child_pid);
		goto oops;
	}

	cgi_state->num_pending_writes_to_peer--;
	if (cgi_state->num_pending_writes_to_peer == CGI_STATE_PENDING_IO_THRESHOLD)
	{
		// TODO: check if (cli has anything to read...) if so ...
		if (mio_dev_sck_read(cgi_state->cli->sck, 1) <= -1) goto oops;
	}
	return 0;

oops:
	cgi_state_halt (cgi_state);
	return 0;
}


static int cgi_client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = RSRCL_FIRST_RSRC(&cli->rsrc);

	MIO_ASSERT (mio, sck == cli->sck);

printf ("** HTTS - cgi client read   %p  %d -> htts:%p\n", sck, (int)len, cli->htts);

	if (len <= -1)
	{
		/* read error */
		MIO_DEBUG2 (cli->htts->mio, "HTTPS(%p) - read error on client socket %p(%d)\n", sck, (int)sck->hnd);
		goto oops;
	}

	if (!cgi_state->peer)
	{
		/* the peer is gone */
		goto oops; /* do what?  just return 0? */
	}

	if (len == 0)
	{
		/* EOF on the client side. arrange to close */
printf ("asking pro to close...\n");
		if (mio_dev_pro_write(cgi_state->peer, MIO_NULL, 0, MIO_NULL) <= -1) goto oops;
	}
	else
	{
		mio_oow_t rem;
/* EOF if it got content as long as cgi_state->req_content_length??? */
		if (mio_htrd_feed(cli->htrd, buf, len, &rem) <= -1)
		{
printf ("FAILED TO FEED TH CLIENT HTRD......\n");
			goto oops;
		}

		if (rem > 0)
		{
/* giving excessive data... */
		}
	}

	return 0;

oops:
/* TODO: arrange to kill the entire cgi_state??? */
	cgi_state_halt (cgi_state);
	return 0;
}

static int cgi_client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = RSRCL_FIRST_RSRC(&cli->rsrc);

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - unable to write to client socket %p(%d)\n", sck->mio, sck, (int)sck->hnd);
		goto oops;
	}

printf ("MANAGED TO WRITE TO CLIENT>... %d\n", (int)wrlen);
	cgi_state->num_pending_writes_to_client--;
	if (cgi_state->peer && cgi_state->num_pending_writes_to_client == CGI_STATE_PENDING_IO_THRESHOLD)
	{
		// TODO: check if there is anything to read... */
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 1) <= -1) goto oops;
	}

/* in case the peer died before peer read handler reads EOF */
	if (cgi_state->peer_eof && cgi_state->num_pending_writes_to_client <= 0)
	{
		/*if (no_more_input is to be read from the client)*/
		cgi_state_halt (cgi_state); // must not use this... must kill the state ... 
		/* finished writing all */
	}

	return 0;

oops:
	cgi_state_halt (cgi_state);
	return 0;
}

static int get_request_content_length (mio_htre_t* req, mio_oow_t* len)
{
	if (req->state & (MIO_HTRE_COMPLETED | MIO_HTRE_DISCARDED))
	{
		/* no more content to receive */
		*len = mio_htre_getcontentlen(req);
	}
	else if (req->flags & MIO_HTRE_ATTR_CHUNKED)
	{
		/* "Transfer-Encoding: chunked" take precedence over "Content-Length: XXX". 
		 *
		 * [RFC7230]
		 *  If a message is received with both a Transfer-Encoding and a
		 *  Content-Length header field, the Transfer-Encoding overrides the
		 *  Content-Length. */

		return -1; /* unable to determine content-length in advance */
	}
	else if (req->flags & MIO_HTRE_ATTR_LENGTH)
	{
		*len = req->attr.content_length;
	}
	else
	{
		/* no content */
		*len = 0;
	}
	return 0;
}

int mio_svc_htts_docgi (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	cgi_state_t* cgi_state = MIO_NULL;
	cgi_peer_xtn_t* cgi_peer;
	mio_oow_t req_content_length;
	mio_dev_pro_make_t mi;

	if (get_request_content_length(req, &req_content_length) <= -1)
	{
		/* chunked - the length is not know in advance */
		/* don't start the process yet. */

		/* option 1. send 411 Length Required */
		/* option 2. support chunked message box in the request */
		/* option 3[non standard]. set CONTENT_LENGTH to -1 and indicate the end of INPUT by sending EOF */
		req_content_length = MIO_TYPE_MAX(mio_oow_t); /* TODO: change type or add another variable ? */
	}

	MIO_MEMSET (&mi, 0, MIO_SIZEOF(mi));
	mi.flags = MIO_DEV_PRO_READOUT | MIO_DEV_PRO_ERRTONUL | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	mi.cmd = mio_htre_getqpath(req); /* TODO: combine it with docroot */
	mi.on_read = cgi_peer_on_read;
	mi.on_write = cgi_peer_on_write;
	mi.on_close = cgi_peer_on_close;

	cgi_state = mio_callocmem(mio, MIO_SIZEOF(*cgi_state));
	if (MIO_UNLIKELY(!cgi_state)) goto oops;

	cgi_state->htts = htts; /*TODO: delete this field after rsrd renewal? */
	cgi_state->on_kill = cgi_state_on_kill;

	cgi_state->cli = cli;
	/*cgi_state->num_pending_writes_to_client = 0;
	cgi_state->num_pending_writes_to_peer = 0;*/
	cgi_state->req_version = *mio_htre_getversion(req);
	cgi_state->req_content_length = req_content_length;

	cgi_state->cli_org_on_read = csck->on_read;
	cgi_state->cli_org_on_write = csck->on_write;
	csck->on_read = cgi_client_on_read;
	csck->on_write = cgi_client_on_write;

	RSRCL_APPEND_RSRC (&cli->rsrc, cgi_state); /* attach it to the client information */

/* TODO: create cgi environment variables... */
/* TODO:
 * never put Expect: 100-continue  to environment variable
 */
	cgi_state->peer = mio_dev_pro_make(mio, MIO_SIZEOF(*cgi_peer), &mi);
	if (MIO_UNLIKELY(!cgi_state->peer)) goto oops;
	cgi_peer = mio_dev_pro_getxtn(cgi_state->peer);
	cgi_peer->state = cgi_state;

	cgi_state->peer_htrd = mio_htrd_open(mio, MIO_SIZEOF(*cgi_peer));
	if (MIO_UNLIKELY(!cgi_state->peer_htrd)) goto oops;
	mio_htrd_setopt (cgi_state->peer_htrd, MIO_HTRD_SKIPINITIALLINE | MIO_HTRD_RESPONSE);
	mio_htrd_setrecbs (cgi_state->peer_htrd, &cgi_peer_htrd_recbs);

	cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
	cgi_peer->state = cgi_state;
/* TODO: any other handling before this? */
/* I CAN't SIMPLY WRITE LIKE THIS, the cgi must require this.
 * send contents depending how cgi want Expect: continue handling to be handled */

	if (req->flags & MIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? cgi? or the http server? */
		if (mio_comp_http_version_numbers(&req->version, 1, 1) >= 0 && req_content_length > 0)
		{
			/* 
			 * Don't send 100 Continue if http verions is lower than 1.1
			 * [RFC7231] 
			 *  A server that receives a 100-continue expectation in an HTTP/1.0
			 *  request MUST ignore that expectation.
			 *
			 * Don't send 100 Continue if expected content lenth is 0. 
			 * [RFC7231]
			 *  A server MAY omit sending a 100 (Continue) response if it has
			 *  already received some or all of the message body for the
			 *  corresponding request, or if the framing indicates that there is
			 *  no message body.
			 */
			mio_bch_t msgbuf[64];
			mio_oow_t msglen;

			msglen = mio_fmttobcstr(mio, msgbuf, MIO_COUNTOF(msgbuf), "HTTP/%d.%d 100 Continue\r\n\r\n", cgi_state->req_version.major, cgi_state->req_version.minor);
			cgi_state->num_pending_writes_to_client++;
			if (mio_dev_sck_write(csck, msgbuf, msglen, MIO_NULL, MIO_NULL) <= -1)
			{
				cgi_state->num_pending_writes_to_client--;
				goto oops;
			}
		}
	}
	else if (req->flags & MIO_HTRE_ATTR_EXPECT)
	{
		mio_htre_discardcontent (req);
		req->flags &= ~MIO_HTRE_ATTR_KEEPALIVE; /* to cause sendstatus() to close */
		if (mio_svc_htts_sendstatus(htts, csck, req, 417, MIO_NULL) <= -1) goto oops;
	}

	if (req_content_length > 0)
	{
		if (mio_htre_getcontentlen(req) > 0)
		{
			/* this means that it's called from the peek context */
			cgi_state->num_pending_writes_to_peer++;
			if (mio_dev_pro_write(cgi_state->peer, mio_htre_getcontentptr(req), mio_htre_getcontentlen(req), MIO_NULL) <= -1) 
			{
				cgi_state->num_pending_writes_to_peer--;
				goto oops;
			}
		}
	}

	if (req->state & (MIO_HTRE_COMPLETED | MIO_HTRE_DISCARDED))
	{
		/* disable input watching from the client */
		if (mio_dev_sck_read(csck, 0) <= -1) goto oops;
	}
	else
	{
		if (req_content_length > 0)
		{
			cgi_state->cli->htrd->recbs.push_content = cgi_client_htrd_push_content;
		}
	}

	/* this may change later if Content-Length is included in the cgi output */
	cgi_state->res_mode_to_cli = (req->flags & MIO_HTRE_ATTR_KEEPALIVE)? CGI_STATE_RES_MODE_CHUNKED: CGI_STATE_RES_MODE_CLOSE;
	return 0;

oops:
	MIO_DEBUG2 (mio, "HTTS(%p) - FAILURE in docgi - socket(%p)\n", htts, csck);
	cgi_state->on_kill (cgi_state); /* TODO: call rsrc_free... */
	mio_dev_sck_halt (csck);
	return -1;
}

/* ----------------------------------------------------------------- */

int mio_svc_htts_dofile (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot)
{
	switch (mio_htre_getqmethodtype(req))
	{
		case MIO_HTTP_OPTIONS:
			mio_htre_discardcontent (req);
			/* TODO: return 200 with Allow: OPTIONS, HEAD, GET, POST, PUT, DELETE... */
			/* However, if there is access configuration, the item list must reflect it */
			break;

		case MIO_HTTP_HEAD:
		case MIO_HTTP_GET:
		case MIO_HTTP_POST:
			mio_htre_discardcontent (req);
			break;

		case MIO_HTTP_PUT:
			break;

		case MIO_HTTP_DELETE:
			mio_htre_discardcontent (req);
			break;

		default:
			/* method not allowed */
			mio_htre_discardcontent (req);
			/* schedule to send 405  */
			break;
	}

	return 0;
}

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

typedef void (*mio_svc_htts_rsrc_cgi_t) (
	int   rfd,
	int   wfd
);

struct mio_svc_htts_rsrc_cgi_peer_t
{
	int rfd;
	int wfd;
};
typedef struct mio_svc_htts_rsrc_cgi_peer_t mio_svc_htts_rsrc_cgi_peer_t;

enum mio_svc_htts_rsrc_cgi_type_t
{
	MIO_SVC_HTTS_RSRC_CGI_TYPE_FUNC,
	MIO_SVC_HTTS_RSRC_CGI_TYPE_PROC
};
typedef enum mio_svc_htts_rsrc_cgi_type_t mio_svc_htts_rsrc_cgi_type_t;

struct rsrc_cgi_xtn_t
{
	mio_svc_htts_rsrc_cgi_type_t type;
	int rfd;
	int wfd;

	mio_svc_htts_rsrc_cgi_t handler;
	pthread_t thr;
	mio_svc_htts_rsrc_cgi_peer_t peer;
};
typedef struct rsrc_cgi_xtn_t rsrc_cgi_xtn_t;


static int rsrc_cgi_on_write (mio_svc_htts_rsrc_t* rsrc, mio_dev_sck_t* sck)
{
	rsrc_cgi_xtn_t* file = (rsrc_cgi_xtn_t*)mio_svc_htts_rsrc_getxtn(rsrc);
	return 0; 
}

static void rsrc_cgi_on_kill (mio_svc_htts_rsrc_t* rsrc)
{
	rsrc_cgi_xtn_t* cgi = (rsrc_cgi_xtn_t*)mio_svc_htts_rsrc_getxtn(rsrc);

	close (cgi->rfd); cgi->rfd = -1;
	close (cgi->wfd); cgi->wfd = -1;

	switch (cgi->type)
	{
		case MIO_SVC_HTTS_RSRC_CGI_TYPE_FUNC:
/* TODO: check cgi->thr is valid.
 *       non-blocking way?  if alive, kill gracefully?? */
			pthread_join (cgi->thr, MIO_NULL);
			break;

		case MIO_SVC_HTTS_RSRC_CGI_TYPE_PROC:
			/* TODO:
			waitpid with no wait
			still alive kill
			waitpid with no wait.
			*/
			break;
	}
}

static void* cgi_thr_func (void* ctx)
{
	rsrc_cgi_xtn_t* func = (rsrc_cgi_xtn_t*)ctx;
	func->handler (func->peer.rfd, func->peer.wfd);
	close (func->peer.rfd); func->peer.rfd = -1;
	close (func->peer.wfd); func->peer.wfd = -1;
	return MIO_NULL;
}

int mio_svc_htts_sendcgi (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_svc_htts_rsrc_cgi_t handler, mio_htre_t* req)
{
	mio_svc_htts_rsrc_t* rsrc = MIO_NULL;
	rsrc_cgi_xtn_t* cgi = MIO_NULL;
	int pfd[2];

	rsrc = mio_svc_htts_rsrc_make(htts, csck, rsrc_cgi_on_write, rsrc_cgi_on_kill, MIO_SIZEOF(*cgi));
	if (MIO_UNLIKELY(!rsrc)) goto oops;

	cgi = mio_svc_htts_rsrc_getxtn(rsrc);
	cgi->type = MIO_SVC_HTTS_RSRC_CGI_TYPE_FUNC;
	cgi->handler = handler;
	cgi->rfd = -1;
	cgi->wfd = -1;
	cgi->peer.rfd = -1;
	cgi->peer.wfd = -1;

	if (pipe(pfd) == -1) goto oops;
	cgi->peer.rfd = pfd[0];
	cgi->wfd = pfd[1];

	if (pipe(pfd) == -1) goto oops;
	cgi->rfd = pfd[0];
	cgi->peer.wfd = pfd[1];

	if (pthread_create(&cgi->thr, MIO_NULL, cgi_thr_func, cgi) != 0) goto oops;
	return 0;

oops:
	if (cgi)
	{
		if (cgi->peer.rfd >= 0) { close (cgi->peer.rfd); cgi->peer.rfd = -1; }
		if (cgi->peer.wfd >= 0) { close (cgi->peer.wfd); cgi->peer.wfd = -1; }
		if (cgi->rfd >= 0) { close (cgi->rfd); cgi->rfd = -1; }
		if (cgi->wfd >= 0) { close (cgi->wfd); cgi->wfd = -1; }
	}
	if (rsrc) mio_svc_htts_rsrc_kill (rsrc);
	return -1;
}

int mio_svc_htts_sendstatus (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, int status_code, void* extra)
{
/* TODO: change this to use send status */
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	mio_bch_t text[1024]; /* TODO: make this buffer dynamic or scalable */
	mio_bch_t dtbuf[64];
	mio_oow_t x;
	mio_http_version_t* version = mio_htre_getversion(req);

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

	x = mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %s\r\nServer: %s\r\nDate: %s\r\nConnection: %s\r\nContent-Type: text/html\r\nContent-Length: %u\r\n%s%s%s\r\n%s",
		version->major, version->minor, status_code, mio_http_status_to_bcstr(status_code), 
		htts->server_name,
		dtbuf, /* DATE */
		((req->flags & MIO_HTRE_ATTR_KEEPALIVE)? "keep-alive": "close"), /* connection */
		mio_count_bcstr(text), /* content length */
		extrapre, extraval, extraval, text
	);
	if (MIO_UNLIKELY(x == (mio_oow_t)-1)) return -1;
	

/* TODO: use timedwrite? */
	if (mio_dev_sck_write(csck, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf), MIO_NULL, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (csck);
		return -1;
	}

	if (!(req->flags & MIO_HTRE_ATTR_KEEPALIVE))
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
