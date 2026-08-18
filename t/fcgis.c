/*
 * A minimal FastCGI responder, built on hio itself, for the test suite.
 *
 * It speaks just enough of the protocol to answer a request:
 * BEGIN_REQUEST / PARAMS / STDIN in, STDOUT / END_REQUEST out. The reply
 * body is a fixed marker so a test can tell that a request really
 * travelled through the fcgi task and the FastCGI protocol both ways
 * rather than being answered somewhere else.
 *
 * The client keeps a connection open across requests, so a connection is
 * served until the peer closes it rather than being torn down after one.
 *
 * Usage: hio-fcgis <ipaddr:port> [ready-file]
 *
 * If ready-file is given it is created once the listener is accepting,
 * which lets a harness start requests without racing this process.
 */

#include <hio.h>
#include <hio-sck.h>
#include <hio-fcgi.h>
#include <hio-utl.h>

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <signal.h>
#include <stdlib.h>

#define RESPONSE_BODY "fcgi-ok\r\n"

/* what a connection has accumulated but not yet consumed */
#define INBUF_CAPA 65536

typedef struct conn_xtn_t conn_xtn_t;
struct conn_xtn_t
{
	hio_uint8_t  buf[INBUF_CAPA];
	hio_oow_t    len;

	/* a request is answerable once it has been begun and both of its
	 * streams have ended. they are tracked separately because the order
	 * they arrive in is not guaranteed - see the note in consume_records() */
	hio_uint16_t id;
	unsigned int begun: 1;
	unsigned int params_done: 1;
	unsigned int stdin_done: 1;
};

static void reset_request (conn_xtn_t* cx)
{
	cx->id = 0;
	cx->begun = 0;
	cx->params_done = 0;
	cx->stdin_done = 0;
}

static hio_t* g_hio = HIO_NULL;
static int g_verbose = 0;

static void on_sigint (int sig)
{
	if (g_hio) hio_stop (g_hio, HIO_STOPREQ_TERMINATION);
}

/* ------------------------------------------------------------------ */

/* The record layout is taken from hio-fcgi.h. Since both this responder
 * and the library's client read it from the same header, a packing
 * mistake would make the two agree with each other while disagreeing
 * with the wire. Check it against the specification once at startup so
 * that shows up as a loud failure instead of a silent one. */
static int check_wire_layout (void)
{
	if (HIO_SIZEOF(hio_fcgi_record_header_t) != 8 ||
	    offsetof(hio_fcgi_record_header_t, version)     != 0 ||
	    offsetof(hio_fcgi_record_header_t, type)        != 1 ||
	    offsetof(hio_fcgi_record_header_t, id)          != 2 ||
	    offsetof(hio_fcgi_record_header_t, content_len) != 4 ||
	    offsetof(hio_fcgi_record_header_t, padding_len) != 6)
	{
		fprintf (stderr, "hio_fcgi_record_header_t does not match the FastCGI wire layout\n");
		return -1;
	}

	if (HIO_SIZEOF(hio_fcgi_begin_request_body_t) != 8 ||
	    HIO_SIZEOF(hio_fcgi_end_request_body_t) != 8)
	{
		fprintf (stderr, "FastCGI body structures do not match the wire layout\n");
		return -1;
	}

	return 0;
}

static int write_record (hio_dev_sck_t* sck, int type, hio_uint16_t id, const void* data, hio_uint16_t len)
{
	hio_fcgi_record_header_t hdr;

	memset (&hdr, 0, HIO_SIZEOF(hdr));
	hdr.version = HIO_FCGI_VERSION;
	hdr.type = type;
	hdr.id = hio_hton16(id);
	hdr.content_len = hio_hton16(len);

	if (hio_dev_sck_write(sck, &hdr, HIO_SIZEOF(hdr), HIO_NULL, HIO_NULL) <= -1) return -1;
	if (len > 0 && hio_dev_sck_write(sck, data, len, HIO_NULL, HIO_NULL) <= -1) return -1;
	return 0;
}

static int respond (hio_dev_sck_t* sck, hio_uint16_t id)
{
	static const char out[] = "Content-Type: text/plain\r\n\r\n" RESPONSE_BODY;
	hio_fcgi_end_request_body_t end;

	if (write_record(sck, HIO_FCGI_STDOUT, id, out, HIO_SIZEOF(out) - 1) <= -1) return -1;
	if (write_record(sck, HIO_FCGI_STDOUT, id, HIO_NULL, 0) <= -1) return -1; /* end of stream */

	memset (&end, 0, HIO_SIZEOF(end));
	end.app_status = hio_hton32(0);
	end.proto_status = HIO_FCGI_REQUEST_COMPLETE;
	return write_record(sck, HIO_FCGI_END_REQUEST, id, &end, HIO_SIZEOF(end));
}

/* consume as many whole records as the buffer holds */
static int consume_records (hio_dev_sck_t* sck, conn_xtn_t* cx)
{
	while (cx->len >= HIO_SIZEOF(hio_fcgi_record_header_t))
	{
		hio_fcgi_record_header_t hdr;
		hio_oow_t clen, plen, total;

		memcpy (&hdr, cx->buf, HIO_SIZEOF(hdr));
		clen = hio_ntoh16(hdr.content_len);
		plen = hdr.padding_len;
		total = HIO_SIZEOF(hdr) + clen + plen;

		if (g_verbose) fprintf (stderr, "record type=%d id=%d clen=%d have=%d\n",
		                        (int)hdr.type, (int)hio_ntoh16(hdr.id), (int)clen, (int)cx->len);
		if (cx->len < total) break; /* wait for the rest of this record */

		/* Answer once the request has been begun and both of its streams
		 * have ended, rather than on any single record. A responder has no
		 * business assuming the three arrive in one particular order, and
		 * tracking them separately keeps this working whatever order a
		 * client picks. */
		switch (hdr.type)
		{
			case HIO_FCGI_BEGIN_REQUEST:
				cx->id = hio_ntoh16(hdr.id);
				cx->begun = 1;
				break;

			case HIO_FCGI_PARAMS:
				if (clen == 0) cx->params_done = 1;
				break;

			case HIO_FCGI_STDIN:
				if (clen == 0) cx->stdin_done = 1;
				break;
		}

		if (cx->begun && cx->params_done && cx->stdin_done)
		{
			if (respond(sck, cx->id) <= -1) return -1;
			reset_request (cx); /* the connection may carry more requests */
		}

		memmove (cx->buf, &cx->buf[total], cx->len - total);
		cx->len -= total;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

static int on_read (hio_dev_sck_t* sck, const void* buf, hio_iolen_t len, const hio_skad_t* srcaddr)
{
	conn_xtn_t* cx = hio_dev_sck_getxtn(sck);

	if (len <= 0) return 0; /* error or peer closed - let the device wind down */

	if (cx->len + len > HIO_SIZEOF(cx->buf))
	{
		/* a test request never gets near this; treat it as a protocol error */
		hio_dev_sck_halt (sck);
		return 0;
	}

	memcpy (&cx->buf[cx->len], buf, len);
	cx->len += len;

	if (consume_records(sck, cx) <= -1) hio_dev_sck_halt (sck);
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
		reset_request (cx);
		if (g_verbose) fprintf (stderr, "accepted\n");
	}
}

static void on_disconnect (hio_dev_sck_t* sck)
{
}

/* ------------------------------------------------------------------ */

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

	if (check_wire_layout() <= -1) return -1;
	g_verbose = (getenv("HIO_FCGIS_VERBOSE") != HIO_NULL);

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
	if (!svr)
	{
		fprintf (stderr, "unable to make a socket - %s\n", hio_geterrbmsg(g_hio));
		goto oops;
	}

	memset (&b, 0, HIO_SIZEOF(b));
	b.localaddr = addr;
	b.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(svr, &b) <= -1)
	{
		fprintf (stderr, "unable to bind - %s\n", hio_geterrbmsg(g_hio));
		goto oops;
	}

	memset (&l, 0, HIO_SIZEOF(l));
	l.backlogs = 64;
	if (hio_dev_sck_listen(svr, &l) <= -1)
	{
		fprintf (stderr, "unable to listen - %s\n", hio_geterrbmsg(g_hio));
		goto oops;
	}

	if (argc >= 3)
	{
		/* tell the harness the listener is up, so it never races us */
		FILE* fp = fopen(argv[2], "w");
		if (fp)
		{
			fputs ("ready\n", fp);
			fclose (fp);
		}
	}

	hio_loop (g_hio);
	ret = 0;

oops:
	if (g_hio) hio_close (g_hio);
	return ret;
}
