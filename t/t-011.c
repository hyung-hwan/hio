/*
 * socket connect readiness.
 *
 * a non-blocking connect finishes when the multiplexer reports the socket
 * writable. the case this file exists for is the one where it reports the
 * socket writable *and* readable in the same wakeup - which happens whenever
 * the peer accepts, consumes whatever was queued while this end was still
 * connecting, and answers, all before the loop gets back to the multiplexer.
 *
 * that is ordinary on loopback and gets more likely the slower the machine
 * is. the race is made deterministic here: the peer is accepted and written
 * to before the loop is ever run, so the first wakeup is guaranteed to carry
 * both bits.
 */

#include <hio-sck.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PEER_GREETING "hello-from-the-peer"

static hio_t* g_hio = HIO_NULL;
static int g_connected;     /* on_connect saw HIO_DEV_SCK_CONNECTED */
static int g_disconnected;
static int g_read_len;      /* octets delivered to on_read */
static hio_bch_t g_read_buf[128];
static int g_done;
static int g_timeout;
static hio_tmridx_t g_deadline = HIO_TMRIDX_INVALID;

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_ERROR | HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static void on_deadline (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	g_timeout = 1;
}

static int cli_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	if (dlen > 0)
	{
		if (dlen > (hio_iolen_t)HIO_SIZEOF(g_read_buf)) dlen = HIO_SIZEOF(g_read_buf);
		HIO_MEMCPY (g_read_buf, data, dlen);
		g_read_len = (int)dlen;
		g_done = 1;
	}
	return 0;
}

static int cli_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void cli_on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_CONNECTED) g_connected = 1;
}

static void cli_on_disconnect (hio_dev_sck_t* sck)
{
	/* reached when the device is killed - which is what the defect did */
	g_disconnected = 1;
	g_done = 1;
}

/* a plain listener, so nothing about the peer depends on the code under test */
static int make_listener (unsigned short* port)
{
	int fd;
	struct sockaddr_in sa;
	socklen_t slen;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd <= -1) return -1;

	HIO_MEMSET (&sa, 0, HIO_SIZEOF(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = 0; /* let the kernel pick, so concurrent runs never collide */
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) <= -1 || listen(fd, 4) <= -1)
	{
		close (fd);
		return -1;
	}

	slen = sizeof(sa);
	if (getsockname(fd, (struct sockaddr*)&sa, &slen) <= -1)
	{
		close (fd);
		return -1;
	}
	*port = ntohs(sa.sin_port);
	return fd;
}

static void test_connect_with_data_already_waiting (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_connect_t ci;
	hio_dev_sck_t* cli;
	hio_bch_t addrbuf[64];
	hio_tmrjob_t j;
	unsigned short port;
	int lfd, afd = -1, i;

	g_connected = g_disconnected = g_read_len = g_done = g_timeout = 0;

	lfd = make_listener(&port);
	if (lfd <= -1) { skip ("cannot create a listener", 3); return; }

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.type = HIO_DEV_SCK_TCP4;
	mi.on_read = cli_on_read;
	mi.on_write = cli_on_write;
	mi.on_connect = cli_on_connect;
	mi.on_disconnect = cli_on_disconnect;

	cli = hio_dev_sck_make(g_hio, 0, &mi);
	if (!cli) { skip ("cannot make a socket device", 3); close(lfd); return; }

	snprintf (addrbuf, HIO_COUNTOF(addrbuf), "127.0.0.1:%u", (unsigned int)port);
	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	if (hio_bcstrtoskad(g_hio, addrbuf, &ci.remoteaddr) <= -1) { skip ("bad address", 3); close(lfd); return; }
	HIO_INIT_NTIME (&ci.connect_tmout, 5, 0);
	if (hio_dev_sck_connect(cli, &ci) <= -1) { skip ("connect failed to start", 3); close(lfd); return; }

	/* the loop has not run yet. accept the pending connection and answer it
	 * right now, so that by the time the multiplexer is first consulted the
	 * client socket is both writable (the connect completed) and readable
	 * (the greeting is waiting). */
	for (i = 0; i < 200 && afd <= -1; i++)
	{
		afd = accept(lfd, HIO_NULL, HIO_NULL);
		if (afd <= -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
	}
	if (afd <= -1) { skip ("peer did not accept", 3); close(lfd); return; }
	if (send(afd, PEER_GREETING, sizeof(PEER_GREETING) - 1, 0) <= -1) { skip ("peer write failed", 3); close(afd); close(lfd); return; }

	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.sec += 5;
	j.handler = on_deadline;
	j.idxptr = &g_deadline;
	g_deadline = hio_instmrjob(g_hio, &j);

	while (!g_done && !g_timeout)
	{
		if (hio_exec(g_hio) <= -1) break;
	}
	if (g_deadline != HIO_TMRIDX_INVALID) { hio_deltmrjob (g_hio, g_deadline); g_deadline = HIO_TMRIDX_INVALID; }

	OK (!g_disconnected, "a connect completing alongside pending input is not treated as an error");
	OK (g_connected, "on_connect reports the socket as connected");
	OK (g_read_len == (int)sizeof(PEER_GREETING) - 1 &&
	    HIO_MEMCMP(g_read_buf, PEER_GREETING, sizeof(PEER_GREETING) - 1) == 0,
	    "the data that arrived during the connect is delivered intact");

	hio_dev_sck_halt (cli);
	hio_exec (g_hio);
	hio_exec (g_hio);
	close (afd);
	close (lfd);
}

int main (void)
{
	hio_errinf_t errinf;

	no_plan ();

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	test_connect_with_data_already_waiting ();

	hio_close (g_hio);
	return exit_status();
}
