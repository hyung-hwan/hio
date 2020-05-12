#include "mio-http.h"
#include "mio-htrd.h"
#include "mio-pro.h" /* for cgi */
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

				const mio_bch_t* qpath = mio_htre_getqpath(req);
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
			mio_htrd_dummify (cli->htrd);

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

static int client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
printf ("** HTTS - client read   %p  %d -> htts:%p\n", sck, (int)len, cli->htts);
	if (len <= 0)
	{
		mio_dev_sck_halt (sck);
	}
	else if (mio_htrd_feed(cli->htrd, buf, len) <= -1)
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

	return 0;
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
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck); /* the contents came from the listening socket */

	if (sck->state & MIO_DEV_SCK_ACCEPTED)
	{
		/* accepted a new client */
		MIO_DEBUG3 (sck->mio, "HTTS(%p) - accepted... %p %d \n", cli->htts, sck, sck->hnd);

		/* the accepted socket inherits various event callbacks. switch some of them to avoid sharing */
		sck->on_read = client_on_read;
		sck->on_write = client_on_write;

		if (init_client(cli, sck) <= -1)
		{
			MIO_INFO2 (sck->mio, "UNABLE TO INITIALIZE NEW CLIENT %p %d\n", sck, (int)sck->hnd);
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
			MIO_INFO1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			/* only for connecting sockets */
			MIO_INFO1 (sck->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_CONNECTED:
			/* only for connecting sockets */
			MIO_INFO1 (sck->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN %p(%d).......\n", (int)sck->hnd);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO2 (sck->mio, "LISTNER SOCKET %p(%d) - SHUTTUING DOWN\n", sck, (int)sck->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL: /* special case. */
			/* this progress code indicates that the ssl-level accept failed.
			 * on_disconnected() with this code is called without corresponding on_connect(). 
			 * the cli extension are is not initialized yet */
			MIO_ASSERT (sck->mio, sck != cli->sck);
			MIO_ASSERT (sck->mio, cli->sck == cli->htts->lsck); /* the field is a copy of the extension are of the listener socket. so it should point to the listner socket */
			MIO_INFO2 (sck->mio, "LISTENER UNABLE TO SSL-ACCEPT CLIENT %p(%d) ....%p\n", sck, (int)sck->hnd);
			return;

		case MIO_DEV_SCK_ACCEPTED:
			/* only for sockets accepted by the listeners. will never come here because
			 * the disconnect call for such sockets have been changed in listener_on_connect() */
			MIO_INFO2 (sck->mio, "ACCEPTED CLIENT SOCKET %p(%d) GOT DISCONNECTED.......\n", sck, (int)sck->hnd);
			break;

		default:
			MIO_INFO2 (sck->mio, "SOCKET %p(%d) DISCONNECTED AFTER ALL.......\n", sck, (int)sck->hnd);
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
	info.b.options |= MIO_DEV_SCK_BIND_SSL; 
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

	MIO_DEBUG3 (mio, "STARTED SVC(HTTS) LISTENER %p LISTENER SOCKET %p(%d)\n", htts, htts->lsck, (int)htts->lsck->hnd);
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

	MIO_DEBUG3 (mio, "STOPPING SVC(HTTS) %p LISTENER SOCKET %p(%d)\n", htts, htts->lsck, (int)(htts->lsck? htts->lsck->hnd: -1));

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
printf ("RSRC KILL >>>>> %p\n", rsrc->htts);
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

static void cgi_peer_on_close (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	mio_t* mio = pro->mio;
	if (sid == MIO_DEV_PRO_MASTER)
		MIO_INFO1 (mio, "PROCESS(%d) CLOSE MASTER\n", (int)pro->child_pid);
	else
		MIO_INFO2 (mio, "PROCESS(%d) CLOSE SLAVE[%d]\n", (int)pro->child_pid, sid);
}

static int cgi_peer_on_read (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = pro->mio;

	if (dlen <= -1)
	{
		MIO_INFO1 (mio, "PROCESS(%d): READ TIMED OUT...\n", (int)pro->child_pid);
		mio_dev_pro_halt (pro);
		return 0;
	}
	else if (dlen <= 0)
	{
		MIO_INFO1 (mio, "PROCESS(%d): EOF RECEIVED...\n", (int)pro->child_pid);
		/* no outstanding request. but EOF */
		mio_dev_pro_halt (pro);
		return 0;
	}

	MIO_INFO5 (mio, "PROCESS(%d) READ DATA ON SLAVE[%d] len=%d [%.*hs]\n", (int)pro->child_pid, (int)sid, (int)dlen, dlen, (char*)data);
	if (sid == MIO_DEV_PRO_OUT)
	{
		mio_dev_pro_read (pro, sid, 0);
		mio_dev_pro_write (pro, "HELLO\n", 6, MIO_NULL);
	}
	return 0;
}

static int cgi_peer_on_write (mio_dev_pro_t* pro, mio_iolen_t wrlen, void* wrctx)
{
	mio_t* mio = pro->mio;
	mio_ntime_t tmout;
	if (wrlen <= -1)
	{
		MIO_INFO1 (mio, "PROCESS(%d): WRITE TIMED OUT...\n", (int)pro->child_pid);
		mio_dev_pro_halt (pro);
		return 0;
	}

	MIO_DEBUG2 (mio, "PROCESS(%d) wrote data of %d bytes\n", (int)pro->child_pid, (int)wrlen);
	/*mio_dev_pro_read (pro, MIO_DEV_PRO_OUT, 1);*/
	MIO_INIT_NTIME (&tmout, 5, 0);
	mio_dev_pro_timedread (pro, MIO_DEV_PRO_OUT, 1, &tmout);
	return 0;
}


static int cgi_client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
printf ("** HTTS - cgi client read   %p  %d -> htts:%p\n", sck, (int)len, cli->htts);
	if (len <= 0)
	{
		/* read error */
		mio_dev_sck_halt (sck);
	}
	else if (mio_htrd_feed(cli->htrd, buf, len) <= -1)
	{
		mio_dev_sck_halt (sck);
	}

	return 0;
}

static int cgi_client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	return 0;
}

static void cgi_client_on_disconnect (mio_dev_sck_t* sck)
{
	client_on_disconnect (sck);
}

typedef int (*mio_htre_concb_t) (
	mio_htre_t*        re,
	const mio_bch_t*   ptr,
	mio_oow_t          len,
	void*              ctx
);

int cgi_forward_client_to_peer (mio_htre_t* re, const mio_bch_t* ptr, mio_oow_t len, void* ctx)
{
	mio_dev_pro_t* pro = (mio_dev_pro_t*)ctx;
	return mio_dev_pro_write(pro, ptr, len, MIO_NULL);
	/* TODO: if there are too many pending write requests, stop read event on the client */
}

int mio_svc_htts_docgi (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot)
{
	mio_dev_pro_t* pro = MIO_NULL;
	mio_dev_pro_make_t mi;

	MIO_MEMSET (&mi, 0, MIO_SIZEOF(mi));
	mi.flags = MIO_DEV_PRO_READOUT /*| MIO_DEV_PRO_READERR*/ | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	mi.cmd = mio_htre_getqpath(req); /* TODO: combine it with docroot */
	mi.on_read = cgi_peer_on_read;
	mi.on_write = cgi_peer_on_write;
	mi.on_close = cgi_peer_on_close;

/* TODO: create cgi environment variables... */
	pro = mio_dev_pro_make(htts->mio, 0, &mi);
	if (MIO_UNLIKELY(!pro)) goto oops;

/* THE process device must be ready for I/O */
	if (mio_htre_getcontentlen(req) > 0)
	{
		if (mio_dev_pro_write(pro, mio_htre_getcontentptr(req), mio_htre_getcontentlen(req), MIO_NULL) <= -1) goto oops;
	}

	mio_htre_setconcb (req, cgi_forward_client_to_peer, pro);

#if 0
	csck->on_read = cgi_client_on_read;
	cssk->on_write = cgi_client_on_write;
	csck->on_disconnect = cgi_client_on_disconnect;
#endif

#if 0
	rsrc = mio_svc_htts_rsrc_make(htts, csck, rsrc_cgi_on_read, rsrc_cgi_on_write, rsrc_cgi_on_kill, MIO_SIZEOF(*rsrc_cgi));
	if (MIO_UNLIKELY(!rsrc)) goto oops;
#endif

	return 0;


oops:
	if (pro) mio_dev_pro_kill (pro);
	return -1;
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

int mio_svc_htts_sendstatus (mio_svc_htts_t* htts, mio_dev_sck_t* csck, int status_code, mio_http_method_t method, const mio_http_version_t* version, int keepalive, void* extra)
{
/* TODO: change this to use send status */
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
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

	x = mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %s\r\nServer: %s\r\nDate: %s\r\nConnection: %s\r\nContent-Type: text/html\r\nContent-Length: %u\r\n%s%s%s\r\n%s",
		version->major, version->minor, status_code, mio_http_status_to_bcstr(status_code), 
		htts->server_name,
		dtbuf, /* DATE */
		(keepalive? "keep-alive": "close"), /* connection */
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
