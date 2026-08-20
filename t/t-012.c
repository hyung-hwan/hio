/*
 * sctp transport tests.
 *
 * these cover the one-to-one types, HIO_DEV_SCK_SCTP4 and its v6 twin. they
 * are stream devices like tcp, but their reads and writes go through
 * sendmsg()/recvmsg() so that the per-message ancillary data is reachable -
 * which is the only reason to prefer sctp here in the first place.
 *
 * what is checked: an association forms and carries data; a stream number and
 * a ppid set on the destination arrive at the far end; notifications are
 * delivered to on_notification() and never spliced into the data stream; and a
 * zero-length write still means "close the writing end" rather than "send an
 * empty message".
 *
 * the second group covers the one-to-many (SOCK_SEQPACKET) types. those are
 * not accept-based: one device carries every association, and a message is
 * attributed by the source address and the association id it arrives with.
 *
 * the whole file skips where the system has no sctp - the library reports that
 * as HIO_ENOIMPL from hio_dev_sck_make(), which is also worth asserting, since
 * it is how a caller is meant to find out.
 */

#include <hio-sck.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>

/* deliberately beyond what a system gives an association by default - linux
 * negotiates ten outbound streams unless asked otherwise. writing on stream
 * 40 therefore only works if SCTP_INITMSG really was set, which is the point:
 * with the old call, which handed a struct sctp_initmsg to SCTP_EVENTS, the
 * request silently did nothing and a stream this high is rejected. */
#define OSTREAMS 64
#define TEST_STREAM 40
#define TEST_PPID 0xC0FFEEu
#define PAYLOAD "sctp-payload"

static hio_t* g_hio = HIO_NULL;
static hio_dev_sck_t* g_srv = HIO_NULL;
static hio_dev_sck_t* g_acc = HIO_NULL;
static hio_dev_sck_t* g_cli = HIO_NULL;

static int g_connected, g_accepted, g_timeout;
static int g_got_data;              /* payload arrived */
static int g_notifications;         /* on_notification calls */
static int g_eof;                   /* the accepted side saw a zero-length read */
static int g_reads;
static hio_bch_t g_rbuf[256];
static int g_rlen;
static hio_uint16_t g_rchan;        /* stream number the receiver saw */
static hio_uint32_t g_rppid;
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

/* ------------------------------------------------------------------ */

static int srv_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	if (dlen <= 0)
	{
		/* the client shut its writing end - EOF, as for any stream device */
		g_eof = 1;
		return 0;
	}

	g_reads++;
	g_got_data = 1;
	if (dlen > (hio_iolen_t)HIO_SIZEOF(g_rbuf) - 1) dlen = HIO_SIZEOF(g_rbuf) - 1;
	HIO_MEMCPY (g_rbuf, data, dlen);
	g_rbuf[dlen] = '\0';
	g_rlen = (int)dlen;

	/* the ancillary data rides on the source address */
	g_rchan = hio_skad_get_chan(srcaddr? srcaddr: &sck->remoteaddr);
	g_rppid = hio_skad_get_ppid(srcaddr? srcaddr: &sck->remoteaddr);
	return 0;
}

static int on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void on_notification (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen)
{
	/* an association or path event. it must never reach on_read(). */
	g_notifications++;
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

static int cli_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	return 0;
}

static void cli_on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_CONNECTED) g_connected = 1;
}

static void cli_on_disconnect (hio_dev_sck_t* sck)
{
	if (sck == g_cli) g_cli = HIO_NULL;
}

/* ------------------------------------------------------------------ */

static void fill_make (hio_dev_sck_make_t* mi, int server)
{
	HIO_MEMSET (mi, 0, HIO_SIZEOF(*mi));
	mi->type = HIO_DEV_SCK_SCTP4;
	mi->on_write = on_write;
	mi->on_read = server? srv_on_read: cli_on_read;
	mi->on_connect = server? srv_on_connect: cli_on_connect;
	mi->on_disconnect = server? srv_on_disconnect: cli_on_disconnect;
	mi->on_notification = on_notification;
	/* ask for more streams than the system default. the negotiated count is
	 * min(our sinit_num_ostreams, the peer's sinit_max_instreams), so both
	 * ends have to ask. */
	mi->sctp_ostreams = OSTREAMS;
	mi->sctp_instreams = OSTREAMS;
}

/* run the loop until the flag is set, or five seconds pass. the timeout is
 * cleared on entry: a phase that legitimately waits must not be starved by an
 * earlier phase having already used the budget up. */
static void run_until (int* flag)
{
	hio_tmrjob_t j;

	g_timeout = 0;
	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.sec += 5;
	j.handler = on_deadline;
	j.idxptr = &g_deadline;
	g_deadline = hio_instmrjob(g_hio, &j);

	while (!*flag && !g_timeout)
	{
		if (hio_exec(g_hio) <= -1) break;
	}

	if (g_deadline != HIO_TMRIDX_INVALID) { hio_deltmrjob (g_hio, g_deadline); g_deadline = HIO_TMRIDX_INVALID; }
}

static void teardown (void)
{
	if (g_cli) hio_dev_sck_halt (g_cli);
	if (g_acc) hio_dev_sck_halt (g_acc);
	if (g_srv) hio_dev_sck_halt (g_srv);
	hio_exec (g_hio);
	hio_exec (g_hio);
	g_cli = g_acc = g_srv = HIO_NULL;
}

/* ------------------------------------------------------------------ */

static void test_association_and_ancillary (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_dev_sck_connect_t ci;
	hio_skad_t peer;

	g_connected = g_accepted = g_timeout = g_got_data = 0;
	g_notifications = g_eof = g_reads = g_rlen = 0;
	g_rchan = 0; g_rppid = 0;

	fill_make (&mi, 1);
	g_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_srv) { skip ("cannot make an sctp server device", 8); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 8); teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	OK (hio_dev_sck_bind(g_srv, &bi) == 0, "an sctp socket binds");

	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	OK (hio_dev_sck_listen(g_srv, &li) == 0, "and listens");

	if (hio_dev_sck_getsockaddr(g_srv, &peer) <= -1) { skip ("cannot read the bound address", 6); teardown(); return; }

	fill_make (&mi, 0);
	g_cli = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_cli) { skip ("cannot make an sctp client device", 6); teardown(); return; }

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.remoteaddr = peer;
	HIO_INIT_NTIME (&ci.connect_tmout, 5, 0);
	if (hio_dev_sck_connect(g_cli, &ci) <= -1) { skip ("connect failed to start", 6); teardown(); return; }

	run_until (&g_accepted);
	OK (g_connected, "the client reaches the CONNECTED state");
	OK (g_accepted, "the server is handed an ACCEPTED association");

	if (!g_accepted) { skip ("no association", 4); teardown(); return; }

	/* write on a specific stream with a specific ppid. both ride on the
	 * destination address rather than on a separate argument, which is why
	 * hio_skad_t carries an extra area at all. */
	{
		hio_skad_t dst = peer;
		hio_skad_set_chan (&dst, TEST_STREAM);
		hio_skad_set_ppid (&dst, TEST_PPID);
		/* [NOTE] a failure here is a failure, not a missing prerequisite -
		 * skipping would hide exactly what this case exists to catch. writing
		 * on a stream above the system default is rejected outright unless
		 * SCTP_INITMSG asked for the streams. */
		if (hio_dev_sck_write(g_cli, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL, &dst) <= -1)
		{
			FAIL ("a write on a requested stream is accepted");
			FAIL ("the stream number set on the destination arrives at the far end");
			FAIL ("and so does the ppid");
			FAIL ("a zero-length write closes the writing end rather than sending an empty message");
			teardown ();
			return;
		}
	}

	run_until (&g_got_data);
	OK (g_got_data && g_rlen == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
	    HIO_MEMCMP(g_rbuf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
	    "the association carries data intact");
	OK (g_rchan == TEST_STREAM, "the stream number set on the destination arrives at the far end");
	OK (g_rppid == TEST_PPID, "and so does the ppid");

	/* a zero-length write is the shutdown indicator for a stream device. the
	 * seqpacket write method would have put an empty message on the wire
	 * instead, which the peer would see as data rather than as EOF. */
	if (hio_dev_sck_write(g_cli, HIO_NULL, 0, HIO_NULL, HIO_NULL) <= -1) FAIL ("the zero-length write is accepted");
	else
	{
		run_until (&g_eof);
		OK (g_eof, "a zero-length write closes the writing end rather than sending an empty message");
	}

	teardown ();
}

static void test_notifications_are_not_data (void)
{
	/* the events subscribed at socket setup mean an association coming up
	 * produces a notification. it must be reported through on_notification
	 * and must not appear as payload - the reads seen must be exactly the
	 * ones the test sent. */
	OK (g_notifications > 0, "association setup produces at least one notification");
	OK (g_reads == 1, "and no notification is delivered to on_read as data");
}

/* ------------------------------------------------------------------ */
/* one-to-many */

static hio_dev_sck_t* g_sp_srv = HIO_NULL;
static hio_dev_sck_t* g_sp_cli = HIO_NULL;
static int g_sp_got, g_sp_reads;
static hio_int32_t g_sp_assoc;
static hio_uint16_t g_sp_chan;
static int g_sp_len;
static hio_bch_t g_sp_buf[512];

static int sp_srv_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	if (dlen <= 0) return 0;
	g_sp_reads++;
	if (dlen > (hio_iolen_t)HIO_SIZEOF(g_sp_buf) - 1) dlen = HIO_SIZEOF(g_sp_buf) - 1;
	HIO_MEMCPY (g_sp_buf, data, dlen);
	g_sp_buf[dlen] = '\0';
	g_sp_len = (int)dlen;
	g_sp_chan = hio_skad_get_chan(srcaddr);
	g_sp_assoc = hio_skad_get_assoc(srcaddr);
	g_sp_got = 1;
	return 0;
}

static int sp_cli_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	return 0;
}

static void sp_on_connect (hio_dev_sck_t* sck) { }
static void sp_on_disconnect (hio_dev_sck_t* sck)
{
	if (sck == g_sp_srv) g_sp_srv = HIO_NULL;
	else if (sck == g_sp_cli) g_sp_cli = HIO_NULL;
}

static void sp_fill_make (hio_dev_sck_make_t* mi, int server)
{
	HIO_MEMSET (mi, 0, HIO_SIZEOF(*mi));
	mi->type = HIO_DEV_SCK_SCTP4_SP;
	mi->on_write = on_write;
	mi->on_read = server? sp_srv_on_read: sp_cli_on_read;
	mi->on_connect = sp_on_connect;
	mi->on_disconnect = sp_on_disconnect;
	mi->on_notification = on_notification;
	mi->sctp_ostreams = OSTREAMS;
	mi->sctp_instreams = OSTREAMS;
}

static void sp_teardown (void)
{
	if (g_sp_cli) hio_dev_sck_halt (g_sp_cli);
	if (g_sp_srv) hio_dev_sck_halt (g_sp_srv);
	hio_exec (g_hio);
	hio_exec (g_hio);
	g_sp_cli = g_sp_srv = HIO_NULL;
}

static void test_one_to_many (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_skad_t peer, dst;
	hio_oow_t bigsz;
	hio_uint8_t* big;

	g_sp_got = g_sp_reads = g_sp_len = 0;
	g_sp_assoc = 0; g_sp_chan = 0;
	g_notifications = 0;

	sp_fill_make (&mi, 1);
	g_sp_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_sp_srv) { skip ("cannot make a one-to-many device", 6); return; }

	/* a message-oriented device asks for a read buffer wide enough for the
	 * largest datagram, since a short read would truncate */
	OK (g_sp_srv->dev_rdmin == HIO_DGRAM_READ_BUFFER_SIZE,
	    "a one-to-many socket requires a whole-message read buffer");

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 5); sp_teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(g_sp_srv, &bi) <= -1) { skip ("bind failed", 5); sp_teardown(); return; }

	/* listen() is called, but nothing is ever accepted - associations are not
	 * devices in this model */
	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	OK (hio_dev_sck_listen(g_sp_srv, &li) == 0, "and it listens without accepting");

	if (hio_dev_sck_getsockaddr(g_sp_srv, &peer) <= -1) { skip ("cannot read the bound address", 4); sp_teardown(); return; }

	sp_fill_make (&mi, 0);
	g_sp_cli = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_sp_cli) { skip ("cannot make a one-to-many client", 4); sp_teardown(); return; }

	/* no connect: sending to an address forms the association implicitly */
	dst = peer;
	hio_skad_set_chan (&dst, TEST_STREAM);
	if (hio_dev_sck_write(g_sp_cli, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL, &dst) <= -1)
	{
		FAIL ("a write forms the association implicitly");
		FAIL ("the stream number arrives with the message");
		FAIL ("the association id arrives with the message");
		FAIL ("an oversized message is dropped whole rather than split");
		sp_teardown ();
		return;
	}

	run_until (&g_sp_got);
	OK (g_sp_got && g_sp_len == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
	    HIO_MEMCMP(g_sp_buf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
	    "a write forms the association implicitly and the message arrives");
	OK (g_sp_chan == TEST_STREAM, "the stream number arrives with the message");
	OK (g_sp_assoc != 0, "the association id arrives with the message");

	/* now a message larger than the read buffer. without the MSG_EOR check
	 * the receiver would see it as two separate messages; with it, the whole
	 * message is dropped and nothing bogus is delivered. */
	bigsz = HIO_DGRAM_READ_BUFFER_SIZE * 2;
	big = (hio_uint8_t*)hio_allocmem(g_hio, bigsz);
	if (!big) skip ("out of memory", 1);
	else
	{
		int reads_before = g_sp_reads;
		HIO_MEMSET (big, 'Z', bigsz);
		g_sp_got = 0;
		if (hio_dev_sck_write(g_sp_cli, big, bigsz, HIO_NULL, &dst) <= -1)
		{
			/* the system may refuse a message this large outright, which is
			 * also a correct outcome - nothing bogus reaches on_read */
			OK (g_sp_reads == reads_before, "an oversized message is dropped whole rather than split");
		}
		else
		{
			run_until (&g_sp_got);
			OK (g_sp_reads == reads_before,
			    "an oversized message is dropped whole rather than split");
		}
		hio_freemem (g_hio, big);
	}

	sp_teardown ();
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* probe;

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		no_plan ();
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	/* a library built without sctp reports HIO_ENOIMPL here, which is how a
	 * caller is meant to discover it. skip_all has to come before no_plan. */
	fill_make (&mi, 0);
	probe = hio_dev_sck_make(g_hio, 0, &mi);
	if (!probe)
	{
		if (hio_geterrnum(g_hio) == HIO_ENOIMPL)
		{
			skip_all ("this build of hio has no sctp support");
			hio_close (g_hio);
			return exit_status();
		}
		no_plan ();
		bail_out ("sctp device creation failed for an unexpected reason");
		hio_close (g_hio);
		return -1;
	}
	hio_dev_sck_kill (probe);
	hio_exec (g_hio);

	no_plan ();

	test_association_and_ancillary ();
	test_notifications_are_not_data ();
	test_one_to_many ();

	hio_close (g_hio);
	return exit_status();
}
