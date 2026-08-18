/*
 * timer subsystem tests.
 *
 * these exercise the binary min-heap in tmr.c without involving the
 * multiplexer or any device. time is supplied explicitly to
 * hio_firetmrjobs() so every case is deterministic and instantaneous.
 *
 * the invariant that matters most here is the idxptr contract: every
 * scheduled job writes its current heap position into *idxptr on every
 * sift, so a caller holding a hio_tmridx_t always has a usable index for
 * hio_deltmrjob()/hio_updtmrjob() no matter how the heap has been
 * reshuffled since insertion.
 */

#include <hio-prv.h>
#include "tap.h"

#define MAX_LOG 64

static hio_t* g_hio = HIO_NULL;
static int g_log[MAX_LOG];
static int g_log_len = 0;

static void log_reset (void)
{
	g_log_len = 0;
}

static int log_matches (const int* expected, int n)
{
	int i;
	if (g_log_len != n) return 0;
	for (i = 0; i < n; i++)
	{
		if (g_log[i] != expected[i]) return 0;
	}
	return 1;
}

static void h_record (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	if (g_log_len < MAX_LOG) g_log[g_log_len++] = (int)(hio_uintptr_t)job->ctx;
}

/* schedule a job firing at 'secs' with a tag recoverable through job->ctx */
static hio_tmridx_t sched_at (int secs, int tag, hio_tmridx_t* idxptr, hio_tmrjob_handler_t h)
{
	hio_tmrjob_t job;
	HIO_MEMSET (&job, 0, HIO_SIZEOF(job));
	job.ctx = (void*)(hio_uintptr_t)tag;
	HIO_INIT_NTIME (&job.when, secs, 0);
	job.handler = h? h: h_record;
	job.idxptr = idxptr;
	return hio_instmrjob(g_hio, &job);
}

static void fire_at (int secs)
{
	hio_ntime_t now;
	HIO_INIT_NTIME (&now, secs, 0);
	hio_firetmrjobs (g_hio, &now, HIO_NULL);
}

/* ------------------------------------------------------------------ */

static void test_ordering (void)
{
	static const int expected[] = { 1, 2, 3, 4, 5 };
	hio_tmridx_t idx[5];
	int i;

	log_reset ();

	/* insert out of order on purpose */
	sched_at (5, 5, &idx[0], HIO_NULL);
	sched_at (1, 1, &idx[1], HIO_NULL);
	sched_at (4, 4, &idx[2], HIO_NULL);
	sched_at (2, 2, &idx[3], HIO_NULL);
	sched_at (3, 3, &idx[4], HIO_NULL);

	OK (g_hio->tmr.size == 5, "5 jobs scheduled");

	/* nothing is due yet at t=0 */
	fire_at (0);
	OK (g_log_len == 0, "no job fires before its deadline");

	/* partially due */
	fire_at (2);
	OK (g_log_len == 2, "only the two due jobs fire at t=2");

	fire_at (10);
	OK (log_matches(expected, 5), "jobs fire in deadline order regardless of insertion order");
	OK (g_hio->tmr.size == 0, "heap is empty after all jobs fire");

	for (i = 0; i < 5; i++)
	{
		if (idx[i] != HIO_TMRIDX_INVALID) break;
	}
	OK (i == 5, "every idxptr is invalidated once its job has fired");
}

static void test_idxptr_tracking (void)
{
	/* the heap reshuffles aggressively during inserts. after every insert
	 * each live job's recorded index must still resolve back to that job. */
	enum { N = 24 };
	hio_tmridx_t idx[N];
	int deadlines[N];
	int i, bad;

	hio_cleartmrjobs (g_hio);
	log_reset ();

	/* a deliberately awkward deadline sequence - not sorted, with duplicates */
	for (i = 0; i < N; i++) deadlines[i] = ((i * 7) % 11) + 1;

	bad = 0;
	for (i = 0; i < N; i++)
	{
		int j;
		sched_at (deadlines[i], i, &idx[i], HIO_NULL);

		/* every previously inserted job must still be reachable at its recorded index */
		for (j = 0; j <= i; j++)
		{
			hio_tmrjob_t* job = hio_gettmrjob(g_hio, idx[j]);
			if (!job || (int)(hio_uintptr_t)job->ctx != j) { bad++; break; }
		}
	}
	OK (bad == 0, "idxptr stays correct for every live job across N inserts");

	/* now delete every third job and re-verify the survivors */
	bad = 0;
	for (i = 0; i < N; i += 3)
	{
		hio_deltmrjob (g_hio, idx[i]);
		if (idx[i] != HIO_TMRIDX_INVALID) bad++;
	}
	OK (bad == 0, "hio_deltmrjob() invalidates the caller's index holder");

	bad = 0;
	for (i = 0; i < N; i++)
	{
		hio_tmrjob_t* job;
		if (i % 3 == 0) continue; /* deleted above */
		job = hio_gettmrjob(g_hio, idx[i]);
		if (!job || (int)(hio_uintptr_t)job->ctx != i) bad++;
	}
	OK (bad == 0, "idxptr stays correct for surviving jobs after interleaved deletes");

	/* the surviving jobs must still come out in deadline order */
	fire_at (100);
	bad = 0;
	for (i = 1; i < g_log_len; i++)
	{
		if (deadlines[g_log[i - 1]] > deadlines[g_log[i]]) bad++;
	}
	OK (bad == 0, "surviving jobs still fire in deadline order");
	OK (g_log_len == N - ((N + 2) / 3), "exactly the undeleted jobs fired");
}

static void test_cancel (void)
{
	hio_tmridx_t a, b, c;
	static const int expected[] = { 1, 3 };

	hio_cleartmrjobs (g_hio);
	log_reset ();

	sched_at (1, 1, &a, HIO_NULL);
	sched_at (2, 2, &b, HIO_NULL);
	sched_at (3, 3, &c, HIO_NULL);

	/* cancel through the *current* index, which is what idxptr holds */
	hio_deltmrjob (g_hio, b);
	OK (b == HIO_TMRIDX_INVALID, "cancelled job's index holder is invalidated");
	OK (g_hio->tmr.size == 2, "heap shrank by exactly one");

	fire_at (10);
	OK (log_matches(expected, 2), "a cancelled job never fires");
}

static void test_update (void)
{
	hio_tmridx_t a, b;
	hio_tmrjob_t job;
	static const int expected[] = { 2 };

	hio_cleartmrjobs (g_hio);
	log_reset ();

	sched_at (10, 1, &a, HIO_NULL);
	sched_at (20, 2, &b, HIO_NULL);

	/* push job 1 out past job 2 */
	HIO_MEMSET (&job, 0, HIO_SIZEOF(job));
	job.ctx = (void*)(hio_uintptr_t)1;
	HIO_INIT_NTIME (&job.when, 30, 0);
	job.handler = h_record;
	job.idxptr = &a;
	hio_updtmrjob (g_hio, a, &job);

	OK (hio_gettmrjob(g_hio, a) != HIO_NULL, "index holder is still valid after update");
	OK ((int)(hio_uintptr_t)hio_gettmrjob(g_hio, a)->ctx == 1, "index holder still points at the same job");

	fire_at (25);
	OK (log_matches(expected, 1), "a job pushed later does not fire at its old deadline");

	fire_at (35);
	OK (g_log_len == 2 && g_log[1] == 1, "the rescheduled job fires at its new deadline");

	/* and the other direction: pull a job earlier */
	hio_cleartmrjobs (g_hio);
	log_reset ();
	sched_at (10, 1, &a, HIO_NULL);
	sched_at (20, 2, &b, HIO_NULL);

	HIO_MEMSET (&job, 0, HIO_SIZEOF(job));
	job.ctx = (void*)(hio_uintptr_t)2;
	HIO_INIT_NTIME (&job.when, 5, 0);
	job.handler = h_record;
	job.idxptr = &b;
	hio_updtmrjob (g_hio, b, &job);

	fire_at (7);
	OK (g_log_len == 1 && g_log[0] == 2, "a job pulled earlier fires at its new deadline");
}

/* ------------------------------------------------------------------ */

static hio_tmridx_t g_victim_idx;

static void h_cancel_other (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	h_record (hio, now, job);
	/* cancelling a still-pending job from inside a firing handler. the heap
	 * is mid-drain here, so this is the ordering most likely to corrupt it. */
	if (g_victim_idx != HIO_TMRIDX_INVALID) hio_deltmrjob (hio, g_victim_idx);
}

static void test_cancel_during_fire (void)
{
	hio_tmridx_t a, c;
	static const int expected[] = { 1, 3 };

	hio_cleartmrjobs (g_hio);
	log_reset ();

	sched_at (1, 1, &a, h_cancel_other);
	sched_at (2, 2, &g_victim_idx, HIO_NULL);
	sched_at (3, 3, &c, HIO_NULL);

	fire_at (10);
	OK (log_matches(expected, 2), "a job cancelled from inside a firing handler never fires");
	OK (g_victim_idx == HIO_TMRIDX_INVALID, "the cancelled job's holder is invalidated mid-drain");
	OK (g_hio->tmr.size == 0, "heap is consistent and empty after cancel-during-fire");
}

/* ------------------------------------------------------------------ */

static int g_rearm_left = 3;
static hio_tmridx_t g_rearm_idx;

static void h_rearm (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	h_record (hio, now, job);
	if (--g_rearm_left > 0)
	{
		/* re-arm from within our own handler. by this point the heap has
		 * already removed us, so this is a plain insert. */
		hio_tmrjob_t nj;
		HIO_MEMSET (&nj, 0, HIO_SIZEOF(nj));
		nj.ctx = job->ctx;
		nj.when = *now;
		nj.when.sec += 1;
		nj.handler = h_rearm;
		nj.idxptr = &g_rearm_idx;
		hio_instmrjob (hio, &nj);
	}
}

static void test_rearm_during_fire (void)
{
	hio_cleartmrjobs (g_hio);
	log_reset ();

	g_rearm_left = 3;
	g_rearm_idx = HIO_TMRIDX_INVALID;
	sched_at (1, 9, &g_rearm_idx, h_rearm);

	/* firing at t=1 must not drain the re-armed job scheduled for t=2 */
	fire_at (1);
	OK (g_log_len == 1, "a handler re-arming itself does not re-fire in the same drain");
	OK (g_hio->tmr.size == 1, "the re-armed job is pending");

	fire_at (2);
	OK (g_log_len == 2, "the re-armed job fires at its new deadline");

	fire_at (3);
	OK (g_log_len == 3, "re-arming works repeatedly");
	OK (g_hio->tmr.size == 0, "the handler stopped re-arming and the heap drained");
}

/* ------------------------------------------------------------------ */

static void test_tmout (void)
{
	hio_ntime_t now, tmout;
	hio_tmridx_t a, b;

	hio_cleartmrjobs (g_hio);
	log_reset ();

	OK (hio_gettmrtmout(g_hio, HIO_NULL, &tmout) == 0, "no timeout is reported with an empty heap");

	sched_at (30, 1, &a, HIO_NULL);
	sched_at (10, 2, &b, HIO_NULL);

	HIO_INIT_NTIME (&now, 4, 0);
	OK (hio_gettmrtmout(g_hio, &now, &tmout) == 1, "a timeout is reported when jobs are pending");
	OK (tmout.sec == 6 && tmout.nsec == 0, "the timeout is measured to the earliest deadline");

	/* an overdue job must clamp to zero rather than going negative */
	HIO_INIT_NTIME (&now, 50, 0);
	hio_gettmrtmout (g_hio, &now, &tmout);
	OK (tmout.sec == 0 && tmout.nsec == 0, "an overdue deadline clamps the timeout to zero");

	hio_cleartmrjobs (g_hio);
	OK (g_hio->tmr.size == 0, "hio_cleartmrjobs() empties the heap");
	OK (a == HIO_TMRIDX_INVALID && b == HIO_TMRIDX_INVALID, "hio_cleartmrjobs() invalidates every index holder");
}

/* ------------------------------------------------------------------ */

static void test_growth (void)
{
	/* the heap array starts at capacity 1 and doubles. walk well past
	 * several reallocations and confirm ordering survives them. */
	enum { N = 200 };
	hio_tmridx_t idx[N];
	int i, bad;

	hio_cleartmrjobs (g_hio);
	log_reset ();

	for (i = 0; i < N; i++) sched_at (N - i, i, &idx[i], HIO_NULL);
	OK (g_hio->tmr.size == N, "heap grew to hold every job");

	bad = 0;
	for (i = 0; i < N; i++)
	{
		hio_tmrjob_t* job = hio_gettmrjob(g_hio, idx[i]);
		if (!job || (int)(hio_uintptr_t)job->ctx != i) bad++;
	}
	OK (bad == 0, "idxptr survives heap reallocation");

	/* drain in slices so the log stays inside MAX_LOG */
	bad = 0;
	for (i = 1; i <= N; i++)
	{
		log_reset ();
		fire_at (i);
		if (g_log_len != 1 || g_log[0] != N - i) bad++;
	}
	OK (bad == 0, "200 jobs drain one per tick in exact deadline order");
	OK (g_hio->tmr.size == 0, "heap fully drained");
}

/* ------------------------------------------------------------------ */

/* keep the library's own debug logging out of the test output; genuine
 * errors still surface on stderr where the TAP driver captures them */
static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_ERROR | HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

int main (void)
{
	hio_errinf_t errinf;

	no_plan ();

	/* the timer needs no multiplexer, so open with logging only. tmrcapa of
	 * 1 forces the growth path to be exercised from the very first insert. */
	g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_LOG, 1, &errinf);
	quiet_logging (g_hio);
	if (!g_hio)
	{
		bail_out ("unable to open hio");
		return -1;
	}

	test_ordering ();
	test_idxptr_tracking ();
	test_cancel ();
	test_update ();
	test_cancel_during_fire ();
	test_rearm_during_fire ();
	test_tmout ();
	test_growth ();

	hio_close (g_hio);
	return exit_status();
}
