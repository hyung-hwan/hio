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
