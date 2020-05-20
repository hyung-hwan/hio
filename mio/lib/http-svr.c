#include "mio-http.h"
#include "mio-htrd.h"
#include "mio-pro.h" /* for cgi */
#include "mio-fmt.h"
#include "mio-chr.h"
#include "mio-prv.h"


#include <unistd.h> /* TODO: move file operations to sys-file.XXX */
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h> /* setenv, clearenv */

#define CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH

typedef struct mio_svc_htts_cli_t mio_svc_htts_cli_t;
struct mio_svc_htts_cli_t
{
	mio_svc_htts_cli_t* cli_prev;
	mio_svc_htts_cli_t* cli_next;

	/* a listener socket sets htts and sck fields only */
	/* a client sockets uses all the fields in this struct */
	mio_svc_htts_t* htts;
	mio_dev_sck_t* sck;

	mio_htrd_t* htrd;
	mio_becs_t* sbuf; /* temporary buffer for status line formatting */

	mio_svc_htts_rsrc_t* rsrc;
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

static int process_request (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req)
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

	MIO_DEBUG2 (htts->mio, "[RAW-REQ] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));

	mio_htre_perdecqpath(req);
	/* TODO: proper request logging */

	MIO_DEBUG2 (htts->mio, "[REQ] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));



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

#if 0
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
/* this part can be checked in actual mio_svc_htts_doXXX() functions.
 * some doXXX handlers may not require length for POST.
 * it may be able to simply accept till EOF? or  treat as if CONTENT_LENGTH is 0*/
		if (mth == MIO_HTTP_POST && !(req->flags & (MIO_HTRE_ATTR_LENGTH | MIO_HTRE_ATTR_CHUNKED)))
		{
			/* POST without Content-Length nor not chunked */
			mio_htre_discardcontent (req); 
			/* 411 Length Required - can't keep alive. Force disconnect */
			req->flags &= ~MIO_HTRE_ATTR_KEEPALIVE; /* to cause sendstatus() to close */
			if (mio_svc_htts_sendstatus(htts, csck, req, 411, MIO_NULL) <= -1) goto oops;
		}
		else

		{
#endif
			/*const mio_bch_t* qpath = mio_htre_getqpath(req);*/
			if (mio_svc_htts_docgi(htts, csck, req, "") <= -1) goto oops;

	return 0;

oops:
	mio_dev_sck_halt (csck);
	return -1;
}

static int client_htrd_peek_request (mio_htrd_t* htrd, mio_htre_t* req)
{
	htrd_xtn_t* htrdxtn = (htrd_xtn_t*)mio_htrd_getxtn(htrd);
	mio_svc_htts_cli_t* sckxtn = (mio_svc_htts_cli_t*)mio_dev_sck_getxtn(htrdxtn->sck);
	return process_request(sckxtn->htts, htrdxtn->sck, req);
}


static mio_htrd_recbs_t client_htrd_recbs =
{
	client_htrd_peek_request,
	MIO_NULL,
	MIO_NULL
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

	/* With MIO_HTRD_TRAILERS, htrd stores trailers in a separate place.
	 * Otherwise, it is merged to the headers. */
	/*mio_htrd_setopt (cli->htrd, MIO_HTRD_REQUEST | MIO_HTRD_TRAILERS);*/

	cli->sbuf = mio_becs_open(sck->mio, 0, 2048);
	if (MIO_UNLIKELY(!cli->sbuf)) goto oops;

	htrdxtn = mio_htrd_getxtn(cli->htrd);
	htrdxtn->sck = sck; /* TODO: remember cli instead? */

	mio_htrd_setrecbs (cli->htrd, &client_htrd_recbs);

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

enum cgi_state_res_mode_t
{
	CGI_STATE_RES_MODE_CHUNKED,
	CGI_STATE_RES_MODE_CLOSE,
	CGI_STATE_RES_MODE_LENGTH
};
typedef enum cgi_state_res_mode_t cgi_state_res_mode_t;

#define CGI_STATE_PENDING_IO_THRESHOLD 5

#define CGI_STATE_OVER_READ_FROM_CLIENT (1 << 0)
#define CGI_STATE_OVER_READ_FROM_PEER   (1 << 1)
#define CGI_STATE_OVER_WRITE_TO_CLIENT  (1 << 2)
#define CGI_STATE_OVER_WRITE_TO_PEER    (1 << 3)
#define CGI_STATE_OVER_ALL (CGI_STATE_OVER_READ_FROM_CLIENT | CGI_STATE_OVER_READ_FROM_PEER | CGI_STATE_OVER_WRITE_TO_CLIENT | CGI_STATE_OVER_WRITE_TO_PEER)


struct cgi_state_t
{
	MIO_SVC_HTTS_RSRC_HEADER;

	mio_oow_t num_pending_writes_to_client;
	mio_oow_t num_pending_writes_to_peer;
	mio_dev_pro_t* peer;
	mio_htrd_t* peer_htrd;
	mio_svc_htts_cli_t* client;
	mio_http_version_t req_version; /* client request */

	unsigned int over: 4; /* must be large enough to accomodate CGI_STATE_OVER_ALL */
	unsigned int keep_alive: 1;
	unsigned int req_content_length_unlimited: 1;
	unsigned int ever_attempted_to_write_to_client: 1;
	unsigned int client_disconnected: 1;
	mio_oow_t req_content_length; /* client request content length */
	cgi_state_res_mode_t res_mode_to_cli;

	mio_dev_sck_on_read_t client_org_on_read;
	mio_dev_sck_on_write_t client_org_on_write;
	mio_dev_sck_on_disconnect_t client_org_on_disconnect;
};
typedef struct cgi_state_t cgi_state_t;

struct cgi_peer_xtn_t
{
	cgi_state_t* state;
};
typedef struct cgi_peer_xtn_t cgi_peer_xtn_t;

static void cgi_state_halt_participating_devices (cgi_state_t* cgi_state)
{
	MIO_ASSERT (cgi_state->client->htts->mio, cgi_state->client != MIO_NULL);
	MIO_ASSERT (cgi_state->client->htts->mio, cgi_state->client->sck != MIO_NULL);

	MIO_DEBUG4 (cgi_state->client->htts->mio, "HTTS(%p) - Halting participating devices in cgi state %p(client=%p,peer=%p)\n", cgi_state->client->htts, cgi_state, cgi_state->client->sck, cgi_state->peer);


	mio_dev_sck_halt (cgi_state->client->sck);
	/* check for peer as it may not have been started */
	if (cgi_state->peer) mio_dev_pro_halt (cgi_state->peer);
}

static int cgi_state_write_to_client (cgi_state_t* cgi_state, const void* data, mio_iolen_t dlen)
{
	cgi_state->ever_attempted_to_write_to_client = 1;

	cgi_state->num_pending_writes_to_client++;
	if (mio_dev_sck_write(cgi_state->client->sck, data, dlen, MIO_NULL, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_client--;
		return -1;
	}

	if (cgi_state->num_pending_writes_to_client > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;
	}
	return 0;
}

static int cgi_state_writev_to_client (cgi_state_t* cgi_state, mio_iovec_t* iov, mio_iolen_t iovcnt)
{
	cgi_state->ever_attempted_to_write_to_client = 1;

	cgi_state->num_pending_writes_to_client++;
	if (mio_dev_sck_writev(cgi_state->client->sck, iov, iovcnt, MIO_NULL, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_client--;
		return -1;
	}

	if (cgi_state->num_pending_writes_to_client > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) return -1;
	}
	return 0;
}

static int cgi_state_write_last_chunk_to_client (cgi_state_t* cgi_state)
{
	if (cgi_state->res_mode_to_cli == CGI_STATE_RES_MODE_CHUNKED &&
	    cgi_state_write_to_client(cgi_state, "0\r\n\r\n", 5) <= -1) return -1;

	if (!cgi_state->keep_alive && cgi_state_write_to_client(cgi_state, MIO_NULL, 0) <= -1) return -1;

	return 0;
}

static int cgi_state_write_to_peer (cgi_state_t* cgi_state, const void* data, mio_iolen_t dlen)
{
	cgi_state->num_pending_writes_to_peer++;
	if (mio_dev_pro_write(cgi_state->peer, data, dlen, MIO_NULL) <= -1) 
	{
		cgi_state->num_pending_writes_to_peer--;
		return -1;
	}

/* TODO: check if it's already finished or something.. */
	if (cgi_state->num_pending_writes_to_peer > CGI_STATE_PENDING_IO_THRESHOLD)
	{
		if (mio_dev_sck_read(cgi_state->client->sck, 0) <= -1) return -1;
	}
	return 0;
}

static int cgi_state_send_final_status_to_client (cgi_state_t* cgi_state, int status_code)
{
	mio_svc_htts_cli_t* cli = cgi_state->client;
	mio_bch_t dtbuf[64];

	mio_svc_htts_fmtgmtime (cli->htts, MIO_NULL, dtbuf, MIO_COUNTOF(dtbuf));

	if (mio_becs_fmt(cli->sbuf, "HTTP/%d.%d %d %hs\r\nServer: %hs\r\nDate: %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
		cgi_state->req_version.major, cgi_state->req_version.minor,
		status_code, mio_http_status_to_bcstr(status_code),
		cli->htts->server_name, dtbuf) == (mio_oow_t)-1) return -1;

	return (cgi_state_write_to_client(cgi_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf)) <= -1 ||
	        cgi_state_write_to_client(cgi_state, MIO_NULL, 0) <= -1)? -1: 0;
}

static MIO_INLINE void cgi_state_mark_over (cgi_state_t* cgi_state, int over_bits)
{
	unsigned int old_over;

	old_over = cgi_state->over;
	cgi_state->over |= over_bits;

	MIO_DEBUG5 (cgi_state->htts->mio, "HTTS(%p) - client=%p peer=%p new-bits=%x over=%x\n", cgi_state->htts, cgi_state->client->sck, cgi_state->peer, (int)over_bits, (int)cgi_state->over);

	if (!(old_over & CGI_STATE_OVER_READ_FROM_CLIENT) && (cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT))
	{
		if (mio_dev_sck_read(cgi_state->client->sck, 0) <= -1) mio_dev_sck_halt (cgi_state->client->sck);
	}

	if (!(old_over & CGI_STATE_OVER_READ_FROM_PEER) && (cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER))
	{
		if (cgi_state->peer && mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 0) <= -1) mio_dev_pro_halt (cgi_state->peer);
	}

	if (old_over != CGI_STATE_OVER_ALL && cgi_state->over == CGI_STATE_OVER_ALL)
	{
		/* ready to stop */
		if (cgi_state->peer) mio_dev_pro_halt (cgi_state->peer);

		if (cgi_state->keep_alive) 
		{
			/* how to arrange to delete this cgi_state object and put the socket back to the normal waiting state??? */
			MIO_ASSERT (cgi_state->htts->mio, cgi_state->client->rsrc == (mio_svc_htts_rsrc_t*)cgi_state);

printf ("DETACHING FROM THE MAIN CLIENT RSRC... state -> %p\n", cgi_state->client->rsrc);
			MIO_SVC_HTTS_RSRC_DETACH (cgi_state->client->rsrc);
			/* cgi_state must not be access from here down as it could have been destroyed */
		}
		else
		{
			mio_dev_sck_shutdown (cgi_state->client->sck, MIO_DEV_SCK_SHUTDOWN_WRITE);
			mio_dev_sck_halt (cgi_state->client->sck);
		}
	}
}

static void cgi_state_on_kill (cgi_state_t* cgi_state)
{
printf ("**** CGI_STATE_ON_KILL \n");
	if (cgi_state->peer)
	{
		cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(cgi_state->peer);
		cgi_peer->state = MIO_NULL;  /* cgi_peer->state many not be NULL if the resource is killed regardless of the reference count */

		mio_dev_pro_kill (cgi_state->peer);
		cgi_state->peer = MIO_NULL;
	}

	if (cgi_state->peer_htrd)
	{
		cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
		cgi_peer->state = MIO_NULL; /* cgi_peer->state many not be NULL if the resource is killed regardless of the reference count */

		mio_htrd_close (cgi_state->peer_htrd);
		cgi_state->peer_htrd = MIO_NULL;
	}

	if (cgi_state->client_org_on_read)
	{
		cgi_state->client->sck->on_read = cgi_state->client_org_on_read;
		cgi_state->client_org_on_read = MIO_NULL;
	}

	if (cgi_state->client_org_on_write)
	{
		cgi_state->client->sck->on_write = cgi_state->client_org_on_write;
		cgi_state->client_org_on_write = MIO_NULL;
	}


	if (cgi_state->client_org_on_disconnect)
	{
		cgi_state->client->sck->on_disconnect = cgi_state->client_org_on_disconnect;
		cgi_state->client_org_on_disconnect = MIO_NULL;
	}

	mio_htrd_setrecbs (cgi_state->client->htrd, &client_htrd_recbs); /* restore the callbacks */

	if (!cgi_state->client_disconnected)
	{
printf ("ENABLING INPUT WATCHING on CLIENT %p. \n", cgi_state->client->sck);
		if (!cgi_state->keep_alive || mio_dev_sck_read(cgi_state->client->sck, 1) <= -1)
		{
printf ("FAILED TO ENABLE INPUT WATCHING on CLINET %p. SO HALTING CLIENT SOCKET>>>>>>>>>>>>>\n", cgi_state->client->sck);
			mio_dev_sck_halt (cgi_state->client->sck);
		}
	}

printf ("**** CGI_STATE_ON_KILL DONE\n");
}

static void cgi_peer_on_close (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	if (!cgi_state) return; /* cgi state already gone */

	switch (sid)
	{
		case MIO_DEV_PRO_MASTER:
			MIO_DEBUG3 (mio, "HTTS(%p) - peer %p(pid=%d) closing master\n", cgi_state->client->htts, pro, (int)pro->child_pid);
			cgi_state->peer = MIO_NULL; /* clear this peer from the state */

			MIO_ASSERT (mio, cgi_peer->state != MIO_NULL);
printf ("DETACHING FROM CGI PEER DEVICE.....................%p   %d\n", cgi_peer->state, (int)cgi_peer->state->rsrc_refcnt);
			MIO_SVC_HTTS_RSRC_DETACH (cgi_peer->state);

			if (cgi_state->peer_htrd)
			{
				/* once this peer device is closed, peer's htrd is also never used.
				 * it's safe to detach the extra information attached on the htrd object. */
				cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
				MIO_ASSERT (mio, cgi_peer->state != MIO_NULL);
printf ("DETACHING FROM CGI PEER HTRD.....................%p   %d\n", cgi_peer->state, (int)cgi_peer->state->rsrc_refcnt);
				MIO_SVC_HTTS_RSRC_DETACH (cgi_peer->state);
			}

			break;

		case MIO_DEV_PRO_OUT:
			MIO_ASSERT (mio, cgi_state->peer == pro);
			MIO_DEBUG4 (mio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi_state->client->htts, pro, (int)pro->child_pid, sid);

			if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER))
			{
				if (cgi_state_write_last_chunk_to_client(cgi_state) <= -1) 
					cgi_state_halt_participating_devices (cgi_state);
				else
					cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_PEER);
			}
			break;

		case MIO_DEV_PRO_IN:
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_PEER);
			break;

		case MIO_DEV_PRO_ERR:
		default:
			MIO_DEBUG4 (mio, "HTTS(%p) - peer %p(pid=%d) closing slave[%d]\n", cgi_state->client->htts, pro, (int)pro->child_pid, sid);
			/* do nothing */
			break;
	}
}

static int cgi_peer_on_read (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (mio, sid == MIO_DEV_PRO_OUT); /* since MIO_DEV_PRO_ERRTONUL is used, there should be no input from MIO_DEV_PRO_ERR */
	MIO_ASSERT (mio, cgi_state != MIO_NULL);

	if (dlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - read error from peer %p(pid=%u)\n", cgi_state->client->htts, pro, (unsigned int)pro->child_pid);
		goto oops;
	}

	if (dlen == 0)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from peer %p(pid=%u)\n", cgi_state->client->htts, pro, (unsigned int)pro->child_pid);

		if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER))
		{
			/* the cgi script could be misbehaviing.
			 * it still has to read more but EOF is read.
			 * otherwise client_peer_htrd_poke() should have been called */
			if (cgi_state_write_last_chunk_to_client(cgi_state) <= -1) goto oops;
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_PEER);
		}
	}
	else
	{
		mio_oow_t rem;

		MIO_ASSERT (mio, !(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER));

		if (mio_htrd_feed(cgi_state->peer_htrd, data, dlen, &rem) <= -1) 
		{
			MIO_DEBUG3 (mio, "HTTPS(%p) - unable to feed peer into to htrd - peer %p(pid=%u)\n", cgi_state->htts, pro, (unsigned int)pro->child_pid);

			if (!cgi_state->ever_attempted_to_write_to_client &&
			    !(cgi_state->over & CGI_STATE_OVER_WRITE_TO_CLIENT))
			{
				cgi_state_send_final_status_to_client (cgi_state, 500); /* don't care about error because it jumps to oops below anyway */
			}

			goto oops; 
		}

		if (rem > 0) 
		{
			/* If the script specifies Content-Length and produces longer data, it will come here */
printf ("AAAAAAAAAAAAAAAAAa EEEEEXcessive DATA..................\n");
/* TODO: or drop this request?? */
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static int cgi_peer_capture_response_header (mio_htre_t* req, const mio_bch_t* key, const mio_htre_hdrval_t* val, void* ctx)
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
	mio_svc_htts_cli_t* cli = cgi_state->client;
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

	if (mio_htre_walkheaders(req, cgi_peer_capture_response_header, cli) <= -1) return -1;

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
			if (mio_becs_cat(cli->sbuf, (cgi_state->keep_alive? "Connection: keep-alive\r\n": "Connection: close\r\n")) == (mio_oow_t)-1) return -1;
	}

	if (mio_becs_cat(cli->sbuf, "\r\n") == (mio_oow_t)-1) return -1;
	return cgi_state_write_to_client(cgi_state, MIO_BECS_PTR(cli->sbuf), MIO_BECS_LEN(cli->sbuf));
}

static int cgi_peer_htrd_poke (mio_htrd_t* htrd, mio_htre_t* req)
{
	/* client request got completed */
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;

printf (">> PEER RESPONSE COMPLETED\n");

	if (cgi_state_write_last_chunk_to_client(cgi_state) <= -1) return -1;

	cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_PEER);
	return 0;
}

static int cgi_peer_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	cgi_peer_xtn_t* cgi_peer = mio_htrd_getxtn(htrd);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (cgi_state->client->htts->mio, htrd == cgi_state->peer_htrd);

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
			iov[1].iov_ptr = (void*)data;
			iov[1].iov_len = dlen;
			iov[2].iov_ptr = "\r\n";
			iov[2].iov_len = 2;

			if (cgi_state_writev_to_client(cgi_state, iov, MIO_COUNTOF(iov)) <= -1) goto oops;
			break;
		}

		case CGI_STATE_RES_MODE_CLOSE:
		case CGI_STATE_RES_MODE_LENGTH:
			if (cgi_state_write_to_client(cgi_state, data, dlen) <= -1) goto oops;
			break;
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
	cgi_peer_htrd_poke,
	cgi_peer_htrd_push_content
};

static int cgi_client_htrd_poke (mio_htrd_t* htrd, mio_htre_t* req)
{
	/* client request got completed */
	htrd_xtn_t* htrdxtn = mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

printf (">> CLIENT REQUEST COMPLETED\n");

	/* indicate EOF to the client peer */
	if (cgi_state_write_to_peer(cgi_state, MIO_NULL, 0) <= -1) return -1;

	cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_CLIENT);
	return 0;
}

static int cgi_client_htrd_push_content (mio_htrd_t* htrd, mio_htre_t* req, const mio_bch_t* data, mio_oow_t dlen)
{
	htrd_xtn_t* htrdxtn = mio_htrd_getxtn(htrd);
	mio_dev_sck_t* sck = htrdxtn->sck;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

	MIO_ASSERT (sck->mio, cli->sck == sck);
	return cgi_state_write_to_peer(cgi_state, data, dlen);
}

static int cgi_peer_on_write (mio_dev_pro_t* pro, mio_iolen_t wrlen, void* wrctx)
{
	mio_t* mio = pro->mio;
	cgi_peer_xtn_t* cgi_peer = mio_dev_pro_getxtn(pro);
	cgi_state_t* cgi_state = cgi_peer->state;

	MIO_ASSERT (mio, cgi_state->peer == pro);

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTS(%p) - unable to write to peer %p(pid=%u)\n", cgi_state->client->htts, pro, (int)pro->child_pid);
		goto oops;
	}
	else if (wrlen == 0)
	{
		/* indicated EOF */
		/* do nothing here as i didn't incremented num_pending_writes_to_peer when making the write request */

		cgi_state->num_pending_writes_to_peer--;
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_peer == 0);
		MIO_DEBUG3 (mio, "HTTS(%p) - indicated EOF to peer %p(pid=%u)\n", cgi_state->client->htts, pro, (int)pro->child_pid);
		/* indicated EOF to the peer side. i need no more data from the client side.
		 * i don't need to enable input watching in the client side either */
		cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_PEER);
	}
	else
	{
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_peer > 0);

		cgi_state->num_pending_writes_to_peer--;
		if (cgi_state->num_pending_writes_to_peer == CGI_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT) &&
			    mio_dev_sck_read(cgi_state->client->sck, 1) <= -1) goto oops;
		}

		if ((cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT) && cgi_state->num_pending_writes_to_peer <= 0)
		{
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_PEER);
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static void cgi_client_on_disconnect (mio_dev_sck_t* sck)
{
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;
	cgi_state->client_disconnected = 1;
	listener_on_disconnect (sck);
}

static int cgi_client_on_read (mio_dev_sck_t* sck, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

	MIO_ASSERT (mio, sck == cli->sck);

	if (len <= -1)
	{
		/* read error */
		MIO_DEBUG2 (cli->htts->mio, "HTTPS(%p) - read error on client %p(%d)\n", sck, (int)sck->hnd);
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
		MIO_DEBUG3 (mio, "HTTPS(%p) - EOF from client %p(hnd=%d)\n", cgi_state->client->htts, sck, (int)sck->hnd);

		if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT)) /* if this is true, EOF is received without cgi_client_htrd_poke() */
		{
			if (cgi_state_write_to_peer(cgi_state, MIO_NULL, 0) <= -1) goto oops;
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_CLIENT);
		}
	}
	else
	{
		mio_oow_t rem;

		MIO_ASSERT (mio, !(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT));

		if (mio_htrd_feed(cli->htrd, buf, len, &rem) <= -1) goto oops;

		if (rem > 0)
		{
			/* TODO store this to client buffer. once the current resource is completed, arrange to call on_read() with it */
printf ("UUUUUUUUUUUUUUUUUUUUUUUUUUGGGGGHHHHHHHHHHHH .......... CGI CLIENT GIVING EXCESSIVE DATA AFTER CONTENTS...\n");
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static int cgi_client_on_write (mio_dev_sck_t* sck, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(sck);
	cgi_state_t* cgi_state = (cgi_state_t*)cli->rsrc;

	if (wrlen <= -1)
	{
		MIO_DEBUG3 (mio, "HTTPS(%p) - unable to write to client %p(%d)\n", sck->mio, sck, (int)sck->hnd);
		goto oops;
	}

	if (wrlen == 0)
	{
		/* if the connect is keep-alive, this part may not be called */
		cgi_state->num_pending_writes_to_client--;
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_client == 0);
		MIO_DEBUG3 (mio, "HTTS(%p) - indicated EOF to client %p(%d)\n", cgi_state->client->htts, sck, (int)sck->hnd);
		/* since EOF has been indicated to the client, it must not write to the client any further.
		 * this also means that i don't need any data from the peer side either.
		 * i don't need to enable input watching on the peer side */
		cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_CLIENT);
	}
	else
	{
		MIO_ASSERT (mio, cgi_state->num_pending_writes_to_client > 0);

		cgi_state->num_pending_writes_to_client--;
		if (cgi_state->peer && cgi_state->num_pending_writes_to_client == CGI_STATE_PENDING_IO_THRESHOLD)
		{
			if (!(cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER) &&
			    mio_dev_pro_read(cgi_state->peer, MIO_DEV_PRO_OUT, 1) <= -1) goto oops;
		}

		if ((cgi_state->over & CGI_STATE_OVER_READ_FROM_PEER) && cgi_state->num_pending_writes_to_client <= 0)
		{
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_WRITE_TO_CLIENT);
		}
	}

	return 0;

oops:
	cgi_state_halt_participating_devices (cgi_state);
	return 0;
}

static MIO_INLINE int get_request_content_length (mio_htre_t* req, mio_oow_t* len)
{
	if (req->flags & MIO_HTRE_ATTR_CHUNKED)
	{
		/* "Transfer-Encoding: chunked" take precedence over "Content-Length: XXX". 
		 *
		 * [RFC7230]
		 *  If a message is received with both a Transfer-Encoding and a
		 *  Content-Length header field, the Transfer-Encoding overrides the
		 *  Content-Length. */
		return 1; /* unable to determine content-length in advance. unlimited */
	}

	if (req->flags & MIO_HTRE_ATTR_LENGTH)
	{
		*len = req->attr.content_length;
	}
	else
	{
		/* If no Content-Length is specified in a request, it's Content-Length: 0 */
		*len = 0;
	}
	return 0; /* limited to the length set in *len */
}

struct cgi_peer_fork_ctx_t
{
	mio_svc_htts_cli_t* cli;
	mio_htre_t* req;
	const mio_bch_t* docroot;
};
typedef struct cgi_peer_fork_ctx_t cgi_peer_fork_ctx_t;

static int cgi_peer_capture_request_header (mio_htre_t* req, const mio_bch_t* key, const mio_htre_hdrval_t* val, void* ctx)
{
	mio_becs_t* dbuf = (mio_becs_t*)ctx;

	if (mio_comp_bcstr(key, "Connection", 1) != 0 &&
	    mio_comp_bcstr(key, "Transfer-Encoding", 1) != 0 &&
	    mio_comp_bcstr(key, "Content-Length", 1) != 0 &&
	    mio_comp_bcstr(key, "Expect", 1) != 0)
	{
		mio_oow_t val_offset;
		mio_bch_t* ptr;

		mio_becs_clear (dbuf);
		if (mio_becs_cpy(dbuf, "HTTP_") == (mio_oow_t)-1 ||
		    mio_becs_cat(dbuf, key) == (mio_oow_t)-1 ||
		    mio_becs_ccat(dbuf, '\0') == (mio_oow_t)-1) return -1;

		for (ptr = MIO_BECS_PTR(dbuf); *ptr; ptr++)
		{
			*ptr = mio_to_bch_upper(*ptr);
			if (*ptr =='-') *ptr = '_';
		}

		val_offset = MIO_BECS_LEN(dbuf);
		if (mio_becs_cat(dbuf, val->ptr) == (mio_oow_t)-1) return -1;
		val = val->next;
		while (val)
		{
			if (mio_becs_cat(dbuf, ",") == (mio_oow_t)-1 ||
			    mio_becs_cat (dbuf, val->ptr) == (mio_oow_t)-1) return -1;
			val = val->next;
		}

		setenv (MIO_BECS_PTR(dbuf), MIO_BECS_CPTR(dbuf, val_offset), 1);
	}

	return 0;
}

static int cgi_peer_on_fork (mio_dev_pro_t* pro, void* fork_ctx)
{
	/*mio_t* mio = pro->mio;*/ /* in this callback, the pro device is not fully up. however, the mio field is guaranteed to be available */
	cgi_peer_fork_ctx_t* fc = (cgi_peer_fork_ctx_t*)fork_ctx;
	mio_oow_t content_length;
	const mio_bch_t* qparam;
	const char* path;
	mio_bch_t tmp[256];
	mio_becs_t dbuf;

	qparam = mio_htre_getqparam(fc->req);

	path = getenv("PATH");
	clearenv ();
	if (path) setenv ("PATH", path, 1);

	setenv ("GATEWAY_INTERFACE", "CGI/1.1", 1);

	mio_fmttobcstr (pro->mio, tmp, MIO_COUNTOF(tmp), "HTTP/%d.%d", (int)mio_htre_getmajorversion(fc->req), (int)mio_htre_getminorversion(fc->req));
	setenv ("SERVER_PROTOCOL", tmp, 1);

	//setenv ("SCRIPT_FILENAME",  
	//setenv ("SCRIPT_NAME",
	setenv ("DOCUMENT_ROOT", fc->docroot, 1);

	setenv ("REQUEST_METHOD", mio_htre_getqmethodname(fc->req), 1);
	setenv ("REQUEST_URI", mio_htre_getqpath(fc->req), 1);
	if (qparam) setenv ("QUERY_STRING", qparam, 1);

	if (get_request_content_length(fc->req, &content_length) == 0)
	{
		mio_fmt_uintmax_to_bcstr(tmp, MIO_COUNTOF(tmp), content_length, 10, 0, '\0', MIO_NULL);
		setenv ("CONTENT_LENGTH", tmp, 1);
	}
	else
	{
		/* content length unknown, neither is it 0 - this is not standard */
		setenv ("CONTENT_LENGTH", "-1", 1);
	}
	setenv ("SERVER_SOFTWARE", fc->cli->htts->server_name, 1);

	mio_skadtobcstr (pro->mio, &fc->cli->sck->localaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_ADDR);
	setenv ("SERVER_ADDR", tmp, 1);

	gethostname (tmp, MIO_COUNTOF(tmp)); /* if this fails, i assume tmp contains the ip address set by mio_skadtobcstr() above */
	setenv ("SERVER_NAME", tmp, 1);

	mio_skadtobcstr (pro->mio, &fc->cli->sck->localaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_PORT);
	setenv ("SERVER_PORT", tmp, 1);

	mio_skadtobcstr (pro->mio, &fc->cli->sck->remoteaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_ADDR);
	setenv ("REMOTE_ADDR", tmp, 1);

	mio_skadtobcstr (pro->mio, &fc->cli->sck->remoteaddr, tmp, MIO_COUNTOF(tmp), MIO_SKAD_TO_BCSTR_PORT);
	setenv ("REMOTE_PORT", tmp, 1);

	if (mio_becs_init(&dbuf, pro->mio, 256) >= 0)
	{
		mio_htre_walkheaders (fc->req,  cgi_peer_capture_request_header, &dbuf);
		/* [NOTE] trailers are not available when this cgi resource is started. let's not call mio_htre_walktrailers() */
		mio_becs_fini (&dbuf);
	}

	return 0;
}


int mio_svc_htts_docgi (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req, const mio_bch_t* docroot)
{
	mio_t* mio = htts->mio;
	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	cgi_state_t* cgi_state = MIO_NULL;
	cgi_peer_xtn_t* cgi_peer;
	mio_dev_pro_make_t mi;
	cgi_peer_fork_ctx_t fc;

	/* ensure that you call this function before any contents is received */
	MIO_ASSERT (mio, mio_htre_getcontentlen(req) == 0);

	MIO_MEMSET (&fc, 0, MIO_SIZEOF(fc));
	fc.cli = cli;
	fc.req = req;
	fc.docroot = docroot;

	MIO_MEMSET (&mi, 0, MIO_SIZEOF(mi));
	mi.flags = MIO_DEV_PRO_READOUT | MIO_DEV_PRO_ERRTONUL | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	mi.cmd = mio_htre_getqpath(req); /* TODO: combine it with docroot */
	mi.on_read = cgi_peer_on_read;
	mi.on_write = cgi_peer_on_write;
	mi.on_close = cgi_peer_on_close;
	mi.on_fork = cgi_peer_on_fork;
	mi.fork_ctx = &fc;

	cgi_state = (cgi_state_t*)mio_svc_htts_rsrc_make(htts, MIO_SIZEOF(*cgi_state), cgi_state_on_kill);
	if (MIO_UNLIKELY(!cgi_state)) goto oops;

	cgi_state->client = cli;
	/*cgi_state->num_pending_writes_to_client = 0;
	cgi_state->num_pending_writes_to_peer = 0;*/
	cgi_state->req_version = *mio_htre_getversion(req);
	cgi_state->req_content_length_unlimited = get_request_content_length(req, &cgi_state->req_content_length);

	cgi_state->client_org_on_read = csck->on_read;
	cgi_state->client_org_on_write = csck->on_write;
	cgi_state->client_org_on_disconnect = csck->on_disconnect;
	csck->on_read = cgi_client_on_read;
	csck->on_write = cgi_client_on_write;
	csck->on_disconnect = cgi_client_on_disconnect;

	MIO_ASSERT (mio, cli->rsrc == MIO_NULL);
	MIO_SVC_HTTS_RSRC_ATTACH (cgi_state, cli->rsrc);

/* TODO: create cgi environment variables... */
/* TODO:
 * never put Expect: 100-continue  to environment variable
 */
	if (access(mi.cmd, X_OK) == -1)
	{
		cgi_state_send_final_status_to_client (cgi_state, 403); /* 403 Forbidden */
		goto oops; /* TODO: must not go to oops.  just destroy the cgi_state and finalize the request .. */
	}

	cgi_state->peer = mio_dev_pro_make(mio, MIO_SIZEOF(*cgi_peer), &mi);
	if (MIO_UNLIKELY(!cgi_state->peer)) goto oops;
	cgi_peer = mio_dev_pro_getxtn(cgi_state->peer);
	MIO_SVC_HTTS_RSRC_ATTACH (cgi_state, cgi_peer->state);

	cgi_state->peer_htrd = mio_htrd_open(mio, MIO_SIZEOF(*cgi_peer));
	if (MIO_UNLIKELY(!cgi_state->peer_htrd)) goto oops;
	mio_htrd_setopt (cgi_state->peer_htrd, MIO_HTRD_SKIPINITIALLINE | MIO_HTRD_RESPONSE);
	mio_htrd_setrecbs (cgi_state->peer_htrd, &cgi_peer_htrd_recbs);

	cgi_peer = mio_htrd_getxtn(cgi_state->peer_htrd);
	MIO_SVC_HTTS_RSRC_ATTACH (cgi_state, cgi_peer->state);

#if !defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (cgi_state->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

		if (cgi_state_send_final_status_to_client(cgi_state, 411) <= -1) goto oops;
	}
#endif

	if (req->flags & MIO_HTRE_ATTR_EXPECT100)
	{
		/* TODO: Expect: 100-continue? who should handle this? cgi? or the http server? */
		/* CAN I LET the cgi SCRIPT handle this? */
		if (mio_comp_http_version_numbers(&req->version, 1, 1) >= 0 && 
		   (cgi_state->req_content_length_unlimited || cgi_state->req_content_length > 0)) 
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
			if (cgi_state_write_to_client(cgi_state, msgbuf, msglen) <= -1) goto oops;
			cgi_state->ever_attempted_to_write_to_client = 0; /* reset this as it's polluted for 100 continue */
		}
	}
	else if (req->flags & MIO_HTRE_ATTR_EXPECT)
	{
		/* 417 Expectation Failed */
		cgi_state_send_final_status_to_client(cgi_state, 417);
		goto oops;
	}

#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	if (cgi_state->req_content_length_unlimited)
	{
		/* Transfer-Encoding is chunked. no content-length is known in advance. */
		
		/* option 1. buffer contents. if it gets too large, send 413 Request Entity Too Large.
		 * option 2. send 411 Length Required immediately
		 * option 3. set Content-Length to -1 and use EOF to indicate the end of content [Non-Standard] */

/* TODO: must set CONTENT-LENGTH to -1 before start the process */
		/* change the callbacks to subscribe to contents to be uploaded */
		cgi_state->client->htrd->recbs.poke = cgi_client_htrd_poke;
		cgi_state->client->htrd->recbs.push_content = cgi_client_htrd_push_content;
	}
	else
	{
#endif
		if (cgi_state->req_content_length > 0)
		{
			/* change the callbacks to subscribe to contents to be uploaded */
			cgi_state->client->htrd->recbs.poke = cgi_client_htrd_poke;
			cgi_state->client->htrd->recbs.push_content = cgi_client_htrd_push_content;
		}
		else
		{
			/* no content to be uploaded from the client */
			/* indicate EOF to the peer and disable input wathching from the client */
			if (cgi_state_write_to_peer(cgi_state, MIO_NULL, 0) <= -1) goto oops;
			cgi_state_mark_over (cgi_state, CGI_STATE_OVER_READ_FROM_CLIENT | CGI_STATE_OVER_WRITE_TO_PEER);
		}
#if defined(CGI_ALLOW_UNLIMITED_REQ_CONTENT_LENGTH)
	}
#endif

	/* this may change later if Content-Length is included in the cgi output */
	if (req->flags & MIO_HTRE_ATTR_KEEPALIVE)
	{
		cgi_state->keep_alive = 1;
		cgi_state->res_mode_to_cli = CGI_STATE_RES_MODE_CHUNKED; 
		/* the mode still can get switched to CGI_STATE_RES_MODE_LENGTH if the cgi script emits Content-Length */
	}
	else
	{
		cgi_state->keep_alive = 0;
		cgi_state->res_mode_to_cli = CGI_STATE_RES_MODE_CLOSE;
	}

	/* TODO: store current input watching state and use it when destroying the cgi_state data */
	if (mio_dev_sck_read(csck, !(cgi_state->over & CGI_STATE_OVER_READ_FROM_CLIENT)) <= -1) goto oops;
	return 0;

oops:
	MIO_DEBUG2 (mio, "HTTS(%p) - FAILURE in docgi - socket(%p)\n", htts, csck);
	if (cgi_state) cgi_state_halt_participating_devices (cgi_state);
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

/* ----------------------------------------------------------------- */

#if 0
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

	rsrc = mio_svc_htts_rsrc_make(htts, csck, rsrc_cgi_on_kill, MIO_SIZEOF(*cgi));
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
