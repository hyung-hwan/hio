/* write-queue byte accounting and the soft cap.
 * included from t-008.c - see the note at the include site. */

static void test_wq_size_accounting (void)
{
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;      /* refuse everything, so it all queues */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 6); return; }

	OK (hio_dev_getwqsize((hio_dev_t*)dev) == 0, "a fresh device reports an empty write queue");

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x81, HIO_NULL);
	OK (hio_dev_getwqsize((hio_dev_t*)dev) == (hio_oow_t)PATLEN,
	    "a queued request counts its bytes, not itself");

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x82, HIO_NULL);
	OK (hio_dev_getwqsize((hio_dev_t*)dev) == (hio_oow_t)(PATLEN * 2),
	    "a second request adds to the byte count");

	/* let exactly one slice through so a partially drained request is visible */
	g_wr_chunk = 30;
	g_wr_calls_left = 1;
	pump ();
	OK (hio_dev_getwqsize((hio_dev_t*)dev) == (hio_oow_t)(PATLEN * 2 - 30),
	    "bytes handed to the transport stop counting immediately, mid-request");

	g_wr_chunk = 0;
	g_wr_calls_left = 100;
	pump ();
	OK (hio_dev_getwqsize((hio_dev_t*)dev) == 0, "a fully drained queue is back to zero");
	OK (g_cw_n == 2, "both requests completed");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_wq_size_released_on_kill (void)
{
	/* the count lives on the device, so a kill only has to not corrupt it on
	 * the way out - but a request dropped with bytes still queued is the one
	 * path where the decrement is not paired with a drain. */
	tdev_t* dev;
	int peerfd;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 2); return; }

	hio_dev_write ((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x83, HIO_NULL);
	OK (hio_dev_getwqsize((hio_dev_t*)dev) == (hio_oow_t)PATLEN, "the request is queued and counted");

	hio_dev_kill ((hio_dev_t*)dev);
	OK (g_cw_n == 0, "the dropped request fires no completion");

	close (peerfd);
}

static void test_wq_limit (void)
{
	tdev_t* dev;
	int peerfd;
	int rc;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;      /* refuse everything, so it all queues */

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 8); return; }

	OK (hio_dev_getwqlimit((hio_dev_t*)dev) == 0, "a device is uncapped by default");

	hio_dev_setwqlimit ((hio_dev_t*)dev, PATLEN + PATLEN / 2);
	OK (hio_dev_getwqlimit((hio_dev_t*)dev) == (hio_oow_t)(PATLEN + PATLEN / 2), "the cap reads back");

	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x84, HIO_NULL);
	OK (rc == 0, "a write below the cap is accepted");

	/* now at PATLEN of a PATLEN*1.5 cap - still under, so this one goes on
	 * whole even though it takes the queue past the cap. that is what makes
	 * the cap soft, and it is what keeps a stream from being cut in half. */
	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x85, HIO_NULL);
	OK (rc == 0, "a request that starts under the cap is accepted whole");
	OK (hio_dev_getwqsize((hio_dev_t*)dev) == (hio_oow_t)(PATLEN * 2),
	    "which can leave the queue above the cap");

	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x86, HIO_NULL);
	OK (rc <= -1 && hio_geterrnum(g_hio) == HIO_EBUFFULL,
	    "a write attempted while at or above the cap fails with HIO_EBUFFULL");
	OK (g_cw_n == 0, "the refused request fires no completion");

	/* drain and confirm the device accepts writes again */
	g_wr_calls_left = 100;
	pump ();
	rc = hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, (void*)0x87, HIO_NULL);
	OK (rc == 0, "the device accepts writes again once the queue drains");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

static void test_wq_limit_zero_is_unlimited (void)
{
	tdev_t* dev;
	int peerfd;
	int i, all_ok = 1;

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 0;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 1); return; }

	/* the default. every release before the cap existed behaved this way and
	 * still has to. */
	for (i = 0; i < 64; i++)
	{
		if (hio_dev_write((hio_dev_t*)dev, g_pattern, PATLEN, HIO_NULL, HIO_NULL) <= -1) all_ok = 0;
	}
	OK (all_ok && hio_dev_getwqsize((hio_dev_t*)dev) == (hio_oow_t)(PATLEN * 64),
	    "an uncapped device queues without limit");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

/* ------------------------------------------------------------------ */
/* completion firing inside the greedy read loop.
 *
 * when on_read asks for another read, the loop fires the completions queued
 * so far before calling on_read again - otherwise a write started from inside
 * on_read would be reported after a later on_read, which is the reordering
 * the comment in handle_event() warns about. the firing is per device, so an
 * unrelated connection's callbacks are not dragged into this device's pass. */

static void test_completion_fires_within_read_loop (void)
{
	tdev_t* dev;
	int peerfd;
	hio_uint8_t chunk[30];

	obs_reset ();
	g_wr_chunk = 0;
	g_wr_calls_left = 100;   /* every write completes at once */
	g_rd_chunk = 10;         /* so 30 octets take three read iterations */
	g_write_on_read = 1;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 3); return; }

	HIO_MEMSET (chunk, 'x', HIO_SIZEOF(chunk));
	if (write(peerfd, chunk, HIO_SIZEOF(chunk)) != (ssize_t)HIO_SIZEOF(chunk))
	{
		skip ("peer write failed", 3);
		hio_dev_kill ((hio_dev_t*)dev);
		close (peerfd);
		return;
	}

	pump ();

	/* iteration 1 reads and writes; iteration 2 fires that write's completion
	 * before reading again. hence R, then W and R alternating. */
	OK (g_seq_n >= 3, "the greedy read loop ran more than once");
	OK (g_seq[0] == 'R', "no completion is fired before the first read");
	OK (strncmp(g_seq, "RWRWR", 5) == 0,
	    "each iteration's write completion is reported before the next on_read");

	g_write_on_read = 0;
	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}

/* ------------------------------------------------------------------ */
/* the shared read buffer's configurable size.
 *
 * every device reads through one buffer owned by the loop. its size decides
 * how much a single read can take, so it is the ceiling on read size for a
 * bulk transfer - and it is memory held per loop, which matters when a
 * process runs one loop per thread. */

static void test_read_buffer_size_option (void)
{
	hio_oow_t capa = 0, want;
	int rc;

	obs_reset ();

	OK (hio_getoption(g_hio, HIO_READ_BUFFER_SIZE, &capa) == 0 && capa == HIO_DFL_READ_BUFFER_SIZE,
	    "the read buffer starts at HIO_DFL_READ_BUFFER_SIZE");

	want = HIO_MIN_READ_BUFFER_SIZE - 1;
	rc = hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &want);
	OK (rc <= -1 && hio_geterrnum(g_hio) == HIO_EINVAL,
	    "a size below HIO_MIN_READ_BUFFER_SIZE is refused with HIO_EINVAL");

	OK (hio_getoption(g_hio, HIO_READ_BUFFER_SIZE, &capa) == 0 && capa == HIO_DFL_READ_BUFFER_SIZE,
	    "a refused set leaves the existing buffer alone");

	want = HIO_DFL_READ_BUFFER_SIZE * 4;
	OK (hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &want) == 0 &&
	    hio_getoption(g_hio, HIO_READ_BUFFER_SIZE, &capa) == 0 && capa == want,
	    "a larger size is accepted and reads back");

	want = HIO_DFL_READ_BUFFER_SIZE;
	OK (hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &want) == 0, "and it can be set back");
}

static void test_read_buffer_size_is_honoured (void)
{
	/* the size has to reach the read path, not merely be stored. the stub
	 * device records the length the core offers it. */
	tdev_t* dev;
	int peerfd;
	hio_oow_t want, restore = HIO_DFL_READ_BUFFER_SIZE;
	hio_uint8_t buf[4096];

	obs_reset ();
	g_wr_calls_left = 100;

	want = HIO_MIN_READ_BUFFER_SIZE;   /* the smallest the core allows */
	if (hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &want) <= -1) { skip ("cannot set the buffer size", 2); return; }

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 2); hio_setoption(g_hio, HIO_READ_BUFFER_SIZE, &restore); return; }

	HIO_MEMSET (buf, 'z', HIO_SIZEOF(buf));
	if (write(peerfd, buf, HIO_SIZEOF(buf)) != (ssize_t)HIO_SIZEOF(buf))
	{
		skip ("peer write failed", 2);
		hio_dev_kill ((hio_dev_t*)dev);
		close (peerfd);
		hio_setoption (g_hio, HIO_READ_BUFFER_SIZE, &restore);
		return;
	}

	pump ();

	OK (g_rd_calls > 0, "the device was read from");
	OK (g_rd_offered == (hio_iolen_t)HIO_MIN_READ_BUFFER_SIZE,
	    "the core offers the read method exactly the configured buffer size");

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
	hio_setoption (g_hio, HIO_READ_BUFFER_SIZE, &restore);
}

static void test_read_buffer_resize_refused_in_callback (void)
{
	/* on_read receives a pointer into the buffer. resizing it there would
	 * leave that pointer dangling, so the attempt has to fail. */
	tdev_t* dev;
	int peerfd;
	hio_uint8_t buf[64];

	obs_reset ();
	g_wr_calls_left = 100;
	g_setopt_from_on_read = 1;

	dev = make_tdev(&peerfd);
	if (!dev) { skip ("device creation failed", 2); return; }

	HIO_MEMSET (buf, 'q', HIO_SIZEOF(buf));
	if (write(peerfd, buf, HIO_SIZEOF(buf)) != (ssize_t)HIO_SIZEOF(buf))
	{
		skip ("peer write failed", 2);
		hio_dev_kill ((hio_dev_t*)dev);
		close (peerfd);
		return;
	}

	pump ();

	OK (g_setopt_rc <= -1 && g_setopt_errnum == HIO_EBUSY,
	    "resizing the read buffer from inside on_read fails with HIO_EBUSY");
	/*
	OK (hio_getoption(g_hio, HIO_READ_BUFFER_SIZE, &(hio_oow_t){0}) == 0,
	    "and the option is still readable afterwards");
	*/
	{
	hio_oow_t tmp;
	OK (hio_getoption(g_hio, HIO_READ_BUFFER_SIZE, &tmp) == 0,
	    "and the option is still readable afterwards");
	}

	hio_dev_kill ((hio_dev_t*)dev);
	close (peerfd);
}
