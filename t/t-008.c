/*
 * write path tests: immediate writes, short writes, EAGAIN queueing and
 * the deferred completion queue.
 *
 * the device sits on one end of a socketpair, but its write method is a
 * stub the test throttles, so partial writes happen exactly where the
 * test wants them rather than wherever the kernel's send buffer happens
 * to fill. every case reads the bytes back off the peer end and compares
 * them to the pattern that was submitted - a write path that delivers the
 * right *number* of bytes can still deliver the wrong ones.
 */

#include <hio-prv.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "tap.h"

#define PATLEN 100

static hio_t* g_hio = HIO_NULL;

/* write-method throttle */
static hio_iolen_t g_wr_chunk = 0;      /* max bytes accepted per write call */
static int g_wr_calls_left = 0;         /* successful calls before reporting EAGAIN */
static int g_wr_calls = 0;              /* how many times the write method ran */

/* completion observations */
#define MAX_CW 16
static hio_iolen_t g_cw_len[MAX_CW];
static void*       g_cw_ctx[MAX_CW];
static int         g_cw_n = 0;

/* the greedy-read interleaving cases below. g_rd_chunk caps how much one read
 * delivers; g_write_on_read makes on_read issue a write and ask for another
 * read, which is the only shape that reaches the completion firing inside the
 * read loop. g_seq records 'R' per on_read and 'W' per on_write. */
static hio_iolen_t g_rd_chunk = 0;
static hio_iolen_t g_rd_offered = 0;   /* the buffer size the core last offered a read */
static int         g_rd_calls = 0;
static int         g_setopt_from_on_read = 0;
static int         g_setopt_rc = 0;
static int         g_setopt_errnum = 0;
static int         g_write_on_read = 0;
static hio_bch_t   g_seq[64];
static int         g_seq_n = 0;

static void seq_put (hio_bch_t c)
{
	if (g_seq_n < (int)HIO_COUNTOF(g_seq) - 1) g_seq[g_seq_n++] = c;
	g_seq[g_seq_n] = '\0';
}

static hio_uint8_t g_pattern[PATLEN];

static void obs_reset (void)
{
	g_wr_calls = 0;
	g_cw_n = 0;
	g_rd_chunk = 0;
	g_rd_offered = 0;
	g_rd_calls = 0;
	g_setopt_from_on_read = 0;
	g_setopt_rc = 0;
	g_setopt_errnum = 0;
	g_write_on_read = 0;
	g_seq_n = 0;
	g_seq[0] = '\0';
}

/* ------------------------------------------------------------------ */

typedef struct tdev_t tdev_t;
struct tdev_t
{
	HIO_DEV_HEADER;
	int fd;
};

static int tdev_make (hio_dev_t* dev, void* ctx)
{
	tdev_t* t = (tdev_t*)dev;
	t->fd = *(int*)ctx;
	t->dev_cap |= HIO_DEV_CAP_STREAM;
	return 0;
}

static int tdev_kill (hio_dev_t* dev, int force)
{
	tdev_t* t = (tdev_t*)dev;
	if (t->fd >= 0) { close (t->fd); t->fd = -1; }
	return 0;
}

static hio_syshnd_t tdev_getsyshnd (hio_dev_t* dev)
{
	return (hio_syshnd_t)((tdev_t*)dev)->fd;
}

static int tdev_read (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	ssize_t n;
	g_rd_offered = *len;   /* what the core is willing to take in one go */
	g_rd_calls++;
	if (g_rd_chunk > 0 && *len > g_rd_chunk) *len = g_rd_chunk; /* force several iterations */
	n = recv(((tdev_t*)dev)->fd, buf, *len, 0);
	if (n <= -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	*len = n;
	return 1;
}

/* the throttled write method.
 *
 * NOTE it deliberately writes from the pointer it was handed, at the
 * length it was handed. that is the contract the core is written
 * against, and testing it faithfully is the whole point here. */
static int tdev_write (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	tdev_t* t = (tdev_t*)dev;
	hio_iolen_t want;
	ssize_t n;

	g_wr_calls++;

	if (*len <= 0)
	{
		/* zero-length request: close the writing end */
		shutdown (t->fd, SHUT_WR);
		return 1;
	}

	if (g_wr_calls_left <= 0) return 0; /* pretend EAGAIN */
	g_wr_calls_left--;

	want = *len;
	if (g_wr_chunk > 0 && want > g_wr_chunk) want = g_wr_chunk;

	n = send(t->fd, data, want, 0);
	if (n <= -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	*len = n;
	return 1;
}

static int tdev_writev (hio_dev_t* dev, const hio_iovec_t* iov, hio_iolen_t* iovcnt, const hio_devaddr_t* dstaddr)
{
	tdev_t* t = (tdev_t*)dev;
	hio_iolen_t i, want, budget;
	struct iovec liov[8];
	ssize_t n;

	g_wr_calls++;
	if (*iovcnt <= 0) { shutdown (t->fd, SHUT_WR); return 1; }
	if (g_wr_calls_left <= 0) return 0;
	g_wr_calls_left--;

	/* clamp the vector to the per-call byte budget */
	budget = (g_wr_chunk > 0)? g_wr_chunk: HIO_TYPE_MAX(hio_iolen_t);
	want = 0;
	for (i = 0; i < *iovcnt && i < (hio_iolen_t)HIO_COUNTOF(liov) && budget > 0; i++)
	{
		hio_iolen_t l = iov[i].iov_len;
		if (l > budget) l = budget;
		liov[i].iov_base = iov[i].iov_ptr;
		liov[i].iov_len = l;
		budget -= l;
		want++;
	}
	if (want <= 0) return 0;

	n = writev(t->fd, liov, want);
	if (n <= -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	*iovcnt = n; /* the core expects a byte count here, not a vector count */
	return 1;
}

/* a sendfile method built on pread()+send() so the same throttle applies.
 * it must send from 'foff' for at most '*len' bytes and report how many
 * went out - the core relies on that byte count to know when the request
 * is finished. */
static int tdev_sendfile (hio_dev_t* dev, hio_syshnd_t in_fd, hio_foff_t foff, hio_iolen_t* len)
{
	tdev_t* t = (tdev_t*)dev;
	hio_uint8_t buf[256];
	hio_iolen_t want;
	ssize_t n, m;

	g_wr_calls++;
	if (*len <= 0) { shutdown (t->fd, SHUT_WR); return 1; }
	if (g_wr_calls_left <= 0) return 0;
	g_wr_calls_left--;

	want = *len;
	if (g_wr_chunk > 0 && want > g_wr_chunk) want = g_wr_chunk;
	if (want > (hio_iolen_t)HIO_SIZEOF(buf)) want = HIO_SIZEOF(buf);

	n = pread(in_fd, buf, want, foff);
	if (n <= -1)
	{
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	if (n == 0) return 0; /* end of file - nothing more to hand over */

	m = send(t->fd, buf, n, 0);
	if (m <= -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	*len = m;
	return 1;
}

static hio_dev_mth_t tdev_mth =
{
	tdev_make, tdev_kill, HIO_NULL,
	tdev_getsyshnd, HIO_NULL, HIO_NULL,
	tdev_read, tdev_write, tdev_writev, tdev_sendfile
};

static int tdev_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	if (g_setopt_from_on_read)
	{
		/* the data pointer we were just handed points into the buffer being
		 * resized, so this must be refused rather than honoured */
		hio_oow_t want = HIO_DFL_READ_BUFFER_SIZE * 2;
		g_setopt_from_on_read = 0;
		g_setopt_rc = hio_setoption(dev->hio, HIO_READ_BUFFER_SIZE, &want);
		g_setopt_errnum = hio_geterrnum(dev->hio);
	}

	if (!g_write_on_read) return 0;

	seq_put ('R');
	if (len <= 0) return 0;
	/* completes straight away, so a completion is queued for the next
	 * iteration of the read loop to fire */
	if (hio_dev_write(dev, g_pattern, 1, HIO_NULL, HIO_NULL) <= -1) return -1;
	return 1; /* be greedy - keep reading in this same pass */
}

static int tdev_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	if (g_write_on_read) seq_put ('W');
	if (g_cw_n < MAX_CW)
	{
		g_cw_len[g_cw_n] = wrlen;
		g_cw_ctx[g_cw_n] = wrctx;
		g_cw_n++;
	}
	return 0;
}

static hio_dev_evcb_t tdev_evcb = { HIO_NULL, tdev_on_read, tdev_on_write };

/* ------------------------------------------------------------------ */

static tdev_t* make_tdev (int* peerfd)
{
	int sp[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) <= -1) return HIO_NULL;
	hio_makesyshndasync (g_hio, sp[0]);
	hio_makesyshndasync (g_hio, sp[1]);
	*peerfd = sp[1];
	return (tdev_t*)hio_dev_make(g_hio, HIO_SIZEOF(tdev_t), &tdev_mth, &tdev_evcb, &sp[0]);
}

static void nop_tmr (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job) { }

static void pump (void)
{
	hio_ntime_t after;
	hio_tmridx_t idx = HIO_TMRIDX_INVALID;
	HIO_INIT_NTIME (&after, 0, 20000000); /* 20ms */
	hio_schedtmrjobafter (g_hio, &after, nop_tmr, &idx, HIO_NULL);
	hio_exec (g_hio);
	if (idx != HIO_TMRIDX_INVALID) hio_deltmrjob (g_hio, idx);
}

/* read everything currently available on the peer end */
static int drain_peer (int fd, hio_uint8_t* buf, int max)
{
	int total = 0;
	for (;;)
	{
		ssize_t n = recv(fd, &buf[total], max - total, 0);
		if (n <= 0) break;
		total += n;
		if (total >= max) break;
	}
	return total;
}

/* ------------------------------------------------------------------ */

static void test_immediate_write (void)
{
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 2];
	int n, rc;

	obs_reset ();
	g_wr_chunk = 0;          /* no clamp: accept everything */
	g_wr_calls_left = 100;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 6); return; }

	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x11, HIO_NULL);
	OK (rc >= 0, "hio_dev_write() reports success for an immediate write");
	OK (g_cw_n == 0, "on_write is not invoked from inside hio_dev_write()");

	OK (rc == 0, "hio_dev_write() returns 0 for an immediate write, same as for an enqueued one");

	pump ();
	OK (g_cw_n == 1, "the deferred completion fires on the next loop iteration");
	OK (g_cw_n == 1 && g_cw_len[0] == PATLEN && g_cw_ctx[0] == (void*)0x11,
	    "the completion reports the original length and the caller's context");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0, "the peer receives the exact bytes submitted");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_short_write_no_queue (void)
{
	/* the write method accepts the request in two successful halves and
	 * never reports EAGAIN, so the whole transfer happens inside
	 * __dev_write()'s own loop with nothing ever reaching the write queue.
	 * this is the narrowest possible test of that loop's bookkeeping. */
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 2];
	int n;

	obs_reset ();
	g_wr_chunk = PATLEN / 2;  /* 50 bytes per call */
	g_wr_calls_left = 100;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 3); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x22, HIO_NULL);
	OK (g_wr_calls == 2, "a 100-byte write clamped to 50 takes exactly two write calls");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN, "both halves reach the peer");

	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0,
	    "a short write resumes from where it stopped rather than restarting");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_short_writes_then_queue (void)
{
	/* the realistic socket pattern: a couple of short writes get through,
	 * then the send buffer fills. whatever is left has to be enqueued from
	 * the point the loop stopped at, so this covers the handoff between
	 * __dev_write()'s cursor and the write queue rather than either alone. */
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 2];
	int n, rounds;

	obs_reset ();
	g_wr_chunk = 30;
	g_wr_calls_left = 2;      /* two 30-byte slices, then EAGAIN */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 4); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0xD1, HIO_NULL);
	OK (g_wr_calls == 3, "two slices went out and the third call reported EAGAIN");
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "the unwritten remainder is queued");

	g_wr_chunk = 0;
	for (rounds = 0; rounds < 5 && !HIO_WQ_IS_EMPTY(&dev->wq); rounds++)
	{
		g_wr_calls_left = 10;
		pump ();
	}
	OK (g_cw_n == 1 && g_cw_len[0] == PATLEN && g_cw_ctx[0] == (void*)0xD1,
	    "the request completes once with its original length");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0,
	    "the queued remainder picks up exactly where the short writes stopped");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_eagain_queues (void)
{
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 2];
	int n, rc;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;      /* refuse immediately */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 6); return; }

	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x33, HIO_NULL);
	OK (rc == 0, "hio_dev_write() returns 0 when the request has to be queued");
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "the request is on the device's write queue");
	OK ((dev->dev_cap & HIO_DEV_CAP_OUT_WATCHED) != 0, "queueing turns on output watching");
	OK (g_cw_n == 0, "no completion fires while the request is still queued");

	/* let it through */
	g_wr_calls_left = 100;
	pump ();

	OK (HIO_WQ_IS_EMPTY(&dev->wq), "the queue drains once the device accepts writes");
	OK (g_cw_n == 1 && g_cw_len[0] == PATLEN && g_cw_ctx[0] == (void*)0x33,
	    "the queued request completes with its original length and context");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0, "a queued request delivers the exact bytes submitted");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_queue_drains_in_slices (void)
{
	/* drain a queued request a few bytes at a time so the core has to
	 * compact the pending buffer repeatedly. the compaction is a memmove
	 * of the remainder to the front of the block, and getting it wrong
	 * corrupts the stream rather than truncating it. */
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 2];
	int n, rounds;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 3); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x44, HIO_NULL);
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "the whole request starts out queued");

	/* release 17 bytes per loop iteration */
	g_wr_chunk = 17;
	for (rounds = 0; rounds < 20 && !HIO_WQ_IS_EMPTY(&dev->wq); rounds++)
	{
		g_wr_calls_left = 1;
		pump ();
	}

	OK (HIO_WQ_IS_EMPTY(&dev->wq), "the queue drains fully across many partial writes");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0,
	    "repeated partial drains of a queued request preserve the byte stream");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_writev (void)
{
	tdev_t* dev;
	int peerfd;
	hio_iovec_t iov[3];
	hio_uint8_t got[PATLEN * 2];
	int n;

	obs_reset ();
	g_wr_chunk = 40;          /* forces the vector to be consumed over several calls */
	g_wr_calls_left = 100;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 3); return; }

	iov[0].iov_ptr = &g_pattern[0];  iov[0].iov_len = 30;
	iov[1].iov_ptr = &g_pattern[30]; iov[1].iov_len = 30;
	iov[2].iov_ptr = &g_pattern[60]; iov[2].iov_len = 40;

	hio_dev_writev ((hio_dev_t*)dev, iov, 3, (void*)0x55, HIO_NULL);
	pump ();

	OK (g_cw_n == 1 && g_cw_len[0] == PATLEN,
	    "a scattered write completes once with the total length");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN, "all vector segments reach the peer");
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0,
	    "a partially consumed vector resumes at the right offset");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_completion_ordering (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 100;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 2); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, 10, (void*)0xA1, HIO_NULL);
	hio_dev_write ((hio_dev_t*)dev, g_pattern, 20, (void*)0xA2, HIO_NULL);
	hio_dev_write ((hio_dev_t*)dev, g_pattern, 30, (void*)0xA3, HIO_NULL);
	OK (g_cw_n == 0, "completions stay deferred across several writes");

	pump ();
	OK (g_cw_n == 3 &&
	    g_cw_ctx[0] == (void*)0xA1 && g_cw_len[0] == 10 &&
	    g_cw_ctx[1] == (void*)0xA2 && g_cw_len[1] == 20 &&
	    g_cw_ctx[2] == (void*)0xA3 && g_cw_len[2] == 30,
	    "completions fire in submission order with matching contexts");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_zero_length_closes_output (void)
{
	tdev_t* dev;
	int peerfd;
	int rc;
	hio_uint8_t got[8];

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 100;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 4); return; }

	rc = hio_dev_write((hio_dev_t*)dev, HIO_NULL, 0, (void*)0x66, HIO_NULL);
	OK (rc >= 0, "a zero-length write is accepted");
	OK ((dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED) != 0, "a zero-length write closes the output side");

	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x67, HIO_NULL);
	OK (rc <= -1, "writing to a closed output fails");
	OK (hio_geterrnum(g_hio) == HIO_ENOCAPA, "the closed-output failure reports HIO_ENOCAPA");

	pump ();
	OK (drain_peer(peerfd, got, sizeof(got)) == 0, "the peer sees EOF rather than stray bytes");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_multiple_queued_requests (void)
{
	/* two requests sit on the queue together and drain back to back. the
	 * completion path decides whether a finished request was the
	 * zero-length "close the output" kind; if that decision looks at a
	 * counter that reaches zero for every completed request, the first
	 * drain closes the output and silently discards the rest of the
	 * queue. */
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 4];
	int n, rounds;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;      /* both requests queue */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 4); return; }

	hio_dev_write ((hio_dev_t*)dev, &g_pattern[0], 50, (void*)0xB1, HIO_NULL);
	hio_dev_write ((hio_dev_t*)dev, &g_pattern[50], 50, (void*)0xB2, HIO_NULL);
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "both requests are queued");

	g_wr_calls_left = 100;
	for (rounds = 0; rounds < 5 && !HIO_WQ_IS_EMPTY(&dev->wq); rounds++) pump ();

	OK (g_cw_n == 2 && g_cw_ctx[0] == (void*)0xB1 && g_cw_ctx[1] == (void*)0xB2,
	    "both queued requests complete, in submission order");
	OK ((dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED) == 0,
	    "completing an ordinary queued write does not close the device output");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0,
	    "both queued requests deliver their bytes");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_sendfile_across_events (void)
{
	/* a queued sendfile must remember how much of the range is left when
	 * it resumes on a later loop iteration. the file is deliberately
	 * larger than the request so that over-sending shows up as extra
	 * bytes rather than stopping harmlessly at end of file. */
	tdev_t* dev;
	int peerfd, filefd;
	char path[] = "/tmp/hio-t008-XXXXXX";
	hio_uint8_t big[PATLEN * 2];
	hio_uint8_t got[PATLEN * 4];
	int i, n, rounds;

	obs_reset ();

	for (i = 0; i < (int)sizeof(big); i++) big[i] = (hio_uint8_t)(i + 1);

	filefd = mkstemp(path);
	if (filefd < 0) { skip ("mkstemp failed", 4); return; }
	unlink (path);
	if (write(filefd, big, sizeof(big)) != (ssize_t)sizeof(big))
	{
		close (filefd);
		skip ("unable to populate the temp file", 4);
		return;
	}

	g_wr_chunk = 0;
	g_wr_calls_left = 0;      /* force the request onto the queue */

	dev = make_tdev(&peerfd);
	if (!dev) { close (filefd); skip ("device creation failed", 4); return; }

	hio_dev_sendfile ((hio_dev_t*)dev, filefd, 0, PATLEN, (void*)0x99);
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "the sendfile request starts out queued");

	/* release 40 bytes per iteration so the range spans several events */
	g_wr_chunk = 40;
	for (rounds = 0; rounds < 20 && !HIO_WQ_IS_EMPTY(&dev->wq); rounds++)
	{
		g_wr_calls_left = 1;
		pump ();
	}

	OK (HIO_WQ_IS_EMPTY(&dev->wq), "the queued sendfile drains");
	OK (g_cw_n == 1 && g_cw_len[0] == PATLEN && g_cw_ctx[0] == (void*)0x99,
	    "the sendfile completes once with its original length");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, big, PATLEN) == 0,
	    "a queued sendfile delivers exactly the requested byte range");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
	close (filefd);
}

static void test_queued_zero_length_closes_output (void)
{
	/* a zero-length request only reaches the queue when something is
	 * already ahead of it. that is the one path where the completion
	 * handler has to tell a finished ordinary write apart from a finished
	 * close-the-output request, so it needs covering separately from the
	 * immediate case above. */
	tdev_t* dev;
	int peerfd;
	hio_uint8_t got[PATLEN * 2];
	int n, rounds;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;      /* the first request has to queue */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 5); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0xC1, HIO_NULL);
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "the data request is queued");

	/* with the queue non-empty this one is queued behind it rather than
	 * being handled inline */
	hio_dev_write ((hio_dev_t*)dev, HIO_NULL, 0, (void*)0xC2, HIO_NULL);
	OK ((dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED) == 0,
	    "queueing a zero-length request does not close the output straight away");

	g_wr_calls_left = 100;
	for (rounds = 0; rounds < 5 && !HIO_WQ_IS_EMPTY(&dev->wq); rounds++) pump ();

	OK (g_cw_n == 2 && g_cw_ctx[0] == (void*)0xC1 && g_cw_ctx[1] == (void*)0xC2,
	    "the data request completes before the zero-length one");
	OK ((dev->dev_cap & HIO_DEV_CAP_OUT_CLOSED) != 0,
	    "the queued zero-length request closes the output once it is reached");

	n = drain_peer(peerfd, got, sizeof(got));
	OK (n == PATLEN && memcmp(got, g_pattern, PATLEN) == 0,
	    "the data ahead of the zero-length request is delivered intact");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_pending_writes_dropped_on_kill (void)
{
	/* killing a device with queued data must not fire completions for the
	 * requests it never sent, and must not leak the queue */
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0; /* everything queues */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 2); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x77, HIO_NULL);
	OK (!HIO_WQ_IS_EMPTY(&dev->wq), "the request is queued before the kill");

	hio_dev_kill ((hio_dev_t*)dev);
	OK (g_cw_n == 0, "killing a device does not complete its unsent writes");

	close (peerfd);
}

/* ------------------------------------------------------------------ */

/* keep the library's own debug logging out of the test output; genuine
 * errors still surface on stderr where the TAP driver captures them */
static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_ERROR | HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

/* the write-queue byte accounting and the soft cap. kept in its own file
 * purely to stop this one from growing past the point of being readable -
 * it uses the same stub device and observers as everything above. */
#include "t-008-wq.inc"

int main (void)
{
	hio_errinf_t errinf;
	int i;

	no_plan ();

	for (i = 0; i < PATLEN; i++) g_pattern[i] = (hio_uint8_t)(i + 1);

	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);

	quiet_logging (g_hio);
	if (!g_hio)
	{
		bail_out ("unable to open hio");
		return -1;
	}

	test_immediate_write ();
	test_short_write_no_queue ();
	test_short_writes_then_queue ();
	test_eagain_queues ();
	test_queue_drains_in_slices ();
	test_writev ();
	test_completion_ordering ();
	test_multiple_queued_requests ();
	test_sendfile_across_events ();
	test_zero_length_closes_output ();
	test_queued_zero_length_closes_output ();
	test_pending_writes_dropped_on_kill ();
	test_wq_size_accounting ();
	test_wq_size_released_on_kill ();
	test_wq_limit ();
	test_wq_limit_zero_is_unlimited ();
	test_completion_fires_within_read_loop ();
	test_read_buffer_size_option ();
	test_read_buffer_size_is_honoured ();
	test_read_buffer_resize_refused_in_callback ();

	hio_close (g_hio);
	return exit_status();
}
