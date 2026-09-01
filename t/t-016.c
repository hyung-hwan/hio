/*
 * the mariadb device and the connection service.
 *
 * from bin/t04.c, which took a host, port, user, password and database on the
 * command line and printed rows - a manual tool, and one nobody runs.
 *
 * a database server cannot be assumed, so what is tested here is what needs
 * none: that a device can be made, that connecting to something that is not a
 * server fails through the callback rather than hanging or being forgotten, and
 * that string escaping - which is pure, and the part a caller can get wrong in
 * a way that matters - does what it claims.
 *
 * where a server is available, point the environment at it and the query path
 * is exercised too:
 *
 *   HIO_TEST_MARIADB_HOST  HIO_TEST_MARIADB_PORT  HIO_TEST_MARIADB_USER
 *   HIO_TEST_MARIADB_PASS  HIO_TEST_MARIADB_DB
 *
 * the whole file skips where the build has no mariadb support, which is how a
 * caller discovers it too.
 */

#include <hio.h>
#include "tap.h"

#include <string.h>
#include <stdlib.h>

#if defined(HIO_ENABLE_MARIADB)

#include <hio-mar.h>
#include <hio-prv.h>

static hio_t* g_hio = HIO_NULL;
static int g_timeout;
static hio_tmridx_t g_tmr = HIO_TMRIDX_INVALID;

/* a port on the loopback with nothing listening. connecting must fail, and
 * fail through the callback rather than by hanging. */
#define DEAD_PORT 9

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static void on_deadline (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	g_timeout = 1;
}

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

static int g_connected, g_disconnected;
static hio_dev_mar_t* g_mar;

static int mar_on_read (hio_dev_mar_t* dev, const void* data, hio_iolen_t dlen) { return 0; }
static int mar_on_write (hio_dev_mar_t* dev, hio_iolen_t wrlen, void* wrctx) { return 0; }
static void mar_on_connect (hio_dev_mar_t* dev) { g_connected = 1; }
static void mar_on_disconnect (hio_dev_mar_t* dev) { g_disconnected = 1; g_mar = HIO_NULL; }

static void fill_make (hio_dev_mar_make_t* mi)
{
	HIO_MEMSET (mi, 0, HIO_SIZEOF(*mi));
	mi->on_read = mar_on_read;
	mi->on_write = mar_on_write;
	mi->on_connect = mar_on_connect;
	mi->on_disconnect = mar_on_disconnect;
}

static void test_device_and_failed_connect (void)
{
	hio_dev_mar_make_t mi;
	hio_dev_mar_connect_t ci;

	g_connected = g_disconnected = 0;

	fill_make (&mi);
	g_mar = hio_dev_mar_make(g_hio, 0, &mi);
	if (!g_mar) { FAIL ("a mariadb device can be made"); skip ("no mariadb device", 2); return; }
	PASS ("a mariadb device can be made");

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.host = "127.0.0.1";
	ci.port = DEAD_PORT;
	ci.username = "nobody";
	ci.password = "";
	ci.dbname = "";

	if (hio_dev_mar_connect(g_mar, &ci) <= -1)
	{
		/* refused synchronously - also a correct answer, and not a hang */
		PASS ("connecting to something that is not a server fails");
		PASS ("and does so without hanging");
	}
	else
	{
		run_until (&g_disconnected, 10);
		OK (g_disconnected, "connecting to something that is not a server fails");
		OK (!g_connected, "and does so without ever reporting success");
	}

	if (g_mar) hio_dev_mar_halt (g_mar);
	hio_exec (g_hio);
	hio_exec (g_hio);
}

static void test_escaping (void)
{
	hio_dev_mar_make_t mi;
	hio_dev_mar_t* d;
	hio_bch_t buf[64];
	hio_oow_t n;

	fill_make (&mi);
	d = hio_dev_mar_make(g_hio, 0, &mi);
	if (!d) { skip ("no mariadb device", 3); return; }

	/* escaping is what stands between a caller's string and a query it did not
	 * mean to write, so the quote is the case that matters */
	n = hio_dev_mar_escapebchars(d, "a'b", 3, buf);
	buf[n] = '\0';
	OK (n == 4 && hio_comp_bcstr(buf, "a\\'b", 0) == 0,
	    "a quote is escaped rather than passed through");

	n = hio_dev_mar_escapebchars(d, "plain", 5, buf);
	buf[n] = '\0';
	OK (n == 5 && hio_comp_bcstr(buf, "plain", 0) == 0,
	    "and a string needing nothing is left alone");

	n = hio_dev_mar_escapebchars(d, "", 0, buf);
	buf[n] = '\0';
	OK (n == 0 && buf[0] == '\0', "and an empty string escapes to nothing");

	hio_dev_mar_halt (d);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */

/* only reached where the environment names a server */
static int g_q_done, g_q_rows, g_q_failed;

static void on_query_done (hio_svc_marc_t* marc, hio_oow_t sid,
                           hio_svc_marc_rcode_t rcode, void* data, void* qctx)
{
	switch (rcode)
	{
		case HIO_SVC_MARC_RCODE_ROW:   g_q_rows++; break;
		case HIO_SVC_MARC_RCODE_ERROR: g_q_failed = 1; g_q_done = 1; break;
		case HIO_SVC_MARC_RCODE_DONE:  g_q_done = 1; break;
	}
}

static void test_query_against_a_real_server (void)
{
	const char* host = getenv("HIO_TEST_MARIADB_HOST");
	hio_svc_marc_t* marc;
	hio_svc_marc_connect_t ci;
	hio_svc_marc_tmout_t tmout;

	if (!host) { skip ("set HIO_TEST_MARIADB_HOST and friends to exercise a real server", 2); return; }

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.host = host;
	ci.port = getenv("HIO_TEST_MARIADB_PORT")? atoi(getenv("HIO_TEST_MARIADB_PORT")): 3306;
	ci.username = getenv("HIO_TEST_MARIADB_USER");
	ci.password = getenv("HIO_TEST_MARIADB_PASS");
	ci.dbname = getenv("HIO_TEST_MARIADB_DB");

	HIO_MEMSET (&tmout, 0, HIO_SIZEOF(tmout));
	HIO_INIT_NTIME (&tmout.c, 5, 0);
	HIO_INIT_NTIME (&tmout.r, 5, 0);
	HIO_INIT_NTIME (&tmout.w, 5, 0);

	marc = hio_svc_marc_start(g_hio, &ci, &tmout, HIO_NULL);
	if (!marc) { FAIL ("the connection service starts against the named server"); skip ("no service", 1); return; }
	PASS ("the connection service starts against the named server");

	g_q_done = g_q_rows = g_q_failed = 0;
	if (hio_svc_marc_querywithbchars(marc, 0, HIO_SVC_MARC_QTYPE_SELECT,
	                                 "SELECT 1", 8, on_query_done, HIO_NULL) <= -1)
	{
		FAIL ("and a trivial select returns a row");
	}
	else
	{
		run_until (&g_q_done, 10);
		OK (g_q_done && !g_q_failed && g_q_rows >= 1, "and a trivial select returns a row");
	}

	hio_svc_marc_stop (marc);
	hio_exec (g_hio);
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

	test_device_and_failed_connect ();
	test_escaping ();
	test_query_against_a_real_server ();

	hio_close (g_hio);
	return exit_status();
}

#else /* HIO_ENABLE_MARIADB */

int main (void)
{
	skip_all ("this build has no mariadb support");
	return exit_status();
}

#endif
