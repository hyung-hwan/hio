/*
 * A minimal HTTP upstream for the test suite, built on hio.
 *
 * It answers any request with a fixed marker plus the request line it
 * received, so a test driving it through the proxy can tell both that the
 * response came from here and that the method and path arrived intact.
 * A request for a path containing "/missing" is answered with 404 so the
 * status-propagation path can be checked too.
 *
 * Usage: httpecho <ipaddr:port> [ready-file]
 */

#include <hio.h>
#include <hio-sck.h>
#include <hio-utl.h>

#include <stdio.h>
#include <string.h>
#include <signal.h>

#define INBUF_CAPA 8192

typedef struct conn_xtn_t conn_xtn_t;
struct conn_xtn_t
{
	hio_bch_t buf[INBUF_CAPA];
	hio_oow_t len;
	int       answered;
};

static hio_t* g_hio = HIO_NULL;

static void on_sigint (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

static int answer (hio_dev_sck_t* sck, conn_xtn_t* cx)
{
	hio_bch_t body[INBUF_CAPA + 64];
	hio_bch_t head[256];
	hio_bch_t reqline[512];
	hio_oow_t i, n;
	int status = 200;
	const hio_bch_t* reason = "OK";

	/* the request line is everything up to the first CR or LF */
	for (i = 0; i < cx->len && i < HIO_COUNTOF(reqline) - 1; i++)
	{
		if (cx->buf[i] == '\r' || cx->buf[i] == '\n') break;
		reqline[i] = cx->buf[i];
	}
	reqline[i] = '\0';

	if (hio_find_bchars_in_bchars(reqline, i, "/missing", 8, 0))
	{
		status = 404;
		reason = "Not Found";
	}

	n = snprintf(body, HIO_COUNTOF(body), "httpecho\r\n%s\r\n", reqline);
	snprintf (head, HIO_COUNTOF(head),
	          "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
	          status, reason, (int)n);

	if (hio_dev_sck_write(sck, head, hio_count_bcstr(head), HIO_NULL, HIO_NULL) <= -1) return -1;
	if (hio_dev_sck_write(sck, body, n, HIO_NULL, HIO_NULL) <= -1) return -1;
	return 0;
}

static int on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	conn_xtn_t* cx = hio_dev_sck_getxtn(sck);

	if (len <= 0) return 0;
	if (cx->answered) return 0;

	if (cx->len + len > HIO_SIZEOF(cx->buf))
	{
		hio_dev_sck_halt (sck);
		return 0;
	}

	memcpy (&cx->buf[cx->len], buf, len);
	cx->len += len;

	/* answer as soon as the header block is complete. a request body is
	 * not needed for what this fixture is for. */
	if (cx->len >= 4 && hio_find_bchars_in_bchars(cx->buf, cx->len, "\r\n\r\n", 4, 0))
	{
		cx->answered = 1;
		if (answer(sck, cx) <= -1) hio_dev_sck_halt (sck);
	}

	return 0;
}

static int on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_ACCEPTED)
	{
		conn_xtn_t* cx = hio_dev_sck_getxtn(sck);
		cx->len = 0;
		cx->answered = 0;
	}
}

static void on_disconnect (hio_dev_sck_t* sck)
{
}

int main (int argc, char* argv[])
{
	hio_dev_sck_t* svr;
	hio_dev_sck_make_t m;
	hio_dev_sck_bind_t b;
	hio_dev_sck_listen_t l;
	hio_skad_t addr;
	int ret = -1;

	if (argc < 2 || argc > 3)
	{
		fprintf (stderr, "Usage: %s <ipaddr:port> [ready-file]\n", argv[0]);
		return -1;
	}

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 256, HIO_NULL);
	if (!g_hio)
	{
		fprintf (stderr, "unable to open hio\n");
		return -1;
	}

	signal (SIGINT, on_sigint);
	signal (SIGTERM, on_sigint);
	signal (SIGPIPE, SIG_IGN);

	if (hio_bcstrtoskad(g_hio, argv[1], &addr) <= -1)
	{
		fprintf (stderr, "invalid address - %s\n", argv[1]);
		goto oops;
	}

	memset (&m, 0, HIO_SIZEOF(m));
	m.type = (hio_skad_get_family(&addr) == HIO_AF_INET)? HIO_DEV_SCK_TCP4: HIO_DEV_SCK_TCP6;
	m.on_read = on_read;
	m.on_write = on_write;
	m.on_connect = on_connect;
	m.on_disconnect = on_disconnect;

	svr = hio_dev_sck_make(g_hio, HIO_SIZEOF(conn_xtn_t), &m);
	if (!svr) { fprintf (stderr, "unable to make a socket - %s\n", hio_geterrbmsg(g_hio)); goto oops; }

	memset (&b, 0, HIO_SIZEOF(b));
	b.localaddr = addr;
	b.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(svr, &b) <= -1) { fprintf (stderr, "unable to bind - %s\n", hio_geterrbmsg(g_hio)); goto oops; }

	memset (&l, 0, HIO_SIZEOF(l));
	l.backlogs = 64;
	if (hio_dev_sck_listen(svr, &l) <= -1) { fprintf (stderr, "unable to listen - %s\n", hio_geterrbmsg(g_hio)); goto oops; }

	if (argc >= 3)
	{
		FILE* fp = fopen(argv[2], "w");
		if (fp) { fputs ("ready\n", fp); fclose (fp); }
	}

	hio_loop (g_hio);
	ret = 0;

oops:
	if (g_hio) hio_close (g_hio);
	return ret;
}
