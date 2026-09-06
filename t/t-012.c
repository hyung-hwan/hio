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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* deliberately beyond what a system gives an association by default - linux
 * negotiates ten outbound streams unless asked otherwise. writing on stream
 * 40 therefore only works if SCTP_INITMSG really was set, which is the point:
 * with the old call, which handed a struct sctp_initmsg to SCTP_EVENTS, the
 * request silently did nothing and a stream this high is rejected. */
#define OSTREAMS 64
#define TEST_STREAM 40
#define TEST_PPID 0xC0FFEEu
#define PAYLOAD "sctp-payload"

/* ------------------------------------------------------------------ */
/* an allocator that can be told to fail exactly once                  */
/*                                                                     */
/* hio_dev_make() closes the handle it was given if it cannot make the */
/* device - through the transport's fail_before_make method, since only */
/* the transport knows what the handle is. that path had no coverage   */
/* anywhere in this library, and it was wrong for peeled-off sctp      */
/* associations: their method table was the one without the method, so  */
/* the descriptor sctp_peeloff() returned was simply dropped.           */
/*                                                                     */
/* it takes fault injection to reach, so this is the suite's first bit  */
/* of it. pass-through until armed.                                    */
/* ------------------------------------------------------------------ */

static int g_fail_next_alloc;
static int g_failed_allocs;

static void* fi_alloc (hio_mmgr_t* mmgr, hio_oow_t n)
{
	if (g_fail_next_alloc)
	{
		g_fail_next_alloc = 0;
		g_failed_allocs++;
		return HIO_NULL;
	}
	return malloc(n);
}

static void* fi_realloc (hio_mmgr_t* mmgr, void* ptr, hio_oow_t n)
{
	if (g_fail_next_alloc)
	{
		g_fail_next_alloc = 0;
		g_failed_allocs++;
		return HIO_NULL;
	}
	return realloc(ptr, n);
}

static void fi_free (hio_mmgr_t* mmgr, void* ptr)
{
	free (ptr);
}

static hio_mmgr_t g_fi_mmgr = { fi_alloc, fi_realloc, fi_free, HIO_NULL };

/* the lowest descriptor the process is not using. a leaked one pushes this up,
 * which is how a leak is detected without reading /proc. */
static int lowest_free_fd (void)
{
	int fd = dup(0);
	if (fd <= -1) return -1;
	close (fd);
	return fd;
}

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
static void run_until (int* flag, int* flag2)
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

	if (flag2)
	{
		while (!*flag2 && !g_timeout)
		{
			if (hio_exec(g_hio) <= -1) break;
		}
	}

	if (g_deadline != HIO_TMRIDX_INVALID)
	{
		hio_deltmrjob (g_hio, g_deadline);
		g_deadline = HIO_TMRIDX_INVALID;
	}
}

/* the same, for a count that has to reach a target rather than a flag that has
 * to go true. the flag form would sit out the whole deadline here. */
static void run_until_count (int* counter, int target)
{
	hio_tmrjob_t j;

	g_timeout = 0;
	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.sec += 5;
	j.handler = on_deadline;
	j.idxptr = &g_deadline;
	g_deadline = hio_instmrjob(g_hio, &j);

	while (*counter < target && !g_timeout)
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

	run_until (&g_accepted, &g_connected);
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

	run_until (&g_got_data, HIO_NULL);
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
		run_until (&g_eof, HIO_NULL);
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
	mi->type = HIO_DEV_SCK_SCTP4_SEQPKT;
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

	run_until (&g_sp_got, HIO_NULL);
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
			run_until (&g_sp_got, HIO_NULL);
			OK (g_sp_reads == reads_before,
			    "an oversized message is dropped whole rather than split");
		}
		hio_freemem (g_hio, big);
	}

	sp_teardown ();
}

/* the socket api allows connect() on a one-to-many socket - it forms an
 * association and makes it the default destination - but hio cannot finish the
 * job. a device without HIO_DEV_CAP_STREAM is given the stateless ready
 * handler, which looks only at ERR and HUP and never at the progress bits, so
 * on_connect() would never fire and the device would sit in CONNECTING for
 * good: writes failing, a second connect() refused as already in progress, and
 * with a connect timeout set, silently halted seconds later.
 *
 * so the type is marked unconnectable, as udp is, and the attempt is refused up
 * front. what this asserts is that the refusal is clean - the device is exactly
 * as usable afterwards as it was before. */
static void test_seqpkt_unconnectable (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_dev_sck_connect_t ci;
	hio_skad_t peer, dst;

	g_sp_got = g_sp_reads = g_sp_len = 0;

	sp_fill_make (&mi, 1);
	g_sp_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_sp_srv) { skip ("cannot make a one-to-many device", 3); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 3); sp_teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(g_sp_srv, &bi) <= -1) { skip ("bind failed", 3); sp_teardown(); return; }

	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	if (hio_dev_sck_listen(g_sp_srv, &li) <= -1) { skip ("listen failed", 3); sp_teardown(); return; }
	if (hio_dev_sck_getsockaddr(g_sp_srv, &peer) <= -1) { skip ("cannot read the bound address", 3); sp_teardown(); return; }

	sp_fill_make (&mi, 0);
	g_sp_cli = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_sp_cli) { skip ("cannot make a one-to-many client", 3); sp_teardown(); return; }

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.remoteaddr = peer;
	HIO_INIT_NTIME (&ci.connect_tmout, 2, 0);
	OK (hio_dev_sck_connect(g_sp_cli, &ci) <= -1 && hio_geterrnum(g_hio) == HIO_EPERM,
	    "connect on a one-to-many socket is refused with HIO_EPERM");

	/* nothing was started, so nothing is in progress - which is what keeps the
	 * device usable rather than stuck waiting for a callback that never comes */
	OK (HIO_DEV_SCK_GET_PROGRESS(g_sp_cli) == 0,
	    "and leaves no progress state behind");

	/* and the device still works the way it is meant to be driven */
	dst = peer;
	if (hio_dev_sck_write(g_sp_cli, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL, &dst) <= -1)
	{
		FAIL ("the device still carries a message after the refused connect");
	}
	else
	{
		run_until (&g_sp_got, HIO_NULL);
		OK (g_sp_got && g_sp_len == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
		    HIO_MEMCMP(g_sp_buf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
		    "the device still carries a message after the refused connect");
	}

	sp_teardown ();
}

/* a write with no destination on a one-to-many socket reaches sendmsg() on a
 * socket that has no association, and the kernel answers EPIPE. that is the
 * one easily reachable sctp write that raises SIGPIPE, and without MSG_NOSIGNAL
 * on the send it kills the process outright - where the equivalent write on a
 * tcp device merely returns -1, because all three of the plain send paths ask
 * for MSG_NOSIGNAL and the sctp one did not.
 *
 * so half of what this case asserts is the return value and the other half is
 * that the process is still running to assert it. a build without the flag does
 * not fail this line - it never reaches it, and the suite reports the whole
 * program as dead. */
static void test_seqpkt_write_without_destination (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* d;

	sp_fill_make (&mi, 0);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { skip ("cannot make a one-to-many device", 1); return; }

	OK (hio_dev_sck_write(d, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL, HIO_NULL) <= -1,
	    "a destinationless write on a one-to-many socket fails without raising SIGPIPE");

	hio_dev_sck_kill (d);
	hio_exec (g_hio);
}

/* tls needs one in-order byte stream, which an sctp association's streams are
 * not, and this implementation drives tls through SSL_set_fd() - bypassing the
 * sendmsg()/recvmsg() that carry the stream number. so the combination could
 * only give up either the encryption or the streams. it must be refused rather
 * than silently doing one of those. */
static void test_tls_over_sctp_refused (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_t* d;

	fill_make (&mi, 0);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { skip ("cannot make an sctp device", 1); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 1); hio_dev_sck_kill(d); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR | HIO_DEV_SCK_BIND_SSL;
	bi.ssl_certfile = "no-such-cert.pem";
	bi.ssl_keyfile = "no-such-key.pem";

	/* the certificate paths are deliberately bogus: the refusal has to come
	 * before anything tries to load them, or the error would be about the
	 * files rather than about the combination. */
	OK (hio_dev_sck_bind(d, &bi) <= -1 && hio_geterrnum(g_hio) == HIO_ENOIMPL,
	    "tls requested on an sctp socket is refused with HIO_ENOIMPL");

	hio_dev_sck_kill (d);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */
/* multi-homing                                                       */

/* a second loopback address. the whole of 127/8 routes to lo on linux, so
 * 127.0.0.2 is reachable without configuring anything - which makes an
 * honest two-address endpoint possible in a unit test. */
/* two more loopback addresses. the whole of 127/8 routes to lo on linux, so
 * these are reachable without configuring anything - which makes an honest
 * three-address endpoint possible in a unit test. two extra rather than one so
 * that a single bindx() call carries more than one address: the sctp calls take
 * a packed run of variable-length sockaddrs, and with one address a wrong
 * stride between them is invisible. */
#define ADDR2 "127.0.0.2"
#define ADDR3 "127.0.0.3"
#define NEXTRA 2

/* whether the system will let anything bind ADDR2 at all, asked with a plain
 * socket so the answer does not depend on the code under test.
 *
 * without this the bindx cases would skip whenever bindx failed - and a skip
 * looks like a pass. a mutation that made hio_dev_sck_bindx() pass sctp_bindx()
 * an address count of zero went undetected for exactly that reason. now the
 * system's inability to offer a second address is the only thing that skips;
 * anything else is a failure. */
/* whether this build has the multi-homing calls at all. they live in libsctp on
 * linux, which a machine with sctp headers and an sctp kernel may not have, so
 * "sctp works" and "multi-homing works" are two different questions and the
 * library answers the second with HIO_ENOIMPL. asked once, with the cheapest of
 * the calls on a bound socket. */
static int g_have_mh;

static int probe_multihoming (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_t* d;
	hio_skad_t got[4];
	hio_oow_t n = HIO_COUNTOF(got);
	int yes;

	fill_make (&mi, 0);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) return 0;

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1 ||
	    hio_dev_sck_bind(d, &bi) <= -1)
	{
		hio_dev_sck_kill (d);
		hio_exec (g_hio);
		return 0;
	}

	yes = !(hio_dev_sck_getladdrs(d, got, &n) <= -1 && hio_geterrnum(g_hio) == HIO_ENOIMPL);

	hio_dev_sck_kill (d);
	hio_exec (g_hio);
	return yes;
}

static int extra_addrs_usable (void)
{
	static const char* const a[NEXTRA] = { ADDR2, ADDR3 };
	int i;

	for (i = 0; i < NEXTRA; i++)
	{
		struct sockaddr_in sa;
		int fd, ok;

		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd <= -1) return 0;

		memset (&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = 0;
		ok = (inet_pton(AF_INET, a[i], &sa.sin_addr) == 1 &&
		      bind(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0);
		close (fd);
		if (!ok) return 0;
	}

	return 1;
}

static int skad_in_list (const hio_skad_t* needle, const hio_skad_t* list, hio_oow_t n)
{
	hio_oow_t i;
	/* hio_equal_skads() always compares the port too - its 'strict' argument
	 * only decides whether an ipv6 scope id counts. every address in both
	 * lists carries the endpoint's port, so that is what we want. */
	for (i = 0; i < n; i++)
	{
		if (hio_equal_skads(&list[i], needle, 0)) return 1;
	}
	return 0;
}

static void test_multihoming (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_dev_sck_connect_t ci;
	hio_skad_t bound, extra[NEXTRA], got[8], peers[8];
	hio_oow_t n;
	int i;

	g_connected = g_accepted = g_timeout = 0;

	if (!g_have_mh) { skip ("this build has no sctp multi-homing support", 12); return; }

	fill_make (&mi, 1);
	g_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_srv) { skip ("cannot make an sctp server device", 12); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 12); teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(g_srv, &bi) <= -1) { skip ("bind failed", 12); teardown(); return; }
	if (hio_dev_sck_getsockaddr(g_srv, &bound) <= -1) { skip ("cannot read the bound address", 12); teardown(); return; }

	/* one bound address so far */
	n = HIO_COUNTOF(got);
	OK (hio_dev_sck_getladdrs(g_srv, got, &n) == 0 && n == 1 &&
	    hio_equal_skads(&got[0], &bound, 1),
	    "getladdrs reports the one address bind() set");

	/* a caller that guessed too small has to be told how many there were, not
	 * just that it was too small */
	n = 0;
	OK (hio_dev_sck_getladdrs(g_srv, got, &n) <= -1 &&
	    hio_geterrnum(g_hio) == HIO_EBUFFULL && n == 1,
	    "and reports the count needed when the buffer is too small");

	/* now the second address. this is local multi-homing: one endpoint, two
	 * addresses, one association reachable over either. */
	{
		/* the same port on each of the other addresses - one endpoint, three
		 * addresses. built as strings because hio_skad_t has no port setter. */
		static const char* const a[NEXTRA] = { ADDR2, ADDR3 };
		for (i = 0; i < NEXTRA; i++)
		{
			char buf[64];
			snprintf (buf, sizeof(buf), "%s:%d", a[i], (int)hio_skad_get_port(&bound));
			if (hio_bcstrtoskad(g_hio, buf, &extra[i]) <= -1) { skip ("bad extra address", 10); teardown(); return; }
		}
	}

	if (hio_dev_sck_bindx(g_srv, extra, NEXTRA, HIO_DEV_SCK_BINDX_ADD) <= -1)
	{
		if (!extra_addrs_usable())
		{
			/* the system has no further addresses to offer. that is its
			 * answer, not a library fault. */
			skip ("the system cannot bind " ADDR2 " and " ADDR3, 10);
		}
		else
		{
			/* they are bindable by a plain socket, so this failure is ours */
			FAIL ("bindx adds two more local addresses in one call");
			skip ("bindx failed", 9);
		}
		teardown ();
		return;
	}
	PASS ("bindx adds two more local addresses in one call");

	n = HIO_COUNTOF(got);
	OK (hio_dev_sck_getladdrs(g_srv, got, &n) == 0 && n == 1 + NEXTRA &&
	    skad_in_list(&bound, got, n) &&
	    skad_in_list(&extra[0], got, n) && skad_in_list(&extra[1], got, n),
	    "and getladdrs reports all three");

	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	if (hio_dev_sck_listen(g_srv, &li) <= -1) { skip ("listen failed", 8); teardown(); return; }

	fill_make (&mi, 0);
	g_cli = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_cli) { skip ("cannot make an sctp client device", 8); teardown(); return; }

	HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
	ci.remoteaddr = bound;
	HIO_INIT_NTIME (&ci.connect_tmout, 5, 0);
	if (hio_dev_sck_connect(g_cli, &ci) <= -1) { skip ("connect failed to start", 8); teardown(); return; }

	run_until (&g_accepted, HIO_NULL);
	if (!g_accepted) { skip ("no association", 8); teardown(); return; }

	/* the payoff: the client learns both of the server's addresses from the
	 * INIT/INIT-ACK exchange, without having been told either of them. that
	 * is what makes the association survive one path failing. */
	/* pre-filled with a pattern, so an address hio wrote can be told from one
	 * it only partly wrote */
	HIO_MEMSET (peers, 0xFF, HIO_SIZEOF(peers));
	n = HIO_COUNTOF(peers);
	OK (hio_dev_sck_getpaddrs(g_cli, peers, &n) == 0 && n >= 1,
	    "getpaddrs reports the peer addresses of the association");
	OK (n == 1 + NEXTRA && skad_in_list(&bound, peers, n) &&
	    skad_in_list(&extra[0], peers, n) && skad_in_list(&extra[1], peers, n),
	    "and a multi-homed peer is seen as multi-homed - every address arrives");

	/* asking the peer to prefer the address it was not reached on. it has to be
	 * one of the association's peer addresses, which is exactly what getpaddrs
	 * just returned. */
	/* a reported address has to be usable as a destination straight away. the
	 * kernel fills the sockaddr and nothing more, so the extra area hio keeps
	 * past it - stream number, ppid, association id - must be cleared rather
	 * than left holding whatever was in the caller's buffer. otherwise handing
	 * one of these to hio_dev_sck_write() would send on a stream nobody asked
	 * for. */
	{
		int clean = 1;
		hio_oow_t k;
		for (k = 0; k < n; k++)
		{
			if (hio_skad_get_chan(&peers[k]) != 0 ||
			    hio_skad_get_ppid(&peers[k]) != 0 ||
			    hio_skad_get_assoc(&peers[k]) != 0) clean = 0;
		}
		OK (clean, "and each is clean enough to use as a destination as it stands");
	}

	OK (hio_dev_sck_setprimaryaddr(g_cli, &extra[1]) == 0,
	    "setprimaryaddr accepts an address from that list");

	/* and an address that is not one of them is refused rather than quietly
	 * pointing the association at nothing */
	{
		hio_skad_t bogus;
		if (hio_bcstrtoskad(g_hio, "127.9.9.9:1", &bogus) <= -1) skip ("bad address", 1);
		else OK (hio_dev_sck_setprimaryaddr(g_cli, &bogus) <= -1,
		         "and refuses one that is not");
	}

	/* the association still carries data over the new primary path */
	g_got_data = 0; g_rlen = 0;
	if (hio_dev_sck_write(g_cli, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1, HIO_NULL, HIO_NULL) <= -1)
	{
		FAIL ("the association still carries data after the primary path changed");
	}
	else
	{
		run_until (&g_got_data, HIO_NULL);
		OK (g_got_data && g_rlen == (int)HIO_SIZEOF(PAYLOAD) - 1 &&
		    HIO_MEMCMP(g_rbuf, PAYLOAD, HIO_SIZEOF(PAYLOAD) - 1) == 0,
		    "the association still carries data after the primary path changed");
	}

	/* taking an address away again is the other half of bindx */
	OK (hio_dev_sck_bindx(g_srv, extra, NEXTRA, HIO_DEV_SCK_BINDX_REM) == 0,
	    "bindx removes local addresses");
	n = HIO_COUNTOF(got);
	OK (hio_dev_sck_getladdrs(g_srv, got, &n) == 0 && n == 1 &&
	    hio_equal_skads(&got[0], &bound, 1),
	    "and getladdrs reflects the removal");

	teardown ();
}

static void test_multihoming_refusals (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* d;
	hio_skad_t a, got[4];
	hio_oow_t n;

	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &a) <= -1) { skip ("bad address", 3); return; }

	/* not an sctp socket at all */
	HIO_MEMSET (&mi, 0, HIO_SIZEOF(mi));
	mi.type = HIO_DEV_SCK_TCP4;
	mi.on_write = on_write;
	mi.on_read = cli_on_read;
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { skip ("cannot make a tcp device", 3); return; }

	/* only meaningful where bindx exists at all - in a build without it every
	 * socket gets HIO_ENOIMPL and the assertion says nothing */
	if (!g_have_mh) skip ("this build has no sctp multi-homing support", 1);
	else OK (hio_dev_sck_bindx(d, &a, 1, HIO_DEV_SCK_BINDX_ADD) <= -1 &&
	         hio_geterrnum(g_hio) == HIO_ENOIMPL,
	         "bindx on a non-sctp socket is refused with HIO_ENOIMPL");

	/* and there is nothing to peel off a socket that has no associations. the
	 * error distinguishes "wrong kind of socket" from "this build cannot" -
	 * HIO_EINVAL rather than HIO_ENOIMPL - so a caller can tell them apart. */
	OK (hio_dev_sck_peeloff(d, 1) <= -1 &&
	    (hio_geterrnum(g_hio) == HIO_EINVAL || hio_geterrnum(g_hio) == HIO_ENOIMPL),
	    "peeloff on a non-sctp socket is refused");

	hio_dev_sck_kill (d);
	hio_exec (g_hio);

	/* peer addresses are per-association, so a one-to-many socket - which holds
	 * many at once - cannot answer for "the" peer. saying so beats returning
	 * whichever one happened to be first. */
	if (!g_have_mh) { skip ("this build has no sctp multi-homing support", 1); return; }

	sp_fill_make (&mi, 1);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { skip ("cannot make a one-to-many device", 1); return; }
	n = HIO_COUNTOF(got);
	OK (hio_dev_sck_getpaddrs(d, got, &n) <= -1 && hio_geterrnum(g_hio) == HIO_EPERM,
	    "getpaddrs on a one-to-many socket is refused - peel the association off first");
	hio_dev_sck_kill (d);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */
/* peel-off                                                           */

/* peeling an association off turns it from one of many on a shared socket into
 * a device of its own: its own write queue, its own read-enable bit, its own
 * addresses - none of which a multiplexed association can have, and all of
 * which per-association multi-homing needs.
 *
 * which associations get that is the application's call, not the library's, and
 * it can be made at any point in an association's life. this group covers the
 * decision made up front, from the association-up notification; the group
 * after it covers the same decision made later, from a message. */

#define NPEELED 2

static hio_dev_sck_t* g_po_srv = HIO_NULL;
static hio_dev_sck_t* g_po_peeled[NPEELED];
static int g_po_npeeled;
static int g_po_srv_reads;                 /* reads on the listener itself */
static int g_po_data[NPEELED];
static int g_po_ndata;                     /* how many peeled devices have read */
static int g_po_peel_failures;
static hio_bch_t g_po_buf[NPEELED][64];
static hio_dev_sck_t* g_po_cli[NPEELED];
static int g_po_cli_connected;
static int g_po_echoed;

/* the peeled device's remoteaddr as it stood the moment on_connect() ran, i.e.
 * before any read could have overwritten it */
static hio_skad_t g_po_raddr[NPEELED];

static int po_peeled_index (hio_dev_sck_t* sck)
{
	int i;
	for (i = 0; i < g_po_npeeled; i++) { if (g_po_peeled[i] == sck) return i; }
	return -1;
}

/* the association-up notification is where an up-front decision is made. the
 * decoder exists so this does not mean picking a kernel structure apart. */
static int g_po_assoc_events, g_po_comm_ups, g_po_bad_streams;

/* the first notification, kept verbatim. a decoder is best tested on bytes the
 * library actually produced rather than on a structure the test hand-builds,
 * which would only prove the test and the library agree about the header. */
static hio_uint8_t g_po_raw[512];
static hio_iolen_t g_po_rawlen;

static void po_on_notification (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen)
{
	hio_sctp_assoc_event_t ev;

	if (sck != g_po_srv) return;

	if (g_po_rawlen <= 0 && dlen > 0 && dlen <= (hio_iolen_t)HIO_SIZEOF(g_po_raw))
	{
		HIO_MEMCPY (g_po_raw, data, dlen);
		g_po_rawlen = dlen;
	}

	if (hio_dev_sck_parse_assoc_event(data, dlen, &ev) <= -1) return; /* some other event */

	g_po_assoc_events++;
	if (ev.state != HIO_SCTP_ASSOC_COMM_UP) return;

	g_po_comm_ups++;
	/* the negotiated stream counts come back with the event and are not
	 * reachable any other way. both ends asked for OSTREAMS. */
	if (ev.ostreams != OSTREAMS || ev.instreams != OSTREAMS) g_po_bad_streams++;

	if (hio_dev_sck_peeloff(sck, ev.assoc_id) <= -1) g_po_peel_failures++;
}

static int po_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	int i;

	if (sck == g_po_srv)
	{
		/* nothing should ever be read here: every association was peeled off
		 * the moment it came up */
		g_po_srv_reads++;
		return 0;
	}

	i = po_peeled_index(sck);
	if (i <= -1 || dlen <= 0) return 0;
	if (dlen > (hio_iolen_t)HIO_SIZEOF(g_po_buf[i]) - 1) dlen = HIO_SIZEOF(g_po_buf[i]) - 1;
	HIO_MEMCPY (g_po_buf[i], data, dlen);
	g_po_buf[i][dlen] = '\0';
	if (!g_po_data[i]) { g_po_data[i] = 1; g_po_ndata++; }

	/* answer on the same association, to prove the peeled device writes as
	 * well as reads */
	hio_dev_sck_write (sck, "ack", 3, HIO_NULL, HIO_NULL);
	return 0;
}

static void po_on_connect (hio_dev_sck_t* sck)
{
	if ((sck->state & HIO_DEV_SCK_ACCEPTED) && g_po_npeeled < NPEELED)
	{
		g_po_raddr[g_po_npeeled] = sck->remoteaddr;
		g_po_peeled[g_po_npeeled++] = sck;
	}
}

static void po_on_disconnect (hio_dev_sck_t* sck)
{
	int i = po_peeled_index(sck);
	if (i >= 0) g_po_peeled[i] = HIO_NULL;
	else if (sck == g_po_srv) g_po_srv = HIO_NULL;
}

static int po_cli_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	if (dlen == 3 && HIO_MEMCMP(data, "ack", 3) == 0) g_po_echoed++;
	return 0;
}

static void po_cli_on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_CONNECTED) g_po_cli_connected++;
}

static void po_cli_on_disconnect (hio_dev_sck_t* sck)
{
	int i;
	for (i = 0; i < NPEELED; i++) { if (g_po_cli[i] == sck) g_po_cli[i] = HIO_NULL; }
}

static void po_teardown (void)
{
	int i;
	for (i = 0; i < NPEELED; i++) { if (g_po_cli[i]) hio_dev_sck_halt (g_po_cli[i]); }
	for (i = 0; i < NPEELED; i++) { if (g_po_peeled[i]) hio_dev_sck_halt (g_po_peeled[i]); }
	if (g_po_srv) hio_dev_sck_halt (g_po_srv);
	hio_exec (g_hio);
	hio_exec (g_hio);
	HIO_MEMSET (g_po_cli, 0, HIO_SIZEOF(g_po_cli));
	HIO_MEMSET (g_po_peeled, 0, HIO_SIZEOF(g_po_peeled));
	g_po_srv = HIO_NULL;
}

static void test_peeloff (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_dev_sck_connect_t ci;
	hio_skad_t bound;
	int i;

	HIO_MEMSET (g_po_peeled, 0, HIO_SIZEOF(g_po_peeled));
	HIO_MEMSET (g_po_cli, 0, HIO_SIZEOF(g_po_cli));
	HIO_MEMSET (g_po_data, 0, HIO_SIZEOF(g_po_data));
	HIO_MEMSET (g_po_raddr, 0xFF, HIO_SIZEOF(g_po_raddr));
	g_po_npeeled = g_po_srv_reads = g_po_cli_connected = g_po_echoed = 0;
	g_po_ndata = g_po_peel_failures = 0;
	g_po_assoc_events = g_po_comm_ups = g_po_bad_streams = 0;
	g_po_rawlen = 0;
	g_timeout = 0;

	sp_fill_make (&mi, 1);
	mi.on_read = po_on_read;
	mi.on_connect = po_on_connect;
	mi.on_disconnect = po_on_disconnect;
	mi.on_notification = po_on_notification;
	g_po_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_po_srv) { skip ("cannot make a one-to-many device", 12); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 12); po_teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(g_po_srv, &bi) <= -1) { skip ("bind failed", 12); po_teardown(); return; }

	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	if (hio_dev_sck_listen(g_po_srv, &li) <= -1) { skip ("listen failed", 12); po_teardown(); return; }

	/* the build may have no sctp_peeloff(). ask once, cheaply, rather than
	 * letting every assertion below fail for the same reason. */
	if (hio_dev_sck_peeloff(g_po_srv, 0) <= -1 && hio_geterrnum(g_hio) == HIO_ENOIMPL)
	{
		skip ("this build has no sctp peel-off support", 12);
		po_teardown ();
		return;
	}

	if (hio_dev_sck_getsockaddr(g_po_srv, &bound) <= -1) { skip ("cannot read the bound address", 12); po_teardown(); return; }

	/* two clients, so the point of peel-off is actually exercised: on the
	 * shared socket these two associations would be one device. */
	for (i = 0; i < NPEELED; i++)
	{
		fill_make (&mi, 0);
		mi.on_read = po_cli_on_read;
		mi.on_connect = po_cli_on_connect;
		mi.on_disconnect = po_cli_on_disconnect;
		g_po_cli[i] = hio_dev_sck_make(g_hio, 0, &mi);
		if (!g_po_cli[i]) { skip ("cannot make an sctp client device", 12); po_teardown(); return; }

		HIO_MEMSET (&ci, 0, HIO_SIZEOF(ci));
		ci.remoteaddr = bound;
		HIO_INIT_NTIME (&ci.connect_tmout, 5, 0);
		if (hio_dev_sck_connect(g_po_cli[i], &ci) <= -1) { skip ("connect failed to start", 12); po_teardown(); return; }
	}

	run_until_count (&g_po_npeeled, NPEELED);

	OK (g_po_comm_ups == NPEELED && g_po_peel_failures == 0,
	    "the association-up event is decoded and each association is peeled by hand");
	OK (g_po_bad_streams == 0,
	    "and the event carries the negotiated stream counts");

	/* the decoder must reject what is not an association change, or an
	 * application walking notifications would read a path change as one. the
	 * notification type is the leading field of every sctp notification, so
	 * altering it makes a well-formed buffer of the wrong kind. */
	if (g_po_rawlen <= 0) skip ("no notification was captured", 1);
	else
	{
		hio_sctp_assoc_event_t ev;
		hio_uint8_t altered[HIO_SIZEOF(g_po_raw)];
		HIO_MEMCPY (altered, g_po_raw, g_po_rawlen);
		altered[0] ^= 0xFF;
		altered[1] ^= 0xFF;
		OK (hio_dev_sck_parse_assoc_event(g_po_raw, g_po_rawlen, &ev) == 0 &&
		    hio_dev_sck_parse_assoc_event(altered, g_po_rawlen, &ev) <= -1 &&
		    hio_dev_sck_parse_assoc_event(g_po_raw, 3, &ev) <= -1,
		    "and the decoder rejects another kind of notification, and a short buffer");
	}
	OK (g_po_npeeled == NPEELED,
	    "each association arrives as a device of its own through on_connect");
	if (g_po_npeeled != NPEELED) { skip ("no peeled devices", 9); po_teardown(); return; }

	/* what peel-off produces is a one-to-one association, not another
	 * one-to-many socket - so it gets the stream methods, the write queue and
	 * everything else a tcp connection gets. */
	OK (g_po_peeled[0]->type == HIO_DEV_SCK_SCTP4 && g_po_peeled[1]->type == HIO_DEV_SCK_SCTP4,
	    "and it is a one-to-one device, not another one-to-many socket");
	OK ((g_po_peeled[0]->dev_cap & HIO_DEV_CAP_STREAM) &&
	    (g_po_peeled[1]->dev_cap & HIO_DEV_CAP_STREAM),
	    "so it is a stream device with a write queue of its own");
	OK (g_po_peeled[0]->hnd != g_po_srv->hnd && g_po_peeled[1]->hnd != g_po_srv->hnd &&
	    g_po_peeled[0]->hnd != g_po_peeled[1]->hnd,
	    "each has a socket handle of its own, separate from the listener's");

	/* the peer address the device starts life with has to be the client's, and
	 * has to be usable as it stands - the kernel fills the sockaddr and hio
	 * clears the extra area past it, so a write to it does not land on some
	 * stream left over from an earlier message on the listener. */
	{
		hio_skad_t cli0, cli1;
		if (hio_dev_sck_getsockaddr(g_po_cli[0], &cli0) <= -1 ||
		    hio_dev_sck_getsockaddr(g_po_cli[1], &cli1) <= -1) skip ("cannot read the client addresses", 1);
		else
		{
			int i2, clean = 1;
			for (i2 = 0; i2 < NPEELED; i2++)
			{
				if (hio_skad_get_chan(&g_po_raddr[i2]) != 0 ||
				    hio_skad_get_ppid(&g_po_raddr[i2]) != 0 ||
				    hio_skad_get_assoc(&g_po_raddr[i2]) != 0) clean = 0;
			}
			OK (clean &&
			    ((hio_equal_skads(&g_po_raddr[0], &cli0, 1) && hio_equal_skads(&g_po_raddr[1], &cli1, 1)) ||
			     (hio_equal_skads(&g_po_raddr[0], &cli1, 1) && hio_equal_skads(&g_po_raddr[1], &cli0, 1))),
			    "and starts out knowing its own peer's address, and nothing stale besides");
		}
	}

	/* a peeled association can answer for its peer addresses - the shared
	 * socket could not, and that is what per-association multi-homing needs.
	 *
	 * this is the one case in the peel-off group that needs the multi-homing
	 * calls, so it is gated on them. the two features are probed apart: a
	 * system can have sctp_peeloff() without the address calls, and until the
	 * build gating was split this case could not be reached in that state and
	 * so failed the moment it could. */
	if (!g_have_mh) skip ("this build has no sctp multi-homing support", 1);
	else
	{
		hio_skad_t peers[8];
		hio_oow_t n = HIO_COUNTOF(peers);
		OK (hio_dev_sck_getpaddrs(g_po_peeled[0], peers, &n) == 0 && n >= 1,
		    "and can report its own peer addresses");
	}

	for (i = 0; i < NPEELED; i++)
	{
		hio_bch_t msg[8];
		msg[0] = 'm'; msg[1] = 's'; msg[2] = 'g'; msg[3] = (hio_bch_t)('0' + i); msg[4] = '\0';
		if (hio_dev_sck_write(g_po_cli[i], msg, 4, HIO_NULL, HIO_NULL) <= -1)
		{
			FAIL ("each association's data reaches its own device");
			FAIL ("and a reply written on a peeled device reaches its own client");
			FAIL ("nothing is read on the listener once associations are peeled off");
			po_teardown ();
			return;
		}
	}

	run_until_count (&g_po_ndata, NPEELED);

	OK (g_po_data[0] && g_po_data[1] &&
	    HIO_MEMCMP(g_po_buf[0], "msg", 3) == 0 && HIO_MEMCMP(g_po_buf[1], "msg", 3) == 0 &&
	    g_po_buf[0][3] != g_po_buf[1][3],
	    "each association's data reaches its own device, and not the other's");

	run_until_count (&g_po_echoed, NPEELED);
	OK (g_po_echoed == NPEELED, "and a reply written on a peeled device reaches its own client");

	OK (g_po_srv_reads == 0, "nothing is read on the listener once associations are peeled off");

	po_teardown ();
}

/* ------------------------------------------------------------------ */
/* peeling one association off, later, from a message                  */

/* the decision need not be made when the association comes up. an application
 * can let a peer be one of many on the shared socket and promote it only once
 * it has said something that warrants a session - and for that it needs no
 * notification decoding at all, because the association id already rides on
 * the source address of every message.
 *
 * what this proves is that the two modes coexist on one socket: the promoted
 * association's later messages arrive on its own device, and the association
 * left alone keeps arriving on the shared socket. */

#define UPGRADE_CMD "UPGRADE"

static hio_dev_sck_t* g_mx_srv = HIO_NULL;
static hio_dev_sck_t* g_mx_peeled = HIO_NULL;
static hio_dev_sck_t* g_mx_cli[2];
static int g_mx_shared_reads, g_mx_peeled_reads, g_mx_peel_rc = -2;
static hio_bch_t g_mx_shared_last[64], g_mx_peeled_last[64];

static void mx_stash (hio_bch_t* dst, hio_oow_t capa, const void* data, hio_iolen_t dlen)
{
	if (dlen > (hio_iolen_t)capa - 1) dlen = capa - 1;
	HIO_MEMCPY (dst, data, dlen);
	dst[dlen] = '\0';
}

static int mx_on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	if (dlen <= 0) return 0;

	if (sck == g_mx_srv)
	{
		g_mx_shared_reads++;
		mx_stash (g_mx_shared_last, HIO_SIZEOF(g_mx_shared_last), data, dlen);

		if (dlen == (hio_iolen_t)HIO_SIZEOF(UPGRADE_CMD) - 1 &&
		    HIO_MEMCMP(data, UPGRADE_CMD, HIO_SIZEOF(UPGRADE_CMD) - 1) == 0)
		{
			/* no notification, no kernel structure - the association id is
			 * already here. called synchronously, so this socket cannot read
			 * another message for that association first. */
			g_mx_peel_rc = hio_dev_sck_peeloff(sck, hio_skad_get_assoc(srcaddr));
		}
		return 0;
	}

	g_mx_peeled_reads++;
	mx_stash (g_mx_peeled_last, HIO_SIZEOF(g_mx_peeled_last), data, dlen);
	return 0;
}

static void mx_on_connect (hio_dev_sck_t* sck)
{
	if (sck->state & HIO_DEV_SCK_ACCEPTED) g_mx_peeled = sck;
}

static void mx_on_disconnect (hio_dev_sck_t* sck)
{
	int i;
	if (sck == g_mx_peeled) g_mx_peeled = HIO_NULL;
	else if (sck == g_mx_srv) g_mx_srv = HIO_NULL;
	for (i = 0; i < 2; i++) { if (g_mx_cli[i] == sck) g_mx_cli[i] = HIO_NULL; }
}

static void mx_teardown (void)
{
	int i;
	for (i = 0; i < 2; i++) { if (g_mx_cli[i]) hio_dev_sck_halt (g_mx_cli[i]); }
	if (g_mx_peeled) hio_dev_sck_halt (g_mx_peeled);
	if (g_mx_srv) hio_dev_sck_halt (g_mx_srv);
	hio_exec (g_hio);
	hio_exec (g_hio);
	HIO_MEMSET (g_mx_cli, 0, HIO_SIZEOF(g_mx_cli));
	g_mx_peeled = g_mx_srv = HIO_NULL;
}

static void test_peeloff_on_demand (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_skad_t peer;
	int i;

	HIO_MEMSET (g_mx_cli, 0, HIO_SIZEOF(g_mx_cli));
	g_mx_shared_reads = g_mx_peeled_reads = 0;
	g_mx_peel_rc = -2;

	sp_fill_make (&mi, 1);
	mi.on_read = mx_on_read;
	mi.on_connect = mx_on_connect;
	mi.on_disconnect = mx_on_disconnect;
	g_mx_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_mx_srv) { skip ("cannot make a one-to-many device", 4); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 4); mx_teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(g_mx_srv, &bi) <= -1) { skip ("bind failed", 4); mx_teardown(); return; }

	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	if (hio_dev_sck_listen(g_mx_srv, &li) <= -1) { skip ("listen failed", 4); mx_teardown(); return; }
	if (hio_dev_sck_peeloff(g_mx_srv, 0) <= -1 && hio_geterrnum(g_hio) == HIO_ENOIMPL)
	{
		skip ("this build has no sctp peel-off support", 4);
		mx_teardown ();
		return;
	}
	if (hio_dev_sck_getsockaddr(g_mx_srv, &peer) <= -1) { skip ("cannot read the bound address", 4); mx_teardown(); return; }

	/* two one-to-many clients, so neither needs to connect */
	for (i = 0; i < 2; i++)
	{
		sp_fill_make (&mi, 0);
		mi.on_read = sp_cli_on_read;
		mi.on_disconnect = mx_on_disconnect;
		g_mx_cli[i] = hio_dev_sck_make(g_hio, 0, &mi);
		if (!g_mx_cli[i]) { skip ("cannot make a one-to-many client", 4); mx_teardown(); return; }
	}

	/* client 0 stays an ordinary datagram peer */
	if (hio_dev_sck_write(g_mx_cli[0], "plain", 5, HIO_NULL, &peer) <= -1) { skip ("write failed", 4); mx_teardown(); return; }
	run_until_count (&g_mx_shared_reads, 1);
	OK (g_mx_shared_reads == 1 && HIO_MEMCMP(g_mx_shared_last, "plain", 5) == 0,
	    "an ordinary message arrives on the shared socket");

	/* client 1 asks to be promoted */
	if (hio_dev_sck_write(g_mx_cli[1], UPGRADE_CMD, HIO_SIZEOF(UPGRADE_CMD) - 1, HIO_NULL, &peer) <= -1) { skip ("write failed", 3); mx_teardown(); return; }
	run_until_count (&g_mx_shared_reads, 2);
	OK (g_mx_peel_rc == 0 && g_mx_peeled != HIO_NULL,
	    "a message can peel its own association off, by the id on its source address");

	if (!g_mx_peeled) { skip ("nothing was peeled", 2); mx_teardown(); return; }

	/* and from here its messages belong to the new device, not the old socket */
	if (hio_dev_sck_write(g_mx_cli[1], "after", 5, HIO_NULL, &peer) <= -1) FAIL ("the promoted association's later messages arrive on its own device");
	else
	{
		run_until_count (&g_mx_peeled_reads, 1);
		OK (g_mx_peeled_reads == 1 && g_mx_shared_reads == 2 &&
		    HIO_MEMCMP(g_mx_peeled_last, "after", 5) == 0,
		    "the promoted association's later messages arrive on its own device");
	}

	/* while the association left alone is entirely unaffected */
	if (hio_dev_sck_write(g_mx_cli[0], "again", 5, HIO_NULL, &peer) <= -1) FAIL ("and the association left alone keeps using the shared socket");
	else
	{
		run_until_count (&g_mx_shared_reads, 3);
		OK (g_mx_shared_reads == 3 && HIO_MEMCMP(g_mx_shared_last, "again", 5) == 0,
		    "and the association left alone keeps using the shared socket");
	}

	mx_teardown ();
}

/* ------------------------------------------------------------------ */
/* the handle when the device cannot be made                           */

/* sctp_peeloff() hands back a descriptor before there is any device to own it.
 * if hio_dev_make() then fails, the only thing that can close it is the
 * transport's fail_before_make method - and dev_mth_clisck_sctp_stream was the
 * one accepted-client table that did not have one, so the descriptor was
 * orphaned. reaching that needs the allocation to fail, hence the injector. */
static void test_peeloff_alloc_failure (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_listen_t li;
	hio_skad_t peer, dst;
	int before, after, rc;

	g_mx_shared_reads = 0;
	g_failed_allocs = 0;

	sp_fill_make (&mi, 1);
	mi.on_read = mx_on_read;
	mi.on_connect = mx_on_connect;
	mi.on_disconnect = mx_on_disconnect;
	g_mx_srv = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_mx_srv) { skip ("cannot make a one-to-many device", 2); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	if (hio_bcstrtoskad(g_hio, "127.0.0.1:0", &bi.localaddr) <= -1) { skip ("bad address", 2); mx_teardown(); return; }
	bi.options = HIO_DEV_SCK_BIND_REUSEADDR;
	if (hio_dev_sck_bind(g_mx_srv, &bi) <= -1) { skip ("bind failed", 2); mx_teardown(); return; }
	HIO_MEMSET (&li, 0, HIO_SIZEOF(li));
	li.backlogs = 4;
	HIO_INIT_NTIME (&li.accept_tmout, -1, 0);
	if (hio_dev_sck_listen(g_mx_srv, &li) <= -1) { skip ("listen failed", 2); mx_teardown(); return; }
	if (hio_dev_sck_peeloff(g_mx_srv, 0) <= -1 && hio_geterrnum(g_hio) == HIO_ENOIMPL)
	{
		skip ("this build has no sctp peel-off support", 2);
		mx_teardown ();
		return;
	}
	if (hio_dev_sck_getsockaddr(g_mx_srv, &peer) <= -1) { skip ("cannot read the bound address", 2); mx_teardown(); return; }

	/* one association, formed by writing to the address */
	sp_fill_make (&mi, 0);
	mi.on_read = sp_cli_on_read;
	mi.on_disconnect = mx_on_disconnect;
	g_mx_cli[0] = hio_dev_sck_make(g_hio, 0, &mi);
	if (!g_mx_cli[0]) { skip ("cannot make a one-to-many client", 2); mx_teardown(); return; }

	dst = peer;
	if (hio_dev_sck_write(g_mx_cli[0], "hello", 5, HIO_NULL, &dst) <= -1) { skip ("write failed", 2); mx_teardown(); return; }
	run_until_count (&g_mx_shared_reads, 1);
	if (g_mx_shared_reads < 1) { skip ("no association was formed", 2); mx_teardown(); return; }

	/* the association id came in on the source address of that message */
	{
		hio_int32_t assoc = hio_skad_get_assoc(&g_mx_srv->remoteaddr);
		if (assoc == 0) { skip ("no association id", 2); mx_teardown(); return; }

		/* hio_dev_make()'s callocmem is the first allocation the peel-off path
		 * reaches, so arming the next one lands exactly there. called from here
		 * rather than from a callback so nothing else can allocate in between. */
		before = lowest_free_fd();
		g_fail_next_alloc = 1;
		rc = hio_dev_sck_peeloff(g_mx_srv, assoc);
		g_fail_next_alloc = 0;
		after = lowest_free_fd();
	}

	OK (rc <= -1 && g_failed_allocs == 1,
	    "a peel-off whose device cannot be allocated reports failure");
	OK (before > 0 && before == after,
	    "and closes the descriptor sctp_peeloff() returned rather than orphaning it");

	mx_teardown ();
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* probe;

	g_hio = hio_open(&g_fi_mmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!g_hio)
	{
		no_plan ();
		bail_out ("unable to open hio");
		return -1;
	}
	quiet_logging (g_hio);

	/* two ways there is nothing here to test, and they are told apart by the
	 * error number:
	 *
	 *   HIO_ENOIMPL - this build of hio has no sctp support, so no binary
	 *                 anywhere would do better.
	 *   HIO_ENOSUP  - the build has it and the system does not. freebsd keeps
	 *                 sctp in a loadable module and leaves it out of GENERIC,
	 *                 so this is what a box without 'kldload sctp' reports.
	 *
	 * neither is a failure of the code under test, and both skip. anything
	 * else is a real problem and bails. skip_all has to come before no_plan. */
	fill_make (&mi, 0);
	probe = hio_dev_sck_make(g_hio, 0, &mi);
	if (!probe)
	{
		hio_errnum_t e = hio_geterrnum(g_hio);
		if (e == HIO_ENOIMPL)
		{
			skip_all ("this build of hio has no sctp support");
			hio_close (g_hio);
			return exit_status();
		}
		if (e == HIO_ENOSUP)
		{
			skip_all ("this system provides no sctp");
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

	g_have_mh = probe_multihoming();

	no_plan ();

	test_association_and_ancillary ();
	test_notifications_are_not_data ();
	test_one_to_many ();
	test_seqpkt_unconnectable ();
	test_seqpkt_write_without_destination ();
	test_tls_over_sctp_refused ();
	test_multihoming ();
	test_multihoming_refusals ();
	test_peeloff ();
	test_peeloff_on_demand ();
	test_peeloff_alloc_failure ();

	hio_close (g_hio);
	return exit_status();
}
