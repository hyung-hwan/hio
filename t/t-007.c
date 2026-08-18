/*
 * device lifecycle tests.
 *
 * these drive hio_dev_make()/halt()/kill() through a synthetic stream
 * device layered over one end of a socketpair. the socket exists only so
 * the device has a real handle for the multiplexer to watch; the method
 * table is a stub the test controls.
 *
 * lifetime bugs in an event loop normally show up as a use-after-free,
 * which is a crash rather than a test failure. to turn them into ordinary
 * assertions the test installs a quarantining allocator: freed blocks are
 * parked instead of released, so a device object stays readable after
 * hio_dev_kill() has freed it. each device carries a magic word that its
 * kill method stamps dead, and every event callback checks that word
 * before doing anything else. a callback reaching a killed device is then
 * a plain 'not ok' instead of a segfault.
 */

#include <hio-prv.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include "tap.h"

#define TDEV_LIVE 0x5A5A5A5Au
#define TDEV_DEAD 0xDEADDEADu

/* whether the multiplexer dispatches from a snapshot of events taken
 * before any callback runs. epoll and kqueue fill mux->revs with raw
 * device pointers and then walk it, so an entry can outlive the device it
 * names. poll and select instead walk their live registration tables,
 * which hio_sys_ctrlmux(DELETE) updates in place, so a killed device
 * simply disappears from the walk. the same selection order as
 * lib/sys-prv.h. */
#if defined(HAVE_SYS_EVENT_H) && defined(HAVE_KQUEUE) && defined(HAVE_KEVENT)
#	define MUX_DISPATCHES_FROM_SNAPSHOT 1
#elif defined(HAVE_SYS_EPOLL_H)
#	define MUX_DISPATCHES_FROM_SNAPSHOT 1
#else
#	define MUX_DISPATCHES_FROM_SNAPSHOT 0
#endif

/* ------------------------------------------------------------------ */
/* quarantining allocator                                             */
/* ------------------------------------------------------------------ */

#define QUARANTINE_MAX 8192
static void* g_quarantine[QUARANTINE_MAX];
static int g_quarantine_n = 0;
static int g_quarantine_overflow = 0;

static void* q_alloc (hio_mmgr_t* mmgr, hio_oow_t size) { return malloc(size); }
static void* q_realloc (hio_mmgr_t* mmgr, void* ptr, hio_oow_t size) { return realloc(ptr, size); }

static void q_free (hio_mmgr_t* mmgr, void* ptr)
{
	/* park the block rather than releasing it, so a stale pointer still
	 * reads back the contents it had at free time */
	if (g_quarantine_n < QUARANTINE_MAX) g_quarantine[g_quarantine_n++] = ptr;
	else { g_quarantine_overflow++; free (ptr); }
}

static hio_mmgr_t g_qmmgr = { q_alloc, q_realloc, q_free, HIO_NULL };

static void quarantine_drain (void)
{
	while (g_quarantine_n > 0) free (g_quarantine[--g_quarantine_n]);
}

/* ------------------------------------------------------------------ */
/* the synthetic device                                               */
/* ------------------------------------------------------------------ */

typedef struct tdev_t tdev_t;
struct tdev_t
{
	HIO_DEV_HEADER;

	unsigned int magic;
	int id;
	int fd;      /* the device's end of the socketpair */

	/* kill behaviour knobs */
	int kill_fail_left; /* fail this many non-forced kills before succeeding */

	/* what the read callback should do when it next fires */
	tdev_t* kill_on_read;   /* if set, hio_dev_kill() this device from on_read */
};

typedef struct tdev_make_t tdev_make_t;
struct tdev_make_t
{
	int id;
	int fd;
	int fail_make;
};

/* observations shared with the test body */
static int g_kill_calls = 0;
static int g_kill_max_force = -1;
static int g_make_calls = 0;
static int g_fail_before_make_calls = 0;
static int g_on_read_calls = 0;
static int g_on_read_eof = 0;
static int g_use_after_free = 0;   /* callbacks reaching a killed device */
static hio_t* g_hio = HIO_NULL;

static void obs_reset (void)
{
	g_kill_calls = 0;
	g_kill_max_force = -1;
	g_make_calls = 0;
	g_fail_before_make_calls = 0;
	g_on_read_calls = 0;
	g_on_read_eof = 0;
	g_use_after_free = 0;
}

/* keep the library's own debug logging out of the test output; genuine
 * errors still surface on stderr where the TAP driver captures them */
static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_ERROR | HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

/* returns 0 if the device is still live; records a violation otherwise */
static int check_live (hio_dev_t* dev, const char* where)
{
	tdev_t* t = (tdev_t*)dev;
	if (t->magic != TDEV_LIVE)
	{
		g_use_after_free++;
		diag (where);
		return -1;
	}
	return 0;
}

static int tdev_make (hio_dev_t* dev, void* ctx)
{
	tdev_t* t = (tdev_t*)dev;
	tdev_make_t* mi = (tdev_make_t*)ctx;

	g_make_calls++;
	if (mi->fail_make)
	{
		hio_seterrbfmt (dev->hio, HIO_EINVAL, "make refused by test");
		return -1;
	}

	t->magic = TDEV_LIVE;
	t->id = mi->id;
	t->fd = mi->fd;
	t->kill_fail_left = 0;
	t->kill_on_read = HIO_NULL;
	t->dev_cap |= HIO_DEV_CAP_STREAM;
	return 0;
}

static int tdev_kill (hio_dev_t* dev, int force)
{
	tdev_t* t = (tdev_t*)dev;

	g_kill_calls++;
	if (force > g_kill_max_force) g_kill_max_force = force;

	/* honour the contract in hio.h: only an unforced kill may refuse */
	if (force == 0 && t->kill_fail_left > 0)
	{
		t->kill_fail_left--;
		hio_seterrnum (dev->hio, HIO_EAGAIN);
		return -1;
	}

	t->magic = TDEV_DEAD;
	if (t->fd >= 0) { close (t->fd); t->fd = -1; }
	return 0;
}

static void tdev_fail_before_make (void* ctx)
{
	g_fail_before_make_calls++;
}

static hio_syshnd_t tdev_getsyshnd (hio_dev_t* dev)
{
	return (hio_syshnd_t)((tdev_t*)dev)->fd;
}

static int tdev_read (hio_dev_t* dev, void* buf, hio_iolen_t* len, hio_devaddr_t* srcaddr)
{
	tdev_t* t = (tdev_t*)dev;
	ssize_t n;

	if (check_live(dev, "read method on a killed device") <= -1) return -1;

	n = recv(t->fd, buf, *len, 0);
	if (n <= -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	*len = n;
	return 1;
}

static int tdev_write (hio_dev_t* dev, const void* data, hio_iolen_t* len, const hio_devaddr_t* dstaddr)
{
	tdev_t* t = (tdev_t*)dev;
	ssize_t n;

	if (check_live(dev, "write method on a killed device") <= -1) return -1;
	if (*len <= 0) { shutdown (t->fd, SHUT_WR); return 1; }

	n = send(t->fd, data, *len, 0);
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
	ssize_t n;

	if (check_live(dev, "writev method on a killed device") <= -1) return -1;

	n = writev(t->fd, (const struct iovec*)iov, *iovcnt);
	if (n <= -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
		hio_seterrwithsyserr (dev->hio, 0, errno);
		return -1;
	}
	*iovcnt = n;
	return 1;
}

static hio_dev_mth_t tdev_mth =
{
	tdev_make,
	tdev_kill,
	tdev_fail_before_make,
	tdev_getsyshnd,
	HIO_NULL,        /* issyshndbroken */
	HIO_NULL,        /* ioctl */
	tdev_read,
	tdev_write,
	tdev_writev,
	HIO_NULL         /* sendfile */
};

static int tdev_ready (hio_dev_t* dev, int events)
{
	/* first thing the loop touches on a dispatched device. checking the
	 * magic here catches a stale pointer before any further dereference,
	 * and returning 0 keeps the loop from attempting I/O on it. */
	tdev_t* t = (tdev_t*)dev;
	if (t->magic != TDEV_LIVE)
	{
		g_use_after_free++;
		diag ("event dispatched to a device that was already killed");
		return 0;
	}
	return 1;
}

static int tdev_on_read (hio_dev_t* dev, const void* data, hio_iolen_t len, const hio_devaddr_t* srcaddr)
{
	tdev_t* t = (tdev_t*)dev;

	if (check_live(dev, "on_read on a killed device") <= -1) return 0;

	g_on_read_calls++;
	if (len == 0) g_on_read_eof++;

	if (t->kill_on_read)
	{
		tdev_t* victim = t->kill_on_read;
		t->kill_on_read = HIO_NULL;
		victim->kill_on_read = HIO_NULL; /* don't let the victim retaliate */
		hio_dev_kill ((hio_dev_t*)victim);
	}

	return 0; /* one read per event; don't be greedy */
}

static int tdev_on_write (hio_dev_t* dev, hio_iolen_t wrlen, void* wrctx, const hio_devaddr_t* dstaddr)
{
	if (check_live(dev, "on_write on a killed device") <= -1) return 0;
	return 0;
}

static hio_dev_evcb_t tdev_evcb = { tdev_ready, tdev_on_read, tdev_on_write };

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static int make_pair (int* devfd, int* peerfd)
{
	int sp[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) <= -1) return -1;
	hio_makesyshndasync (g_hio, sp[0]);
	hio_makesyshndasync (g_hio, sp[1]);
	*devfd = sp[0];
	*peerfd = sp[1];
	return 0;
}

static tdev_t* make_tdev (int id, int* peerfd)
{
	tdev_make_t mi;
	int devfd;

	if (make_pair(&devfd, peerfd) <= -1) return HIO_NULL;
	mi.id = id;
	mi.fd = devfd;
	mi.fail_make = 0;
	return (tdev_t*)hio_dev_make(g_hio, HIO_SIZEOF(tdev_t), &tdev_mth, &tdev_evcb, &mi);
}

static void nop_tmr (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job) { }

/* run exactly one loop iteration, bounded by a short timer so an
 * iteration with nothing ready cannot stall the test for a full second */
static void pump (void)
{
	hio_ntime_t after;
	hio_tmridx_t idx = HIO_TMRIDX_INVALID;
	HIO_INIT_NTIME (&after, 0, 20000000); /* 20ms */
	hio_schedtmrjobafter (g_hio, &after, nop_tmr, &idx, HIO_NULL);
	hio_exec (g_hio);
	if (idx != HIO_TMRIDX_INVALID) hio_deltmrjob (g_hio, idx);
}

/* ------------------------------------------------------------------ */
/* tests                                                              */
/* ------------------------------------------------------------------ */

static void test_make_and_caps (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	dev = make_tdev(1, &peerfd);
	OK (dev != HIO_NULL, "hio_dev_make() succeeds");
	OK (g_make_calls == 1, "the make method is called exactly once");
	OK ((dev->dev_cap & HIO_DEV_CAP_ACTIVE) != 0, "a new device is marked active");
	OK ((dev->dev_cap & HIO_DEV_CAP_WATCH_STARTED) != 0, "a new device is registered with the multiplexer");
	OK ((dev->dev_cap & HIO_DEV_CAP_IN_WATCHED) != 0, "input watching is on by default");
	OK ((dev->dev_cap & HIO_DEV_CAP_OUT_WATCHED) == 0, "output watching is off until there is something to write");
	OK (g_kill_calls == 0, "the kill method is not called during make");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_make_failures (void)
{
	tdev_make_t mi;
	hio_dev_t* dev;
	int devfd, peerfd;

	/* dev_size below the header: rejected before the make method runs */
	obs_reset ();
	mi.id = 0; mi.fd = -1; mi.fail_make = 0;
	dev = hio_dev_make(g_hio, HIO_SIZEOF(hio_dev_t) - 1, &tdev_mth, &tdev_evcb, &mi);
	OK (dev == HIO_NULL, "hio_dev_make() rejects an undersized dev_size");
	OK (g_fail_before_make_calls == 1, "fail_before_make is invoked when make is never reached");
	OK (g_make_calls == 0, "the make method is not called for an undersized device");
	OK (hio_geterrnum(g_hio) == HIO_EINVAL, "the undersized case reports HIO_EINVAL");

	/* the make method itself refusing */
	obs_reset ();
	if (make_pair(&devfd, &peerfd) >= 0)
	{
		mi.id = 0; mi.fd = devfd; mi.fail_make = 1;
		dev = hio_dev_make(g_hio, HIO_SIZEOF(tdev_t), &tdev_mth, &tdev_evcb, &mi);
		OK (dev == HIO_NULL, "hio_dev_make() fails when the make method refuses");
		OK (g_make_calls == 1, "the make method ran before refusing");
		OK (g_kill_calls == 0, "the kill method is not called when make itself fails");
		OK (g_fail_before_make_calls == 0, "fail_before_make is not invoked once make has run");
		close (devfd);
		close (peerfd);
	}
	else skip ("socketpair unavailable", 4);
}

static void test_halt_is_deferred (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	dev = make_tdev(2, &peerfd);
	if (!dev) { skip ("device creation failed", 6); return; }

	hio_dev_halt ((hio_dev_t*)dev);
	OK (g_kill_calls == 0, "hio_dev_halt() does not destroy the device immediately");
	OK ((dev->dev_cap & HIO_DEV_CAP_ACTIVE) == 0, "a halted device is no longer active");
	OK ((dev->dev_cap & HIO_DEV_CAP_HALTED) != 0, "a halted device is marked halted");
	OK (dev->magic == TDEV_LIVE, "a halted device is still live");

	/* halting again must not queue a second destruction */
	hio_dev_halt ((hio_dev_t*)dev);

	pump ();
	OK (g_kill_calls == 1, "the loop destroys a halted device exactly once");
	OK (g_kill_max_force == 0, "an ordinary destruction is not forced");

	close (peerfd);
}

static void test_kill_is_immediate (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	dev = make_tdev(3, &peerfd);
	if (!dev) { skip ("device creation failed", 3); return; }

	hio_dev_kill ((hio_dev_t*)dev);
	OK (g_kill_calls == 1, "hio_dev_kill() destroys the device synchronously");
	OK (dev->magic == TDEV_DEAD, "the kill method ran before hio_dev_kill() returned");

	pump ();
	OK (g_kill_calls == 1, "a killed device is not destroyed a second time by the loop");

	close (peerfd);
}

static void test_kill_after_halt (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	dev = make_tdev(4, &peerfd);
	if (!dev) { skip ("device creation failed", 2); return; }

	hio_dev_halt ((hio_dev_t*)dev);
	hio_dev_kill ((hio_dev_t*)dev);
	OK (g_kill_calls == 1, "killing a halted device destroys it once");

	pump ();
	OK (g_kill_calls == 1, "the loop does not re-destroy a device killed while halted");

	close (peerfd);
}

static void test_zombie_escalation (void)
{
	/* a kill method that refuses turns the device into a zombie. hio_fini()
	 * must escalate the force argument rather than leaking or spinning. */
	hio_t* hio;
	hio_errinf_t errinf;
	hio_t* saved = g_hio;
	tdev_t* dev;
	int peerfd;

	obs_reset ();

	hio = hio_open(&g_qmmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!hio) { skip ("unable to open a second hio", 4); return; }

	quiet_logging (hio);
	g_hio = hio;

	dev = make_tdev(5, &peerfd);
	if (!dev) { g_hio = saved; hio_close (hio); skip ("device creation failed", 4); return; }

	dev->kill_fail_left = 99; /* refuse every unforced kill */
	hio_dev_kill ((hio_dev_t*)dev);

	OK (g_kill_calls == 1, "the refusing kill method was called once");
	OK ((dev->dev_cap & HIO_DEV_CAP_ZOMBIE) != 0, "a device whose kill refuses becomes a zombie");
	OK (dev->magic == TDEV_LIVE, "a zombie device has not been destroyed yet");

	hio_close (hio);
	OK (g_kill_max_force >= 1, "shutdown escalates to a forced kill rather than leaking the zombie");

	g_hio = saved;
	close (peerfd);
}

static void test_read_and_eof (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	dev = make_tdev(6, &peerfd);
	if (!dev) { skip ("device creation failed", 5); return; }

	if (write(peerfd, "hello", 5) != 5) { skip ("peer write failed", 5); goto done; }
	pump ();
	OK (g_on_read_calls == 1, "readable data delivers exactly one on_read");
	OK (g_on_read_eof == 0, "a data read is not reported as EOF");

	/* backpressure: with input watching off, pending data must not be delivered */
	hio_dev_read ((hio_dev_t*)dev, 0);
	if (write(peerfd, "more", 4) != 4) { skip ("peer write failed", 3); goto done; }
	pump ();
	OK (g_on_read_calls == 1, "no on_read is delivered while input watching is disabled");

	hio_dev_read ((hio_dev_t*)dev, 1);
	pump ();
	OK (g_on_read_calls == 2, "re-enabling input watching delivers the buffered data");

	/* EOF */
	close (peerfd);
	peerfd = -1;
	pump ();
	OK (g_on_read_eof == 1, "peer close is reported as a zero-length on_read");

done:
	if (peerfd >= 0) close (peerfd);
	if (dev->magic == TDEV_LIVE) hio_dev_kill ((hio_dev_t*)dev);
}

static void test_kill_peer_from_callback (void)
{
	/* two devices are made readable together so the multiplexer reports
	 * both in one batch. whichever is dispatched first kills the other,
	 * which means the second entry in the batch always refers to a device
	 * that has already been freed - regardless of the order the kernel
	 * chose. nothing in the dispatch loop revalidates that pointer.
	 *
	 * this only bites on a multiplexer that dispatches from a snapshot;
	 * see MUX_DISPATCHES_FROM_SNAPSHOT above. the test runs either way so
	 * that the safe backends stay covered against a regression. */
	tdev_t* a, * b;
	int peer_a = -1, peer_b = -1;

	obs_reset ();

	a = make_tdev(7, &peer_a);
	b = make_tdev(8, &peer_b);
	if (!a || !b) { skip ("device creation failed", 2); goto done; }

	a->kill_on_read = b;
	b->kill_on_read = a;

	if (write(peer_a, "x", 1) != 1 || write(peer_b, "y", 1) != 1)
	{
		skip ("peer write failed", 2);
		goto done;
	}

	pump ();

	OK (g_on_read_calls == 1, "the surviving device read once and killed its peer");

#if MUX_DISPATCHES_FROM_SNAPSHOT
	todo ("known issue C1: the mux dispatch loop does not revalidate device pointers within a batch", 1);
#endif
	OK (g_use_after_free == 0, "no event is dispatched to a device killed earlier in the same batch");

done:
	if (peer_a >= 0) close (peer_a);
	if (peer_b >= 0) close (peer_b);
	if (a && a->magic == TDEV_LIVE) hio_dev_kill ((hio_dev_t*)a);
	if (b && b->magic == TDEV_LIVE) hio_dev_kill ((hio_dev_t*)b);
}

/* ------------------------------------------------------------------ */
/* evcb stack                                                         */
/* ------------------------------------------------------------------ */

static int g_layer_read[2];

static int layer_x_on_read (hio_dev_t* dev, const void* p, hio_iolen_t l, const hio_devaddr_t* s)
{
	g_layer_read[0]++;
	return 0;
}

static int layer_y_on_read (hio_dev_t* dev, const void* p, hio_iolen_t l, const hio_devaddr_t* s)
{
	g_layer_read[1]++;
	return 0;
}

static int layer_on_write (hio_dev_t* dev, hio_iolen_t l, void* c, const hio_devaddr_t* a)
{
	return 0;
}

/* declared the way every device type in the library declares its own: one
 * file-scope table per role, shared by every device playing that role.
 * that sharing is the whole point of the pattern, and it is what any
 * stack-link stored inside the table has to survive. */
static hio_dev_evcb_t g_layer_x = { HIO_NULL, layer_x_on_read, layer_on_write };
static hio_dev_evcb_t g_layer_y = { HIO_NULL, layer_y_on_read, layer_on_write };

/* the stack links are per-device storage owned by the pusher */
static hio_dev_evcb_link_t g_link_a, g_link_b0, g_link_b1;

static void test_evcb_stack (void)
{
	tdev_t* a = HIO_NULL, * b = HIO_NULL;
	int pa = -1, pb = -1;

	obs_reset ();
	g_layer_read[0] = g_layer_read[1] = 0;

	a = make_tdev(11, &pa);
	b = make_tdev(12, &pb);
	if (!a || !b) { skip ("device creation failed", 7); goto done; }

	OK (a->dev_evcb == &tdev_evcb && b->dev_evcb == &tdev_evcb,
	    "both devices start on the base handler table");

	hio_dev_pushevcb ((hio_dev_t*)a, &g_link_a, &g_layer_x, a);
	OK (a->dev_evcb == &g_layer_x, "push swaps the device onto the pushed table");
	OK (hio_dev_getevcbctx((hio_dev_t*)a) == a, "the pushed layer's context is readable");

	/* b stacks two deep and reuses the very table a is already sitting on -
	 * the ordinary case when one handler set serves many connections */
	hio_dev_pushevcb ((hio_dev_t*)b, &g_link_b0, &g_layer_y, b);
	hio_dev_pushevcb ((hio_dev_t*)b, &g_link_b1, &g_layer_x, b);
	OK (b->dev_evcb == &g_layer_x, "a second device can stack two layers deep");
	OK (hio_dev_getevcbctx((hio_dev_t*)a) == a && hio_dev_getevcbctx((hio_dev_t*)b) == b,
	    "each device keeps its own context for the same shared table");

	hio_dev_popevcb ((hio_dev_t*)a);
	OK (a->dev_evcb == (hio_dev_evcb_t*)&tdev_evcb,
	    "popping restores a device's own previous table, not another device's");

	/* and behaviourally: input on 'a' must reach the base handler again */
	if (write(pa, "z", 1) == 1) pump ();
	OK (g_on_read_calls == 1 && g_layer_read[0] == 0 && g_layer_read[1] == 0,
	    "after popping, events are delivered to the base handler");

done:
	if (b) { while (hio_dev_popevcb((hio_dev_t*)b)) ; }
	if (pa >= 0) close (pa);
	if (pb >= 0) close (pb);
	if (a && a->magic == TDEV_LIVE) hio_dev_kill ((hio_dev_t*)a);
	if (b && b->magic == TDEV_LIVE) hio_dev_kill ((hio_dev_t*)b);
}

static void test_fini_kills_survivors (void)
{
	/* devices still active or halted at hio_close() time must be destroyed */
	hio_t* hio;
	hio_errinf_t errinf;
	hio_t* saved = g_hio;
	tdev_t* live, * halted;
	int pa = -1, pb = -1;

	obs_reset ();

	hio = hio_open(&g_qmmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
	if (!hio) { skip ("unable to open a second hio", 2); return; }

	quiet_logging (hio);
	g_hio = hio;

	live = make_tdev(9, &pa);
	halted = make_tdev(10, &pb);
	if (halted) hio_dev_halt ((hio_dev_t*)halted);

	hio_close (hio);
	OK (g_kill_calls == 2, "hio_close() destroys both active and halted devices");
	OK ((!live || live->magic == TDEV_DEAD) && (!halted || halted->magic == TDEV_DEAD),
	    "no device survives hio_close()");

	g_hio = saved;
	if (pa >= 0) close (pa);
	if (pb >= 0) close (pb);
}

/* ------------------------------------------------------------------ */

int main (void)
{
	hio_errinf_t errinf;

	no_plan ();

	g_hio = hio_open(&g_qmmgr, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);

	quiet_logging (g_hio);
	if (!g_hio)
	{
		bail_out ("unable to open hio");
		return -1;
	}

	test_make_and_caps ();
	test_make_failures ();
	test_halt_is_deferred ();
	test_kill_is_immediate ();
	test_kill_after_halt ();
	test_zombie_escalation ();
	test_read_and_eof ();
	test_kill_peer_from_callback ();
	test_evcb_stack ();
	test_fini_kills_survivors ();

	hio_close (g_hio);

	OK (g_quarantine_overflow == 0, "the quarantine held every freed block for the whole run");
	quarantine_drain ();

	return exit_status();
}
