/*
 * $Id$
 *
    Copyright (c) 2016-2020 Chung, Hyung-Hwan. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WAfRRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <mio.h>
#include <mio-utl.h>
#include <mio-sck.h>
#include <mio-pro.h>
#include <mio-pipe.h>
#include <mio-thr.h>
#include <mio-dns.h>
#include <mio-nwif.h>
#include <mio-http.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <assert.h>

#if defined(HAVE_OPENSSL_SSL_H) && defined(HAVE_SSL)
#	include <openssl/ssl.h>
#	if defined(HAVE_OPENSSL_ERR_H)
#		include <openssl/err.h>
#	endif
#	if defined(HAVE_OPENSSL_ENGINE_H)
#		include <openssl/engine.h>
#	endif
#	define USE_SSL
#endif

/* ========================================================================= */

struct mmgr_stat_t
{
	mio_oow_t total_count;
};

typedef struct mmgr_stat_t mmgr_stat_t;

static mmgr_stat_t mmgr_stat;

static void* mmgr_alloc (mio_mmgr_t* mmgr, mio_oow_t size)
{
	void* x;

	if (((mmgr_stat_t*)mmgr->ctx)->total_count > 3000)
	{
printf ("CRITICAL ERROR ---> too many heap chunks...\n");
		return MIO_NULL;
	}

	x = malloc (size);
	if (x)
	{
		((mmgr_stat_t*)mmgr->ctx)->total_count++;
		/*printf ("MMGR total_count INCed to %d => %p\n", ((mmgr_stat_t*)mmgr->ctx)->total_count, x);*/
	}
	return x;
}

static void* mmgr_realloc (mio_mmgr_t* mmgr, void* ptr, mio_oow_t size)
{
	void* x;

	x = realloc (ptr, size);
	if (x && !ptr) 
	{
		((mmgr_stat_t*)mmgr->ctx)->total_count++;
		/*printf ("MMGR total_count INCed to %d => %p\n", ((mmgr_stat_t*)mmgr->ctx)->total_count, x);*/
	}
	return x;
}

static void mmgr_free (mio_mmgr_t* mmgr, void* ptr)
{
	((mmgr_stat_t*)mmgr->ctx)->total_count--;
	/*printf ("MMGR total_count DECed to %d => %p\n", ((mmgr_stat_t*)mmgr->ctx)->total_count, ptr);*/
	return free (ptr);
}


static mio_mmgr_t mmgr = 
{
	mmgr_alloc,
	mmgr_realloc,
	mmgr_free,
	&mmgr_stat
};

/* ========================================================================= */

#if defined(USE_SSL)
static void cleanup_openssl ()
{
	/* ERR_remove_state() should be called for each thread if the application is thread */
	ERR_remove_state (0);
#if defined(HAVE_ENGINE_CLEANUP)
	ENGINE_cleanup ();
#endif
	ERR_free_strings ();
	EVP_cleanup ();
#if defined(HAVE_CRYPTO_CLEANUP_ALL_EX_DATA)
	CRYPTO_cleanup_all_ex_data ();
#endif
}
#endif

struct tcp_server_t
{
	int tally;
};
typedef struct tcp_server_t tcp_server_t;

static void tcp_sck_on_disconnect (mio_dev_sck_t* tcp)
{
	switch (MIO_DEV_SCK_GET_PROGRESS(tcp))
	{
		case MIO_DEV_SCK_CONNECTING:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_CONNECTING_SSL:
			MIO_INFO1 (tcp->mio, "OUTGOING SESSION DISCONNECTED - FAILED TO SSL-CONNECT (%d) TO REMOTE SERVER\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_LISTENING:
			MIO_INFO1 (tcp->mio, "SHUTTING DOWN THE SERVER SOCKET(%d)...\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_CONNECTED:
			MIO_INFO1 (tcp->mio, "OUTGOING CLIENT CONNECTION GOT TORN DOWN(%d).......\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTING_SSL:
			MIO_INFO1 (tcp->mio, "INCOMING SSL-ACCEPT GOT DISCONNECTED(%d) ....\n", (int)tcp->hnd);
			break;

		case MIO_DEV_SCK_ACCEPTED:
			MIO_INFO1 (tcp->mio, "INCOMING CLIENT BEING SERVED GOT DISCONNECTED(%d).......\n", (int)tcp->hnd);
			break;

		default:
			MIO_INFO2 (tcp->mio, "SOCKET DEVICE DISCONNECTED (%d - %x)\n", (int)tcp->hnd, (unsigned int)tcp->state);
			break;
	}
}

static void tcp_sck_on_connect (mio_dev_sck_t* tcp)
{
	mio_bch_t buf1[128], buf2[128];
	mio_bch_t k[50000];
	mio_iovec_t iov[] =
	{
		{ "hello", 5 },
		{ "world", 5 },
		{ k, MIO_COUNTOF(k) },
		{ "mio test", 8 },
		{ k, MIO_COUNTOF(k) }
	};
	int i;

	
	mio_skadtobcstr (tcp->mio, &tcp->localaddr, buf1, MIO_COUNTOF(buf1), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT);
	mio_skadtobcstr (tcp->mio, &tcp->remoteaddr, buf2, MIO_COUNTOF(buf2), MIO_SKAD_TO_BCSTR_ADDR | MIO_SKAD_TO_BCSTR_PORT);

	if (tcp->state & MIO_DEV_SCK_CONNECTED)
	{
		MIO_INFO3 (tcp->mio, "DEVICE connected to a remote server... LOCAL %hs REMOTE %hs SCK: %d\n", buf1, buf2, tcp->hnd);
	}
	else if (tcp->state & MIO_DEV_SCK_ACCEPTED)
	{
		MIO_INFO3 (tcp->mio, "DEVICE accepted client device... .LOCAL %hs REMOTE %hs  SCK: %d\n", buf1, buf2, tcp->hnd);
	}

	for (i = 0; i < MIO_COUNTOF(k); i++) k[i]  = 'A' + (i % 26);

/*
	{
	int sndbuf = 2000;
	mio_dev_sck_setsockopt(tcp, SOL_SOCKET, SO_SNDBUF, &sndbuf, MIO_SIZEOF(sndbuf));
	}
*/

	if (mio_dev_sck_writev(tcp, iov, MIO_COUNTOF(iov), MIO_NULL, MIO_NULL) <= -1)
	{
		mio_dev_sck_halt (tcp);
	}
}

static int tcp_sck_on_write (mio_dev_sck_t* tcp, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	tcp_server_t* ts;
	mio_ntime_t tmout;

	if (wrlen <= -1)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> SEDING TIMED OUT...........\n", (int)tcp->hnd);
		mio_dev_sck_halt (tcp);
	}
	else
	{
		ts = (tcp_server_t*)(tcp + 1);
		if (wrlen == 0)
		{
			MIO_INFO1 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> CLOSED WRITING END\n", (int)tcp->hnd);
		}
		else
		{
			MIO_INFO3 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> SENT MESSAGE %d of length %ld\n", (int)tcp->hnd, ts->tally, (long int)wrlen);
		}

		ts->tally++;
	//	if (ts->tally >= 2) mio_dev_sck_halt (tcp);

		
		MIO_INIT_NTIME (&tmout, 5, 0);
		//mio_dev_sck_read (tcp, 1);

		MIO_INFO3 (tcp->mio, "TCP_SCK_ON_WRITE(%d) >>> REQUESTING to READ with timeout of %ld.%08ld\n", (int)tcp->hnd, (long int)tmout.sec, (long int)tmout.nsec);
		mio_dev_sck_timedread (tcp, 1, &tmout);
	}
	return 0;
}

static int tcp_sck_on_read (mio_dev_sck_t* tcp, const void* buf, mio_iolen_t len, const mio_skad_t* srcaddr)
{
	int n;

	if (len <= -1)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: TIMED OUT...\n", (int)tcp->hnd);
		mio_dev_sck_halt (tcp);
		return 0;
	}
	else if (len <= 0)
	{
		MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) STREAM DEVICE: EOF RECEIVED...\n", (int)tcp->hnd);
		/* no outstanding request. but EOF */
		mio_dev_sck_halt (tcp);
		return 0;
	}

	MIO_INFO2 (tcp->mio, "TCP_SCK_ON_READ(%d) - received %d bytes\n", (int)tcp->hnd, (int)len);

	{
		mio_ntime_t tmout;

		static char a ='A';
		static char xxx[1000000];
		memset (xxx, a++ , MIO_SIZEOF(xxx));

		MIO_INFO2 (tcp->mio, "TCP_SCK_ON_READ(%d) >>> REQUESTING to write data of %d bytes\n", (int)tcp->hnd, MIO_SIZEOF(xxx));
		//return mio_dev_sck_write  (tcp, "HELLO", 5, MIO_NULL);
		MIO_INIT_NTIME (&tmout, 5, 0);
		n = mio_dev_sck_timedwrite(tcp, xxx, MIO_SIZEOF(xxx), &tmout, MIO_NULL, MIO_NULL);

		if (n <= -1) return -1;
	}

	MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) - REQUESTING TO STOP READ\n", (int)tcp->hnd);
	mio_dev_sck_read (tcp, 0);

#if 0
	MIO_INFO1 (tcp->mio, "TCP_SCK_ON_READ(%d) - REQUESTING TO CLOSE WRITING END\n", (int)tcp->hnd);
	/* post the write finisher - close the writing end */
	n = mio_dev_sck_write(tcp, MIO_NULL, 0, MIO_NULL, MIO_NULL);
	if (n <= -1) return -1;
#endif

	return 0;

/* return 1; let the main loop to read more greedily without consulting the multiplexer */
}

/* ========================================================================= */

static void pro_on_close (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid)
{
	mio_t* mio = pro->mio;
	if (sid == MIO_DEV_PRO_MASTER)
		MIO_INFO1 (mio, "PROCESS(%d) CLOSE MASTER\n", (int)pro->child_pid);
	else
		MIO_INFO2 (mio, "PROCESS(%d) CLOSE SLAVE[%d]\n", (int)pro->child_pid, sid);
}

static int pro_on_read (mio_dev_pro_t* pro, mio_dev_pro_sid_t sid, const void* data, mio_iolen_t dlen)
{
	mio_t* mio = pro->mio;

	if (dlen <= -1)
	{
		MIO_INFO1 (mio, "PROCESS(%d): READ TIMED OUT...\n", (int)pro->child_pid);
		mio_dev_pro_halt (pro);
		return 0;
	}
	else if (dlen <= 0)
	{
		MIO_INFO1 (mio, "PROCESS(%d): EOF RECEIVED...\n", (int)pro->child_pid);
		/* no outstanding request. but EOF */
		mio_dev_pro_halt (pro);
		return 0;
	}

	MIO_INFO5 (mio, "PROCESS(%d) READ DATA ON SLAVE[%d] len=%d [%.*hs]\n", (int)pro->child_pid, (int)sid, (int)dlen, dlen, (char*)data);
	if (sid == MIO_DEV_PRO_OUT)
	{
		mio_dev_pro_read (pro, sid, 0);
		mio_dev_pro_write (pro, "HELLO\n", 6, MIO_NULL);
	}
	return 0;
}

static int pro_on_write (mio_dev_pro_t* pro, mio_iolen_t wrlen, void* wrctx)
{
	mio_t* mio = pro->mio;
	mio_ntime_t tmout;
	if (wrlen <= -1)
	{
		MIO_INFO1 (mio, "PROCESS(%d): WRITE TIMED OUT...\n", (int)pro->child_pid);
		mio_dev_pro_halt (pro);
		return 0;
	}

	MIO_DEBUG2 (mio, "PROCESS(%d) wrote data of %d bytes\n", (int)pro->child_pid, (int)wrlen);
	/*mio_dev_pro_read (pro, MIO_DEV_PRO_OUT, 1);*/
	MIO_INIT_NTIME (&tmout, 5, 0);
	mio_dev_pro_timedread (pro, MIO_DEV_PRO_OUT, 1, &tmout);
	return 0;
}

/* ========================================================================= */

static int arp_sck_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_skad_t* srcaddr)
{
	mio_etharp_pkt_t* eap;


	if (dlen < MIO_SIZEOF(*eap)) return 0; /* drop */

	eap = (mio_etharp_pkt_t*)data;

	printf ("ARP ON IFINDEX %d OPCODE: %d", mio_skad_ifindex(srcaddr), ntohs(eap->arphdr.opcode));

	printf (" SHA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.sha[0], eap->arppld.sha[1], eap->arppld.sha[2], eap->arppld.sha[3], eap->arppld.sha[4], eap->arppld.sha[5]);
	printf (" SPA: %d.%d.%d.%d", eap->arppld.spa[0], eap->arppld.spa[1], eap->arppld.spa[2], eap->arppld.spa[3]);
	printf (" THA: %02X:%02X:%02X:%02X:%02X:%02X", eap->arppld.tha[0], eap->arppld.tha[1], eap->arppld.tha[2], eap->arppld.tha[3], eap->arppld.tha[4], eap->arppld.tha[5]);
	printf (" TPA: %d.%d.%d.%d", eap->arppld.tpa[0], eap->arppld.tpa[1], eap->arppld.tpa[2], eap->arppld.tpa[3]);
	printf ("\n");
	return 0;
}

static int arp_sck_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	return 0;
}

static void arp_sck_on_connect (mio_dev_sck_t* dev)
{
printf ("STARTING UP ARP SOCKET %d...\n", dev->hnd);
}

static void arp_sck_on_disconnect (mio_dev_sck_t* dev)
{
printf ("SHUTTING DOWN ARP SOCKET %d...\n", dev->hnd);
}

static int setup_arp_tester (mio_t* mio)
{
	mio_skad_t ethdst;
	mio_etharp_pkt_t etharp;
	mio_dev_sck_make_t sck_make;
	mio_dev_sck_t* sck;
	unsigned int ifindex;

	memset (&sck_make, 0, MIO_SIZEOF(sck_make));
	sck_make.type = MIO_DEV_SCK_ARP;
	//sck_make.type = MIO_DEV_SCK_ARP_DGRAM;
	sck_make.on_write = arp_sck_on_write;
	sck_make.on_read = arp_sck_on_read;
	sck_make.on_connect = arp_sck_on_connect;
	sck_make.on_disconnect = arp_sck_on_disconnect;
	sck = mio_dev_sck_make(mio, 0, &sck_make);
	if (!sck)
	{
		MIO_INFO1 (mio, "Cannot make arp socket device - %js\n", mio_geterrmsg(mio));
		return -1;
	}

	//mio_bcstrtoifindex (mio, "enp0s25.3", &ifindex);
	//mio_skad_init_for_eth (&ethdst, ifindex, (mio_ethaddr_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
	//mio_skad_init_for_eth (&ethdst, ifindex, (mio_ethaddr_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");
	mio_bcstrtoifindex (mio, "eno1", &ifindex);
	mio_skad_init_for_eth (&ethdst, ifindex, (mio_ethaddr_t*)"\xAA\xBB\xFF\xCC\xDD\xFF");

	memset (&etharp, 0, MIO_SIZEOF(etharp));

	memcpy (etharp.ethhdr.source, "\xB8\x6B\x23\x9C\x10\x76", MIO_ETHADDR_LEN);
	//memcpy (etharp.ethhdr.dest, "\xFF\xFF\xFF\xFF\xFF\xFF", MIO_ETHADDR_LEN);
	memcpy (etharp.ethhdr.dest, "\xAA\xBB\xFF\xCC\xDD\xFF", MIO_ETHADDR_LEN);
	etharp.ethhdr.proto = MIO_CONST_HTON16(MIO_ETHHDR_PROTO_ARP);

	etharp.arphdr.htype = MIO_CONST_HTON16(MIO_ARPHDR_HTYPE_ETH);
	etharp.arphdr.ptype = MIO_CONST_HTON16(MIO_ARPHDR_PTYPE_IP4);
	etharp.arphdr.hlen = MIO_ETHADDR_LEN;
	etharp.arphdr.plen = MIO_IP4ADDR_LEN;
	etharp.arphdr.opcode = MIO_CONST_HTON16(MIO_ARPHDR_OPCODE_REQUEST);

	memcpy (etharp.arppld.sha, "\xB8\x6B\x23\x9C\x10\x76", MIO_ETHADDR_LEN);

	if (mio_dev_sck_write(sck, &etharp, MIO_SIZEOF(etharp), MIO_NULL, &ethdst) <= -1)
	//if (mio_dev_sck_write (sck, &etharp.arphdr, MIO_SIZEOF(etharp) - MIO_SIZEOF(etharp.ethhdr), MIO_NULL, &ethaddr) <= -1)
	{
		MIO_INFO1 (mio, "Cannot write arp - %js\n", mio_geterrmsg(mio));
	}


	return 0;
}

/* ========================================================================= */

struct icmpxtn_t
{
	mio_uint16_t icmp_seq;
	mio_tmridx_t tmout_jobidx;
	int reply_received;
};

typedef struct icmpxtn_t icmpxtn_t;

static int schedule_icmp_wait (mio_dev_sck_t* dev);

static void send_icmp (mio_dev_sck_t* dev, mio_uint16_t seq)
{
	mio_t* mio = dev->mio;
	mio_skad_t dstaddr;
	mio_icmphdr_t* icmphdr;
	mio_uint8_t buf[512];

	mio_bcstrtoskad (mio, "192.168.9.1", &dstaddr); 

	memset(buf, 0, MIO_SIZEOF(buf));
	icmphdr = (mio_icmphdr_t*)buf;
	icmphdr->type = MIO_ICMP_ECHO_REQUEST;
	icmphdr->u.echo.id = MIO_CONST_HTON16(100);
	icmphdr->u.echo.seq = mio_hton16(seq);

	memset (&buf[MIO_SIZEOF(*icmphdr)], 'A', MIO_SIZEOF(buf) - MIO_SIZEOF(*icmphdr));
	icmphdr->checksum = mio_checksum_ip(icmphdr, MIO_SIZEOF(buf));

	if (mio_dev_sck_write(dev, buf, MIO_SIZEOF(buf), MIO_NULL, &dstaddr) <= -1)
	{
		MIO_INFO1 (mio, "Cannot write icmp - %js\n", mio_geterrmsg(mio));
		mio_dev_sck_halt (dev);
	}

	if (schedule_icmp_wait(dev) <= -1)
	{
		MIO_INFO1 (mio, "Cannot schedule icmp wait - %js\n", mio_geterrmsg(mio));
		mio_dev_sck_halt (dev);
	}
}

static void on_icmp_due (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* tmrjob)
{
	mio_dev_sck_t* dev;
	icmpxtn_t* icmpxtn;

	dev = tmrjob->ctx;
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (icmpxtn->reply_received)
		icmpxtn->reply_received = 0;
	else
		MIO_INFO0 (mio, "NO IMCP reply received in time\n");

	send_icmp (dev, ++icmpxtn->icmp_seq);
}

static int schedule_icmp_wait (mio_dev_sck_t* dev)
{
	mio_t* mio = dev->mio;
	icmpxtn_t* icmpxtn;
	mio_tmrjob_t tmrjob;
	mio_ntime_t fire_after;

	icmpxtn = (icmpxtn_t*)(dev + 1);
	MIO_INIT_NTIME (&fire_after, 2, 0);

	memset (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = dev;
	mio_gettime (mio, &tmrjob.when);
	MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, &fire_after);
	tmrjob.handler = on_icmp_due;
	tmrjob.idxptr = &icmpxtn->tmout_jobidx;

	assert (icmpxtn->tmout_jobidx == MIO_TMRIDX_INVALID);

	return (mio_instmrjob(dev->mio, &tmrjob) == MIO_TMRIDX_INVALID)? -1: 0;
}

static int icmp_sck_on_read (mio_dev_sck_t* dev, const void* data, mio_iolen_t dlen, const mio_skad_t* srcaddr)
{
	icmpxtn_t* icmpxtn;
	mio_iphdr_t* iphdr;
	mio_icmphdr_t* icmphdr;

	/* when received, the data contains the IP header.. */
	icmpxtn = (icmpxtn_t*)(dev + 1);

	if (dlen < MIO_SIZEOF(*iphdr) + MIO_SIZEOF(*icmphdr))
	{
		printf ("INVALID ICMP PACKET.. TOO SHORT...%d\n", (int)dlen);
	}
	else
	{
		/* TODO: consider IP options... */
		iphdr = (mio_iphdr_t*)data;

		if (iphdr->ihl * 4 + MIO_SIZEOF(*icmphdr) > dlen)
		{
			printf ("INVALID ICMP PACKET.. WRONG IHL...%d\n", (int)iphdr->ihl * 4);
		}
		else
		{
			icmphdr = (mio_icmphdr_t*)((mio_uint8_t*)data + (iphdr->ihl * 4));

			/* TODO: check srcaddr against target */

			if (icmphdr->type == MIO_ICMP_ECHO_REPLY && 
			    mio_ntoh16(icmphdr->u.echo.seq) == icmpxtn->icmp_seq) /* TODO: more check.. echo.id.. */
			{
				icmpxtn->reply_received = 1;
				printf ("ICMP REPLY RECEIVED...ID %d SEQ %d\n", (int)mio_ntoh16(icmphdr->u.echo.id), (int)mio_ntoh16(icmphdr->u.echo.seq));
			}
			else
			{
				printf ("GARBAGE ICMP PACKET...LEN %d SEQ %d,%d\n", (int)dlen, (int)icmpxtn->icmp_seq, (int)mio_ntoh16(icmphdr->u.echo.seq));
			}
		}
	}
	return 0;
}


static int icmp_sck_on_write (mio_dev_sck_t* dev, mio_iolen_t wrlen, void* wrctx, const mio_skad_t* dstaddr)
{
	/*icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1); */

	return 0;
}

static void icmp_sck_on_disconnect (mio_dev_sck_t* dev)
{
	icmpxtn_t* icmpxtn;

	icmpxtn = (icmpxtn_t*)(dev + 1);

printf ("SHUTTING DOWN ICMP SOCKET %d...\n", dev->hnd);
	if (icmpxtn->tmout_jobidx != MIO_TMRIDX_INVALID)
	{

		mio_deltmrjob (dev->mio, icmpxtn->tmout_jobidx);
		icmpxtn->tmout_jobidx = MIO_TMRIDX_INVALID;
	}
}

static int setup_ping4_tester (mio_t* mio)
{
	mio_dev_sck_make_t sck_make;
	mio_dev_sck_t* sck;
	icmpxtn_t* icmpxtn;

	memset (&sck_make, 0, MIO_SIZEOF(sck_make));
	sck_make.type = MIO_DEV_SCK_ICMP4;
	sck_make.on_write = icmp_sck_on_write;
	sck_make.on_read = icmp_sck_on_read;
	sck_make.on_disconnect = icmp_sck_on_disconnect;

	sck = mio_dev_sck_make (mio, MIO_SIZEOF(icmpxtn_t), &sck_make);
	if (!sck)
	{
		MIO_INFO1 (mio, "Cannot make ICMP4 socket device - %js\n", mio_geterrmsg(mio));
		return -1;
	}

	icmpxtn = (icmpxtn_t*)(sck + 1);
	icmpxtn->tmout_jobidx = MIO_TMRIDX_INVALID;
	icmpxtn->icmp_seq = 0;

	/*TODO: mio_dev_sck_setbroadcast (sck, 1);*/

	send_icmp (sck, ++icmpxtn->icmp_seq);

	return 0;
}

/* ========================================================================= */
static int pipe_on_read (mio_dev_pipe_t* dev, const void* data, mio_iolen_t dlen)
{
	MIO_INFO3 (dev->mio, "PIPE READ %d bytes - [%.*s]\n", (int)dlen, (int)dlen, data);
	return 0;
}
static int pipe_on_write (mio_dev_pipe_t* dev, mio_iolen_t wrlen, void* wrctx)
{
	MIO_INFO1 (dev->mio, "PIPE WRITTEN %d bytes\n", (int)wrlen);
	return 0;
}

static void pipe_on_close (mio_dev_pipe_t* dev, mio_dev_pipe_sid_t sid)
{
	MIO_INFO1 (dev->mio, "PIPE[%d] CLOSED \n", (int)sid);
}


static int thr_on_read (mio_dev_thr_t* dev, const void* data, mio_iolen_t dlen)
{
	MIO_INFO3 (dev->mio, "THR READ FROM THR - %d bytes - [%.*s]\n", (int)dlen, (int)dlen, data);
	//if (dlen == 0) mio_dev_halt(dev); /* EOF on the input. treat this as end of whole thread transaction */
	return 0;
}

static int thr_on_write (mio_dev_thr_t* dev, mio_iolen_t wrlen, void* wrctx)
{
	MIO_INFO1 (dev->mio, "THR WRITTEN TO THR - %d bytes\n", (int)wrlen);
	return 0;
}

static void thr_on_close (mio_dev_thr_t* dev, mio_dev_thr_sid_t sid)
{
	if (sid == MIO_DEV_THR_OUT) mio_dev_thr_haltslave (dev, MIO_DEV_THR_IN);
	MIO_INFO1 (dev->mio, "THR[%d] CLOSED \n", (int)sid);
}

static void thr_func (mio_t* mio, mio_dev_thr_iopair_t* iop, void* ctx)
{
	mio_bch_t buf[5];
	ssize_t n;

static int x = 0;
int y;
int z = 0;

#if defined(__ATOMIC_RELAXED)
	y = __atomic_add_fetch (&x, 1, __ATOMIC_RELAXED);
#else
	// this is buggy..
	y = ++x;
#endif

	while ((n = read(iop->rfd, buf, MIO_COUNTOF(buf)))> 0) write (iop->wfd, buf, n);

	while (1)
	{
		sleep (1);
		write (iop->wfd, "THR LOOPING", 11);
		z++;
		if ((y % 2) && (z >5)) 
		{
			write (iop->wfd, 0, MIO_NULL);
			break;
		}
	}

}
/* ========================================================================= */

static void on_dnc_resolve(mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg, mio_errnum_t status, const void* data, mio_oow_t dlen)
{
	mio_dns_pkt_info_t* pi = (mio_dns_pkt_info_t*)data;

	if (pi) // status == MIO_ENOERR
	{
		mio_uint32_t i;


		printf (">>>>>>>> RRDLEN = %d\n", (int)pi->_rrdlen);
		printf (">>>>>>>> RCODE %d EDNS exist %d uplen %d version %d dnssecok %d\n", pi->hdr.rcode, pi->edns.exist, pi->edns.uplen, pi->edns.version, pi->edns.dnssecok);
		if (pi->hdr.rcode == MIO_DNS_RCODE_BADCOOKIE)
		{
			/* TODO: must retry */
		}

		if (pi->edns.cookie.client_len > 0 && !pi->edns.cookie_verified) /* TODO: do i need to check if cookie.server_len > 0? */
		{
			/* client cookie is bad.. */
			printf ("CLIENT COOKIE IS BAD>>>>>>>>>>>>>>>>>>>\n");
		}

		//if (pi->hdr.rcode != MIO_DNS_RCODE_NOERROR) goto no_data;
		if (pi->ancount < 0) goto no_data;

		for (i = 0; i < pi->ancount; i++)
		{
			mio_dns_brr_t* brr = &pi->rr.an[i];
			switch (pi->rr.an[i].rrtype)
			{
				case MIO_DNS_RRT_A:
				{
					struct in_addr ia;
					memcpy (&ia.s_addr, brr->dptr, brr->dlen);
					printf ("^^^  GOT REPLY A........................   %d ", brr->dlen);
					printf ("[%s]", inet_ntoa(ia));
					printf ("\n");
					goto done;
				}
				case MIO_DNS_RRT_AAAA:
				{
					struct in6_addr ia;
					char buf[128];
					memcpy (&ia.s6_addr, brr->dptr, brr->dlen);
					printf ("^^^  GOT REPLY AAAA........................   %d ", brr->dlen);
					printf ("[%s]", inet_ntop(AF_INET6, &ia, buf, MIO_COUNTOF(buf)));
					printf ("\n");
					goto done;
				}
				case MIO_DNS_RRT_CNAME:
					printf ("^^^  GOT REPLY.... CNAME [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				case MIO_DNS_RRT_MX:
					printf ("^^^  GOT REPLY.... MX [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				case MIO_DNS_RRT_NS:
					printf ("^^^  GOT REPLY.... NS [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				case MIO_DNS_RRT_PTR:
					printf ("^^^  GOT REPLY.... PTR [%s] %d\n", brr->dptr, (int)brr->dlen);
					goto done;
				default:
					goto no_data;
			}
		}
		goto no_data;
	}
	else
	{
	no_data:
		if (status == MIO_ETMOUT) printf ("XXXXXXXXXXXXXXXX TIMED OUT XXXXXXXXXXXXXXXXX\n");
		else printf ("XXXXXXXXXXXXXXXXx NO REPLY DATA XXXXXXXXXXXXXXXXXXXXXXXXX\n");
	}

done:
	/* nothing special */
	return;
}

static void on_dnc_resolve_brief (mio_svc_dnc_t* dnc, mio_dns_msg_t* reqmsg, mio_errnum_t status, const void* data, mio_oow_t dlen)
{
	if (data) /* status must be MIO_ENOERR */
	{
		mio_dns_brr_t* brr = (mio_dns_brr_t*)data;

		if (brr->rrtype == MIO_DNS_RRT_AAAA)
		{
			struct in6_addr ia;
			char buf[128];
			memcpy (&ia.s6_addr, brr->dptr, brr->dlen);
			printf ("^^^ SIMPLE -> GOT REPLY........................   %d ", brr->dlen);
			printf ("[%s]", inet_ntop(AF_INET6, &ia, buf, MIO_COUNTOF(buf)));
			printf ("\n");
		}
		else if (brr->rrtype == MIO_DNS_RRT_A)
		{
			struct in_addr ia;
			memcpy (&ia.s_addr, brr->dptr, brr->dlen);
			printf ("^^^ SIMPLE -> GOT REPLY........................   %d ", brr->dlen);
			printf ("[%s]", inet_ntoa(ia));
			printf ("\n");
		}
		else if (brr->rrtype == MIO_DNS_RRT_CNAME)
		{
			printf ("^^^ SIMPLE -> CNAME [%s] %d\n", brr->dptr, (int)brr->dlen);
		}
		else if (brr->rrtype == MIO_DNS_RRT_NS)
		{
			printf ("^^^ SIMPLE -> NS [%s] %d\n", brr->dptr, (int)brr->dlen);
		}
		else if (brr->rrtype == MIO_DNS_RRT_PTR)
		{
			printf ("^^^ SIMPLE -> PTR [%s] %d\n", brr->dptr, (int)brr->dlen);
		}
		else if (brr->rrtype == MIO_DNS_RRT_SOA)
		{
			mio_dns_brrd_soa_t* soa = brr->dptr;
			printf ("^^^ SIMPLE -> SOA [%s] [%s] [%u %u %u %u %u] %d\n", soa->mname, soa->rname, (unsigned)soa->serial, (unsigned)soa->refresh, (unsigned)soa->retry, (unsigned)soa->expire, (unsigned)soa->minimum, (int)brr->dlen);
		}
		else
		{
			printf ("^^^ SIMPLE -> UNKNOWN DATA... [%.*s] %d\n", (int)brr->dlen, brr->dptr, (int)brr->dlen);
		}
	}
	else
	{
		if (status == MIO_ETMOUT) printf ("QQQQQQQQQQQQQQQQQQQ TIMED OUT QQQQQQQQQQQQQQQQQ\n");
		else printf ("QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ NO REPLY DATA QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQq - %d\n", mio_geterrnum(mio_svc_dnc_getmio(dnc)));
	}
}

static int print_qparam (mio_bcs_t* key, mio_bcs_t* val, void* ctx)
{
	key->len = mio_perdec_http_bcs(key, key->ptr, MIO_NULL);
	val->len = mio_perdec_http_bcs(val, val->ptr, MIO_NULL);
	fprintf ((FILE*)ctx, "\t[%.*s] = [%.*s]\n", (int)key->len, key->ptr, (int)val->len, val->ptr);
	return 0;
}

static void on_htts_thr_request (mio_t* mio, mio_dev_thr_iopair_t* iop, mio_svc_htts_thr_func_info_t* tfi, void* ctx)
{
	FILE* fp;
	int i;

	if (tfi->req_method != MIO_HTTP_GET)
	{
		write (iop->wfd, "Status: 405\r\n\r\n", 15); /* method not allowed */
		return;
	}

	fp = fdopen(iop->wfd, "w");
	if (!fp)
	{
		write (iop->wfd, "Status: 500\r\n\r\n", 15); /* internal server error */
		return;
	}

	fprintf (fp, "Status: 201\r\n");
	fprintf (fp, "Content-Type: text/html\r\n\r\n");

	fprintf (fp, "request path = %s\n", tfi->req_path);
	if (tfi->req_param) 
	{
		fprintf (fp, "request params:\n");
		mio_scan_http_qparam (tfi->req_param, print_qparam, fp);
	}
	for (i = 0; i < 100; i++) fprintf (fp, "%d * %d => %d\n", i, i, i * i);

	fclose (fp);
	iop->wfd = MIO_SYSHND_INVALID;
}

/* ========================================================================= */
int process_http_request (mio_svc_htts_t* htts, mio_dev_sck_t* csck, mio_htre_t* req)
{
	mio_t* mio = mio_svc_htts_getmio(htts);
//	mio_svc_htts_cli_t* cli = mio_dev_sck_getxtn(csck);
	mio_http_method_t mth;

	/* percent-decode the query path to the original buffer
	 * since i'm not going to need it in the original form
	 * any more. once it's decoded in the peek mode,
	 * the decoded query path is made available in the
	 * non-peek mode as well */

	MIO_DEBUG2 (mio, "[RAW-REQ] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));

	mio_htre_perdecqpath(req);
	/* TODO: proper request logging */

	MIO_DEBUG2 (mio, "[REQ] %s %s\n", mio_htre_getqmethodname(req), mio_htre_getqpath(req));

#if 0
mio_printf (MIO_T("================================\n"));
mio_printf (MIO_T("[%lu] %hs REQUEST ==> [%hs] version[%d.%d %hs] method[%hs]\n"),
	(unsigned long)time(NULL),
	(peek? MIO_MT("PEEK"): MIO_MT("HANDLE")),
	mio_htre_getqpath(req),
	mio_htre_getmajorversion(req),
	mio_htre_getminorversion(req),
	mio_htre_getverstr(req),
	mio_htre_getqmethodname(req)
);
if (mio_htre_getqparam(req))
	mio_printf (MIO_T("PARAMS ==> [%hs]\n"), mio_htre_getqparam(req));

mio_htb_walk (&req->hdrtab, walk, MIO_NULL);
if (mio_htre_getcontentlen(req) > 0)
{
	mio_printf (MIO_T("CONTENT [%.*S]\n"), (int)mio_htre_getcontentlen(req), mio_htre_getcontentptr(req));
}
#endif

	mth = mio_htre_getqmethodtype(req);
	/* determine what to do once the header fields are all received.
	 * i don't want to delay this until the contents are received.
	 * if you don't like this behavior, you must implement your own
	 * callback function for request handling. */
#if 0
	/* TODO support X-HTTP-Method-Override */
	if (data.method == MIO_HTTP_POST)
	{
		tmp = mio_htre_getheaderval(req, MIO_MT("X-HTTP-Method-Override"));
		if (tmp)
		{
			/*while (tmp->next) tmp = tmp->next;*/ /* get the last value */
			data.method = mio_mbstohttpmethod (tmp->ptr);
		}
	}
#endif

#if 0
	if (mth == MIO_HTTP_CONNECT)
	{
		/* CONNECT method must not have content set. 
		 * however, arrange to discard it if so. 
		 *
		 * NOTE: CONNECT is implemented to ignore many headers like
		 *       'Expect: 100-continue' and 'Connection: keep-alive'. */
		mio_htre_discardcontent (req);
	}
	else 
	{
/* this part can be checked in actual mio_svc_htts_doXXX() functions.
 * some doXXX handlers may not require length for POST.
 * it may be able to simply accept till EOF? or  treat as if CONTENT_LENGTH is 0*/
		if (mth == MIO_HTTP_POST && !(req->flags & (MIO_HTRE_ATTR_LENGTH | MIO_HTRE_ATTR_CHUNKED)))
		{
			/* POST without Content-Length nor not chunked */
			mio_htre_discardcontent (req); 
			/* 411 Length Required - can't keep alive. Force disconnect */
			req->flags &= ~MIO_HTRE_ATTR_KEEPALIVE; /* to cause sendstatus() to close */
			if (mio_svc_htts_sendstatus(htts, csck, req, 411, MIO_NULL) <= -1) goto oops;
		}
		else

		{
#endif
			const mio_bch_t* qpath = mio_htre_getqpath(req);
			int x;
			if (mio_comp_bcstr_limited(qpath, "/thr/", 5, 1) == 0)
				x = mio_svc_htts_dothr(htts, csck, req, on_htts_thr_request, MIO_NULL);
			else if (mio_comp_bcstr_limited(qpath, "/txt/", 5, 1) == 0)
				x = mio_svc_htts_dotxt(htts, csck, req, 200, "text/plain", qpath);
			else
				x = mio_svc_htts_docgi(htts, csck, req, "", mio_htre_getqpath(req));
			if (x <= -1) goto oops;

	return 0;

oops:
	mio_dev_sck_halt (csck);
	return -1;
}

/* ========================================================================= */

static mio_t* g_mio;

static void handle_signal (int sig)
{
	if (g_mio) mio_stop (g_mio, MIO_STOPREQ_TERMINATION);
}

static int schedule_timer_job_after (mio_t* mio, const mio_ntime_t* fire_after, mio_tmrjob_handler_t handler, void* ctx)
{
	mio_tmrjob_t tmrjob;

	memset (&tmrjob, 0, MIO_SIZEOF(tmrjob));
	tmrjob.ctx = ctx;

	mio_gettime (mio, &tmrjob.when);
	MIO_ADD_NTIME (&tmrjob.when, &tmrjob.when, fire_after);

	tmrjob.handler = handler;
	tmrjob.idxptr = MIO_NULL;

	return mio_instmrjob(mio, &tmrjob);
}


static void send_test_query (mio_t* mio, const mio_ntime_t* now, mio_tmrjob_t* job)
{
	//if (!mio_svc_dnc_resolve((mio_svc_dnc_t*)job->ctx, "www.microsoft.com", MIO_DNS_RRT_CNAME, MIO_SVC_DNC_RESOLVE_FLAG_COOKIE, on_dnc_resolve, 0))
	if (!mio_svc_dnc_resolve((mio_svc_dnc_t*)job->ctx, "mailserver.manyhost.net", MIO_DNS_RRT_A, MIO_SVC_DNC_RESOLVE_FLAG_COOKIE, on_dnc_resolve, 0))
	{
		printf ("resolve attempt failure ---> mailserver.manyhost.net\n");
	}
}

int main (int argc, char* argv[])
{
	int i;

	mio_t* mio;
	mio_dev_sck_t* tcp[3];

	struct sigaction sigact;
	mio_dev_sck_connect_t tcp_conn;
	mio_dev_sck_listen_t tcp_lstn;
	mio_dev_sck_bind_t tcp_bind;
	mio_dev_sck_make_t tcp_make;

	tcp_server_t* ts;

#if defined(USE_SSL)
	SSL_load_error_strings ();
	SSL_library_init ();
#endif

	mio = mio_open(&mmgr, 0, MIO_NULL, 512, MIO_NULL);
	if (!mio)
	{
		printf ("Cannot open mio\n");
		return -1;
	}

	g_mio = mio;

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = handle_signal;
	sigaction (SIGINT, &sigact, MIO_NULL);

	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sigact, MIO_NULL);

/*
	memset (&sigact, 0, MIO_SIZEOF(sigact));
	sigact.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigact, MIO_NULL);
*/

	/*memset (&sin, 0, MIO_SIZEOF(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234); */
/*
	udp = (mio_dev_udp_t*)mio_dev_make(mio, MIO_SIZEOF(*udp), &udp_mth, &udp_evcb, &sin);
	if (!udp)
	{
		printf ("Cannot make udp\n");
		goto oops;
	}
*/

	memset (&tcp_make, 0, MIO_SIZEOF(tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;
	tcp[0] = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[0])
	{
		MIO_INFO1 (mio, "Cannot make tcp - %js\n", mio_geterrmsg(mio));
		goto oops;
	}

	ts = (tcp_server_t*)(tcp[0] + 1);
	ts->tally = 0;

	memset (&tcp_conn, 0, MIO_SIZEOF(tcp_conn));
	/* openssl s_server -accept 9999 -key localhost.key  -cert localhost.crt */
	mio_bcstrtoskad(mio, "127.0.0.1:9999", &tcp_conn.remoteaddr);

	MIO_INIT_NTIME (&tcp_conn.connect_tmout, 5, 0);
	tcp_conn.options = MIO_DEV_SCK_CONNECT_SSL;
	if (mio_dev_sck_connect(tcp[0], &tcp_conn) <= -1)
	{
		MIO_INFO1 (mio, "tcp[0] mio_dev_sck_connect() failed - %js\n", mio_geterrmsg(mio));
		/* carry on regardless of failure */
	}

	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, MIO_SIZEOF(tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[1] = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[1])
	{
		MIO_INFO1 (mio, "Cannot make tcp[1] - %js\n", mio_geterrmsg(mio));
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[1] + 1);
	ts->tally = 0;

	memset (&tcp_bind, 0, MIO_SIZEOF(tcp_bind));
	mio_bcstrtoskad(mio, "0.0.0.0:1234", &tcp_bind.localaddr);
	tcp_bind.options = MIO_DEV_SCK_BIND_REUSEADDR;

	if (mio_dev_sck_bind(tcp[1],&tcp_bind) <= -1)
	{
		MIO_INFO1 (mio, "tcp[1] mio_dev_sck_bind() failed - %js\n", mio_geterrmsg(mio));
		goto oops;
	}


	memset (&tcp_lstn, 0, MIO_SIZEOF(tcp_lstn));
	tcp_lstn.backlogs = 100;
	if (mio_dev_sck_listen(tcp[1], &tcp_lstn) <= -1)
	{
		MIO_INFO1 (mio, "tcp[1] mio_dev_sck_listen() failed - %js\n", mio_geterrmsg(mio));
		goto oops;
	}


	/* -------------------------------------------------------------- */
	memset (&tcp_make, 0, MIO_SIZEOF(tcp_make));
	tcp_make.type = MIO_DEV_SCK_TCP4;
	tcp_make.on_write = tcp_sck_on_write;
	tcp_make.on_read = tcp_sck_on_read;
	tcp_make.on_connect = tcp_sck_on_connect;
	tcp_make.on_disconnect = tcp_sck_on_disconnect;

	tcp[2] = mio_dev_sck_make(mio, MIO_SIZEOF(tcp_server_t), &tcp_make);
	if (!tcp[2])
	{
		MIO_INFO1 (mio, "Cannot make tcp[2] - %js\n", mio_geterrmsg(mio));
		goto oops;
	}
	ts = (tcp_server_t*)(tcp[2] + 1);
	ts->tally = 0;


	memset (&tcp_bind, 0, MIO_SIZEOF(tcp_bind));
	mio_bcstrtoskad(mio, "0.0.0.0:1235", &tcp_bind.localaddr);
	tcp_bind.options = MIO_DEV_SCK_BIND_REUSEADDR /*| MIO_DEV_SCK_BIND_REUSEPORT |*/;
#if defined(USE_SSL)
	tcp_bind.options |= MIO_DEV_SCK_BIND_SSL; 
	tcp_bind.ssl_certfile = "localhost.crt";
	tcp_bind.ssl_keyfile = "localhost.key";
#endif

	if (mio_dev_sck_bind(tcp[2], &tcp_bind) <= -1)
	{
		MIO_INFO1 (mio, "tcp[2] mio_dev_sck_bind() failed - %js\n", mio_geterrmsg(mio));
		goto oops;
	}

	tcp_lstn.backlogs = 100;
	MIO_INIT_NTIME (&tcp_lstn.accept_tmout, 5, 1);
	if (mio_dev_sck_listen(tcp[2], &tcp_lstn) <= -1)
	{
		MIO_INFO1 (mio, "tcp[2] mio_dev_sck_listen() failed - %js\n", mio_geterrmsg(mio));
		goto oops;
	}


	//mio_dev_sck_sendfile (tcp[2], fd, offset, count);

	setup_arp_tester(mio);
	setup_ping4_tester(mio);

#if 0
for (i = 0; i < 5; i++)
{
	mio_dev_pro_t* pro;
	mio_dev_pro_make_t pro_make;

	memset (&pro_make, 0, MIO_SIZEOF(pro_make));
	pro_make.flags = MIO_DEV_PRO_READOUT | MIO_DEV_PRO_READERR | MIO_DEV_PRO_WRITEIN /*| MIO_DEV_PRO_FORGET_CHILD*/;
	//pro_make.cmd = "/bin/ls -laF /usr/bin";
	//pro_make.cmd = "/bin/ls -laF";
	pro_make.cmd = "./a";
	pro_make.on_read = pro_on_read;
	pro_make.on_write = pro_on_write;
	pro_make.on_close = pro_on_close;

	pro = mio_dev_pro_make(mio, 0, &pro_make);
	if (!pro)
	{
		MIO_INFO1 (mio, "CANNOT CREATE PROCESS PIPE - %js\n", mio_geterrmsg(mio));
		goto oops;
	}
	mio_dev_pro_read (pro, MIO_DEV_PRO_OUT, 0);
	//mio_dev_pro_read (pro, MIO_DEV_PRO_ERR, 0);

	mio_dev_pro_write (pro, "MY MIO LIBRARY\n", 16, MIO_NULL);
//mio_dev_pro_killchild (pro); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_IN); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_OUT); 
//mio_dev_pro_close (pro, MIO_DEV_PRO_ERR); 
}
#endif

{
	mio_svc_dnc_t* dnc;
	mio_svc_htts_t* htts;
	mio_ntime_t send_tmout, reply_tmout;
	mio_skad_t servaddr;
	mio_skad_t htts_bind_addr;

	send_tmout.sec = 0;
	send_tmout.nsec = 0;
	reply_tmout.sec = 1;
	reply_tmout.nsec = 0;

	//mio_bcstrtoskad (mio, "8.8.8.8:53", &servaddr);
	//mio_bcstrtoskad (mio, "130.59.31.29:53", &servaddr); // ns2.switch.ch
	mio_bcstrtoskad (mio, "134.119.216.86:53", &servaddr); // ns.manyhost.net
	//mio_bcstrtoskad (mio, "[fe80::c7e2:bd6e:1209:ac1b]:1153", &servaddr);
	//mio_bcstrtoskad (mio, "[fe80::c7e2:bd6e:1209:ac1b%eno1]:1153", &servaddr);

	//mio_bcstrtoskad (mio, "[::]:9988", &htts_bind_addr);
	mio_bcstrtoskad (mio, "0.0.0.0:9988", &htts_bind_addr);

	dnc = mio_svc_dnc_start(mio, &servaddr, MIO_NULL, &send_tmout, &reply_tmout, 2); /* option - send to all, send one by one */
	if (!dnc)
	{
		MIO_INFO1 (mio, "UNABLE TO START DNC - %js\n", mio_geterrmsg(mio));
	}

	htts = mio_svc_htts_start(mio, &htts_bind_addr, process_http_request);
	if (htts) mio_svc_htts_setservernamewithbcstr (htts, "MIO-HTTP");
	else MIO_INFO1 (mio, "UNABLE TO START HTTS - %js\n", mio_geterrmsg(mio));

#if 0
	if (dnc)
	{
		mio_dns_bqr_t qrs[] = 
		{
			{ "code.miflux.com",  MIO_DNS_RRT_A,    MIO_DNS_RRC_IN },
			{ "code.miflux.com",  MIO_DNS_RRT_AAAA, MIO_DNS_RRC_IN },
			{ "code.abiyo.net",   MIO_DNS_RRT_A,    MIO_DNS_RRC_IN },
			{ "code6.abiyo.net",  MIO_DNS_RRT_AAAA, MIO_DNS_RRC_IN },
			{ "abiyo.net",        MIO_DNS_RRT_MX,   MIO_DNS_RRC_IN }
		};

		mio_ip4ad_t rrdata_a = { { 4, 3, 2, 1 } };
		mio_ip6ad_t rrdata_aaaa = { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, }};

		mio_dns_brrd_soa_t miflux_soa_data = 
		{
			"ns9.dnszi.com", "root.dnszi.com", 2019091905, 43200, 3600, 1209600, 1100
		};

		mio_dns_brr_t rrs[] = 
		{
			{ MIO_DNS_RR_PART_ANSWER,    "code.miflux.com",  MIO_DNS_RRT_A,     MIO_DNS_RRC_IN, 86400, MIO_SIZEOF(rrdata_a),    &rrdata_a },
			{ MIO_DNS_RR_PART_ANSWER,    "code.miflux.com",  MIO_DNS_RRT_AAAA,  MIO_DNS_RRC_IN, 86400, MIO_SIZEOF(rrdata_aaaa), &rrdata_aaaa },
			{ MIO_DNS_RR_PART_ANSWER,    "miflux.com",       MIO_DNS_RRT_NS,    MIO_DNS_RRC_IN, 86400, 0,  "ns1.miflux.com" },
			{ MIO_DNS_RR_PART_ANSWER,    "miflux.com",       MIO_DNS_RRT_NS,    MIO_DNS_RRC_IN, 86400, 0,  "ns2.miflux.com" },
			{ MIO_DNS_RR_PART_AUTHORITY, "miflux.com",       MIO_DNS_RRT_SOA,    MIO_DNS_RRC_IN, 86400, MIO_SIZEOF(miflux_soa_data),  &miflux_soa_data }, //, 
			//{ MIO_DNS_RR_PART_ANSERT,    "www.miflux.com",   MIO_DNS_RRT_CNAME, MIO_DNS_RRC_IN, 60,    15, "code.miflux.com" }  
		};

		mio_dns_beopt_t beopt[] =
		{
			{ MIO_DNS_EOPT_COOKIE, 8, "\x01\x02\x03\x04\0x05\x06\0x07\0x08" },
			{ MIO_DNS_EOPT_NSID,   0, MIO_NULL                              }
		};

		mio_dns_bedns_t qedns =
		{
			4096, /* uplen */

			0,    /* edns version */
			0,    /* dnssec ok */

			MIO_COUNTOF(beopt),    /* number of edns options */
			beopt
		};

		mio_dns_bhdr_t qhdr =
		{
			-1,              /* id */
			0,                  /* qr */
			MIO_DNS_OPCODE_QUERY, /* opcode */
			0, /* aa */
			0, /* tc */
			1, /* rd */
			0, /* ra */
			0, /* ad */
			0, /* cd */
			MIO_DNS_RCODE_NOERROR /* rcode */
		};

		mio_dns_bhdr_t rhdr =
		{
			0x1234,               /* id */
			1,                    /* qr */
			MIO_DNS_OPCODE_QUERY, /* opcode */

			0, /* aa */
			0, /* tc */
			0, /* rd */
			1, /* ra */
			0, /* ad */
			0, /* cd */
			MIO_DNS_RCODE_BADCOOKIE /* rcode */
		}; 

		mio_svc_dnc_sendreq (dnc, &qhdr, &qrs[0], &qedns, 0, MIO_NULL, 0);
		mio_svc_dnc_sendmsg (dnc, &rhdr, qrs, MIO_COUNTOF(qrs), rrs, MIO_COUNTOF(rrs), &qedns, 0, MIO_NULL, 0);
	}
#endif

	if (dnc)
	{
		mio_ntime_t x;
		MIO_INIT_NTIME (&x, 5, 0);
		schedule_timer_job_after (mio, &x, send_test_query, dnc);

		if (!mio_svc_dnc_resolve(dnc, "b.wild.com", MIO_DNS_RRT_A, MIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP, on_dnc_resolve, 0))
		{
			printf ("resolve attempt failure ---> a.wild.com\n");
		}
		
		if (!mio_svc_dnc_resolve(dnc, "www.microsoft.com", MIO_DNS_RRT_CNAME, MIO_SVC_DNC_RESOLVE_FLAG_COOKIE, on_dnc_resolve, 0))
		{
			printf ("resolve attempt failure ---> www.microsoft.com\n");
		}
		
		
		//if (!mio_svc_dnc_resolve(dnc, "www.microsoft.com", MIO_DNS_RRT_A, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		if (!mio_svc_dnc_resolve(dnc, "code.miflux.com", MIO_DNS_RRT_A, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF | MIO_SVC_DNC_RESOLVE_FLAG_PREFER_TCP, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> code.miflux.com\n");
		}
		
		if (!mio_svc_dnc_resolve(dnc, "45.77.246.105.in-addr.arpa", MIO_DNS_RRT_PTR, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> 45.77.246.105.in-addr.arpa.\n");
		}
		
		#if 0
		if (!mio_svc_dnc_resolve(dnc, "1.1.1.1.in-addr.arpa", MIO_DNS_RRT_PTR, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> 1.1.1.1.in-addr.arpa\n");
		}
		
		//if (!mio_svc_dnc_resolve(dnc, "ipv6.google.com", MIO_DNS_RRT_AAAA, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		if (!mio_svc_dnc_resolve(dnc, "google.com", MIO_DNS_RRT_SOA, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		//if (!mio_svc_dnc_resolve(dnc, "google.com", MIO_DNS_RRT_NS, MIO_SVC_DNC_RESOLVE_FLAG_BRIEF, on_dnc_resolve_brief, 0))
		{
			printf ("resolve attempt failure ---> code.miflux.com\n");
		}
		#endif
	}

#if 0
{
	mio_dev_pipe_t* pp;
	mio_dev_pipe_make_t mi;
	mi.on_read = pipe_on_read;
	mi.on_write = pipe_on_write;
	mi.on_close = pipe_on_close;
	pp = mio_dev_pipe_make(mio, 0, &mi);
	mio_dev_pipe_write (pp, "this is good", 12, MIO_NULL);
}

for (i = 0; i < 20; i++)
{
	mio_dev_thr_t* tt;
	mio_dev_thr_make_t mi;
	mi.thr_func = thr_func;
	mi.thr_ctx = MIO_NULL;
	mi.on_read = thr_on_read;
	mi.on_write = thr_on_write;
	mi.on_close =  thr_on_close;
	tt = mio_dev_thr_make(mio, 0, &mi);
	mio_dev_thr_write (tt, "hello, world", 12, MIO_NULL);
	mio_dev_thr_write (tt, MIO_NULL, 0, MIO_NULL);
}
#endif

	mio_loop (mio);

	/* TODO: let mio close it ... dnc is svc. sck is dev. */
	if (htts) mio_svc_htts_stop (htts);
	if (dnc) mio_svc_dnc_stop (dnc);
}

	g_mio = MIO_NULL;
	mio_close (mio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif

	return 0;

oops:
	g_mio = MIO_NULL;
	mio_close (mio);
#if defined(USE_SSL)
	cleanup_openssl ();
#endif
	return -1;
}

