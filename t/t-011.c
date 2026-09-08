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
 *
 * the second group of cases covers what a socket device asks of the loop's
 * shared read buffer, which is the other place the socket layer and the core
 * have to agree on something.
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
static int g_accepted;      /* on_connect saw an ACCEPTED device */
static hio_dev_sck_t* g_qx; /* cleared by on_disconnect - the device is freed with it */
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
	if (sck->state & HIO_DEV_SCK_ACCEPTED) g_accepted = 1;
}

static void cli_on_disconnect (hio_dev_sck_t* sck)
{
	/* reached when the device is killed - which is what the defect did */
	g_disconnected = 1;
	g_done = 1;
	if (sck == g_qx) g_qx = HIO_NULL;
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

/* the read buffer is shared by every device on the loop, so a device that can
 * only work with a whole message at a time has to say so - what will not fit
 * in one read is discarded by the kernel with nothing reported. a stream
 * device has no such need and asks for nothing. */
static void test_device_read_buffer_requirement (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* d;
	hio_oow_t small = HIO_MIN_READ_BUFFER_SIZE, dfl = HIO_DFL_READ_BUFFER_SIZE;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.on_read = cli_on_read;
	mi.on_write = cli_on_write;
	mi.on_connect = cli_on_connect;
	mi.on_disconnect = cli_on_disconnect;

	mi.type = HIO_DEV_SCK_TCP4;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	OK (d && d->dev_rdmin == 0, "a stream socket asks for no particular read buffer size");
	if (d) hio_dev_sck_kill (d);

	mi.type = HIO_DEV_SCK_UDP4;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	OK (d && d->dev_rdmin == HIO_DGRAM_READ_BUFFER_SIZE,
	    "a datagram socket asks for enough to hold the largest datagram");
	if (d) hio_dev_sck_kill (d);

	/* the qx channel is message-oriented too, but its message is a fixed
	 * struct - it must not be made to demand a datagram's worth */
	mi.type = HIO_DEV_SCK_QX;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	OK (d && d->dev_rdmin > 0 && d->dev_rdmin < HIO_MIN_READ_BUFFER_SIZE,
	    "the qx channel asks only for its own message size");
	if (d) hio_dev_sck_kill (d);

	if (hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &small) <= -1) { skip ("cannot shrink the read buffer", 3); return; }

	mi.type = HIO_DEV_SCK_UDP4;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	OK (!d && hio_geterrnum(g_hio) == HIO_ENOCAPA,
	    "a datagram socket refuses to start when the read buffer is too small");
	if (d) hio_dev_sck_kill (d);

	/* ...while the types that do not need it are unaffected, which is what
	 * makes the option usable at all - an http service uses a qx channel */
	mi.type = HIO_DEV_SCK_TCP4;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	OK (d != HIO_NULL, "a stream socket still starts on the smaller buffer");
	if (d) hio_dev_sck_kill (d);

	mi.type = HIO_DEV_SCK_QX;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	OK (d != HIO_NULL, "and so does the qx channel");
	if (d) hio_dev_sck_kill (d);

	hio_setoption (g_hio, HIO_READ_BUFFER_SIZE, &dfl);

	/* and a live device's requirement cannot be pulled out from under it */
	mi.type = HIO_DEV_SCK_UDP4;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (d)
	{
		OK (hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &small) <= -1 && hio_geterrnum(g_hio) == HIO_EPERM,
		    "shrinking below a running device's requirement is refused");
		hio_dev_sck_kill (d);
	}
	else skip ("cannot make a udp device", 1);

	hio_setoption (g_hio, HIO_READ_BUFFER_SIZE, &dfl);
}

/* the accepted-client path takes its socket type from elsewhere - for the qx
 * channel, from a message another thread sent - so it cannot rely on a local
 * listener of that type having already vouched for the read buffer. this hands
 * the qx channel a datagram socket while the buffer is too small for one.
 *
 * it is the only way to reach that path today, since every listenable type in
 * sck_type_map happens to be a stream type. that is what makes the check worth
 * having rather than obvious. */
static int handover_via_qx (hio_dev_sck_t* qx, hio_dev_sck_type_t type)
{
	hio_dev_sck_qxmsg_t msg;
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd <= -1) return -1;

	HIO_MEMSET (&msg, 0, HIO_SIZEOF(msg));
	msg.cmd = HIO_DEV_SCK_QXMSG_NEWCONN;
	msg.scktype = type;
	msg.syshnd = fd;
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:1", &msg.remoteaddr) <= -1) { close(fd); return -1; }

	/* the qx device reads from its own handle; the side channel is the end a
	 * producer writes to */
	if (write(qx->u.qx.side_chan, &msg, HIO_SIZEOF(msg)) != (ssize_t)HIO_SIZEOF(msg)) { close(fd); return -1; }
	return 0;
}

/* returns 1 if a device was accepted, 0 if not, -1 if the setup failed */
static int try_qx_handover (hio_oow_t bufsz, hio_dev_sck_type_t type)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* qx;
	int i, r;

	g_qx = HIO_NULL;

	if (hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &bufsz) <= -1) return -1;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.type = HIO_DEV_SCK_QX;
	mi.on_read = cli_on_read;
	mi.on_write = cli_on_write;
	mi.on_connect = cli_on_connect;
	mi.on_disconnect = cli_on_disconnect;

	/* a fresh channel each time - a refused handover halts the one that
	 * carried it, so it cannot be reused for the next attempt */
	qx = hio_dev_sck_make(g_hio, 0, &mi);
	if (!qx) return -1;
	g_qx = qx;

	g_accepted = 0;
	if (handover_via_qx(qx, type) <= -1) r = -1;
	else
	{
		for (i = 0; i < 20 && !g_accepted; i++) hio_exec(g_hio);
		r = g_accepted;
	}

	/* a refused handover halts this channel, and the loop above will already
	 * have reaped it - so the pointer cannot be dereferenced to find out.
	 * on_disconnect clears g_qx, which is the only safe way to ask. */
	if (g_qx) { hio_dev_sck_kill (g_qx); g_qx = HIO_NULL; }
	hio_exec (g_hio);
	return r;
}

static void test_accepted_device_read_buffer_requirement (void)
{
	hio_oow_t dfl = HIO_DFL_READ_BUFFER_SIZE;
	int refused, allowed;

	refused = try_qx_handover(HIO_MIN_READ_BUFFER_SIZE, HIO_DEV_SCK_UDP4);
	allowed = try_qx_handover(HIO_DFL_READ_BUFFER_SIZE, HIO_DEV_SCK_UDP4);

	if (refused <= -1 || allowed <= -1) skip ("qx handover setup failed", 2);
	else
	{
		OK (refused == 0, "a datagram socket handed over the qx channel is refused when the buffer is too small");
		/* the control: the same handover with room for it must succeed, or the
		 * case above proves nothing about the buffer check */
		OK (allowed == 1, "and the same handover succeeds once the buffer is large enough");
	}

	hio_setoption (g_hio, HIO_READ_BUFFER_SIZE, &dfl);
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
	test_device_read_buffer_requirement ();
	test_accepted_device_read_buffer_requirement ();

	hio_close (g_hio);
	return exit_status();
}
