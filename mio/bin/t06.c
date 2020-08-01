#include <mio-sck.h>
#include <mio-http.h>
#include <mio-utl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#define NUM_THRS 8
static int  g_reuse_port = 0;
static mio_svc_htts_t* g_htts[NUM_THRS];
static int g_htts_no = 0;
static pthread_mutex_t g_htts_mutex = PTHREAD_MUTEX_INITIALIZER;

static int print_qparam (mio_bcs_t* key, mio_bcs_t* val, void* ctx)
{
	key->len = mio_perdec_http_bcs(key, key->ptr, MIO_NULL);
	val->len = mio_perdec_http_bcs(val, val->ptr, MIO_NULL);
	fprintf ((FILE*)ctx, "\t[%.*s] = [%.*s]\n", (int)key->len, key->ptr, (int)val->len, val->ptr);
	return 0;
}

static void on_htts_thr_request (mio_t* mio, mio_dev_thr_iopair_t* iop, mio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* fp;
	int i;

	if (tfi->req_method != MIO_HTTP_GET)
	{
		write (iop->wfd, "Status: 405\r\n\r\n", 15); /* method not allowed */
		return;
	}

	fp = fdopen(iop->wfd, "w");
	if (!fp)
	{
		write (iop->wfd, "Status: 500\r\n\r\n", 15); /* internal server error */
		return;
	}

	fprintf (fp, "Status: 200\r\n");
	fprintf (fp, "Content-Type: text/html\r\n\r\n");

	fprintf (fp, "request path = %s\n", tfi->req_path);
	if (tfi->req_param) 
	{
		fprintf (fp, "request params:\n");
		mio_scan_http_qparam (tfi->req_param, print_qparam, fp);
	}
	for (i = 0; i < 100; i++) fprintf (fp, "%d * %d => %d\n", i, i, i * i);

	/* invalid iop->wfd to mark that this function closed this file descriptor. 
	 * no invalidation will lead to double closes on the same file descriptor. */
	iop->wfd = MIO_SYSHND_INVALID; 
	fclose (fp);
}

static void on_htts_thr2_request (mio_t* mio, mio_dev_thr_iopair_t* iop, mio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* fp, * sf;

	if (tfi->req_method != MIO_HTTP_GET)
	{
		write (iop->wfd, "Status: 405\r\n\r\n", 15); /* method not allowed */
		return;
	}

	fp = fdopen(iop->wfd, "w");
	if (!fp)
	{
		write (iop->wfd, "Status: 500\r\n\r\n", 15); /* internal server error */
		return;
	}

	sf = fopen(&tfi->req_path[5],  "r");
	if (!sf)
	{
		fprintf (fp, "Status: 404\r\n\r\n");
	}
	else
	{
		char buf[4096];

		fprintf (fp, "Status: 200\r\n");
		fprintf (fp, "Content-Type: text/html\r\n\r\n");

		while (!feof(sf))
		{
			size_t n;
			n = fread(buf, 1, sizeof(buf), sf);
			if (n > 0) fwrite (buf, 1, n, fp);
		}

		fclose (sf);
	}

	/* invalid iop->wfd to mark that this function closed this file descriptor. 
	 * no invalidation will lead to double closes on the same file descriptor. */
	iop->wfd = MIO_SYSHND_INVALID; 
	fclose (fp);
}

/* ========================================================================= */
int process_http_request (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req)
{
	mio_t* mio = mio_svc_htts_getmio(htts);
//	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	mio_http_method_t mth;

	/* percent-decode the query path to the original buffer
	 * since i'm not going to need it in the original form
	 * any more. once it's decoded in the peek mode,
	 * the decoded query path is made available in the
	 * non-peek mode as well */

	MIO_DEBUG2 (mio, "[RAW-REQ] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));

	mio_htre_perdecqpath(req);
	/* TODO: proper request logging */

	MIO_DEBUG2 (mio, "[REQ] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));

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
			const mio_bch_t* qpath = mio_htre_getqpath(req);
			int x;
			if (mio_comp_bcstr_limited(qpath, "/thr/", 5, 1) == 0)
				x = mio_svc_htts_dothr(htts, csck, req, on_htts_thr_request, MIO_NULL);
			else if (mio_comp_bcstr_limited(qpath, "/thr2/", 6, 1) == 0)
				x = mio_svc_htts_dothr(htts, csck, req, on_htts_thr2_request, MIO_NULL);
			else if (mio_comp_bcstr_limited(qpath, "/txt/", 5, 1) == 0)
				x = mio_svc_htts_dotxt(htts, csck, req, 200, "text/plain", qpath);
			else if (mio_comp_bcstr_limited(qpath, "/cgi/", 5, 1) == 0)
				x = mio_svc_htts_docgi(htts, csck, req, "", mio_htre_getqpath(req));
			else
				x = mio_svc_htts_dofile(htts, csck, req, "", mio_htre_getqpath(req), "text/plain");
			if (x <= -1) goto oops;

	return 0;

oops:
	mio_dev_sck_halt (csck);
	return -1;
}

void* thr_func (void* arg)
{
	mio_t* mio = MIO_NULL;
	mio_svc_htts_t* htts = MIO_NULL;
	mio_dev_sck_bind_t htts_bind_info;

	mio = mio_open(MIO_NULL, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	memset (&htts_bind_info, 0, MIO_SIZEOF(htts_bind_info));
	if (g_reuse_port)
	{
		mio_bcstrtoskad (mio, "0.0.0.0:9987", &htts_bind_info.localaddr);
		htts_bind_info.options = MIO_DEV_SCK_BIND_REUSEADDR | MIO_DEV_SCK_BIND_REUSEPORT | MIO_DEV_SCK_BIND_IGNERR;
		//htts_bind_info.options |= MIO_DEV_SCK_BIND_SSL; 
		htts_bind_info.ssl_certfile = "localhost.crt";
		htts_bind_info.ssl_keyfile = "localhost.key";
	}

	htts = mio_svc_htts_start(mio, &htts_bind_info, process_http_request);
	if (!htts) 
	{
		printf ("Unable to start htts\n");
		goto oops;
	}

	pthread_mutex_lock (&g_htts_mutex);
	g_htts[g_htts_no] = htts;
printf ("starting the loop for %d\n", g_htts_no);
	g_htts_no = (g_htts_no + 1) % MIO_COUNTOF(g_htts);
	pthread_mutex_unlock (&g_htts_mutex);

	mio_loop (mio);

oops:
	if (htts) mio_svc_htts_stop (htts);
	if (mio) mio_close (mio);

	pthread_exit (MIO_NULL);
	return MIO_NULL;
}


/* ========================================================================= */

static void tcp_sck_on_disconnect (mio_dev_sck_t* tcp)
{
	switch (MIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case MIO_DEV_SCK_CONNECTING:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO1 (tcp->mio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_CONNECTED:
			MIO_INFO1 (tcp->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			MIO_INFO1 (tcp->mio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			MIO_INFO1 (tcp->mio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->hnd);
			break;

		default:
			MIO_INFO2 (tcp->mio, "SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->hnd, (unsigned int)tcp->state);
			break;
	}
}

static void tcp_sck_on_connect (mio_dev_sck_t* tcp)
{
	mio_bch_t buf1[128], buf2[128];

	mio_skadtobcstr (tcp->mio, &tcp->localaddr, buf1, MIO_COUNTOF(buf1), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT);
	mio_skadtobcstr (tcp->mio, &tcp->remoteaddr, buf2, MIO_COUNTOF(buf2), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT);

	if (tcp->state & MIO_DEV_SCK_CONNECTED)
	{
		MIO_INFO3 (tcp->mio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}
	else if (tcp->state & MIO_DEV_SCK_ACCEPTED)
	{
		/* TODO: pass it to distributor??? */
/* THIS PART WON"T BE CALLED FOR tcp_sck_on_raw_accept.. */
	}
}


static mio_tmridx_t xx_tmridx;
static int try_to_accept (mio_dev_sck_t* sck, mio_dev_sck_qxmsg_t* qxmsg, int in_mq);

typedef struct xx_mq_t xx_mq_t;

struct xx_mq_t
{
	xx_mq_t*    q_next;
	xx_mq_t*    q_prev;

	mio_dev_sck_qxmsg_t msg;
};

#define XX_MQ_INIT(mq) ((mq)->q_next = (mq)->q_prev = (mq))
#define XX_MQ_TAIL(mq) ((mq)->q_prev)
#define XX_MQ_HEAD(mq) ((mq)->q_next)
#define XX_MQ_IS_EMPTY(mq) (XX_MQ_HEAD(mq) == (mq))
#define XX_MQ_IS_NODE(mq,x) ((mq) != (x))
#define XX_MQ_IS_HEAD(mq,x) (XX_MQ_HEAD(mq) == (x))
#define XX_MQ_IS_TAIL(mq,x) (XX_MQ_TAIL(mq) == (x))
#define XX_MQ_NEXT(x) ((x)->q_next)
#define XX_MQ_PREV(x) ((x)->q_prev)
#define XX_MQ_LINK(p,x,n) MIO_Q_LINK((mio_q_t*)p,(mio_q_t*)x,(mio_q_t*)n)
#define XX_MQ_UNLINK(x) MIO_Q_UNLINK((mio_q_t*)x)
#define XX_MQ_REPL(o,n) MIO_Q_REPL(o,n);
#define XX_MQ_ENQ(mq,x) XX_MQ_LINK(XX_MQ_TAIL(mq), (mio_q_t*)x, mq)
#define XX_MQ_DEQ(mq) XX_MQ_UNLINK(XX_MQ_HEAD(mq))

static xx_mq_t xx_mq;

#if 0
static int schedule_timer_job_at (mio_dev_sck_t* dev, const mio_ntime_t* fire_at, mio_tmrjob_handler_t handler, mio_tmridx_t* tmridx)
{
	mio_tmrjob_t tmrjob;

	memset (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	if (fire_at) tmrjob.when = *fire_at;

	tmrjob.handler = handler;
	tmrjob.idxptr = tmridx;

	return mio_instmrjob(dev->mio, &tmrjob);
}
#endif

static void enable_accept (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	mio_dev_sck_t* rdev = (mio_dev_sck_t*)job->ctx;

	while (!XX_MQ_IS_EMPTY(&xx_mq))
	{
		xx_mq_t* mq;
	       
		mq = XX_MQ_HEAD(&xx_mq);
		if (try_to_accept(rdev, &mq->msg, 1) == 0) return; /* EAGAIN situation */

		XX_MQ_UNLINK (mq);
		mio_freemem (mio, mq);
	}

	assert (XX_MQ_IS_EMPTY(&xx_mq));
	if (mio_dev_sck_read(rdev, 1) <= -1) // it's a disaster if this fails. the acceptor will get stalled if it happens
	{
printf ("DISASTER.... UNABLE TO ENABLE READ ON ACCEPTOR\n");
	}
}

static int try_to_accept (mio_dev_sck_t* sck, mio_dev_sck_qxmsg_t* qxmsg, int in_mq)
{
	mio_t* mio = sck->mio;
	mio_svc_htts_t* htts;

	pthread_mutex_lock (&g_htts_mutex);
	htts = g_htts[g_htts_no];
	g_htts_no = (g_htts_no + 1) % MIO_COUNTOF(g_htts);
	pthread_mutex_unlock (&g_htts_mutex);

	if (mio_svc_htts_writetosidechan(htts, qxmsg, MIO_SIZEOF(*qxmsg)) <= -1)
	{
		mio_bch_t buf[128];

		if (errno == EAGAIN)
		{
//printf ("sidechannel retrying %s\n", strerror(errno));

			if (mio_dev_sck_read(sck, 0) <= -1) goto sidechan_write_error;

			if (!in_mq)
			{
				xx_mq_t* mq;
				mq = mio_allocmem(mio, MIO_SIZEOF(*mq));
				if (MIO_UNLIKELY(!mq)) goto sidechan_write_error;
				mq->msg = *qxmsg;
				XX_MQ_ENQ (&xx_mq, mq);
			}

			if (xx_tmridx == MIO_TMRIDX_INVALID)
				mio_schedtmrjobat (mio, MIO_NULL, enable_accept, &xx_tmridx, sck);

			return 0; /* enqueued for later writing */
		}
		else
		{
		sidechan_write_error:
printf ("sidechannel write error %s\n", strerror(errno));
			mio_skadtobcstr (mio, &qxmsg->remoteaddr, buf, MIO_COUNTOF(buf), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT); 
			MIO_INFO2 (mio, "unable to handle the accepted connection %ld from %hs\n", (long int)qxmsg->syshnd, buf);

			const char* msg = "HTTP/1.0 503 Service unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
			write (qxmsg->syshnd, msg, strlen(msg));
	printf ("close %d\n", qxmsg->syshnd);
			close (qxmsg->syshnd);

			return -1; /* failed to accept */
		}
	}

/************************************
{
static int sc = 0;
printf ("sc => %d\n", sc++);
}
************************************/

	return 1; /* full success */
}

static void tcp_sck_on_raw_accept (mio_dev_sck_t* sck, mio_syshnd_t syshnd, mio_skad_t* remoteaddr)
{
	/*mio_t* mio = sck->mio;*/

	/* inform the worker of this accepted syshnd */
	mio_dev_sck_qxmsg_t qxmsg;
	memset (&qxmsg, 0, MIO_SIZEOF(qxmsg));
	qxmsg.cmd = MIO_DEV_SCK_QXMSG_NEWCONN;
	qxmsg.scktype = sck->type;
	qxmsg.syshnd = syshnd;
	qxmsg.remoteaddr = *remoteaddr;

//printf ("A %d\n", qxmsg.syshnd);
	try_to_accept (sck, &qxmsg, 0);
}

static int tcp_sck_on_write (mio_dev_sck_t* tcp, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	/* won't be invoked */
	return 0;
}

static int tcp_sck_on_read (mio_dev_sck_t* tcp, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	/* won't be invoked */
	return 0;
}

static int add_listener (mio_t* mio, mio_bch_t* addrstr)
{
	mio_dev_sck_make_t mi;
	mio_dev_sck_t* tcp;
	mio_dev_sck_bind_t bi;
	mio_dev_sck_listen_t li;

	memset (&bi, 0, MIO_SIZEOF(bi));
	if (mio_bcstrtoskad(mio, addrstr, &bi.localaddr) <= -1)
	{
		MIO_INFO1 (mio, "invalid listening address - %hs\n", addrstr);
		return -1;
	}
	bi.options = MIO_DEV_SCK_BIND_REUSEADDR /*| MIO_DEV_SCK_BIND_REUSEPORT |*/;
#if defined(USE_SSL)
	bi.options |= MIO_DEV_SCK_BIND_SSL; 
	bi.ssl_certfile = "localhost.crt";
	bi.ssl_keyfile = "localhost.key";
#endif

	memset (&mi, 0, MIO_SIZEOF(mi));
	mi.type = (mio_skad_family(&bi.localaddr) == MIO_AF_INET? MIO_DEV_SCK_TCP4: MIO_DEV_SCK_TCP6);
	mi.options = MIO_DEV_SCK_MAKE_LENIENT;
	mi.on_write = tcp_sck_on_write;
	mi.on_read = tcp_sck_on_read;
	mi.on_connect = tcp_sck_on_connect; /* this is invoked on a client accept as well */
	mi.on_disconnect = tcp_sck_on_disconnect;
	mi.on_raw_accept = tcp_sck_on_raw_accept;

	tcp = mio_dev_sck_make(mio, 0, &mi);
	if (!tcp)
	{
		MIO_INFO1 (mio, "Cannot make tcp - %js\n", mio_geterrmsg(mio));
		return -1;
	}

	if (!g_reuse_port)
	{
		if (mio_dev_sck_bind(tcp, &bi) <= -1)
		{
			MIO_INFO1 (mio, "tcp mio_dev_sck_bind() failed - %js\n", mio_geterrmsg(mio));
			return -1;
		}
	}

	memset (&li, 0, MIO_SIZEOF(li));
	li.backlogs = 4096;
	MIO_INIT_NTIME (&li.accept_tmout, 5, 1);
	if (mio_dev_sck_listen(tcp, &li) <= -1)
	{
		MIO_INFO1 (mio, "tcp[2] mio_dev_sck_listen() failed - %js\n", mio_geterrmsg(mio));
		return -1;
	}

	return 0;
}


int main (int argc, char* argv[])
{
	mio_t* mio = MIO_NULL;
	pthread_t t[NUM_THRS];
	mio_oow_t i;
	struct sigaction sigact;

	if (argc >= 2 && strcmp(argv[1], "-r") == 0)
	{
		g_reuse_port = 1;
	}

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, MIO_NULL);

	XX_MQ_INIT (&xx_mq);
	xx_tmridx = MIO_TMRIDX_INVALID;

	mio = mio_open(MIO_NULL, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		goto oops;
	}

	for (i = 0; i < MIO_COUNTOF(t); i++)
		pthread_create (&t[i], MIO_NULL, thr_func, mio);

	sleep (1); /* TODO: use pthread_cond_wait()/pthread_cond_signal() or a varialble to see if all threads are up */
/* TODO: wait until all threads are ready to serve... */

	if (add_listener(mio, "[::]:9987") <= -1 ||
	    add_listener(mio, "0.0.0.0:9987") <= -1) goto oops;

printf ("starting the main loop\n");
	mio_loop (mio);

	/* close all threaded mios here */
printf ("TERMINATING..NORMALLY \n");
	mio_close (mio);
	return 0;

oops:
printf ("TERMINATING..ABNORMALLY \n");
	if (mio) mio_close (mio);
	return -1;
}

