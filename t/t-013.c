/*
 * L2 (link-layer) socket tests.
 *
 * HIO_DEV_SCK_PACKET and the two ARP types capture and inject raw frames. The
 * device type is one thing; how it is opened is not. On Linux it is an
 * AF_PACKET socket. On the BSDs there is no AF_PACKET at all - socket(AF_LINK,
 * SOCK_RAW, 0) is EAFNOSUPPORT even as root - so it is a /dev/bpf device
 * instead, with a filter standing in for the protocol argument. Nothing here
 * knows which, which is the point of the types.
 *
 * These need CAP_NET_RAW (or root). Rather than skipping on a developer
 * machine, the test re-executes itself inside a user+network namespace where
 * that is granted unprivileged, which also gives it an interface of its own to
 * make traffic on without touching the real network. Where that is not
 * available - any BSD, or a Linux with user namespaces off - it skips and says
 * how to get the coverage.
 */

#include <hio-sck.h>
#include <hio-prv.h>
#include "tap.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ldh [12] / jeq #type / ret #-1 / ret #0 - classic BPF, and the opcodes are
 * the same numbers on every system that has this, which is why they can be
 * written as numbers here without pulling in a platform header. */
#define BPF_OP_LDH_ABS  0x28
#define BPF_OP_JEQ_K    0x15
#define BPF_OP_RET_K    0x06

#define TEST_IFINDEX 1   /* loopback, the only interface in a fresh netns */

static hio_t* g_hio = HIO_NULL;
static int g_frames, g_timeout;
static hio_tmridx_t g_tmr = HIO_TMRIDX_INVALID;

static void quiet_logging (hio_t* hio)
{
	hio_bitmask_t mask = HIO_LOG_FATAL | HIO_LOG_ALL_TYPES;
	hio_setoption (hio, HIO_LOG_MASK, &mask);
}

static int on_read (hio_dev_sck_t* sck, const void* data, hio_iolen_t dlen, const hio_skad_t* srcaddr)
{
	if (dlen > 0) g_frames++;
	return 0;
}

static int on_write (hio_dev_sck_t* sck, hio_iolen_t wrlen, void* wrctx, const hio_skad_t* dstaddr)
{
	return 0;
}

static void on_deadline (hio_t* hio, const hio_ntime_t* now, hio_tmrjob_t* job)
{
	g_timeout = 1;
}

static void fill_make (hio_dev_sck_make_t* mi, hio_dev_sck_type_t type)
{
	HIO_MEMSET (mi, 0, HIO_SIZEOF(*mi));
	mi->type = type;
	mi->on_read = on_read;
	mi->on_write = on_write;
}

/* run the loop until 'want' frames have arrived, or two seconds pass */
static void run_until_frames (int want)
{
	hio_tmrjob_t j;

	g_timeout = 0;
	HIO_MEMSET (&j, 0, HIO_SIZEOF(j));
	hio_gettime (g_hio, &j.when);
	j.when.sec += 2;
	j.handler = on_deadline;
	j.idxptr = &g_tmr;
	g_tmr = hio_instmrjob(g_hio, &j);

	while (g_frames < want && !g_timeout)
	{
		if (hio_exec(g_hio) <= -1) break;
	}

	if (g_tmr != HIO_TMRIDX_INVALID) { hio_deltmrjob (g_hio, g_tmr); g_tmr = HIO_TMRIDX_INVALID; }
}

/* something for the capture device to see. ping is not guaranteed present, so
 * a plain udp send to a loopback port that nothing listens on is used instead:
 * it puts a frame on the wire either way. */
static void emit_traffic (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* u;
	hio_skad_t dst;
	int i;

	fill_make (&mi, HIO_DEV_SCK_UDP4);
	u = hio_dev_sck_make(g_hio, 0, &mi);
	if (!u) return;

	if (hio_bcstrtoskad(g_hio, "127.0.0.1:9", &dst) >= 0)
	{
		for (i = 0; i < 4; i++) hio_dev_sck_write (u, "x", 1, HIO_NULL, &dst);
	}

	hio_dev_sck_halt (u);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */

static void test_packet_device (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_t* d;

	fill_make (&mi, HIO_DEV_SCK_PACKET);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { FAIL ("a packet device can be made"); skip ("no packet device", 4); return; }
	PASS ("a packet device can be made");

	/* a frame must arrive whole or not at all, so the device asks for a read
	 * buffer that can hold the largest one */
	OK (d->dev_rdmin == HIO_DGRAM_READ_BUFFER_SIZE,
	    "and asks for a whole-frame read buffer");
	OK (!(d->dev_cap & HIO_DEV_CAP_STREAM),
	    "and is not a stream device");

	/* the interface is named by index, which is the one part of an L2 address
	 * that means the same thing on both systems */
	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	hio_skad_init_for_eth (&bi.localaddr, TEST_IFINDEX, HIO_NULL);
	OK (hio_dev_sck_bind(d, &bi) == 0, "and binds to an interface by index");

	g_frames = 0;
	emit_traffic ();
	run_until_frames (1);
	OK (g_frames > 0, "and captures frames from it");

	hio_dev_sck_halt (d);
	hio_exec (g_hio);
}

static void test_arp_devices (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_t* d;
	int i;
	static const hio_dev_sck_type_t types[2] = { HIO_DEV_SCK_ARP, HIO_DEV_SCK_ARP_DGRAM };
	static const char* const names[2] = { "arp", "arp-dgram" };

	for (i = 0; i < 2; i++)
	{
		fill_make (&mi, types[i]);
		d = hio_dev_sck_make(g_hio, 0, &mi);
		if (!d) { FAIL ("an arp device can be made"); continue; }

		HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
		hio_skad_init_for_eth (&bi.localaddr, TEST_IFINDEX, HIO_NULL);
		OK (hio_dev_sck_bind(d, &bi) == 0, "an arp device binds to an interface");

		/* narrowed to arp, so the udp traffic above must not reach it. on the
		 * bsds that narrowing is a bpf filter the bind installed; on linux it
		 * is the protocol the socket was opened with. either way the type
		 * means the same thing. */
		g_frames = 0;
		emit_traffic ();
		run_until_frames (1);
		OK (g_frames == 0, "and an arp device sees no non-arp traffic");

		hio_dev_sck_halt (d);
		hio_exec (g_hio);
		(void)names[i];
	}
}

static void test_filter (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_bind_t bi;
	hio_dev_sck_t* d;
	hio_bpf_insn_t drop_all[1];
	int filtered;

	fill_make (&mi, HIO_DEV_SCK_PACKET);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { skip ("no packet device", 3); return; }

	HIO_MEMSET (&bi, 0, HIO_SIZEOF(bi));
	hio_skad_init_for_eth (&bi.localaddr, TEST_IFINDEX, HIO_NULL);
	if (hio_dev_sck_bind(d, &bi) <= -1) { skip ("bind failed", 3); hio_dev_sck_kill(d); hio_exec(g_hio); return; }

	/* 'ret #0' - keep nothing */
	drop_all[0].code = BPF_OP_RET_K;
	drop_all[0].jt = 0;
	drop_all[0].jf = 0;
	drop_all[0].k = 0;

	if (hio_dev_sck_setfilter(d, drop_all, 1) <= -1)
	{
		if (hio_geterrnum(g_hio) == HIO_ENOIMPL) skip ("no packet filtering on this system", 3);
		else { FAIL ("a filter can be attached"); skip ("setfilter failed", 2); }
		hio_dev_sck_halt (d);
		hio_exec (g_hio);
		return;
	}
	PASS ("a filter can be attached");

	g_frames = 0;
	emit_traffic ();
	run_until_frames (1);
	filtered = (g_frames == 0);
	OK (filtered, "and a drop-everything filter drops everything");

	/* and taking it away lets traffic through again, which is what says the
	 * silence above was the filter and not an absence of traffic */
	if (hio_dev_sck_clearfilter(d) <= -1) FAIL ("and clearing it lets traffic through again");
	else
	{
		g_frames = 0;
		emit_traffic ();
		run_until_frames (1);
		OK (g_frames > 0, "and clearing it lets traffic through again");
	}

	hio_dev_sck_halt (d);
	hio_exec (g_hio);
}

static void test_filter_refusals (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* d;
	hio_bpf_insn_t one[1];

	one[0].code = BPF_OP_RET_K; one[0].jt = 0; one[0].jf = 0; one[0].k = 0;

	fill_make (&mi, HIO_DEV_SCK_PACKET);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) { skip ("no packet device", 2); return; }

	OK (hio_dev_sck_setfilter(d, one, 0) <= -1 && hio_geterrnum(g_hio) == HIO_EINVAL,
	    "an empty filter program is refused");
	OK (hio_dev_sck_setfilter(d, HIO_NULL, 1) <= -1 && hio_geterrnum(g_hio) == HIO_EINVAL,
	    "and so is a null one");

	hio_dev_sck_halt (d);
	hio_exec (g_hio);
}

/* ------------------------------------------------------------------ */

/* can an L2 device be opened at all? asked with the type the rest of the file
 * uses, so the answer covers whichever implementation is behind it. */
static int l2_permitted (void)
{
	hio_dev_sck_make_t mi;
	hio_dev_sck_t* d;

	fill_make (&mi, HIO_DEV_SCK_PACKET);
	d = hio_dev_sck_make(g_hio, 0, &mi);
	if (!d) return 0;
	hio_dev_sck_kill (d);
	hio_exec (g_hio);
	return 1;
}

int main (int argc, char* argv[])
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

	if (!l2_permitted())
	{
		/* try once more inside a namespace that grants it, unless this already
		 * is that second attempt. HIO_T013_NESTED keeps it from recursing. */
		if (!getenv("HIO_T013_NESTED"))
		{
			hio_close (g_hio);
			setenv ("HIO_T013_NESTED", "1", 1);
			/* -r maps this user to root in a new user namespace, which carries
			 * CAP_NET_RAW; -n gives a network namespace with a loopback of its
			 * own, so the traffic below cannot touch the real network. */
			execlp ("unshare", "unshare", "-r", "-n", "--",
			        "/bin/sh", "-c", "ip link set lo up 2>/dev/null; exec \"$0\"", argv[0], (char*)HIO_NULL);
			/* exec failed - no unshare, or not permitted */
			g_hio = hio_open(HIO_NULL, 0, HIO_NULL, HIO_FEATURE_ALL, 16, &errinf);
			if (!g_hio) { no_plan(); bail_out("unable to reopen hio"); return -1; }
			quiet_logging (g_hio);
		}

		skip_all ("l2 sockets need CAP_NET_RAW - run as root, or where 'unshare -r -n' is permitted");
		hio_close (g_hio);
		return exit_status();
	}

	no_plan ();

	test_packet_device ();
	test_arp_devices ();
	test_filter ();
	test_filter_refusals ();

	hio_close (g_hio);
	return exit_status();
}
