#include <hio-sck.h>
#include <hio-http.h>
#include <hio-dhcp.h>
#include <hio-utl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

#define MAX_NUM_THRS 256
static int g_num_thrs = 2;
static hio_svc_htts_t* g_htts[MAX_NUM_THRS];
static int g_htts_no = 0;
static pthread_mutex_t g_htts_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_dev_type4 = HIO_DEV_SCK_TCP4;
static int g_dev_type6 = HIO_DEV_SCK_TCP6;

static int print_qparam (hio_bcs_t* key, hio_bcs_t* val, void* ctx)
{
	key->len = hio_perdec_http_bcs(key, key->ptr, HIO_NULL);
	val->len = hio_perdec_http_bcs(val, val->ptr, HIO_NULL);
	fprintf ((FILE*)ctx, "\t[%.*s] = [%.*s]\n", (int)key->len, key->ptr, (int)val->len, val->ptr);
	return 0;
}

static void on_htts_thr_request (hio_svc_htts_t* htts, hio_dev_thr_iopair_t* iop, hio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* fp;
	int i;

	if (tfi->req_method != HIO_HTTP_GET)
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

	fprintf (fp, "Status: %d\r\n", HIO_HTTP_STATUS_OK);
	fprintf (fp, "Content-Type: text/html\r\n\r\n");

	fprintf (fp, "request path = %s\n", tfi->req_path);
	if (tfi->req_param) 
	{
		fprintf (fp, "request params:\n");
		hio_scan_http_qparam (tfi->req_param, print_qparam, fp);
	}
	for (i = 0; i < 100; i++) fprintf (fp, "%d * %d => %d\n", i, i, i * i);

	/* invalid iop->wfd to mark that this function closed this file descriptor. 
	 * no invalidation will lead to double closes on the same file descriptor. */
	iop->wfd = HIO_SYSHND_INVALID; 
	fclose (fp);
}

static void on_htts_thr2_request (hio_svc_htts_t* htts, hio_dev_thr_iopair_t* iop, hio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* fp, * sf;

	if (tfi->req_method != HIO_HTTP_GET)
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
		fprintf (fp, "Status: %d\r\n\r\n", HIO_HTTP_STATUS_NOT_FOUND);
	}
	else
	{
		char buf[4096];

		fprintf (fp, "Status: %d\r\n", HIO_HTTP_STATUS_OK);
		fprintf (fp, "Content-Type: text/html\r\n\r\n");

		/* echo back the posted data */
		while (1)
		{
			ssize_t n = read(iop->rfd, buf, sizeof(buf));
			if (n <= 0) break;
			fwrite (buf, 1, n, fp);
		}


		/* output the file contents */
		while (!feof(sf) && !ferror(sf))
		{
			size_t n;
			n = fread(buf, 1, sizeof(buf), sf);
			if (n > 0) fwrite (buf, 1, n, fp);
		}

		fclose (sf);
	}

	/* invalid iop->wfd to mark that this function closed this file descriptor. 
	 * no invalidation will lead to double closes on the same file descriptor. 
	 * the hio library attempt to close it if it's not INVALID after this handler. */
	iop->wfd = HIO_SYSHND_INVALID; 
	fclose (fp);
}

/* ========================================================================= */
int process_http_request (hio_svc_htts_t* htts, hio_dev_sck_t* csck, hio_htre_t* req)
{
	hio_t* hio = hio_svc_htts_gethio(htts);
//	hio_svc_htts_cli_t* cli = hio_dev_sck_getxtn(csck);
//	hio_http_method_t mth;

	/* percent-decode the query path to the original buffer
	 * since i'm not going to need it in the original form
	 * any more. once it's decoded in the peek mode,
	 * the decoded query path is made available in the
	 * non-peek mode as well */

	HIO_DEBUG2 (hio, "[RAW-REQ] %s %s\n", hio_htre_getqmethodname(req), hio_htre_getqpath(req));

	hio_htre_perdecqpath(req);
	/* TODO: proper request logging */

	HIO_DEBUG2 (hio, "[REQ] %s %s\n", hio_htre_getqmethodname(req), hio_htre_getqpath(req));

#if 0
hio_printf (HIO_T("================================\n"));
hio_printf (HIO_T("[%lu] %hs REQUEST ==> [%hs] version[%d.%d %hs] method[%hs]\n"),
	(unsigned long)time(NULL),
	(peek? HIO_MT("PEEK"): HIO_MT("HANDLE")),
	hio_htre_getqpath(req),
	hio_htre_getmajorversion(req),
	hio_htre_getminorversion(req),
	hio_htre_getverstr(req),
	hio_htre_getqmethodname(req)
);
if (hio_htre_getqparam(req))
	hio_printf (HIO_T("PARAMS ==> [%hs]\n"), hio_htre_getqparam(req));

hio_htb_walk (&req->hdrtab, walk, HIO_NULL);
if (hio_htre_getcontentlen(req) > 0)
{
	hio_printf (HIO_T("CONTENT [%.*S]\n"), (int)hio_htre_getcontentlen(req), hio_htre_getcontentptr(req));
}
#endif

//	mth = hio_htre_getqmethodtype(req);
	/* determine what to do once the header fields are all received.
	 * i don't want to delay this until the contents are received.
	 * if you don't like this behavior, you must implement your own
	 * callback function for request handling. */
#if 0
	/* TODO support X-HTTP-Method-Override */
	if (data.method == HIO_HTTP_POST)
	{
		tmp = hio_htre_getheaderval(req, HIO_MT("X-HTTP-Method-Override"));
		if (tmp)
		{
			/*while (tmp->next) tmp = tmp->next;*/ /* get the last value */
			data.method = hio_mbstohttpmethod (tmp->ptr);
		}
	}
#endif

#if 0
	if (mth == HIO_HTTP_CONNECT)
	{
		/* CONNECT method must not have content set. 
		 * however, arrange to discard it if so. 
		 *
		 * NOTE: CONNECT is implemented to ignore many headers like
		 *       'Expect: 100-continue' and 'Connection: keep-alive'. */
		hio_htre_discardcontent (req);
	}
	else 
	{
/* this part can be checked in actual hio_svc_htts_doXXX() functions.
 * some doXXX handlers may not require length for POST.
 * it may be able to simply accept till EOF? or  treat as if CONTENT_LENGTH is 0*/
		if (mth == HIO_HTTP_POST && !(req->flags & (HIO_HTRE_ATTR_LENGTH | HIO_HTRE_ATTR_CHUNKED)))
		{
			/* POST without Content-Length nor not chunked */
			hio_htre_discardcontent (req); 
			/* 411 Length Required - can't keep alive. Force disconnect */
			req->flags &= ~HIO_HTRE_ATTR_KEEPALIVE; /* to cause sendstatus() to close */
			if (hio_svc_htts_sendstatus(htts, csck, req, HIO_HTTP_STATUS_LENGTH_REQUIRED, HIO_NULL) <= -1) goto oops;
		}
		else

#endif
		{
			const hio_bch_t* qpath = hio_htre_getqpath(req);
			int x;

			if (hio_comp_bcstr_limited(qpath, "/thr/", 5, 1) == 0)
				x = hio_svc_htts_dothr(htts, csck, req, on_htts_thr_request, HIO_NULL, 0, HIO_NULL);
			else if (hio_comp_bcstr_limited(qpath, "/thr2/", 6, 1) == 0)
				x = hio_svc_htts_dothr(htts, csck, req, on_htts_thr2_request, HIO_NULL, 0, HIO_NULL);
			else if (hio_comp_bcstr_limited(qpath, "/txt/", 5, 1) == 0)
				x = hio_svc_htts_dotxt(htts, csck, req, HIO_HTTP_STATUS_OK, "text/plain", qpath, 0, HIO_NULL);
			else if (hio_comp_bcstr_limited(qpath, "/cgi/", 5, 1) == 0)
				x = hio_svc_htts_docgi(htts, csck, req, "", qpath + 4, 0, HIO_NULL);
			else if (hio_comp_bcstr_limited(qpath, "/fcgi/", 5, 1) == 0)
			{
				hio_skad_t fcgis_addr;
				hio_bcstrtoskad(hio, "127.0.0.1:9000", &fcgis_addr);
				x = hio_svc_htts_dofcgi(htts, csck, req, &fcgis_addr, "", qpath + 5, 0, HIO_NULL);
			}
			else
				x = hio_svc_htts_dofile(htts, csck, req, "", qpath, "text/plain", 0, HIO_NULL, HIO_NULL);
			if (x <= -1) goto oops;
		}
#if 0
	}
#endif

	return 0;

oops:
	hio_dev_sck_halt (csck);
	return -1;
}

void* thr_func (void* arg)
{
	hio_t* hio = HIO_NULL;
	hio_svc_htts_t* htts = HIO_NULL;
	hio_dev_sck_bind_t htts_bind_info[2];
	int htts_no = -1;

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		goto oops;
	}

	hio_setoption (hio, HIO_LOG_TARGET_BCSTR, "/dev/stderr");

	memset (&htts_bind_info, 0, HIO_SIZEOF(htts_bind_info));
	hio_skad_init_for_qx (&htts_bind_info[0].localaddr); /* QX socket device */

	hio_bcstrtoskad (hio, "0.0.0.0:9988", &htts_bind_info[1].localaddr);
	htts_bind_info[1].options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_REUSEPORT | HIO_DEV_SCK_BIND_IGNERR;
#if 0
	htts_bind_info[1].options |= HIO_DEV_SCK_BIND_SSL; 
	htts_bind_info[1].ssl_certfile = "localhost.crt";
	htts_bind_info[1].ssl_keyfile = "localhost.key";
#endif

	htts = hio_svc_htts_start(hio, 0, htts_bind_info, HIO_COUNTOF(htts_bind_info), process_http_request);
	if (!htts) 
	{
		printf ("Unable to start htts\n");
		goto oops;
	}

	pthread_mutex_lock (&g_htts_mutex);
	htts_no = g_htts_no;
	g_htts[htts_no] = htts;
	g_htts_no = (g_htts_no + 1) % g_num_thrs;
	pthread_mutex_unlock (&g_htts_mutex);

	hio_loop (hio);

oops:
	pthread_mutex_lock (&g_htts_mutex);
	if (htts) 
	{
		hio_svc_htts_stop (htts);
		g_htts[htts_no] = HIO_NULL;
	}
	pthread_mutex_unlock (&g_htts_mutex);
	if (hio) hio_close (hio);

	pthread_exit (HIO_NULL);
	return HIO_NULL;
}


/* ========================================================================= */

static void tcp_sck_on_disconnect (hio_dev_sck_t* tcp)
{
	switch (HIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case HIO_DEV_SCK_CONNECTING:
			HIO_INFO1 (tcp->hio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_CONNECTING_SSL:
			HIO_INFO1 (tcp->hio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_LISTENING:
			HIO_INFO1 (tcp->hio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_CONNECTED:
			HIO_INFO1 (tcp->hio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_ACCEPTING_SSL:
			HIO_INFO1 (tcp->hio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->hnd);
			break;

		case HIO_DEV_SCK_ACCEPTED:
			HIO_INFO1 (tcp->hio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->hnd);
			break;

		default:
			HIO_INFO2 (tcp->hio, "SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->hnd, (unsigned int)tcp->state);
			break;
	}
}

static void tcp_sck_on_connect (hio_dev_sck_t* tcp)
{
	hio_bch_t buf1[128], buf2[128];

	hio_skadtobcstr (tcp->hio, &tcp->localaddr, buf1, HIO_COUNTOF(buf1), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
	hio_skadtobcstr (tcp->hio, &tcp->remoteaddr, buf2, HIO_COUNTOF(buf2), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);

	if (tcp->state & HIO_DEV_SCK_CONNECTED)
	{
		HIO_INFO3 (tcp->hio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}
	else if (tcp->state & HIO_DEV_SCK_ACCEPTED)
	{
		/* TODO: pass it to distributor??? */
		/* THIS PART WON'T BE CALLED FOR tcp_sck_on_raw_accept.. */
	}
}


static hio_tmridx_t xx_tmridx;
static int try_to_accept (hio_dev_sck_t* sck, hio_dev_sck_qxmsg_t* qxmsg, int in_mq);

typedef struct xx_mq_t xx_mq_t;

struct xx_mq_t
{
	xx_mq_t*    q_next;
	xx_mq_t*    q_prev;

	hio_dev_sck_qxmsg_t msg;
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
#define XX_MQ_LINK(p,x,n) HIO_Q_LINK((hio_q_t*)p,(hio_q_t*)x,(hio_q_t*)n)
#define XX_MQ_UNLINK(x) HIO_Q_UNLINK((hio_q_t*)x)
#define XX_MQ_REPL(o,n) HIO_Q_REPL(o,n);
#define XX_MQ_ENQ(mq,x) XX_MQ_LINK(XX_MQ_TAIL(mq), (hio_q_t*)x, mq)
#define XX_MQ_DEQ(mq) XX_MQ_UNLINK(XX_MQ_HEAD(mq))

static xx_mq_t xx_mq;

static void enable_accept (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* rdev = (hio_dev_sck_t*)job->ctx;

	while (!XX_MQ_IS_EMPTY(&xx_mq))
	{
		xx_mq_t* mq;
	       
		mq = XX_MQ_HEAD(&xx_mq);
		if (try_to_accept(rdev, &mq->msg, 1) == 0) return; /* EAGAIN situation */

		XX_MQ_UNLINK (mq);
		hio_freemem (hio, mq);
	}

	assert (XX_MQ_IS_EMPTY(&xx_mq));
	if (hio_dev_sck_read(rdev, 1) <= -1) // it's a disaster if this fails. the acceptor will get stalled if it happens
	{
printf ("DISASTER.... UNABLE TO ENABLE READ ON ACCEPTOR\n");
	}
}

static int try_to_accept (hio_dev_sck_t* sck, hio_dev_sck_qxmsg_t* qxmsg, int in_mq)
{
	hio_t* hio = sck->hio;
	hio_svc_htts_t* htts;

	pthread_mutex_lock (&g_htts_mutex);
	htts = g_htts[g_htts_no];
	g_htts_no = (g_htts_no + 1) % g_num_thrs;
	pthread_mutex_unlock (&g_htts_mutex);

	/* 0: index to the QX socket device (see the the first binding address to hi_svc_htts_start) */
	if (hio_svc_htts_writetosidechan(htts, 0, qxmsg, HIO_SIZEOF(*qxmsg)) <= -1)
	{
		hio_bch_t buf[128];

		if (errno == EAGAIN)
		{
//printf ("sidechannel retrying %s\n", strerror(errno));

			if (hio_dev_sck_read(sck, 0) <= -1) goto sidechan_write_error;

			if (!in_mq)
			{
				xx_mq_t* mq;
				mq = hio_allocmem(hio, HIO_SIZEOF(*mq));
				if (HIO_UNLIKELY(!mq)) goto sidechan_write_error;
				mq->msg = *qxmsg;
				XX_MQ_ENQ (&xx_mq, mq);
			}

			if (xx_tmridx == HIO_TMRIDX_INVALID)
				hio_schedtmrjobat (hio, HIO_NULL, enable_accept, &xx_tmridx, sck);

			return 0; /* enqueued for later writing */
		}
		else
		{
			const char* msg;
		sidechan_write_error:
//printf ("sidechannel write error errno=%d strerror=%s\n", errno, strerror(errno));
			hio_skadtobcstr (hio, &qxmsg->remoteaddr, buf, HIO_COUNTOF(buf), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT); 
			HIO_INFO2 (hio, "unable to handle the accepted connection %ld from %hs\n", (long int)qxmsg->syshnd, buf);

			msg = "HTTP/1.0 503 Service unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
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

static void tcp_sck_on_raw_accept (hio_dev_sck_t* sck, hio_syshnd_t syshnd, hio_skad_t* remoteaddr)
{
	/*hio_t* hio = sck->hio;*/

	/* inform the worker of this accepted syshnd */
	hio_dev_sck_qxmsg_t qxmsg;
	memset (&qxmsg, 0, HIO_SIZEOF(qxmsg));
	qxmsg.cmd = HIO_DEV_SCK_QXMSG_NEWCONN;
	qxmsg.scktype = sck->type;
	qxmsg.syshnd = syshnd;
	qxmsg.remoteaddr = *remoteaddr;

	try_to_accept (sck, &qxmsg, 0);
}

static int tcp_sck_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	/* won't be invoked if tcp.
	 * invokde if sctp_sp */
//printf ("wrote back chan %d\n", hio_skad_chan(dstaddr));
	return 0;
}

static int tcp_sck_on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	/* won't be invoked 
	 * invokde if sctp_sp */
#if 0
	hio_iovec_t iov;

/* the code here is invoked on a seqpacket socket. .. this part not ready. will rewrite if the core support is done */
printf ("[%.*s] chan %d\n", (int)len, buf, hio_skad_chan(srcaddr));
iov.iov_ptr = buf;
iov.iov_len = len;
hio_skad_set_scope_id (srcaddr, 3);
hio_dev_sck_writev (sck, &iov, 1, HIO_NULL, srcaddr);
#endif

	return 0;
}

static int add_listener (hio_t* hio, hio_bch_t* addrstr)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* tcp;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	int f;

	memset (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(hio, addrstr, &bi.localaddr) <= -1)
	{
		HIO_INFO1 (hio, "invalid listening address - %hs\n", addrstr);
		return -1;
	}
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR /*| HIO_DEV_SCK_BIND_REUSEPORT |*/;
	bi.options |= HIO_DEV_SCK_BIND_IGNERR;
#if defined(USE_SSL)
	bi.options |= HIO_DEV_SCK_BIND_SSL; 
	bi.ssl_certfile = "localhost.crt";
	bi.ssl_keyfile = "localhost.key";
#endif

	memset (&mi, 0, HIO_SIZEOF(mi));
	f = hio_skad_get_family(&bi.localaddr);
	if (f == HIO_AF_INET) mi.type = g_dev_type4;
	else if (f == HIO_AF_INET6) mi.type = g_dev_type6;
	else if (f == HIO_AF_UNIX) mi.type = HIO_DEV_SCK_UNIX;
	else
	{
		HIO_INFO1 (hio, "unsupported address type - %hs\n", addrstr);
		return -1;
	}

	mi.options = HIO_DEV_SCK_MAKE_LENIENT;
	mi.on_write = tcp_sck_on_write;
	mi.on_read = tcp_sck_on_read;
	mi.on_connect = tcp_sck_on_connect; /* this is invoked on a client accept as well */
	mi.on_disconnect = tcp_sck_on_disconnect;
	mi.on_raw_accept = tcp_sck_on_raw_accept;

	tcp = hio_dev_sck_make(hio, 0, &mi);
	if (!tcp)
	{
		HIO_INFO2 (hio, "Cannot make tcp for %hs - %js\n", addrstr, hio_geterrmsg(hio));
		return -1;
	}

	if (hio_dev_sck_bind(tcp, &bi) <= -1)
	{
		HIO_INFO2 (hio, "tcp hio_dev_sck_bind() failed with %hs - %js\n", addrstr, hio_geterrmsg(hio));
		return -1;
	}

	memset (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4096;
	HIO_INIT_NTIME (&li.accept_tmout, 5, 1);
	if (hio_dev_sck_listen(tcp, &li) <= -1)
	{
		HIO_INFO2 (hio, "tcp hio_dev_sck_listen() failed on %hs - %js\n", addrstr, hio_geterrmsg(hio));
		return -1;
	}
	else
	{
		hio_skad_t skad;
		hio_bch_t buf[HIO_SKAD_IP_STRLEN + 1];
		hio_dev_sck_getsockaddr (tcp, &skad);
		hio_skadtobcstr (hio, &skad, buf, HIO_COUNTOF(buf), HIO_SKAD_TO_BCSTR_ADDR | HIO_SKAD_TO_BCSTR_PORT);
		HIO_INFO1 (hio, "main listener on %hs\n", buf);
	}

	return 0;
}


static hio_t* g_hio = HIO_NULL;

static void handle_sigint (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

int main (int argc, char* argv[])
{
	hio_t* hio = HIO_NULL;
	pthread_t t[MAX_NUM_THRS];
	hio_oow_t i;
	struct sigaction sigact;
	int xret = -1;

// TODO: use getopt() or something similar
	for (i = 1; i < argc; )
	{
		if (strcmp(argv[i], "-s") == 0)
		{
			i++;
			g_dev_type4 = HIO_DEV_SCK_SCTP4;
			g_dev_type6 = HIO_DEV_SCK_SCTP6;
		}
		else if (strcmp(argv[i], "-ss") == 0)
		{
			i++;
			g_dev_type4 = HIO_DEV_SCK_SCTP4_SP;
			g_dev_type6 = HIO_DEV_SCK_SCTP6_SP;
		}
		else if (strcmp(argv[i], "-t") == 0)
		{
			i++;
			if (i < argc)
			{
				g_num_thrs = atoi(argv[i]);
				if (g_num_thrs < 1 || g_num_thrs > MAX_NUM_THRS)
				{
					printf ("Error: %s not allowed for -t\n", argv[i]);
					return -1;
				}
				i++;
			}
			else
			{
				g_num_thrs = 2;
			}
		}
		else
		{
			printf ("Error: invalid argument %s\n", argv[i]);
			return -1;
		}
	}

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, HIO_NULL);

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = handle_sigint;
	sigaction (SIGINT, &sigact, HIO_NULL);
	

	XX_MQ_INIT (&xx_mq);
	xx_tmridx = HIO_TMRIDX_INVALID;

	hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 512, HIO_NULL);
	if (!hio)
	{
		printf ("Cannot open hio\n");
		goto oops;
	}

	hio_setoption (hio, HIO_LOG_TARGET_BCSTR, "/dev/stderr");

	g_hio = hio;

	for (i = 0; i < g_num_thrs; i++)
		pthread_create (&t[i], HIO_NULL, thr_func, hio);

	sleep (1); /* TODO: use pthread_cond_wait()/pthread_cond_signal() or a varialble to see if all threads are up */
/* TODO: wait until all threads are ready to serve... */

	if (add_listener(hio, "[::]:9987") <= -1 &&
	    add_listener(hio, "0.0.0.0:9987") <= -1) goto oops; /* as long as 1 listener is alive */

	/* add a unix socket listener. 
	 * don't check for an error to continue without it in case it fails. */
	unlink ("t06.sck");
	add_listener(hio, "@t06.sck");

{
hio_skad_t skad;
hio_bcstrtoskad(hio, "[::]:3547", &skad);
hio_svc_dhcs_start (hio, &skad, 1);
}

	hio_loop (hio);

	xret = 0;

oops:

	memset (&sigact, 0, HIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGINT, &sigact, HIO_NULL);

	pthread_mutex_lock (&g_htts_mutex);
	for (i = 0; i < g_num_thrs; i++)
	{
		if (g_htts[i]) hio_stop (hio_svc_htts_gethio(g_htts[i]), HIO_STOPREQ_TERMINATION);
	}
	pthread_mutex_unlock (&g_htts_mutex);

	for (i = 0; i < g_num_thrs; i++)
	{
		pthread_join (t[i], HIO_NULL);
	}

	if (hio) hio_close (hio);
	return xret;
}

