/*
 * tls transport tests.
 *
 * an hio tcp server and an hio tcp client living in the same loop, both
 * speaking ssl. this exercises dev_sck_read_stream()/dev_sck_write_stream()
 * on the openssl branch rather than the plain recv()/send() one, which no
 * other test in this directory reaches.
 *
 * the payloads deliberately span many tls records (a record tops out at
 * 16kb), so one logical hio_dev_sck_write() turns into repeated SSL_write()
 * calls that complete partially, and the reading side takes many SSL_read()
 * calls to drain. the byte pattern is position-dependent so reordering,
 * duplication or truncation all show up as a mismatch rather than as a
 * short count.
 *
 * the certificate is generated at run time with the openssl command. if that
 * is not available the whole file skips rather than fails - the library is
 * still perfectly usable without the openssl tool installed.
 */

#include <hio-sck.h>
#include <hio-prv.h>
#include "tap.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CERTFILE "t009-cert.pem"
#define KEYFILE  "t009-key.pem"

#define BULK_LEN    (128 * 1024)
#define BP_LEN      (64 * 1024)
#define BP_CHUNK    (8 * 1024)   /* stop reading after this much, then resume */
#define DEADLINE_S  20

static hio_t* g_hio = HIO_NULL;
static hio_dev_sck_t* g_srv = HIO_NULL;   /* listener */
static hio_dev_sck_t* g_acc = HIO_NULL;   /* accepted peer of the client */
static hio_dev_sck_t* g_cli = HIO_NULL;   /* connecting client */

static int g_connected;      /* client reached the CONNECTED state */
static int g_accepted;       /* listener produced an ACCEPTED device */
static int g_timeout;

static hio_uint8_t* g_out;   /* what the client sends */
static hio_oow_t g_outlen;

static hio_uint8_t* g_srv_in;   /* what the server collected */
static hio_oow_t g_srv_inlen;
static hio_uint8_t* g_cli_in;   /* what the client collected back */
static hio_oow_t g_cli_inlen;

static int g_srv_bad;        /* server saw a byte it did not expect */
static int g_echo_started;
static int g_done;

static int g_backpressure;   /* toggle reading off in the server on_read */
static int g_bp_paused;      /* reading is currently off */
static int g_bp_pauses;      /* how many times we paused - proves it happened */

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_ERROR | HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

/* position-dependent so any shuffling is visible */
static hio_uint8_t pat (hio_oow_t i)
{
	return (hio_uint8_t)((i * 7 + (i >> 8) * 31 + 13) & 0xff);
}

static void fill_pattern (hio_uint8_t* buf, hio_oow_t len)
{
	hio_oow_t i;
	for (i = 0; i < len; i++) buf[i] = pat(i);
}

static int matches_pattern (const hio_uint8_t* buf, hio_oow_t len)
{
	hio_oow_t i;
	for (i = 0; i < len; i++)
	{
		if (buf[i] != pat(i)) return 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ */

static void on_deadline (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	g_timeout = 1;
}

static hio_tmridx_t g_deadline_idx = HIO_TMRIDX_INVALID;

static void arm_deadline (void)
{
	hio_tmrjob_t j;
	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.sec += DEADLINE_S;
	j.handler = on_deadline;
	/* idxptr keeps g_deadline_idx correct as the heap reshuffles, so the
	 * job can still be cancelled once the transfer finishes. */
	j.idxptr = &g_deadline_idx;
	g_deadline_idx = hio_instmrjob(g_hio, &j);
}

static void disarm_deadline (void)
{
	/* a pending timer keeps hio_exec() sleeping until it expires, which
	 * would make teardown take the full deadline rather than no time. */
	if (g_deadline_idx != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (g_hio, g_deadline_idx);
		g_deadline_idx = HIO_TMRIDX_INVALID;
	}
}

/* resume reading on the server side after a short pause */
static void on_resume (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	hio_dev_sck_t* sck = (hio_dev_sck_t*)job->ctx;
	g_bp_paused = 0;
	hio_dev_sck_read (sck, 1);
}

static void pause_reading (hio_dev_sck_t* sck)
{
	hio_tmrjob_t j;

	g_bp_paused = 1;
	g_bp_pauses++;
	hio_dev_sck_read (sck, 0);

	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.nsec += 20000000;  /* 20ms */
	if (j.when.nsec >= HIO_NSECS_PER_SEC) { j.when.sec++; j.when.nsec -= HIO_NSECS_PER_SEC; }
	j.handler = on_resume;
	j.ctx = sck;
	hio_instmrjob (g_hio, &j);
}

/* ------------------------------------------------------------------ */
/* server side */

static int srv_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_oow_t i;
	const hio_uint8_t* p = (const hio_uint8_t*)data;

	if (dlen <= 0) return 0;   /* eof or error */

	for (i = 0; i < (hio_oow_t)dlen; i++)
	{
		if (g_srv_inlen >= g_outlen) { g_srv_bad = 1; break; }
		if (p[i] != pat(g_srv_inlen)) g_srv_bad = 1;
		g_srv_in[g_srv_inlen++] = p[i];
	}

	if (g_backpressure && !g_bp_paused && g_srv_inlen < g_outlen &&
	    (g_srv_inlen / BP_CHUNK) > (hio_oow_t)g_bp_pauses)
	{
		pause_reading (sck);
	}

	if (g_srv_inlen >= g_outlen && !g_echo_started)
	{
		/* echo the whole thing back so the opposite direction is covered too */
		g_echo_started = 1;
		if (hio_dev_sck_write(sck, g_srv_in, g_outlen, HIO_NULL, HIO_NULL) <= -1) return -1;
	}

	return 0;   /* deliberately non-greedy - the case the pending path exists for */
}

static int srv_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void srv_on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_ACCEPTED)
	{
		g_accepted = 1;
		g_acc = sck;
	}
}

static void srv_on_disconnect (hio_dev_sck_t* sck)
{
	if (sck == g_acc) g_acc = HIO_NULL;
	else if (sck == g_srv) g_srv = HIO_NULL;
}

/* ------------------------------------------------------------------ */
/* client side */

static int cli_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	hio_oow_t i;
	const hio_uint8_t* p = (const hio_uint8_t*)data;

	if (dlen <= 0) return 0;

	for (i = 0; i < (hio_oow_t)dlen; i++)
	{
		if (g_cli_inlen >= g_outlen) break;
		g_cli_in[g_cli_inlen++] = p[i];
	}

	if (g_cli_inlen >= g_outlen) g_done = 1;
	return 0;
}

static int cli_on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void cli_on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_CONNECTED)
	{
		g_connected = 1;
		if (hio_dev_sck_write(sck, g_out, g_outlen, HIO_NULL, HIO_NULL) <= -1)
			hio_dev_sck_halt (sck);
	}
}

static void cli_on_disconnect (hio_dev_sck_t* sck)
{
	if (sck == g_cli) g_cli = HIO_NULL;
}

/* ------------------------------------------------------------------ */

static void reset_state (hio_oow_t len)
{
	g_connected = g_accepted = g_timeout = 0;
	g_srv_inlen = g_cli_inlen = 0;
	g_srv_bad = g_echo_started = g_done = 0;
	g_bp_paused = g_bp_pauses = 0;
	g_outlen = len;
	fill_pattern (g_out, len);
	HIO_MEMSET (g_srv_in, 0, len);
	HIO_MEMSET (g_cli_in, 0, len);
}

/* build the ssl listener and return its actual bound address */
static int start_server (hio_skad_t* boundaddr)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.type = HIO_DEV_SCK_TCP4;
	mi.on_read = srv_on_read;
	mi.on_write = srv_on_write;
	mi.on_connect = srv_on_connect;
	mi.on_disconnect = srv_on_disconnect;

	g_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_srv) return -1;

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	/* port 0 - the kernel picks a free one, so concurrent runs never collide */
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) return -1;
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_SSL;
	bi.ssl_certfile = CERTFILE;
	bi.ssl_keyfile = KEYFILE;
	if (hio_dev_sck_bind(g_srv, &bi) <= -1) return -1;

	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 8;
	/* how long an accepted socket may take to finish its ssl handshake.
	 * a negative value means no limit, which is what a test wants - the
	 * run is already bounded by its own deadline timer, and a hard limit
	 * here would only add a way to flake on a loaded machine. zero is a
	 * zero-length window and would halt the peer the instant it is
	 * accepted, before any ClientHello could be read. */
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	if (hio_dev_sck_listen(g_srv, &li) <= -1) return -1;

	/* the bind request asked for port 0. dev->localaddr keeps the requested
	 * address, so ask the kernel which port it actually handed out. */
	if (hio_dev_sck_getsockaddr(g_srv, boundaddr) <= -1) return -1;
	return 0;
}

static int start_client (const hio_skad_t* peer)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_connect_t ci;

	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.type = HIO_DEV_SCK_TCP4;
	mi.on_read = cli_on_read;
	mi.on_write = cli_on_write;
	mi.on_connect = cli_on_connect;
	mi.on_disconnect = cli_on_disconnect;

	g_cli = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_cli) return -1;

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.remoteaddr = *peer;
	ci.options = HIO_DEV_SCK_CONNECT_SSL;
	HIO_INIT_NTIME (&ci.connect_tmout, 5, 0);
	if (hio_dev_sck_connect(g_cli, &ci) <= -1) return -1;

	return 0;
}

static void run_until_done (void)
{
	arm_deadline ();
	while (!g_done && !g_timeout)
	{
		if (hio_exec(g_hio) <= -1) break;
	}
	disarm_deadline ();
}

static void teardown (void)
{
	if (g_cli) hio_dev_sck_halt (g_cli);
	if (g_acc) hio_dev_sck_halt (g_acc);
	if (g_srv) hio_dev_sck_halt (g_srv);
	/* let the halted devices be reaped so the loop is quiescent */
	hio_exec (g_hio);
	hio_exec (g_hio);
	g_cli = g_acc = g_srv = HIO_NULL;
}

/* ------------------------------------------------------------------ */

/* a full-size transfer in both directions over tls. this is the plain
 * regression net for the openssl read and write paths. */
static void test_bulk (void)
{
	hio_skad_t peer;

	reset_state (BULK_LEN);
	g_backpressure = 0;

	OK (start_server(&peer) == 0, "tls listener binds and listens");
	OK (start_client(&peer) == 0, "tls client connect initiated");

	run_until_done ();

	OK (!g_timeout, "tls bulk transfer completed before the deadline");
	OK (g_connected, "client reached the CONNECTED state (handshake done)");
	OK (g_accepted, "listener produced an ACCEPTED device (handshake done)");
	OK (g_srv_inlen == (hio_oow_t)BULK_LEN, "server received the whole payload");
	OK (!g_srv_bad, "server payload arrived in order and unmodified");
	OK (g_cli_inlen == (hio_oow_t)BULK_LEN, "client received the whole echo");
	OK (matches_pattern(g_cli_in, BULK_LEN), "echoed payload is byte-identical");
	OK (g_hio->nrdpendings == 0, "no device left flagged as read-pending");

	teardown ();
}

/* the receiving side repeatedly turns input off mid-transfer. with tls this
 * is the case where SSL_write() on the peer can report SSL_ERROR_WANT_READ
 * while HIO_DEV_CAP_IN_DISABLED is set on this end. */
static void test_backpressure (void)
{
	hio_skad_t peer;

	reset_state (BP_LEN);
	g_backpressure = 1;

	OK (start_server(&peer) == 0, "tls listener binds and listens (backpressure)");
	OK (start_client(&peer) == 0, "tls client connect initiated (backpressure)");

	run_until_done ();

	OK (!g_timeout, "transfer completed despite repeated read suspension");
	OK (g_bp_pauses > 0, "input was actually suspended at least once");
	OK (g_srv_inlen == (hio_oow_t)BP_LEN, "server received the whole payload");
	OK (!g_srv_bad, "server payload arrived in order and unmodified");
	OK (matches_pattern(g_cli_in, BP_LEN), "echoed payload is byte-identical");
	OK (g_hio->nrdpendings == 0, "no device left flagged as read-pending");

	teardown ();
}

/* ------------------------------------------------------------------ */

/* a library built without openssl rejects HIO_DEV_SCK_BIND_SSL with
 * HIO_ENOIMPL. there is nothing to test in that case. */
static int ssl_supported (void)
{
	hio_skad_t addr;
	int supported;

	if (start_server(&addr) == 0) supported = 1;
	else supported = (hio_geterrnum(g_hio) != HIO_ENOIMPL);

	teardown ();
	return supported;
}

static int make_cert (void)
{
	int rc = system("openssl req -x509 -newkey rsa:2048 -nodes"
	                " -keyout " KEYFILE " -out " CERTFILE
	                " -days 3650 -subj /CN=localhost >/dev/null 2>&1");
	return (rc == 0 && access(CERTFILE, R_OK) == 0 && access(KEYFILE, R_OK) == 0);
}

int main (void)
{
	hio_errinf_t errinf;
	int have_ssl;

	/* skip_all() has to come before no_plan() - tap.h refuses a second plan -
	 * so both preconditions are settled before any test output is produced. */

	g_out = (hio_uint8_t*)malloc(BULK_LEN);
	g_srv_in = (hio_uint8_t*)malloc(BULK_LEN);
	g_cli_in = (hio_uint8_t*)malloc(BULK_LEN);
	if (!g_out || !g_srv_in || !g_cli_in)
	{
		no_plan ();
		bail_out ("out of memory");
		return -1;
	}

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		no_plan ();
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	if (!make_cert())
	{
		skip_all ("openssl command not usable - cannot generate a test certificate");
		hio_close (g_hio);
		return exit_status();
	}

	have_ssl = ssl_supported();
	if (!have_ssl)
	{
		skip_all ("this build of hio has no ssl support");
		unlink (CERTFILE);
		unlink (KEYFILE);
		hio_close (g_hio);
		return exit_status();
	}

	no_plan ();

	test_bulk ();
	test_backpressure ();

	hio_close (g_hio);

	free (g_out); free (g_srv_in); free (g_cli_in);
	unlink (CERTFILE);
	unlink (KEYFILE);

	return exit_status();
}
