/*
 * the device types that had no test of their own: pipe, thread, process and
 * pty, plus the dns client.
 *
 * these came out of bin/t01.c and bin/t05.c, demos that exercised the library by
 * printing what happened and leaving a human to judge it. everything else it
 * covered - sockets, tls, the http tasks - has a test of its own by now. these
 * four did not, so this is what was actually worth keeping from it.
 *
 * a demo can sleep and loop forever; a test cannot. so nothing here is a
 * translation of that code - it is the same API driven to a definite end, with
 * a deadline on every wait so a hang is a failure rather than a stuck suite.
 */

#include <hio-pipe.h>
#include <hio-thr.h>
#include <hio-pro.h>
#include <hio-pty.h>
#include <hio-dns.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <unistd.h>

#define PAYLOAD "device-payload"

static hio_t* g_hio = HIO_NULL;
static int g_timeout;
static hio_tmridx_t g_tmr = HIO_TMRIDX_INVALID;

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static void on_deadline (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	g_timeout = 1;
}

/* run the loop until the flag goes true or the deadline passes. every wait in
 * this file goes through here, so nothing can hang the suite. */
static void run_until (int* flag, int secs)
{
	hio_tmrjob_t j;

	g_timeout = 0;
	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.sec += secs;
	j.handler = on_deadline;
	j.idxptr = &g_tmr;
	g_tmr = hio_instmrjob(g_hio, &j);

	while (!*flag && !g_timeout)
	{
		if (hio_exec(g_hio) <= -1) break;
	}

	if (g_tmr != HIO_TMRIDX_INVALID) { hio_deltmrjob (g_hio, g_tmr); g_tmr = HIO_TMRIDX_INVALID; }
}

/* ------------------------------------------------------------------ */
/* pipe                                                                */

/* a pipe device is a write end and a read end of one pipe, presented as a
 * single device. what goes in comes back out to on_read(). */

static hio_dev_pipe_t* g_pipe;
static int g_pipe_read, g_pipe_wrote, g_pipe_closed_ends;
static hio_bch_t g_pipe_buf[64];
static int g_pipe_len;

static int pipe_on_read (hio_dev_pipe_t* dev, const void* data, hio_iolen_t dlen)
{
	if (dlen <= 0) return 0;
	if (dlen > (hio_iolen_t)HIO_SIZEOF(g_pipe_buf) - 1) dlen = HIO_SIZEOF(g_pipe_buf) - 1;
	HIO_MEMCPY (g_pipe_buf, data, dlen);
	g_pipe_buf[dlen] = '\0';
	g_pipe_len = (int)dlen;
	g_pipe_read = 1;
	return 0;
}

static int pipe_on_write (hio_dev_pipe_t* dev, hio_iolen_t wrlen, void* wrctx)
{
	g_pipe_wrote = 1;
	return 0;
}

static void pipe_on_close (hio_dev_pipe_t* dev, hio_dev_pipe_sid_t sid)
{
	/* fires once per end and once for the master */
	g_pipe_closed_ends++;
	if (sid == HIO_DEV_PIPE_MASTER) g_pipe = HIO_NULL;
}

static void test_pipe (void)
{
	hio_dev_pipe_make_t mi;

	g_pipe_read = g_pipe_wrote = g_pipe_closed_ends = g_pipe_len = 0;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.on_read = pipe_on_read;
	mi.on_write = pipe_on_write;
	mi.on_close = pipe_on_close;

	g_pipe = hio_dev_pipe_make(g_hio, 0, &mi);
	if (!g_pipe) { FAIL ("a pipe device can be made"); skip ("no pipe device", 3); return; }
	PASS ("a pipe device can be made");

	if (hio_dev_pipe_write(g_pipe, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL) <= -1)
	{
		FAIL ("what is written to a pipe comes back out of it");
		FAIL ("and the write is acknowledged");
	}
	else
	{
		run_until (&g_pipe_read, 3);
		OK (g_pipe_read && g_pipe_len == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
		    HIO_MEMCMP(g_pipe_buf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
		    "what is written to a pipe comes back out of it");
		OK (g_pipe_wrote, "and the write is acknowledged");
	}

	if (g_pipe) hio_dev_pipe_halt (g_pipe);
	hio_exec (g_hio);
	hio_exec (g_hio);

	/* a pipe is a master over two slaves, so tearing it down closes three
	 * things, not one - which is the part of the slave model worth pinning */
	OK (g_pipe_closed_ends >= 2, "and closing it closes both ends");
}

/* ------------------------------------------------------------------ */
/* thread                                                              */

/* the worker runs in its own thread with a pipe to the loop at each side. it
 * must not outlive what it is asked to do, so it echoes once and returns -
 * writing nothing more and looping over nothing. */

static hio_dev_thr_t* g_thr;
static int g_thr_read, g_thr_closed;
static hio_bch_t g_thr_buf[64];
static int g_thr_len;

static void thr_worker (hio_t* hio, hio_dev_thr_iopair_t* iop, void* ctx)
{
	hio_bch_t buf[64];
	ssize_t n;

	n = read(iop->rfd, buf, HIO_COUNTOF(buf));
	if (n > 0)
	{
		ssize_t k = 0;
		while (k < n)
		{
			ssize_t w = write(iop->wfd, &buf[k], n - k);
			if (w <= 0) break;
			k += w;
		}
	}
	/* returning ends the thread, which closes its side and gives the loop the
	 * EOF that on_close() below is waiting for */
}

static int thr_on_read (hio_dev_thr_t* dev, const void* data, hio_iolen_t dlen)
{
	if (dlen <= 0) return 0;
	if (dlen > (hio_iolen_t)HIO_SIZEOF(g_thr_buf) - 1) dlen = HIO_SIZEOF(g_thr_buf) - 1;
	HIO_MEMCPY (g_thr_buf, data, dlen);
	g_thr_buf[dlen] = '\0';
	g_thr_len = (int)dlen;
	g_thr_read = 1;
	return 0;
}

static int thr_on_write (hio_dev_thr_t* dev, hio_iolen_t wrlen, void* wrctx)
{
	return 0;
}

static void thr_on_close (hio_dev_thr_t* dev, hio_dev_thr_sid_t sid)
{
	if (sid == HIO_DEV_THR_MASTER) { g_thr_closed = 1; g_thr = HIO_NULL; }
}

static void test_thr (void)
{
	hio_dev_thr_make_t mi;

	g_thr_read = g_thr_closed = g_thr_len = 0;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.thr_func = thr_worker;
	mi.thr_ctx = HIO_NULL;
	mi.on_read = thr_on_read;
	mi.on_write = thr_on_write;
	mi.on_close = thr_on_close;

	g_thr = hio_dev_thr_make(g_hio, 0, &mi);
	if (!g_thr) { FAIL ("a thread device can be made"); skip ("no thread device", 2); return; }
	PASS ("a thread device can be made");

	if (hio_dev_thr_write(g_thr, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL) <= -1)
	{
		FAIL ("data crosses to the worker thread and back");
	}
	else
	{
		run_until (&g_thr_read, 5);
		OK (g_thr_read && g_thr_len == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
		    HIO_MEMCMP(g_thr_buf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
		    "data crosses to the worker thread and back");
	}

	/* the worker has returned by now, so the device should come down on its
	 * own once told to. waiting for on_close rather than assuming it is what
	 * catches a thread that never gets joined. */
	if (g_thr) hio_dev_thr_halt (g_thr);
	run_until (&g_thr_closed, 5);
	OK (g_thr_closed, "and the device closes once the worker returns");

	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */
/* process                                                             */

static hio_dev_pro_t* g_pro;
static int g_pro_read, g_pro_closed_slaves;
static hio_bch_t g_pro_buf[128];
static int g_pro_len;

static int pro_on_read (hio_dev_pro_t* dev, hio_dev_pro_sid_t sid, const void* data, hio_iolen_t dlen)
{
	if (sid != HIO_DEV_PRO_OUT || dlen <= 0) return 0;
	if (dlen > (hio_iolen_t)HIO_SIZEOF(g_pro_buf) - 1) dlen = HIO_SIZEOF(g_pro_buf) - 1;
	HIO_MEMCPY (g_pro_buf, data, dlen);
	g_pro_buf[dlen] = '\0';
	g_pro_len = (int)dlen;
	g_pro_read = 1;
	return 0;
}

static int pro_on_write (hio_dev_pro_t* dev, hio_iolen_t wrlen, void* wrctx)
{
	return 0;
}

static void pro_on_close (hio_dev_pro_t* dev, hio_dev_pro_sid_t sid)
{
	if (sid == HIO_DEV_PRO_MASTER) g_pro = HIO_NULL;
	else g_pro_closed_slaves++;
}

static void test_pro (void)
{
	hio_dev_pro_make_t mi;

	g_pro_read = g_pro_closed_slaves = g_pro_len = 0;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	/* 'cat' echoes stdin to stdout and exits on EOF - the smallest child that
	 * exercises writing to one slave and reading from another */
	mi.flags = HIO_DEV_PRO_READOUT | HIO_DEV_PRO_WRITEIN | HIO_DEV_PRO_ERRTONUL | HIO_DEV_PRO_SHELL;
	mi.cmd = "cat";
	mi.on_read = pro_on_read;
	mi.on_write = pro_on_write;
	mi.on_close = pro_on_close;

	g_pro = hio_dev_pro_make(g_hio, 0, &mi);
	if (!g_pro) { FAIL ("a process device can be made"); skip ("no process device", 2); return; }
	PASS ("a process device can be made");

	if (hio_dev_pro_write(g_pro, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL) <= -1)
	{
		FAIL ("what is written to the child comes back from its output");
		FAIL ("and closing the input ends the child");
	}
	else
	{
		run_until (&g_pro_read, 5);
		OK (g_pro_read && g_pro_len == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
		    HIO_MEMCMP(g_pro_buf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
		    "what is written to the child comes back from its output");

		/* a zero-length write closes the child's stdin, which is what makes
		 * cat exit - so this also checks the device notices a child leaving */
		g_pro_closed_slaves = 0;
		hio_dev_pro_write (g_pro, HIO_NULL, 0, HIO_NULL);
		run_until (&g_pro_closed_slaves, 5);
		OK (g_pro_closed_slaves > 0, "and closing the input ends the child");
	}

	if (g_pro) hio_dev_pro_halt (g_pro);
	hio_exec (g_hio);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */
/* pty                                                                 */

/* a pty device runs a child on a pseudo-terminal rather than on pipes. the
 * difference that matters to a test is that a terminal echoes: what is written
 * to it comes back without the child doing anything, which is why this reads
 * back its own input rather than a command's output. */

static hio_dev_pty_t* g_pty;
static int g_pty_read, g_pty_closed;
static hio_bch_t g_pty_buf[256];
static int g_pty_len;

static int pty_on_read (hio_dev_pty_t* dev, const void* data, hio_iolen_t dlen)
{
	if (dlen <= 0) return 0;
	if (g_pty_len + (int)dlen < (int)HIO_SIZEOF(g_pty_buf) - 1)
	{
		HIO_MEMCPY (&g_pty_buf[g_pty_len], data, dlen);
		g_pty_len += (int)dlen;
		g_pty_buf[g_pty_len] = '\0';
	}
	g_pty_read = 1;
	return 0;
}

static int pty_on_write (hio_dev_pty_t* dev, hio_iolen_t wrlen, void* wrctx)
{
	return 0;
}

static void pty_on_close (hio_dev_pty_t* dev)
{
	g_pty_closed = 1;
	g_pty = HIO_NULL;
}

static void test_pty (void)
{
	hio_dev_pty_make_t mi;

	g_pty_read = g_pty_closed = g_pty_len = 0;
	g_pty_buf[0] = '\0';

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	/* 'cat' on a terminal: the line is echoed by the tty discipline as it is
	 * written, and echoed again by cat once the newline completes it */
	mi.flags = HIO_DEV_PTY_SHELL;
	mi.cmd = "cat";
	mi.on_read = pty_on_read;
	mi.on_write = pty_on_write;
	mi.on_close = pty_on_close;

	g_pty = hio_dev_pty_make(g_hio, 0, &mi);
	if (!g_pty)
	{
		/* no pty available - a container without /dev/pts, say. the system's
		 * answer, not a library fault. */
		skip ("cannot allocate a pty", 2);
		return;
	}
	PASS ("a pty device can be made");

	if (hio_dev_pty_write(g_pty, PAYLOAD "\n", HIO_SIZEOF(PAYLOAD), HIO_NULL) <= -1)
	{
		FAIL ("and what is written to the terminal comes back from it");
	}
	else
	{
		run_until (&g_pty_read, 5);
		OK (g_pty_read && strstr(g_pty_buf, PAYLOAD) != HIO_NULL,
		    "and what is written to the terminal comes back from it");
	}

	if (g_pty) hio_dev_pty_halt (g_pty);
	hio_exec (g_hio);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */
/* dns client                                                          */

/* no nameserver is assumed here, so what is tested is the half that needs
 * none: that a lookup can be started, that an unanswerable one is reported
 * through the callback rather than hanging or being forgotten, and that the
 * service can be stopped with that request still outstanding.
 *
 * the last of those is the interesting one. there is no per-request cancel in
 * the dnc api, so stopping the service is the only way to end a lookup, and
 * anything built on it - the http proxy, when it grows name support - has to
 * survive that happening while a request is in flight. */

static int g_dns_called;
static hio_errnum_t g_dns_status;

static void on_resolved (hio_svc_dnc_t* dnc, hio_dns_msg_t* reqmsg, hio_errnum_t status, const void* data, hio_oow_t len)
{
	g_dns_called++;
	g_dns_status = status;
}

static void test_dnc (void)
{
	hio_svc_dnc_t* dnc;
	hio_skad_t servaddr;
	hio_ntime_t send_tmout, reply_tmout;

	g_dns_called = 0;
	g_dns_status = HIO_ENOERR;

	/* a loopback port with nothing on it. the request goes out and is never
	 * answered, which is exactly the path worth covering. */
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:9", &servaddr) <= -1) { skip ("bad address", 4); return; }
	HIO_INIT_NTIME (&send_tmout, 1, 0);
	HIO_INIT_NTIME (&reply_tmout, 1, 0);

	dnc = hio_svc_dnc_start(g_hio, &servaddr, HIO_NULL, &send_tmout, &reply_tmout, 1);
	if (!dnc) { FAIL ("a dns client can be started"); skip ("no dns client", 3); return; }
	PASS ("a dns client can be started");

	OK (hio_svc_dnc_resolve(dnc, "test.invalid", HIO_DNS_RRT_A,
	                        HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_resolved, 0) != HIO_NULL,
	    "and a lookup can be started against it");

	/* one try, one second - so this must be reported, not left pending */
	run_until (&g_dns_called, 6);
	/* HIO_ETMOUT specifically, not merely "some failure". a udp send to a
	 * closed port could plausibly come back as a refusal instead, and the two
	 * arrive by different paths in the client - naming the one expected is
	 * what keeps this pinned to the timeout path. */
	OK (g_dns_called == 1 && g_dns_status == HIO_ETMOUT,
	    "and an unanswered lookup times out rather than being forgotten");

	/* and stopping the service with a request outstanding must not leave the
	 * loop holding anything - which the leak checker is what really proves */
	g_dns_called = 0;
	hio_svc_dnc_resolve (dnc, "test2.invalid", HIO_DNS_RRT_A,
	                     HIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_resolved, 0);
	hio_svc_dnc_stop (dnc);
	hio_exec (g_hio);
	PASS ("and the service can be stopped with a lookup still in flight");
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		no_plan ();
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	no_plan ();

	test_pipe ();
	test_thr ();
	test_pro ();
	test_pty ();
	test_dnc ();

	hio_close (g_hio);
	return exit_status();
}
